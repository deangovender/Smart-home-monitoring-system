/**
 * Temperature-forecast TinyML interface
 * 
 * Public C API for the TinyML temperature forecasting pipeline.
 * Functions are C-linkage safe and do not allocate memory dynamically.
 * 
 * Call order (typical):
 *  1) ml_init() once at startup.
 *  2) ml_push_temp() after each new temperature reading.
 *  3) ml_infer_and_publish() when you have W samples or on a 1-minute tick.
 *  4) ml_on_state() is a callback target used by the app to publish results.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void ml_init(void);                       //  Call this once during system startup.
void ml_reset(void);                      //  Clear the internal 30-sample ring buffer (use after long data gaps).
void ml_push_temp(float t_degC);          //  Push one new temperature reading in degrees Celsius.
void ml_infer_and_publish(int minute_of_day); //  Run inference when you have W samples (or on a minute boundary).
void ml_on_state(float obs, float pred, float resid, float slope, int minute_of_day, int anomaly, int window_open);


#ifdef __cplusplus
}
#endif
