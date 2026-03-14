# Smart Home Monitoring Node (ESP32 + MQTT + Home Assistant)

## Problem (2 lines)
- Reduce wasted energy by detecting real room occupancy and automating lighting, while keeping telemetry local to the home network.【F:HomeAssistant/automations.yaml†L1-L36】
- Provide a simple, observable telemetry pipeline for motion + environment sensing and on‑device ML inference.【F:Firmware/ESP32/main/main.c†L237-L519】

## Solution (3–5 bullets)
- ESP32 node captures PIR motion via ISR + FreeRTOS queue and publishes MQTT state/event updates with timestamps.【F:Firmware/ESP32/main/main.c†L237-L275】
- SHT40 temperature/humidity sampled every 60 seconds, published to MQTT, and fed into a TinyML forecaster.【F:Firmware/ESP32/main/main.c†L457-L518】【F:Firmware/ESP32/main/ml.cpp†L17-L121】
- Occupancy classifier runs every minute with hysteresis and pushes probability + decision to MQTT for UI stability.【F:Firmware/ESP32/main/main.c†L295-L316】【F:Firmware/ESP32/main/ml_occ.cpp†L147-L189】
- Home Assistant consumes MQTT topics and enforces a Sonoff switch with a manual override helper.【F:HomeAssistant/config.yaml†L11-L97】【F:HomeAssistant/automations.yaml†L1-L75】

## Architecture (tiny diagram)
```
[PIR + SHT40] -> [ESP32 (FreeRTOS + TFLM)] -> MQTT -> [Home Assistant]
                                      \-> ML inference JSON -> Sensors/Automations
```
Evidence: ESP32 tasks + ML inference + MQTT publish flow and HA MQTT sensors/automations.【F:Firmware/ESP32/main/main.c†L237-L519】【F:Firmware/ESP32/main/ml.cpp†L17-L121】【F:HomeAssistant/config.yaml†L11-L97】【F:HomeAssistant/automations.yaml†L1-L75】

## Key behaviours (5 bullets)
- Interrupt‑driven PIR events trigger immediate MQTT motion state/event publishes with wall‑clock timestamps.【F:Firmware/ESP32/main/main.c†L237-L275】
- Telemetry pipeline publishes air readings and ML inference JSON retained to survive HA restarts.【F:Firmware/ESP32/main/main.c†L457-L519】
- Occupancy decisions use minute‑level features and 2‑on/5‑off hysteresis for stable UI states.【F:Firmware/ESP32/main/ml_occ.cpp†L147-L189】
- MQTT PUBACK latency is tracked for QoS 1 messages to instrument end‑to‑end latency.【F:Firmware/ESP32/main/main.c†L45-L69】【F:Firmware/ESP32/main/main.c†L356-L369】
- Automation respects manual override via a hidden `input_boolean` toggle in Home Assistant.【F:HomeAssistant/config.yaml†L90-L97】【F:HomeAssistant/automations.yaml†L41-L75】

## Tech stack
- **Firmware:** ESP‑IDF + FreeRTOS, ESP MQTT client, ESP‑TFLite‑Micro.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L29】【F:Firmware/ESP32/main/main.c†L8-L23】
- **Home Assistant:** MQTT sensors/binary_sensors, YAML automations, Lovelace dashboard.【F:HomeAssistant/config.yaml†L11-L97】【F:HomeAssistant/automations.yaml†L1-L75】【F:HomeAssistant/dashboard.yaml†L1-L52】

## Engineering challenges (evidence‑backed)
- **Latency visibility:** correlation IDs + PUBACK timing for QoS 1 publishes.【F:Firmware/ESP32/main/main.c†L45-L69】【F:Firmware/ESP32/main/main.c†L356-L369】
- **Timestamp correctness:** SNTP sync + ISO8601 formatting for payloads.【F:Firmware/ESP32/main/main.c†L122-L159】【F:Firmware/ESP32/main/main.c†L161-L184】
- **Occupancy stability:** minute‑level hysteresis and idle‑decay logic to avoid flicker.【F:Firmware/ESP32/main/ml_occ.cpp†L115-L189】

## Testing / validation
- **TBD:** No explicit test or validation scripts are referenced in the repo docs or configuration; verify external test coverage if any.【F:README.md†L70-L109】

## Future improvements (evidence‑grounded gaps)
- **TBD:** Consider documenting/adding test coverage for firmware and HA automations; no test hooks are described in repo docs.【F:README.md†L70-L109】
- **TBD:** Consider clarifying payload schema mappings between firmware and HA templates if the JSON keys change; keep config and firmware in sync.【F:Firmware/ESP32/main/main.c†L93-L111】【F:HomeAssistant/config.yaml†L11-L31】
- **TBD:** Consider adding explicit reconnect/backoff strategy for MQTT if needed; current code only tracks connect/disconnect events.【F:Firmware/ESP32/main/main.c†L339-L380】
