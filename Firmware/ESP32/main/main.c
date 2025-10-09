#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_sntp.h"

#include "mqtt_client.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "ml.h"
#include "ml_occ.h"
#include <math.h>
#include "model_consts.h"

/* ---- Latency instrumentation ---- */
static uint32_t g_occ_evt_id = 0;
static uint32_t g_temp_evt_id = 0;

/* Small ring buffer to map MQTT msg_id -> (evt_id, start_mono_us, topic) */
typedef struct {
    int msg_id;
    uint32_t evt_id;
    int64_t start_mono_us;
    const char *topic;  // pointer to const string literal
} pub_track_t;

#define PUB_TRACK_SZ 16
static pub_track_t g_pub_track[PUB_TRACK_SZ];
static int g_pub_track_head = 0;

static void track_publish(int msg_id, uint32_t evt_id, const char *topic, int64_t start_mono_us) {
    pub_track_t *s = &g_pub_track[g_pub_track_head++ % PUB_TRACK_SZ];
    s->msg_id = msg_id;
    s->evt_id = evt_id;
    s->start_mono_us = start_mono_us;
    s->topic = topic;
}

static bool lookup_publish(int msg_id, pub_track_t *out) {
    for (int i = 0; i < PUB_TRACK_SZ; ++i) {
        if (g_pub_track[i].msg_id == msg_id) { *out = g_pub_track[i]; return true; }
    }
    return false;
}

static const char *TAG = "MQTT_PIR_AIR_TS";


/*
 * Configuration notes (2025-10-08):
 * - Secrets removed. Fill in WIFI_SSID, WIFI_PASS, BROKER_URI,
 *   and MQTT username/password with your own values.
 * - Air sampling period set to 60 s for minute readings.
 * - Occupancy tick remains 60 s.
 * - PIR grace timer removed. On a falling edge, vacancy is published immediately.
 */
/* -------- Wi-Fi ---------- */
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"

/* -------- MQTT broker ---------- */
#define BROKER_URI     "mqtt://BROKER_HOST_OR_IP"   // change to HA-broker IP when you move it

/* -------- PIR wiring ---------- */
#define PIR_GPIO       GPIO_NUM_33

/* -------- I2C + SHT40 ---------- */
#define I2C_SDA        GPIO_NUM_21
#define I2C_SCL        GPIO_NUM_22
#define I2C_HZ         100000
#define SHT40_ADDR     0x44

/* -------- MQTT topics ---------- */
// Back-compat plain topics:
#define TOPIC_STATE          "home/study/motion/state"        // "occupied"/"vacant" (retained)
#define TOPIC_EVENT          "home/study/motion/event"        // "detected"/"cleared" (non-retained)
// JSON with timestamps (new):
#define TOPIC_STATE_JSON     "home/study/motion/state_json"   // retained
#define TOPIC_EVENT_JSON     "home/study/motion/event_json"   // non-retained
#define TOPIC_AIR            "home/study/air"                 // retained JSON (T/RH + timestamp)
#define TOPIC_OCC_JSON     "home/study/occ/state"
static const char *OCC_JSON_FMT = "{\"prob\":%.3f,\"occupied\":%d,\"timestamp\":\"%s\"}";
static const char *STATE_JSON_FMT = "{\"state\":\"%s\",\"timestamp\":\"%s\"}";
static const char *EVENT_JSON_FMT = "{\"event\":\"%s\",\"timestamp\":\"%s\"}";
static const char *AIR_JSON_FMT   = "{\"t\":%.2f,\"rh\":%.2f,\"timestamp\":\"%s\"}";

/* -------- Globals ---------- */
static esp_mqtt_client_handle_t s_mqtt = NULL;
static bool s_mqtt_connected = false;

/* I2C handles (new driver API) */
static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t sht40_dev = NULL;

/* PIR event pipeline */
typedef enum { EVT_EDGE=1 } evt_type_t;
typedef struct {
    evt_type_t type;
    int level;                 // for EVT_EDGE
    int64_t t_mono_us;         // monotonic timestamp captured at ISR (or 0 for timer)
} pir_evt_t;

static QueueHandle_t      s_evtq;
static bool               s_motion = false;

/* --------- Time helpers ---------- */

