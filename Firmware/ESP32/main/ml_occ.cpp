/**
 * TinyML occupancy classifier glue (TFLM).
 * C-callable API: ml_occ_init(), ml_occ_note_motion_start/end(), ml_occ_tick_minute().
 * Maintains minute-rolled PIR features, hysteresis, and edge snaps for stable UI.
 */
#include <math.h>
#include <string.h>

static int s_idle_minutes = 0;

#include "ml_occ.h"
#include "model_data_occ.h"

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
// --- Edge snap tuning (stay near decision threshold) ---
static constexpr float kSnapOnProb  = 0.55f;  // PIR edge -> just above 50%
static constexpr float kSnapOffProb = 0.45f;  // 5-off -> just below 50%
static bool s_snap_on_edge = false;           // latched by PIR edge, consumed in tick


// Some ESP-TFLM builds don't ship tensorflow/lite/version.h.
// Provide a safe default so TFLM's schema check compiles.
#ifndef TFLITE_SCHEMA_VERSION
#define TFLITE_SCHEMA_VERSION 3
#endif

// Allow picking the compiled-in model blob symbol from the build system.
// If your model_data_occ.cc exports a different name (e.g. g_model_data),
// pass -DOCC_MODEL_SYM=g_model_data in CMake, or edit the default below.
#ifndef OCC_MODEL_SYM
#define OCC_MODEL_SYM model_occ_int8_tflite   // <- match your header exactly
#endif

namespace {
  // Quant params from your training run
  constexpr float kInScale  = 0.4745098f;
  constexpr int   kInZp     = -126;
  constexpr float kOutScale = 1.0f/256.0f;   // 0.00390625
  constexpr int   kOutZp    = -128;

  // TFLM bits
  tflite::MicroMutableOpResolver<8> resolver;
  constexpr int kArenaSize = 8 * 1024;
  static uint8_t arena[kArenaSize];
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interp = nullptr;
  TfLiteTensor* in  = nullptr;
  TfLiteTensor* out = nullptr;

  // Feature state
  bool   s_any_motion_this_min = false;
  int    s_hits5 = 0;                 // rolling sum of last 5 minutes of pir_now
  int    s_ring[5] = {0}, s_ridx = 0; // circular minute buffer
  float  s_mins_since = 120.0f;       // clip at 120
  bool   s_motion_now = false;        // tracks inside the minute

  // Hysteresis
  int s_pos_streak = 0;
  int s_neg_streak = 0;
  int s_occ_state  = 0;               // exported occupied flag

  inline int8_t q(float x) {
    int32_t v = (int32_t)lrintf(x / kInScale + kInZp);
    if (v < -128) v = -128; else if (v > 127) v = 127;
    return (int8_t)v;
  }
}  // namespace

extern "C" void ml_occ_init(void)
{
  // Add operators once
  static bool s_resolver_ready = false;
  if (!s_resolver_ready) {
    resolver.AddFullyConnected();
    resolver.AddLogistic();      // sigmoid
    resolver.AddReshape();
    resolver.AddQuantize();
    resolver.AddDequantize();
    s_resolver_ready = true;
  }

  model = tflite::GetModel(OCC_MODEL_SYM);
  static tflite::MicroInterpreter s_interpreter(model, resolver, arena, kArenaSize);
  interp = &s_interpreter;
  (void)interp->AllocateTensors();
  in  = interp->input(0);
  out = interp->output(0);

  // Reset state
  s_any_motion_this_min = false;
  s_hits5 = 0; memset(s_ring, 0, sizeof s_ring); s_ridx = 0;
  s_mins_since = 120.0f;
  s_motion_now = false;
  s_pos_streak = s_neg_streak = 0;
  s_occ_state = 0;
}

void ml_occ_note_motion_start(void)
{
  s_motion_now = true;
  s_any_motion_this_min = true;
  s_pos_streak = (s_pos_streak < 2) ? 2 : s_pos_streak;
  s_snap_on_edge = true;
}


void ml_occ_note_motion_end(void)
{
  s_motion_now = false;
  s_mins_since = 0.0f; // we measure "since end of last ON interval"
}

void ml_occ_tick_minute(int minute_of_day, float *prob_out, int *occupied_out)
{
    int prev_occ = s_occ_state;
// 1) Build this minute’s features
  int pir_now = s_any_motion_this_min ? 1 : 0;

  // Update rolling hits5
  s_hits5 -= s_ring[s_ridx];
  s_ring[s_ridx] = pir_now;
  s_hits5 += s_ring[s_ridx];
  s_ridx = (s_ridx + 1) % 5;

  // mins_since_motion (clip 0..120); if motion still high, keep it 0
  if (!s_motion_now) {
    s_mins_since += 1.0f;
    if (s_mins_since > 120.0f) s_mins_since = 120.0f;
  } else {
    s_mins_since = 0.0f;
  }

  const float hour = (minute_of_day % 1440) / 60.0f; // 0..23.999
  const float hour_sin = sinf(2.0f * (float)M_PI * hour / 24.0f);
  const float hour_cos = cosf(2.0f * (float)M_PI * hour / 24.0f);

  // 2) Quantize & infer
  int8_t* d = in->data.int8;
  d[0] = q((float)pir_now);
  d[1] = q((float)s_hits5);
  d[2] = q(s_mins_since);
  d[3] = q(hour_sin);
  d[4] = q(hour_cos);

  if (interp->Invoke() != kTfLiteOk) {
    if (prob_out) *prob_out = 0.0f;
    if (occupied_out) *occupied_out = s_occ_state;
    s_any_motion_this_min = false; // reset
    return;
  }

  int8_t yq = out->data.int8[0];
  float prob = (yq - kOutZp) * kOutScale;
  if (prob_out) *prob_out = prob;

  
  // --- Idle-decay: if no motion for N minutes, damp the probability and bias OFF.
  // Update idle counter using the per-minute motion accumulator.
  if (s_any_motion_this_min) {
    s_idle_minutes = 0;
  } else {
    if (s_idle_minutes < 60000) s_idle_minutes++;  // saturate
  }

  // If we've been idle long enough, clamp probability downward and bias OFF.
  const int   kIdleDecayMinutes = 5;     // after 5 quiet minutes…
  const float kDecayFloor       = 0.40f; // …never report above 0.40
  if (s_idle_minutes >= kIdleDecayMinutes) {
    if (prob > kDecayFloor) prob = kDecayFloor;
    // Push the debouncer toward OFF immediately.
    s_pos_streak = 0;
    s_neg_streak = 5;   // equals your OFF threshold below
  }
// 3) Hysteresis: 2-on / 5-off
  const float thr = 0.50f;
  if (prob >= thr) {
    s_pos_streak++; s_neg_streak = 0;
    if (s_pos_streak >= 2) s_occ_state = 1;
  } else {
    s_neg_streak++; s_pos_streak = 0;
    if (s_neg_streak >= 5) s_occ_state = 0;
  }
  
  // --- Edge snaps for UI consistency ---
  if (prev_occ == 0 && s_occ_state == 1) {
    if (prob < kSnapOnProb) prob = kSnapOnProb;
  }
  if (prev_occ == 1 && s_occ_state == 0) {
    if (prob > kSnapOffProb) prob = kSnapOffProb;
  }
  if (s_snap_on_edge) {
    if (prob < kSnapOnProb) prob = kSnapOnProb;
    s_snap_on_edge = false;
  }
  if (prob_out) *prob_out = prob;
if (occupied_out) *occupied_out = s_occ_state;

  // 4) Reset per-minute edge accumulator
  s_any_motion_this_min = false;
}
