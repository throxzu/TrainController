extern "C" {
    void app_main();
}

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "protocol_examples_common.h"

#include "PCF8574.h"
#include "detect_config.h"

static const char* TAG = "DETECT";

static esp_mqtt_client_handle_t mqtt_client;
static volatile bool s_mqtt_connected = false;

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

    bool entryState[NUM_SECTIONS] = {};
    bool exitState[NUM_SECTIONS]  = {};
    bool initialised = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DETECT_POLL_MS));

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
// Heartbeat task
// ---------------------------------------------------------------------------
static void heartbeat_task(void* /*arg*/)
{
    char payload[64];
    for (;;) {
        if (s_mqtt_connected) {
            uint32_t uptime_s = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
            snprintf(payload, sizeof(payload),
                     "{\"status\":\"alive\",\"uptime\":%lu}", (unsigned long)uptime_s);
            esp_mqtt_client_publish(mqtt_client, "train/detector/status", payload, 0, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
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

    esp_log_level_set("*",           ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    static esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri                   = CONFIG_BROKER_URL;
    mqtt_cfg.credentials.username                 = "esp32";
    mqtt_cfg.credentials.authentication.password  = "password123";
    mqtt_cfg.credentials.client_id                = "ESP32-detector";

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    xTaskCreate(detection_task, "detect_task", 4096, NULL, 10, NULL);
    xTaskCreate(heartbeat_task, "heartbeat",   2048, NULL,  5, NULL);
}
