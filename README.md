# Smart‑Home Monitoring & Energy‑Saving System (ESP32 + Home Assistant)

A small, local-first system that detects room occupancy and reduces wasted energy. An ESP32 node with a PIR and SHT40 sensor publishes telemetry to a Home Assistant broker via MQTT. A Sonoff R4 Basic relay switches the light. Two TinyML models run on-device: a temperature forecaster (for anomaly flags) and a lightweight occupancy classifier. Latency is measured end‑to‑end using MQTT PUBACKs (QoS 1).

---

## Contents

- **Firmware (ESP32, ESP‑IDF)** — C/C++ application, TinyML glue, and model blobs.
- **Home Assistant configuration** — Automations, sensors, and a simple dashboard.
- **(Optional) Model assets** — INT8 `.tflite` files.

---

## Repository structure (recommended)

```
smart-home-monitoring-system/
├── firmware/
│   ├── src/
│   │   ├── main.c
│   │   ├── ml.cpp
│   │   ├── ml_occ.cpp
│   │   ├── model_data.cc
│   │   └── model_data_occ.cc
│   ├── include/
│   │   ├── ml.h
│   │   ├── ml_occ.h
│   │   ├── model_consts.h
│   │   ├── model_data.h
│   │   └── model_data_occ.h
│   ├── models/
│   │   ├── temp_model_int8.tflite
│   │   └── occ_model_int8.tflite
│   ├── tools/
│   │   └── gen_model_header.py
│   └── CMakeLists.txt
└── ha/
    ├── configuration.yaml
    ├── automations.yaml
    └── dashboard.yaml
```

---

## Hardware

- ESP32 DevKit‑C (Wi‑Fi STA)
- PIR motion sensor (GPIO ISR)
- SHT40 temperature/humidity (I²C @ 0x44)
- Sonoff R4 Basic (relay) controlled by Home Assistant
- Home Assistant OS (broker) on Raspberry Pi

---

## Firmware overview

- `pir_isr` → queue → `pir_task`: publishes `motion/state` and event JSON immediately.
- `sht40_task`: reads SHT40 every **60,000 ms** and publishes `air` JSON.
- `occ_task`: minute tick; runs occupancy logic and publishes `occ/state` (probability + decision).
- MQTT QoS **1** across telemetry → PUBACK collected in `mqtt_event_handler` for latency.

**MQTT topics**
```
home/study/motion/state
home/study/motion/event
home/study/motion/state_json
home/study/motion/event_json
home/study/air
home/study/occ/state
home/study/ml/ping
home/study/ml/inference
```

**TinyML**
- Temperature forecaster: residual & slope thresholds in `model_consts.h`.
- Occupancy classifier: PIR hits, minutes since motion, and time‑of‑day features.

---

## Build & flash (ESP‑IDF)

```
idf.py set-target esp32
idf.py menuconfig   # set Wi‑Fi SSID/PASS & MQTT broker URI if using Kconfig
idf.py build flash monitor
```

**Secrets**
```c
#define WIFI_SSID  "YOUR_WIFI_SSID"
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"
#define BROKER_URI "mqtt://BROKER_HOST_OR_IP"
.username = "YOUR_MQTT_USERNAME",
.authentication = { .password = "YOUR_MQTT_PASSWORD" },
```

**Model blobs**
- Keep `.tflite` files in `firmware/models/` and generate `model_data*.{h,cc}` during build, **or** commit the generated blobs if you prefer zero‑tool builds.

---

## Home Assistant

Place the files from `ha/` into your HA config directory and reload:

- `configuration.yaml` — MQTT `sensor`/`binary_sensor` entities.
- `automations.yaml` — ML enforcement + manual override (`input_boolean.study_override_internal`).
- `dashboard.yaml` — Lovelace view (toggle + charts).

### Prerequisites (HA Add‑ons & Setup)

**Required add‑on (if HA is your broker): Mosquitto broker**
1. **Install:** Settings → Add‑ons → Add‑on Store → *Mosquitto broker* → Install → Start → enable Watchdog.
2. **Create MQTT user:** Settings → People → Users → *Add user* (e.g., `mqtt`). Use this username/password in the ESP32 firmware.
3. **Integration:** Settings → Devices & services → **+ Add integration** → *MQTT* (or auto‑discovered). Broker: `localhost` (if add‑on) or your Pi’s IP. Port: **1883**.
4. **Auth & anonymity:** Keep **anonymous: false**. Use the HA user credentials created above.

