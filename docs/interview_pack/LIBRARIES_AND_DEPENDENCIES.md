# Libraries & Dependencies (Evidence-Based)

## A) Embedded/Firmware (ESP-IDF) Dependencies

### ESP-IDF components (build-time requirements)
- **esp_wifi** — Wi‑Fi station networking stack used for STA connect and network init.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L14-L18】【F:Firmware/ESP32/main/main.c†L318-L337】
- **esp_event** — event loop required by Wi‑Fi/MQTT initialization.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L14-L20】【F:Firmware/ESP32/main/main.c†L318-L337】
- **esp_netif** — network interface initialization for STA mode.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L14-L18】【F:Firmware/ESP32/main/main.c†L318-L337】
- **nvs_flash** — non‑volatile storage init for Wi‑Fi credentials stack.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L14-L17】【F:Firmware/ESP32/main/main.c†L530-L536】
- **mqtt** (ESP‑IDF MQTT client) — publishes all telemetry to the broker and handles PUBACK latency tracking.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L20-L21】【F:Firmware/ESP32/main/main.c†L276-L312】
- **driver** — GPIO (PIR ISR) and I2C (SHT40) drivers.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L22-L23】【F:Firmware/ESP32/main/main.c†L457-L518】
- **esp_timer** — monotonic timing for ISR/event correlation and latency metrics.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】【F:Firmware/ESP32/main/main.c†L19-L20】【F:Firmware/ESP32/main/main.c†L45-L69】
- **lwip** — network stack dependency for MQTT/Wi‑Fi.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L28】
- **esp-tflite-micro** — TinyML runtime for temperature forecast + occupancy classifier models.【F:Firmware/ESP32/main/CMakeLists.txt†L14-L29】【F:Firmware/ESP32/main/ml.cpp†L13-L16】【F:Firmware/ESP32/main/ml_occ.cpp†L13-L16】

### Direct include dependencies (runtime code)
- **FreeRTOS** — task creation and queues (pir_task, sht40_task, occ_task).【F:Firmware/ESP32/main/main.c†L8-L11】【F:Firmware/ESP32/main/main.c†L542-L559】
- **SNTP (esp_sntp)** — time synchronization for timestamps in MQTT JSON payloads.【F:Firmware/ESP32/main/main.c†L19-L21】【F:Firmware/ESP32/main/main.c†L161-L184】
- **C/C++ standard libs** — math/time utilities for feature construction and timestamp formatting.【F:Firmware/ESP32/main/main.c†L1-L7】【F:Firmware/ESP32/main/ml.cpp†L7-L10】【F:Firmware/ESP32/main/ml_occ.cpp†L7-L8】

### Embedded model assets
- **Temperature model blob** — compiled into `model_data.cc` and used by TFLM in `ml.cpp`.【F:Firmware/ESP32/main/CMakeLists.txt†L3-L13】【F:Firmware/ESP32/main/ml.cpp†L11-L16】
- **Occupancy model blob** — compiled into `model_data_occ.cc` and used by TFLM in `ml_occ.cpp`.【F:Firmware/ESP32/main/CMakeLists.txt†L7-L13】【F:Firmware/ESP32/main/ml_occ.cpp†L11-L16】

## B) Python Dependencies
- **TBD** — no Python dependency manifest is referenced in the documented repo structure; verify if any external tooling is used outside the tracked files.【F:README.md†L15-L37】

## C) Node/npm Dependencies
- **TBD** — no Node/npm dependency manifest is referenced in the documented repo structure; verify if any external tooling is used outside the tracked files.【F:README.md†L15-L37】

## D) Home Assistant Integrations / Components
- **MQTT integration** — HA uses MQTT `sensor` and `binary_sensor` entities mapped to ESP32 topics for motion, air, ML inference, and occupancy probability/state.【F:HomeAssistant/config.yaml†L11-L70】
- **Template sensors** — “Minutes Since Motion” and “Study Automation Status” computed from MQTT sensors and `input_boolean` override state.【F:HomeAssistant/config.yaml†L72-L97】
- **Automation rules** — ML‑driven light enforcement with manual override via switch toggles.【F:HomeAssistant/automations.yaml†L1-L75】

## E) Tooling / Build Dependencies
- **ESP‑IDF CMake project** — IDF project scaffold using `project.cmake` and `idf_component_register` for components/deps.【F:Firmware/ESP32/CMakeLists.txt†L1-L8】【F:Firmware/ESP32/main/CMakeLists.txt†L3-L29】
