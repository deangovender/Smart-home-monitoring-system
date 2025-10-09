/**
 * TinyML temperature forecaster glue (TFLM).
 * Exposes a C API used by the C runtime. No dynamic allocation. 
 * Keeps ring buffer of last W readings; per-minute inference + state publish.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "model_consts.h"
#include "model_data.h"   // from bin2c/xxd (model_int8_tflite[])

extern "C" void ml_on_state(float, float, float, float, int, int, int);
extern "C" void ml_on_event(float, float, float, float, int);

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

constexpr int kMinutesPerDay = 1440;  // minutes in a day
// -------- ring buffer for last W temps --------
static float ring[W];
static int head = 0;
static int count = 0;

extern "C" void ml_reset(void) { head = 0; count = 0; memset(ring, 0, sizeof(ring)); }
static inline void push_temp(float t){ ring[head] = t; head = (head + 1) % W; if (count < W) count++; }

// -------- tiny helpers --------
static inline float tod_sin(int m){ return sinf(2.f * M_PI * m / static_cast<float>(kMinutesPerDay)); }
static inline float tod_cos(int m){ return cosf(2.f * M_PI * m / static_cast<float>(kMinutesPerDay)); }

// -------- TFLM objects --------
constexpr int kTensorArenaSize = 32 * 1024;  // arena size tuned for this model
static uint8_t tensor_arena[kTensorArenaSize];   // tune if AllocateTensors() fails
static tflite::MicroInterpreter* interpreter = nullptr;
static TfLiteTensor *input_t = nullptr, *output_t = nullptr;

extern "C" void ml_init(void) {
  const tflite::Model* model = tflite::GetModel(model_int8_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    printf("TFLM: schema mismatch\n");
    return;
  }

  static tflite::MicroMutableOpResolver<2> resolver;
  resolver.AddFullyConnected();
  resolver.AddRelu();

  static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, sizeof(tensor_arena));
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    printf("TFLM: AllocateTensors failed\n");
    return;
  }

  input_t  = interpreter->input(0);
  output_t = interpreter->output(0);

  printf("TFLM ready: in(scale=%f,zp=%" PRId32 ") out(scale=%f,zp=%" PRId32 ")\n",
       input_t->params.scale,  (int32_t)input_t->params.zero_point,
       output_t->params.scale, (int32_t)output_t->params.zero_point);
}

extern "C" void ml_push_temp(float t_degC) {
  push_temp(t_degC);
}

extern "C" void ml_infer_and_publish(int minute_of_day) {
  if (!interpreter || count < W) return;

  // 1) Build normalized input vector [W temps, sin, cos]
  float x[W + 2];
  for (int i = 0; i < W; ++i) {
    int idx = (head + i) % W;
    x[i] = (ring[idx] - TRAIN_MEAN) / TRAIN_STD;
  }
  x[W]   = tod_sin(minute_of_day);
  x[W+1] = tod_cos(minute_of_day);

  // 2) Quantize to int8
  const float s_in = input_t->params.scale;
  const int   z_in = input_t->params.zero_point;
  for (int i = 0; i < W+2; ++i) {
    int q = lroundf(x[i] / s_in) + z_in;
    if (q < -128) q = -128; else if (q > 127) q = 127;
    input_t->data.int8[i] = (int8_t)q;
  }

  // 3) Inference
  if (interpreter->Invoke() != kTfLiteOk) return;

  // 4) Dequantize output → normalized → °C
  const float s_out = output_t->params.scale;
  const int   z_out = output_t->params.zero_point;
  float y_norm = (output_t->data.int8[0] - z_out) * s_out;
  float y_degC = y_norm * TRAIN_STD + TRAIN_MEAN;

  // 5) Residual + slope (°C/min)
  int last_idx = (head + W - 1) % W;
  float latest = ring[last_idx];
  float prev   = ring[(last_idx - 1 + W) % W];
  float slope  = latest - prev;
  float resid  = fabsf(latest - y_degC);

  bool window_open  = fabsf(slope) > SLOPE_THR;
  bool temp_anom    = resid > RESID_THR;

  
  // publish per-minute state (always)
  ml_on_state(latest, y_degC, resid, slope, minute_of_day,
              temp_anom ? 1 : 0, window_open ? 1 : 0);
// 6) Event publish / log (only when thresholds trip)
  if (window_open || temp_anom) {
    printf("{\"event\":\"temp_anomaly\",\"obs\":%.3f,\"pred\":%.3f,\"resid\":%.3f,\"slope\":%.3f,\"tod\":%d}\n",
           latest, y_degC, resid, slope, minute_of_day);
    // mqtt_publish(...);
  }
}