// Convert an event’s monotonic timestamp into wall-clock timeval using “now” alignment.
// event_wall = now_wall - (now_mono - event_mono)
static void wall_time_from_event_mono(int64_t evt_mono_us, struct timeval *out)
{
    struct timeval now_tv;
    gettimeofday(&now_tv, NULL);
    int64_t now_mono = esp_timer_get_time();

    int64_t now_us = (int64_t)now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
    int64_t evt_us = now_us - (now_mono - evt_mono_us);
    if (evt_us < 0) evt_us = 0;

    out->tv_sec  = (time_t)(evt_us / 1000000LL);
    out->tv_usec = (suseconds_t)(evt_us % 1000000LL);
}

static void iso8601_utc_ms_from_timeval(const struct timeval *tv, char *buf, size_t len)
{
    if (!buf || len == 0) return;

    struct tm tm;
    gmtime_r(&tv->tv_sec, &tm);

    unsigned ms = (unsigned)(tv->tv_usec / 1000U); // 0..999

    // "YYYY-MM-DDTHH:MM:SS.mmmZ" = 24 chars + NUL
    (void)snprintf(buf, len,
                   "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}



static void iso8601_utc_ms_now(char *buf, size_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    iso8601_utc_ms_from_timeval(&tv, buf, len);
}
static inline void mqtt_pub_occ_prob(float prob, int occ)
{
    if (!s_mqtt_connected) return;
    char tstr[48]; iso8601_utc_ms_now(tstr, sizeof tstr);
    char json[128];
    int n = snprintf(json, sizeof json, OCC_JSON_FMT,
                     (double)prob, occ, tstr);
    if (n > 0 && n < (int)sizeof(json)) {
        int64_t t0 = esp_timer_get_time();
        int msg_id = esp_mqtt_client_publish(s_mqtt, TOPIC_OCC_JSON, json, 0, 1, 1);
        track_publish(msg_id, g_occ_evt_id, TOPIC_OCC_JSON, t0);
        ESP_LOGI("LAT", "MQTT_PUB_START id=%u msg_id=%d mono_us=%lld topic=%s",
                 g_occ_evt_id, msg_id, (long long)t0, TOPIC_OCC_JSON);
    }
}



static int minute_of_day(void) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return tm_now.tm_hour * 60 + tm_now.tm_min; // 0..1439
}

/* --------- SNTP ---------- */

static void sntp_start_and_wait(const char *server_ip_or_name)
{
    ESP_LOGI(TAG, "SNTP starting (server: %s)", server_ip_or_name);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, server_ip_or_name);          // e.g., "192.168.1.124" (Pi) or "pool.ntp.org"
    sntp_init();

    // Wait until we’ve got time (max ~20s)
    for (int i = 0; i < 10 && sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET; ++i) {
        ESP_LOGI(TAG, "Waiting for time sync... (%d/10)", i+1);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    char ts[48];
    iso8601_utc_ms_now(ts, sizeof ts);
    ESP_LOGI(TAG, "Time synced: %s", ts);
}

/* --------- MQTT helpers ---------- */

static inline void mqtt_pub_state_plain(bool occupied)
{
    if (!s_mqtt_connected) return;
    esp_mqtt_client_publish(s_mqtt, TOPIC_STATE, occupied ? "occupied" : "vacant", 0, 1, 1);
}

static inline void mqtt_pub_state_json(bool occupied, const struct timeval *tv)
{
    if (!s_mqtt_connected) return;
    char tstr[48], json[128];
    iso8601_utc_ms_from_timeval(tv, tstr, sizeof tstr);
    snprintf(json, sizeof json, STATE_JSON_FMT, occupied ? "occupied" : "vacant", tstr);

    int64_t t0   = esp_timer_get_time();                                  // start timestamp
    int     msg_id = esp_mqtt_client_publish(s_mqtt, TOPIC_STATE_JSON,    // do the publish
                                             json, 0, 1, 1);
    track_publish(msg_id, g_occ_evt_id, TOPIC_STATE_JSON, t0);            // remember for PUBACK timing
    ESP_LOGI("LAT", "MQTT_PUB_START id=%u msg_id=%d mono_us=%lld topic=%s",
             g_occ_evt_id, msg_id, (long long)t0, TOPIC_STATE_JSON);
}


static inline void mqtt_pub_event_json(const char *event, const struct timeval *tv)
{
    if (!s_mqtt_connected) return;
    char tstr[48], json[128];
    iso8601_utc_ms_from_timeval(tv, tstr, sizeof tstr);
    snprintf(json, sizeof json, EVENT_JSON_FMT, event, tstr);
    esp_mqtt_client_publish(s_mqtt, TOPIC_EVENT_JSON, json, 0, 1, 0);
}

/* --------- ISR ---------- *//

static void IRAM_ATTR pir_isr(void *arg)
{
    pir_evt_t e = {
        .type = EVT_EDGE,
        .level = gpio_get_level(PIR_GPIO),
        .t_mono_us = esp_timer_get_time()
    };
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_evtq, &e, &hpw);
    if (hpw) portYIELD_FROM_ISR();
}

