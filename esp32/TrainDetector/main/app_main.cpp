extern "C" {
    void app_main();
}

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "protocol_examples_common.h"

#include "PCF8574.h"
#include "detect_config.h"

static const char* TAG = "DETECT";

static esp_mqtt_client_handle_t mqtt_client;
static volatile bool s_mqtt_connected = false;
static volatile bool s_mqtt_ever      = false;

// Wi-Fi drop bookkeeping, reported in the heartbeat. Reconnection itself is
// handled by the connect helper; this only records that it happened, and why,
// so a board that keeps dropping can be told apart from one that crashes.
// Common reasons: 200 BEACON_TIMEOUT and 201 NO_AP_FOUND point at RF or range,
// 8 ASSOC_LEAVE means the AP dropped us deliberately.
static volatile uint32_t s_wifi_drops       = 0;
static volatile uint8_t  s_wifi_last_reason = 0;

static void wifi_drop_handler(void* /*arg*/, esp_event_base_t /*base*/,
                              int32_t /*id*/, void* event_data)
{
    const wifi_event_sta_disconnected_t* ev =
        (const wifi_event_sta_disconnected_t*)event_data;
    s_wifi_drops++;
    if (ev) s_wifi_last_reason = ev->reason;
}

// LED commands arrive on the MQTT task but the I2C bus belongs to the detection
// task, so they are handed over through this queue rather than touching the bus
// from two tasks at once.
typedef struct {
    uint8_t index;   // index into LED_PINS
    bool    on;
} LedCmd_t;

static QueueHandle_t s_led_queue;

// Fan speed changes go to their own task because a kickstart has to hold full
// power briefly, and the MQTT task must not block.
static QueueHandle_t s_fan_queue;
static volatile int  s_fan_speed = 0;      // last requested speed, 0-100
static volatile bool s_fan_auto  = true;   // temperature-driven unless overridden

// ---------------------------------------------------------------------------
// Publish helper
// ---------------------------------------------------------------------------
static void publish(const char* topic, const char* payload)
{
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, 1);
}

