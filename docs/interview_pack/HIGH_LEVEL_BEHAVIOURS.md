# High‑Level Behaviours (Evidence‑Based)

## Project structure & build systems (from repo docs/config)
- **Firmware (ESP‑IDF CMake project):** `Firmware/ESP32` with IDF CMake project + `main/` component sources and headers.【F:Firmware/ESP32/CMakeLists.txt†L1-L8】【F:Firmware/ESP32/main/CMakeLists.txt†L1-L29】
- **Home Assistant config:** YAML configuration for MQTT entities, automations, and dashboard UI in `HomeAssistant/`.【F:HomeAssistant/config.yaml†L1-L97】【F:HomeAssistant/automations.yaml†L1-L75】【F:HomeAssistant/dashboard.yaml†L1-L52】
- **Documented repo structure:** README describes firmware + HA configuration layout and model assets (used as a structural reference).【F:README.md†L15-L37】

## 1) System overview
- ESP32 node uses PIR (GPIO ISR) + SHT40 (I²C) to generate motion and air telemetry, then publishes to MQTT with QoS 1 retention where appropriate.【F:Firmware/ESP32/main/main.c†L22-L23】【F:Firmware/ESP32/main/main.c†L86-L102】【F:Firmware/ESP32/main/main.c†L237-L289】
- TinyML temperature forecast and occupancy classification run on-device using TFLite Micro; ML results are published to MQTT for Home Assistant consumption.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L29】【F:Firmware/ESP32/main/main.c†L479-L519】【F:Firmware/ESP32/main/ml.cpp†L17-L121】【F:Firmware/ESP32/main/ml_occ.cpp†L1-L189】
- Home Assistant consumes MQTT topics for sensors, ML inference, and occupancy state, then enforces a light switch with a manual override helper.【F:HomeAssistant/config.yaml†L11-L97】【F:HomeAssistant/automations.yaml†L1-L75】

## 2) Runtime behaviours

### Motion detection logic (interrupt → queue → task)
- PIR GPIO edges trigger an ISR that pushes events into a FreeRTOS queue; `pir_task` consumes the queue and publishes motion state/event JSON on rising edges.【F:Firmware/ESP32/main/main.c†L237-L289】
- Motion start increments a correlation ID and triggers occupancy ML edge-note + immediate MQTT state/event publishes.【F:Firmware/ESP32/main/main.c†L259-L275】
- Evidence:
  - ISR queue push: `pir_isr` uses `xQueueSendFromISR` with monotonic timestamp.【F:Firmware/ESP32/main/main.c†L237-L247】
  - Task processing + publish: `pir_task` publishes state/event JSON and notifies ML occupancy on motion start.【F:Firmware/ESP32/main/main.c†L259-L275】

### Temperature/Humidity sampling (interval + ML feed)
- SHT40 is read via I²C with CRC validation; readings are published as JSON and fed into the temperature ML pipeline.【F:Firmware/ESP32/main/main.c†L391-L517】
- Sampling period is 60 seconds (minute readings).【F:Firmware/ESP32/main/main.c†L516-L518】
- Evidence:
  - SHT40 measure & CRC guard: `sht40_measure` validates CRC and converts raw data to °C/%RH.【F:Firmware/ESP32/main/main.c†L391-L451】
  - Sample → MQTT publish → ML feed: `sht40_task` publishes `home/study/air`, then pushes temp into ML and invokes inference.【F:Firmware/ESP32/main/main.c†L457-L507】

### TinyML temperature forecaster
- Uses a sliding window (`W`) of temperature samples and time‑of‑day features; runs TFLite Micro inference, computes residual/slope, and calls back into the app to publish inference JSON.【F:Firmware/ESP32/main/ml.cpp†L17-L121】【F:Firmware/ESP32/include/model_consts.h†L12-L33】
- Evidence:
  - Model inference & residual/slope computation: `ml_infer_and_publish` builds features, quantizes, invokes TFLM, and computes residual/slope thresholds.【F:Firmware/ESP32/main/ml.cpp†L63-L120】
  - App callback publishes `home/study/ml/inference` JSON and a debug ping topic.【F:Firmware/ESP32/main/main.c†L481-L519】

