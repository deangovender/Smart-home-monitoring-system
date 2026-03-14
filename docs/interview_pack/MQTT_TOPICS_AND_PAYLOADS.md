# MQTT Topics & Payloads (Evidence‑Based)

> Direction is from the ESP32 unless noted (Home Assistant subscribes to these topics).

| Topic | Direction | Payload example | Trigger condition | Evidence |
| --- | --- | --- | --- | --- |
| `home/study/motion/state` | Publish | `occupied` / `vacant` (plain string) | Motion edge handling; state publish on motion detection and on MQTT connect (current state). | `TOPIC_STATE` constant + `mqtt_pub_state_plain` usage in `pir_task` and `mqtt_event_handler`.【F:Firmware/ESP32/main/main.c†L86-L111】【F:Firmware/ESP32/main/main.c†L259-L275】【F:Firmware/ESP32/main/main.c†L339-L356】 |
| `home/study/motion/event` | Publish | **TBD** (plain string; topic defined, but no publish call shown in code) | TBD — topic defined but no publish call found in firmware. | Topic constant is defined without usage in code.【F:Firmware/ESP32/main/main.c†L86-L92】 |
| `home/study/motion/state_json` | Publish | `{"state":"occupied","timestamp":"2025-01-01T00:00:00.000Z"}` | Motion edge handling and on MQTT connect (current state). | `STATE_JSON_FMT` + `mqtt_pub_state_json` in `pir_task` and `mqtt_event_handler`.【F:Firmware/ESP32/main/main.c†L93-L111】【F:Firmware/ESP32/main/main.c†L201-L273】【F:Firmware/ESP32/main/main.c†L339-L356】 |
| `home/study/motion/event_json` | Publish | `{"event":"detected","timestamp":"2025-01-01T00:00:00.000Z"}` | Published on motion detected (rising edge). | `EVENT_JSON_FMT` + `mqtt_pub_event_json` usage in `pir_task`.【F:Firmware/ESP32/main/main.c†L93-L111】【F:Firmware/ESP32/main/main.c†L226-L275】 |
| `home/study/air` | Publish | `{"t":22.50,"rh":45.10,"timestamp":"2025-01-01T00:00:00.000Z"}` | Every 60 seconds after SHT40 sample completes. | `AIR_JSON_FMT` + `sht40_task` publish loop + 60s delay.【F:Firmware/ESP32/main/main.c†L93-L111】【F:Firmware/ESP32/main/main.c†L457-L518】 |
| `home/study/occ/state` | Publish | `{"prob":0.732,"occupied":1,"timestamp":"2025-01-01T00:00:00.000Z"}` | Every minute in `occ_task` and on motion edge snap publish. | `OCC_JSON_FMT` + `occ_task` + `mqtt_pub_occ_prob` call on motion detect.【F:Firmware/ESP32/main/main.c†L90-L119】【F:Firmware/ESP32/main/main.c†L259-L316】 |
| `home/study/ml/ping` | Publish | `tick 1 anom=0` | Emitted on each ML temperature state callback. | Publish call in `ml_on_state`.【F:Firmware/ESP32/main/main.c†L481-L492】 |
| `home/study/ml/inference` | Publish | `{"obs":22.50,"pred":22.60,"resid":0.10,"slope":0.020,"minute":123,"anomaly":0,"window_open":0,"timestamp":"2025-01-01T00:00:00.000Z"}` | Emitted when temperature ML inference runs. | JSON payload construction + publish in `ml_on_state`.【F:Firmware/ESP32/main/main.c†L494-L519】 |

## Home Assistant subscriptions (entity mapping)
- Motion, air, occupancy, and ML inference are consumed via MQTT `sensor` / `binary_sensor` entities, e.g., `home/study/air`, `home/study/motion/state`, and `home/study/ml/inference`.【F:HomeAssistant/config.yaml†L11-L70】