// ---------------------------------------------------------------------------
// Detection task
//
// Collects the unique PCF8574 addresses referenced in SECTION_SENSORS, inits
// one chip instance per address, then polls every DETECT_POLL_MS ms.
// Each section has an entry and exit sensor (either may be unwired).
// Publishes train/detection/{n}/entry and train/detection/{n}/exit with
// {"triggered":true/false} on state change only.
// ---------------------------------------------------------------------------
static void detection_task(void* /*arg*/)
{
    // Collect unique PCF8574 addresses (range 0x20–0x27 → index 0–7)
    PCF8574* chips[8] = {};

    auto ensure_chip = [&](uint8_t addr) {
        if (addr == SENSOR_NOT_WIRED) return;
        int idx = addr - 0x20;
        if (idx < 0 || idx >= 8) return;
        if (!chips[idx]) {
            chips[idx] = new PCF8574(addr);
            chips[idx]->init(DETECT_I2C_SDA, DETECT_I2C_SCL);
            chips[idx]->write(0xFF);  // all pins high → input mode
            ESP_LOGI(TAG, "PCF8574 0x%02X initialised", addr);
        }
    };

    for (int s = 0; s < NUM_SECTIONS; s++) {
        ensure_chip(SECTION_SENSORS[s].entry.addr);
        ensure_chip(SECTION_SENSORS[s].exit.addr);
    }
    for (int l = 0; l < NUM_LEDS; l++) {
        ensure_chip(LED_PINS[l].addr);
    }

    // Shadow of the last byte written to each chip. Starts all-high to match the
    // write(0xFF) above: sensor pins stay in input mode and every LED is off.
    uint8_t chipOutputs[8];
    for (int i = 0; i < 8; i++) chipOutputs[i] = 0xFF;

    bool entryState[NUM_SECTIONS] = {};
    bool exitState[NUM_SECTIONS]  = {};
    bool initialised = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DETECT_POLL_MS));

        // While the broker is unreachable, drop the baseline so that the first
        // poll after (re)connecting republishes the current state of every
        // wired sensor instead of waiting for the next physical change.
        if (!s_mqtt_connected) {
            initialised = false;
            continue;
        }

        // Apply any LED commands that arrived since the last cycle. Only the
        // LED's own bit changes; every other bit keeps its shadow value so the
        // sensor pins on the same chip stay high (input mode).
        LedCmd_t cmd;
        while (xQueueReceive(s_led_queue, &cmd, 0) == pdTRUE) {
            if (cmd.index >= NUM_LEDS) continue;
            const SensorPin_t& lp = LED_PINS[cmd.index];
            int idx = lp.addr - 0x20;
            if (idx < 0 || idx >= 8 || !chips[idx]) continue;

            if (cmd.on) chipOutputs[idx] &= ~(1 << lp.pin);   // LOW = lit
            else        chipOutputs[idx] |=  (1 << lp.pin);
            chips[idx]->write(chipOutputs[idx]);

            ESP_LOGI(TAG, "LED%d %s (0x%02X pin %d)",
                     cmd.index + 1, cmd.on ? "on" : "off", lp.addr, lp.pin);
        }

        // Read each wired chip once per cycle
        uint8_t chipBits[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        for (int i = 0; i < 8; i++) {
            if (chips[i]) chipBits[i] = chips[i]->read();
        }

        for (int s = 0; s < NUM_SECTIONS; s++) {
            const SectionSensors_t& ss = SECTION_SENSORS[s];

            // Entry sensor
            if (ss.entry.addr != SENSOR_NOT_WIRED) {
                int idx = ss.entry.addr - 0x20;
                bool now = !(chipBits[idx] & (1 << ss.entry.pin));  // LOW = triggered
                if (!initialised || now != entryState[s]) {
                    entryState[s] = now;
                    if (s_mqtt_connected) {
                        char topic[48], payload[24];
                        snprintf(topic,   sizeof(topic),   "train/detection/%d/entry", s + 1);
                        snprintf(payload, sizeof(payload), "{\"triggered\":%s}", now ? "true" : "false");
                        publish(topic, payload);
                        ESP_LOGI(TAG, "S%d entry: %s", s + 1, now ? "triggered" : "clear");
                    }
                }
            }

            // Exit sensor
            if (ss.exit.addr != SENSOR_NOT_WIRED) {
                int idx = ss.exit.addr - 0x20;
                bool now = !(chipBits[idx] & (1 << ss.exit.pin));
                if (!initialised || now != exitState[s]) {
                    exitState[s] = now;
                    if (s_mqtt_connected) {
                        char topic[48], payload[24];
                        snprintf(topic,   sizeof(topic),   "train/detection/%d/exit", s + 1);
                        snprintf(payload, sizeof(payload), "{\"triggered\":%s}", now ? "true" : "false");
                        publish(topic, payload);
                        ESP_LOGI(TAG, "S%d exit:  %s", s + 1, now ? "triggered" : "clear");
                    }
                }
            }
        }
        initialised = true;
    }
}

// ---------------------------------------------------------------------------
// Cooling fans — LEDC PWM into the IRLZ44N gate on GPIO18
// ---------------------------------------------------------------------------
static void fan_init(void)
{
    ledc_timer_config_t timer = {};
    timer.speed_mode      = FAN_PWM_MODE;
    timer.duty_resolution = FAN_PWM_RES_BITS;
    timer.timer_num       = FAN_PWM_TIMER;
    timer.freq_hz         = FAN_PWM_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {};
    ch.gpio_num   = FAN_PWM_GPIO;
    ch.speed_mode = FAN_PWM_MODE;
    ch.channel    = FAN_PWM_CHANNEL;
    ch.timer_sel  = FAN_PWM_TIMER;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.duty       = 0;      // fans off until asked; matches the gate pulldown
    ch.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ESP_LOGI(TAG, "fan PWM ready on GPIO%d @ %d Hz", (int)FAN_PWM_GPIO, FAN_PWM_FREQ_HZ);
}