### TinyML occupancy classifier (minute tick + hysteresis)
- Occupancy inference runs every minute; feature set includes PIR hits, minutes since motion, and time‑of‑day; decisions use hysteresis (2‑on / 5‑off).【F:Firmware/ESP32/main/main.c†L295-L316】【F:Firmware/ESP32/main/ml_occ.cpp†L96-L189】
- Evidence:
  - Minute tick scheduler: `occ_task` aligns to minute boundary and ticks every 60s.【F:Firmware/ESP32/main/main.c†L295-L316】
  - Hysteresis logic: `ml_occ_tick_minute` enforces 2‑on/5‑off thresholds with edge snaps.【F:Firmware/ESP32/main/ml_occ.cpp†L147-L189】

### MQTT publishing/subscribing
- ESP32 publishes multiple topics with QoS 1 (retained for state/air/inference) and logs PUBACK latency per message ID for timing instrumentation.【F:Firmware/ESP32/main/main.c†L45-L76】【F:Firmware/ESP32/main/main.c†L220-L312】
- Home Assistant subscribes to those topics to create sensors, binary sensors, and ML anomaly detection entities.【F:HomeAssistant/config.yaml†L11-L70】
- Evidence:
  - Publish helpers and latency tracking: `track_publish`, `mqtt_pub_state_json`, and PUBACK handler log latency.【F:Firmware/ESP32/main/main.c†L45-L69】【F:Firmware/ESP32/main/main.c†L201-L273】
  - HA MQTT entities configured on `home/study/*` topics.【F:HomeAssistant/config.yaml†L11-L70】

### Connection management / time sync
- Wi‑Fi is initialized in STA mode with explicit connect call; MQTT client is created and event handler tracks connect/disconnect states.【F:Firmware/ESP32/main/main.c†L318-L380】【F:Firmware/ESP32/main/main.c†L530-L548】
- SNTP time sync is performed at boot for timestamped telemetry payloads.【F:Firmware/ESP32/main/main.c†L161-L184】【F:Firmware/ESP32/main/main.c†L530-L539】
- Evidence:
  - Wi‑Fi init: `wifi_init` configures STA and connects.【F:Firmware/ESP32/main/main.c†L318-L337】
  - MQTT event handling: connect/disconnect and PUBACK logic in `mqtt_event_handler`.【F:Firmware/ESP32/main/main.c†L339-L380】
  - SNTP startup: `sntp_start_and_wait` is invoked before MQTT starts.【F:Firmware/ESP32/main/main.c†L161-L184】【F:Firmware/ESP32/main/main.c†L530-L539】

### Home Assistant automation behaviour
- Light is enforced based on ML occupancy, with a manual override toggle that suspends automation when a user manually switches the light.【F:HomeAssistant/automations.yaml†L1-L75】
- Evidence:
  - ML enforcement + minute reconciliation: automation triggers on occupancy changes, HA start, and every minute.【F:HomeAssistant/automations.yaml†L1-L36】
  - Override handling: manual switch changes toggle an internal `input_boolean` to suspend/restore automation.【F:HomeAssistant/automations.yaml†L41-L75】

### Fail‑safes / error handling
- MQTT publishes are gated by `s_mqtt_connected` checks; SHT40 read failures are logged without crashing the task.【F:Firmware/ESP32/main/main.c†L105-L119】【F:Firmware/ESP32/main/main.c†L457-L518】
- Evidence:
  - Publish guards: MQTT helper functions early‑return when disconnected.【F:Firmware/ESP32/main/main.c†L105-L119】
  - Sensor error log: SHT40 read failure logs warning and continues loop.【F:Firmware/ESP32/main/main.c†L510-L517】

## 3) Data flow summary
- PIR/SHT40 → ESP32 tasks → MQTT topics (`home/study/*`) → Home Assistant MQTT entities → automations + dashboard UI.【F:Firmware/ESP32/main/main.c†L237-L518】【F:HomeAssistant/config.yaml†L11-L97】【F:HomeAssistant/automations.yaml†L1-L75】

## 4) Performance constraints inferred
- **Sampling rate:** SHT40 air readings every 60 seconds, occupancy inference every 60 seconds.【F:Firmware/ESP32/main/main.c†L295-L316】【F:Firmware/ESP32/main/main.c†L516-L518】
- **I²C bus speed:** 100 kHz configured for SHT40 bus.【F:Firmware/ESP32/main/main.c†L73-L82】
- **ML window size:** temperature model uses a 30‑sample window, implying at least 30 minutes of history for inference.【F:Firmware/ESP32/include/model_consts.h†L12-L23】【F:Firmware/ESP32/main/ml.cpp†L20-L44】
