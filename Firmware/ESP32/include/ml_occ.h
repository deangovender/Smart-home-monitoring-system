/**
 * Occupancy-classifier TinyML interface
 * 
 * Public C API for the TinyML occupancy classifier.
 * The model maintains internal hysteresis (2-on / 5-off) to avoid flicker.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void ml_occ_init(void);                 //  Initialize the occupancy model once at boot (e.g., after network comes up).
void ml_occ_note_motion_start(void);    //  Notify a motion start (call on PIR rising, after your debounce/filter).
void ml_occ_note_motion_end(void);      //  Notify a motion end (call when your clear-timer confirms “no motion”).
void ml_occ_tick_minute(int minute_of_day, float *prob_out, int *occupied_out);
//  On return: prob_out in [0..1], occupied_out is 0/1 (internal 2-on / 5-off hysteresis).

#ifdef __cplusplus
}
#endif