/* --------- PIR task ---------- */

static void pir_task(void *arg)
{
    s_motion = (gpio_get_level(PIR_GPIO) == 1); // initial state
    pir_evt_t e;

    for (;;) {
        if (xQueueReceive(s_evtq, &e, portMAX_DELAY)) {
            if (e.type == EVT_EDGE) {
                struct timeval evt_tv; wall_time_from_event_mono(e.t_mono_us, &evt_tv);

                if (e.level == 1) {
                    if (!s_motion) {
                        s_motion = true;
                        g_occ_evt_id++;  // <<< correlate this motion burst
                        ESP_LOGI("LAT", "PIR_TRIGGER id=%u mono_us=%lld",
                                g_occ_evt_id, (long long)e.t_mono_us);  // t0
                        mqtt_pub_state_plain(true);
                        mqtt_pub_state_json(true, &evt_tv);
                        ml_occ_note_motion_start();
                        mqtt_pub_occ_prob(0.55f, 1); // edge snap publish (see §4 to time it too)
                        mqtt_pub_event_json("detected", &evt_tv);
                        ESP_LOGI(TAG, "Motion detected @ %ld.%03lds",
                                (long)evt_tv.tv_sec, (long)(evt_tv.tv_usec/1000));
                    }
} else {
                    start_clear_timer_sec(10); // confirm clear after grace period
                    ESP_LOGI(TAG, "Low observed; arm clear timer");
                }
            }
            }
        }
    }
}
}



/* --------- Occupancy (ML) minute task ---------- */

/* --------- Occupancy (ML) minute task ---------- */
static void occ_task(void *arg)
{
    // Align to the next wall-clock minute boundary
    {
        time_t now = time(NULL);
        int sec_rem = (int)(60 - (now % 60));
        if (sec_rem <= 0 || sec_rem > 60) sec_rem = 1;
        vTaskDelay(pdMS_TO_TICKS(sec_rem * 1000));
    }

    // Now tick exactly every 60s with minimal drift
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(60000);

    for (;;) {
        vTaskDelayUntil(&last, period);

        int mod = minute_of_day();
        float prob = 0.0f; 
        int occ = 0;
        ml_occ_tick_minute(mod, &prob, &occ);
        // t1 (occupancy)
        ESP_LOGI("LAT", "ML_DONE id=%u mono_us=%lld model=occ decision=%d prob=%.3f",
                g_occ_evt_id, (long long)esp_timer_get_time(), occ, (double)prob);
        if (s_mqtt_connected) {
            char tstr[48];
            iso8601_utc_ms_now(tstr, sizeof tstr);
            char json[128];
            int n = snprintf(json, sizeof json, OCC_JSON_FMT,
                            (double)prob, occ, tstr);
            if (n > 0 && n < (int)sizeof(json)) {
                int64_t t0 = esp_timer_get_time();
                int msg_id = esp_mqtt_client_publish(s_mqtt, TOPIC_OCC_JSON, json, 0, 1, 1);
                track_publish(msg_id, g_occ_evt_id, TOPIC_OCC_JSON, t0);
                ESP_LOGI("LAT", "MQTT_PUB_START id=%u msg_id=%d mono_us=%lld topic=%s",
                        g_occ_evt_id, msg_id, (long long)t0, TOPIC_OCC_JSON);
            }
        }
    }
}
/* --------- Wi-Fi ---------- */

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wc = {0};
    strncpy((char*)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid));
    strncpy((char*)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

/* --------- MQTT ---------- */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_mqtt_event_handle_t e = event_data;
    switch (e->event_id) {
        case MQTT_EVENT_CONNECTED: {
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT connected");

            // Publish current PIR state + timestamp immediately (unchanged)
            bool now_motion = (gpio_get_level(PIR_GPIO) == 1);
            s_motion = now_motion;
            struct timeval tv; gettimeofday(&tv, NULL);
            mqtt_pub_state_plain(now_motion);
            mqtt_pub_state_json(now_motion, &tv);

            // Your ML setup (unchanged)
            ml_init();
            ml_reset();
            ml_occ_init();
            ESP_LOGI(TAG, "ML init + reset done");
            break;
        }

        case MQTT_EVENT_PUBLISHED: {
            // Broker has acknowledged a QoS1 publish -> compute publish→PUBACK latency
            pub_track_t tr;
            int64_t now = esp_timer_get_time();
            if (lookup_publish(e->msg_id, &tr)) {
                double dt_ms = (now - tr.start_mono_us) / 1000.0;
                ESP_LOGI("LAT", "MQTT_PUBACK id=%u msg_id=%d dt_ms=%.2f topic=%s mono_us=%lld",
                         tr.evt_id, e->msg_id, dt_ms, tr.topic ? tr.topic : "(null)", (long long)now);
            } else {
                // We didn’t track this publish (fine for system/retained/etc.)
                ESP_LOGI("LAT", "MQTT_PUBACK msg_id=%d (untracked)", e->msg_id);
            }
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        default:
            break;
    }
}


/* --------- I2C + SHT40 ---------- */

static uint8_t sht_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int j = 0; j < len; j++) {
        crc ^= data[j];
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags = { .enable_internal_pullup = true }
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address  = SHT40_ADDR,
        .scl_speed_hz    = I2C_HZ,
    };
    return i2c_master_bus_add_device(i2c_bus, &dev_cfg, &sht40_dev);
}

