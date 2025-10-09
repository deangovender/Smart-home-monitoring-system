// main/model_consts.h
/**
 * Training-derived constants.
 * - W: sliding window length (samples).
 * - TRAIN_MEAN / TRAIN_STD: z-score normalization parameters for temperature (°C).
 * - RESID_THR: residual threshold (|obs - pred|) in °C used to flag anomalies.
 * - SLOPE_THR: slope threshold in °C/min used to flag rapid changes.
 *
 * Notes: We preserve legacy macro names for compatibility, and provide
 * descriptive aliases below for readability. Adjust with care—model expects
 * these to match its training distribution.
 */
#pragma once
#define W 30
#define TRAIN_MEAN 24.7678318f
#define TRAIN_STD  0.60468835f
#define RESID_THR  0.08075944f   // °C
#define SLOPE_THR  0.07999992f   // °C/min

// Descriptive aliases (no behavior change)
#ifndef WINDOW_SIZE_SAMPLES
#define WINDOW_SIZE_SAMPLES W
#endif
#ifndef TEMP_Z_MEAN_C
#define TEMP_Z_MEAN_C TRAIN_MEAN
#endif
#ifndef TEMP_Z_STD_C
#define TEMP_Z_STD_C  TRAIN_STD
#endif
#ifndef RESIDUAL_THRESHOLD_C
#define RESIDUAL_THRESHOLD_C RESID_THR
#endif
#ifndef SLOPE_THRESHOLD_C_PER_MIN
#define SLOPE_THRESHOLD_C_PER_MIN SLOPE_THR
#endif