static void fan_apply(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    // Full power holds the gate continuously high rather than chopping at
    // 2047/2048, which costs a little voltage and needless switching loss.
    uint32_t duty = (pct >= 100)
                  ? (uint32_t)FAN_PWM_FULL_DUTY
                  : ((uint32_t)FAN_PWM_MAX_DUTY * (uint32_t)pct) / 100;
    ledc_set_duty(FAN_PWM_MODE, FAN_PWM_CHANNEL, duty);
    ledc_update_duty(FAN_PWM_MODE, FAN_PWM_CHANNEL);
}

static void fan_task(void* /*arg*/)
{
    int target;
    for (;;) {
        if (xQueueReceive(s_fan_queue, &target, portMAX_DELAY) != pdTRUE) continue;

        // Below the kickstart threshold a stopped fan may not break away, so
        // give it full power briefly before settling to the requested speed.
        if (target > 0 && target < FAN_KICKSTART_PCT) {
            ESP_LOGI(TAG, "fan kickstart -> %d%%", target);
            fan_apply(100);
            vTaskDelay(pdMS_TO_TICKS(FAN_KICKSTART_MS));
            // A newer command during the kick wins; skip settling to this one.
            if (uxQueueMessagesWaiting(s_fan_queue) > 0) continue;
        }

        fan_apply(target);
        ESP_LOGI(TAG, "fan speed %d%%", target);
    }
}

// ---------------------------------------------------------------------------
// KY-013 temperature sensor on ADC1
// ---------------------------------------------------------------------------
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_adc_cali;      // NULL if no calibration available
static volatile float            s_temp_c = NAN;

static void temp_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;                    // ADC2 is unusable while Wi-Fi runs
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));

    adc_oneshot_chan_cfg_t chan = {};
    chan.bitwidth = ADC_BITWIDTH_DEFAULT;
    chan.atten    = TEMP_ADC_ATTEN;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, TEMP_ADC_CHANNEL, &chan));

    // Per-chip calibration turns raw counts into millivolts. The ESP32's ADC is
    // markedly non-linear, so without this the temperature is off by degrees.
    adc_cali_line_fitting_config_t cali = {};
    cali.unit_id  = ADC_UNIT_1;
    cali.atten    = TEMP_ADC_ATTEN;
    cali.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_line_fitting(&cali, &s_adc_cali) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration active");
    } else {
        s_adc_cali = NULL;
        ESP_LOGW(TAG, "no ADC calibration (no eFuse data) — readings approximate");
    }
}

// Returns NAN when the divider reads at either rail, which means an open or
// shorted sensor rather than a real temperature.
static float temp_read_celsius(void)
{
    int32_t sum = 0;
    for (int i = 0; i < TEMP_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, TEMP_ADC_CHANNEL, &raw) != ESP_OK) return NAN;
        sum += raw;
    }
    int avg = (int)(sum / TEMP_SAMPLES);

    int mv = 0;
    if (s_adc_cali) {
        if (adc_cali_raw_to_voltage(s_adc_cali, avg, &mv) != ESP_OK) return NAN;
    } else {
        mv = (int)(avg * TEMP_SUPPLY_MV / 4095.0f);   // rough fallback
    }

    float v = (float)mv;
    if (v <= 1.0f || v >= TEMP_SUPPLY_MV - 1.0f) return NAN;   // rail-to-rail = not connected

    // Recover the thermistor's resistance from the divider.
    float r_ntc = TEMP_NTC_HIGH_SIDE
                ? TEMP_SERIES_OHMS * ((TEMP_SUPPLY_MV / v) - 1.0f)
                : TEMP_SERIES_OHMS * (v / (TEMP_SUPPLY_MV - v));
    if (r_ntc <= 0.0f) return NAN;

    // Beta equation: 1/T = 1/T0 + (1/B) * ln(R/R0)
    float inv_t = 1.0f / TEMP_NOMINAL_K
                + (1.0f / TEMP_BETA) * logf(r_ntc / TEMP_NOMINAL_OHMS);
    float c = (1.0f / inv_t) - 273.15f;

    if (c < TEMP_MIN_VALID_C || c > TEMP_MAX_VALID_C) {
        ESP_LOGW(TAG, "implausible temp %.1f C (%.0f ohms, %d mV) — check wiring",
                 c, r_ntc, mv);
        return NAN;
    }
    return c;
}