static esp_err_t sht40_measure(float *t_c, float *rh_pct)
{
    const uint8_t cmd = 0xFD; // high repeatability, no clock stretching
    esp_err_t err = i2c_master_transmit(sht40_dev, &cmd, 1, 100); // ms
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(12)); // ~10ms conversion

    uint8_t buf[6] = {0};
    err = i2c_master_receive(sht40_dev, buf, 6, 100);
    if (err != ESP_OK) return err;

    if (sht_crc8(&buf[0], 2) != buf[2] || sht_crc8(&buf[3], 2) != buf[5]) return ESP_ERR_INVALID_CRC;

    uint16_t rawT  = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawRH = ((uint16_t)buf[3] << 8) | buf[4];

    float T = -45.0f + 175.0f * ((float)rawT  / 65535.0f);
    float H =  -6.0f + 125.0f * ((float)rawRH / 65535.0f);
    if (H < 0)   H = 0;
    if (H > 100) H = 100;

    if (t_c)   *t_c   = T;
    if (rh_pct)*rh_pct= H;
    return ESP_OK;
}

static void sht40_task(void *arg)
{
    for (;;) {
        float t, h;
        esp_err_t err = sht40_measure(&t, &h);
        if (err == ESP_OK) {
            struct timeval tv; gettimeofday(&tv, NULL); // time of sample completion
            char tstr[48]; iso8601_utc_ms_from_timeval(&tv, tstr, sizeof tstr);

            char json[128];
            int n = snprintf(json, sizeof json, AIR_JSON_FMT, (double)t, (double)h, tstr);
            if (n > 0 && n < (int)sizeof(json) && s_mqtt_connected) {
                int64_t t0 = esp_timer_get_time();
                int msg_id = esp_mqtt_client_publish(s_mqtt, TOPIC_AIR, json, 0, 1, 1);
                track_publish(msg_id, g_temp_evt_id, TOPIC_AIR, t0);
                ESP_LOGI("LAT", "MQTT_PUB_START id=%u msg_id=%d mono_us=%lld topic=%s",
                        g_temp_evt_id, msg_id, (long long)t0, TOPIC_AIR);
            }
            // ---- ML pipeline tick (feed & infer) ----
            // New temp cycle: mark t0T
            g_temp_evt_id++;
            ESP_LOGI("LAT", "TEMP_TRIGGER id=%u mono_us=%lld",
                    g_temp_evt_id, (long long)esp_timer_get_time());

            ESP_LOGI("ML", "push_temp t=%.2f", (double)t);
            ml_push_temp(t);

            int mod = minute_of_day();
            ESP_LOGI("ML", "infer_and_publish minute=%d", mod);
            ml_infer_and_publish(mod);
// t1T (temp)
ESP_LOGI("LAT", "ML_DONE id=%u mono_us=%lld model=temp",
         g_temp_evt_id, (long long)esp_timer_get_time());

            ESP_LOGI("SHT40", "T=%.2f C, RH=%.2f %% @ %s", (double)t, (double)h, tstr);
        } else {
            ESP_LOGW("SHT40", "read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(60000)); // 60 s (minute readings)
    }
}

// ML callback: the ML library calls this when it has a new result.
// We publish ONE JSON to Home Assistant; no policy decisions here.
void ml_on_state(float obs, float pred, float resid, float slope,
                 int minute_of_day_val, int anomaly, int window_open)
{
    ESP_LOGI("ML", "on_state: anom=%d obs=%.2f pred=%.2f resid=%.2f slope=%.3f min=%d win=%d",
             anomaly, (double)obs, (double)pred, (double)resid, (double)slope,
             minute_of_day_val, window_open);

    if (!s_mqtt_connected) return;           // publish only if MQTT is up

    // Optional debug ping so we can see traffic even if JSON gets mangled
    static uint32_t cnt;
    char ping[64];
    snprintf(ping, sizeof ping, "tick %" PRIu32 " anom=%d", ++cnt, anomaly);
    esp_mqtt_client_publish(s_mqtt, "home/study/ml/ping", ping, 0, 1, false);

    // Publish the single inference JSON (retained so HA sees it after restarts)
    char tstr[48]; iso8601_utc_ms_now(tstr, sizeof tstr);
    char payload[256];
    int n = snprintf(payload, sizeof payload,
        "{\"obs\":%.2f,\"pred\":%.2f,\"resid\":%.2f,\"slope\":%.3f,"
        "\"minute\":%d,\"anomaly\":%d,\"window_open\":%d,\"timestamp\":\"%s\"}",
        (double)obs, (double)pred, (double)resid, (double)slope,
        minute_of_day_val, anomaly, window_open, tstr);
    if (n > 0 && n < (int)sizeof(payload)) {
        int64_t t0 = esp_timer_get_time();
        int msg_id = esp_mqtt_client_publish(s_mqtt, "home/study/ml/inference", payload, 0, 1, true);
        track_publish(msg_id, g_temp_evt_id, "home/study/ml/inference", t0);
        ESP_LOGI("LAT", "MQTT_PUB_START id=%u msg_id=%d mono_us=%lld topic=%s",
                g_temp_evt_id, msg_id, (long long)t0, "home/study/ml/inference");
}
}


/* --------- app_main ---------- */

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    // Time sync with your Pi (or change to pool.ntp.org if you prefer)
    sntp_start_and_wait("pool.ntp.org");

    
    ml_occ_init(); // ensure occupancy model is armed before minute task starts
// MQTT
    esp_mqtt_client_config_t mqtt_cfg = { 
        .broker.address.uri = BROKER_URI,
        .credentials ={
            .username = "YOUR_MQTT_USERNAME",
            .authentication = {
                .password = "YOUR_MQTT_PASSWORD", 
            },
        },
    };
    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);

    // PIR wiring + pipeline
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIR_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    if (gpio_install_isr_service(0) != ESP_OK) { /* ignore if already installed */ }
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIR_GPIO, pir_isr, NULL));

    s_evtq = xQueueCreate(8, sizeof(pir_evt_t));
    xTaskCreatePinnedToCore(pir_task, "pir_task", 3072, NULL, 9, NULL, tskNO_AFFINITY);

    // I2C + SHT40
    ESP_ERROR_CHECK(i2c_init());
    xTaskCreatePinnedToCore(sht40_task, "sht40_task", 4096, NULL, 8, NULL, tskNO_AFFINITY);

        xTaskCreatePinnedToCore(occ_task, "occ_task", 3072, NULL, 8, NULL, tskNO_AFFINITY);

    vTaskDelay(portMAX_DELAY);
}