**Optional helpers for editing HA config**
- *Studio Code Server* (rich editor) **or** *File editor* (simple).
- Or use *Samba share* / *Terminal & SSH* add‑ons.

**Create the override helper (if not using YAML)**
- Settings → Devices & services → **Helpers** → **Create helper** → *Toggle*  
  Name: **Study Override (internal)** → Entity ID: `input_boolean.study_override_internal`

**Import the dashboard**
- Settings → Dashboards → **+ Add Dashboard** → *Create*  
  Three‑dot menu → **Raw configuration editor** → paste `ha/dashboard.yaml`.

**Quick smoke test**
- Developer Tools → **MQTT** → *Listen to a topic* → `home/study/#`  
  Trigger your PIR or wait one minute: expect `motion/state`, `air`, and `occ/state` messages; dashboard icons colorize when “on/true”.

> Using an external MQTT broker? Skip the add‑on and set `BROKER_URI` accordingly.

---

## File‑by‑file index

**Firmware**
- `firmware/src/main.c` — app entry, Wi‑Fi, MQTT client, ISR/queues, tasks, JSON pubs.
- `firmware/src/ml.cpp` / `ml_occ.cpp` — TinyML glue (forecaster/occupancy).
- `firmware/include/ml.h` / `ml_occ.h` — public C APIs.
- `firmware/include/model_consts.h` — thresholds/params.
- `firmware/src|include/model_data*.{cc,h}` — *(generated)* model blobs.

**Home Assistant**
- `ha/configuration.yaml` — sensors and binary_sensors.
- `ha/automations.yaml` — minute‑tick enforcement + override.
- `ha/dashboard.yaml` — Lovelace dashboard.

---

## License

MIT

---

## Hardware chosen

- **ESP32 DevKit‑C** — Wi‑Fi STA node running the firmware.
- **PIR motion sensor** — GPIO interrupt (edge -> queue -> `pir_task`).
- **SHT40** — Temperature/humidity via I²C (addr `0x44`).
- **Sonoff R4 Basic** — Relay to switch the room light. Can be controlled via:
  - **eWeLink cloud** using the *eWeLink Smart Home* integration (HACS), or
  - **Local control** using the community *Sonoff LAN* integration (HACS).

> If you prefer strictly local operation, use *Sonoff LAN*. If cloud is fine and you want quick setup, *eWeLink Smart Home* is easy.

---

## Sonoff / eWeLink integration (via HACS)

**Install HACS (Home Assistant Community Store)**  
Follow the official steps (HACS installer / "get hacs") and restart Home Assistant. After restart you’ll see **HACS** in the sidebar.

**Add the integration via HACS**
1. HACS → **Integrations** → **Explore & Download Repositories**.
2. Install **eWeLink Smart Home** (cloud) **or** **Sonoff LAN** (local).
3. Restart Home Assistant when prompted.

**Configure the integration**
- Settings → Devices & services → **+ Add integration** → search for the one you installed.  
- For *eWeLink Smart Home*: sign in with your eWeLink account.  
- For *Sonoff LAN*: follow the integration’s instructions; most devices are auto‑discovered on the LAN.

**Use the switch in your UI**  
Once added, your relay appears as a `switch.*` entity (e.g., `switch.study_light`). Add it to the dashboard or wire it into your automations.

Example Lovelace card:
```yaml
type: tile
entity: switch.study_light     # replace with your actual entity_id
name: Study Light
tap_action:
  action: toggle
```

---

## Remote access (Tailscale add‑on)

If you want secure remote access without opening ports:

1. Settings → **Add‑ons** → Add‑on Store → search **Tailscale** → Install → Start.
2. Open the add‑on **Web UI** → **Authenticate** with your Tailscale account.
3. Your HA instance now has a Tailscale IP/URL on your tailnet. Use it to reach HA remotely.
4. Optional: enable *Auto start* and *Watchdog* in the add‑on so it survives reboots.

> Tip: keep MQTT internal (no WAN exposure). Remote access is only for the HA UI via Tailscale.