static void temp_task(void* /*arg*/)
{
    char payload[96];
    for (;;) {
        float c = temp_read_celsius();
        s_temp_c = c;

        // Automatic control, unless someone has taken manual command. A failed
        // reading leaves the fans as they are rather than guessing.
        if (s_fan_auto && !isnan(c)) {
            int desired = s_fan_speed;
            if (c >= FAN_TEMP_ON_C)       desired = 100;
            else if (c <  FAN_TEMP_OFF_C) desired = 0;

            if (desired != s_fan_speed) {
                ESP_LOGI(TAG, "auto: %.1f C -> fans %s", c, desired ? "on" : "off");
                s_fan_speed = desired;
                xQueueSend(s_fan_queue, &desired, 0);
            }
        }

        if (s_mqtt_connected) {
            if (isnan(c)) {
                snprintf(payload, sizeof(payload),
                         "{\"error\":\"no sensor\",\"fan\":%d,\"auto\":%s}",
                         s_fan_speed, s_fan_auto ? "true" : "false");
            } else {
                snprintf(payload, sizeof(payload),
                         "{\"celsius\":%.1f,\"fan\":%d,\"auto\":%s}",
                         c, s_fan_speed, s_fan_auto ? "true" : "false");
            }
            publish("train/detector/temp", payload);
        }
        vTaskDelay(pdMS_TO_TICKS(TEMP_PUBLISH_MS));
    }
}

// ---------------------------------------------------------------------------
// OTA update over Wi-Fi
//
// Triggered by train/ota/detector carrying {"url":"http://host:port/path"}.
// The image is fetched over plain HTTP — fine on a trusted LAN, and it avoids
// having to embed and rotate certificates.
// ---------------------------------------------------------------------------
#define OTA_DEVICE_ID "detector"

static char          s_ota_url[192];
static volatile bool s_ota_running = false;

// Minimal JSON string extractor — enough for {"url":"..."} without pulling in
// a parser. Returns false if the key is missing or the value does not fit.
static bool json_string_field(const char* json, const char* key,
                              char* out, size_t out_len)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* p = strstr(json, pattern);
    if (!p) return false;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++;

    const char* end = strchr(p, '"');
    if (!end) return false;

    size_t n = (size_t)(end - p);
    if (n == 0 || n >= out_len) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static void ota_task(void* /*arg*/)
{
    ESP_LOGW(TAG, "OTA starting from %s", s_ota_url);

    esp_http_client_config_t http = {};
    http.url               = s_ota_url;
    http.timeout_ms        = 10000;
    http.keep_alive_enable = true;

    esp_https_ota_config_t ota = {};
    ota.http_config = &http;

    esp_err_t err = esp_https_ota(&ota);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "OTA complete — rebooting into the new image");
        vTaskDelay(pdMS_TO_TICKS(500));   // let the log drain first
        esp_restart();
    }

    ESP_LOGE(TAG, "OTA failed: %s — staying on the current image", esp_err_to_name(err));
    s_ota_running = false;
    vTaskDelete(NULL);
}

// With rollback enabled the bootloader leaves a freshly written image marked
// PENDING_VERIFY and reverts to the previous slot on the next boot unless it is
// confirmed. Confirming only once MQTT is up means an image that cannot reach
// the broker undoes itself instead of needing USB recovery.
static void ota_confirm_image(void)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t   state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGW(TAG, "new image confirmed good, rollback cancelled");
    }
}

// ---------------------------------------------------------------------------
// Connectivity watchdog
//
// Every way this board can end up running but unreachable — a Wi-Fi link that
// never comes back, a wedged MQTT session, a lost DHCP lease — looks identical
// from the outside and ends with someone crawling under the layout with a USB
// cable. Rebooting after a few minutes without the broker turns all of them
// into a self-healing outage. A reset is cheap here: the sensor baseline is
// republished on reconnect, and the fans resume under their own thermostat.
//
// Counting starts only after the first successful connection, so a board that
// boots while the broker is down waits for it instead of rebooting in a loop.
// ---------------------------------------------------------------------------
#define MQTT_LOSS_REBOOT_MS (3 * 60 * 1000)

static void connectivity_watchdog_task(void* /*arg*/)
{
    TickType_t last_ok = xTaskGetTickCount();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        // An OTA download deliberately leaves the broker idle — never reboot
        // in the middle of writing a new image.
        if (s_mqtt_connected || !s_mqtt_ever || s_ota_running) {
            last_ok = xTaskGetTickCount();
            continue;
        }

        uint32_t down_ms = (uint32_t)(xTaskGetTickCount() - last_ok) * portTICK_PERIOD_MS;
        if (down_ms >= MQTT_LOSS_REBOOT_MS) {
            ESP_LOGE(TAG, "no broker for %lu s — rebooting to recover",
                     (unsigned long)(down_ms / 1000));
            vTaskDelay(pdMS_TO_TICKS(100));   // let the log drain
            esp_restart();
        }
    }
}

// ---------------------------------------------------------------------------
// Heartbeat task
// ---------------------------------------------------------------------------
static void heartbeat_task(void* /*arg*/)
{
    // Version and build time travel with the heartbeat so a wireless update can
    // be confirmed without a serial port: uptime resets and version changes.
    const esp_app_desc_t* app = esp_app_get_description();

    char payload[288];
    for (;;) {
        if (s_mqtt_connected) {
            uint32_t uptime_s = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
            snprintf(payload, sizeof(payload),
                     "{\"status\":\"alive\",\"uptime\":%lu,"
                     "\"version\":\"%s\",\"built\":\"%s %s\","
                     "\"heap\":%lu,\"wifi_drops\":%lu,\"wifi_reason\":%u}",
                     (unsigned long)uptime_s, app->version, app->date, app->time,
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)s_wifi_drops,
                     (unsigned)s_wifi_last_reason);
            esp_mqtt_client_publish(mqtt_client, "train/detector/status", payload, 0, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ---------------------------------------------------------------------------
// Route an incoming MQTT message by topic
//
//   train/led/{1..NUM_LEDS}   {"on":true|false}
//   train/fan/{1..NUM_FANS}   {"speed":0-100}
//   train/ota/detector        {"url":"http://host:port/path"}
// ---------------------------------------------------------------------------
static void handle_mqtt_message(const char* topic, int topic_len,
                                const char* data,  int data_len)
{
    // Neither topic nor payload arrives null-terminated.
    char topic_buf[48] = {};
    int  tlen = (topic_len < (int)sizeof(topic_buf) - 1) ? topic_len : (int)sizeof(topic_buf) - 1;
    memcpy(topic_buf, topic, tlen);

    // Roomy enough for an OTA URL, not just the short control payloads.
    char data_buf[224] = {};
    int  dlen = (data_len < (int)sizeof(data_buf) - 1) ? data_len : (int)sizeof(data_buf) - 1;
    memcpy(data_buf, data, dlen);

    int id = 0;

    if (sscanf(topic_buf, "train/led/%d", &id) == 1) {
        if (id < 1 || id > NUM_LEDS) {
            ESP_LOGW(TAG, "LED %d out of range (1-%d)", id, NUM_LEDS);
            return;
        }
        LedCmd_t cmd = { (uint8_t)(id - 1), strstr(data_buf, "true") != NULL };
        xQueueSend(s_led_queue, &cmd, 0);

    } else if (sscanf(topic_buf, "train/fan/%d", &id) == 1) {
        if (id < 1 || id > NUM_FANS) {
            ESP_LOGW(TAG, "fan %d out of range (1-%d)", id, NUM_FANS);
            return;
        }
        // {"mode":"auto"} hands control back to the temperature thresholds.
        if (strstr(data_buf, "\"auto\"")) {
            s_fan_auto = true;
            ESP_LOGI(TAG, "fan control: auto");
            return;
        }

        int speed = 0;
        const char* p = strstr(data_buf, "\"speed\"");
        if (!p || sscanf(p, "\"speed\"%*[: ]%d", &speed) != 1) {
            ESP_LOGW(TAG, "fan payload has no speed: %s", data_buf);
            return;
        }
        if (speed < 0)   speed = 0;
        if (speed > 100) speed = 100;
        // An explicit speed is a manual override; auto would otherwise undo it
        // at the next temperature reading.
        s_fan_auto  = false;
        s_fan_speed = speed;
        xQueueSend(s_fan_queue, &speed, 0);
        ESP_LOGI(TAG, "fan control: manual %d%%", speed);

    } else if (strncmp(topic_buf, "train/ota/", 10) == 0) {
        // Exact device match, deliberately. Wildcard subscriptions mean a board
        // can see another board's update command, and flashing the wrong image
        // would brick it until someone reaches it with a USB cable.
        if (strcmp(topic_buf + 10, OTA_DEVICE_ID) != 0) {
            ESP_LOGD(TAG, "ota for another device: %s", topic_buf);
            return;
        }
        if (s_ota_running) {
            ESP_LOGW(TAG, "ota already in progress, ignoring");
            return;
        }
        if (!json_string_field(data_buf, "url", s_ota_url, sizeof(s_ota_url))) {
            ESP_LOGW(TAG, "ota payload has no usable url: %s", data_buf);
            return;
        }
        s_ota_running = true;
        xTaskCreate(ota_task, "ota", 8192, NULL, 5, NULL);

    } else {
        ESP_LOGD(TAG, "unhandled topic: %s", topic_buf);
    }
}

// ---------------------------------------------------------------------------
// MQTT event handler
// ---------------------------------------------------------------------------
static void mqtt_event_handler(void* /*arg*/, esp_event_base_t /*base*/,
                               int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_mqtt_connected = true;
        s_mqtt_ever      = true;
        esp_mqtt_client_subscribe(mqtt_client, "train/led/#", 0);
        esp_mqtt_client_subscribe(mqtt_client, "train/fan/#", 0);
        esp_mqtt_client_subscribe(mqtt_client, "train/ota/#", 0);
        ESP_LOGI(TAG, "subscribed to train/led/#, train/fan/# and train/ota/#");
        // Reaching the broker is the bar for calling a new image good.
        ota_confirm_image();
        break;
    case MQTT_EVENT_DATA:
        handle_mqtt_message(event->topic, event->topic_len,
                            event->data,  event->data_len);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "TrainDetector starting — free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    // Which slot booted, and what is in it. After a wireless update this is the
    // quickest confirmation that the new image actually took: the label flips
    // between ota_0 and ota_1 and the build time moves.
    {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_app_desc_t*  desc    = esp_app_get_description();
        ESP_LOGW(TAG, "running from '%s' — version %s, built %s %s",
                 running->label, desc->version, desc->date, desc->time);
    }

    esp_log_level_set("*",           ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    // Mains-powered board: modem sleep buys nothing and costs reliability.
    // Dozing between beacons under a layout full of PWM-switched H-bridges is
    // a good way to miss them and be deauthenticated.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Runs alongside the connect helper's own handler — this one only counts.
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_drop_handler, NULL));

    static esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri                   = CONFIG_BROKER_URL;
    mqtt_cfg.credentials.username                 = "esp32";
    mqtt_cfg.credentials.authentication.password  = "password123";
    mqtt_cfg.credentials.client_id                = "ESP32-detector";

    s_led_queue = xQueueCreate(8, sizeof(LedCmd_t));
    s_fan_queue = xQueueCreate(4, sizeof(int));
    fan_init();
    temp_init();

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    xTaskCreate(detection_task, "detect_task", 4096, NULL, 10, NULL);
    // 4096, not 2048: the heartbeat payload buffer grew when version and build
    // time were added, and snprintf's own frame on top of it overflowed the old
    // stack — intermittently, which made it look like a phantom crash.
    xTaskCreate(heartbeat_task, "heartbeat",   4096, NULL,  5, NULL);
    xTaskCreate(fan_task,       "fan",         2560, NULL,  5, NULL);
    xTaskCreate(temp_task,      "temp",        3072, NULL,  4, NULL);
    xTaskCreate(connectivity_watchdog_task, "net_wdt", 2560, NULL, 4, NULL);
}
