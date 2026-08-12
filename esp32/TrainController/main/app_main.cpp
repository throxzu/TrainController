extern "C" {
    void app_main();
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "mqtt_client.h"
#include "json_parser.h"

#include "train_config.h"
#include "turnout_control.h"
#include "motor_l298n.h"

// ---------------------------------------------------------------------------
// Pin assignments not managed by train_config.h
// GPIO 33 is reserved for Motor 2 (section 2 enable).
// Push button moved to GPIO 34 (input-only pin).
// ---------------------------------------------------------------------------
#define LED_PIN         GPIO_NUM_2
#define PUSH_BUTTON_PIN GPIO_NUM_34

static const char* TAG = "MAIN";

static esp_mqtt_client_handle_t mqtt_client;
static volatile bool     s_mqtt_connected  = false;
static volatile uint32_t g_cmds_dispatched = 0;

extern uint32_t g_i2c_errors;

QueueHandle_t xPcfQueue     = NULL;
QueueHandle_t xSectionQueue = NULL;

// ---------------------------------------------------------------------------
// MQTT topic → command dispatch
//
// Topics:
//   train/turnout/{id}   payload: {"position":"straight"} or {"position":"diverge"}
//   train/section/{id}   payload: {"speed":75,"direction":"forward"}
//                                 direction is "forward" or "reverse"
// ---------------------------------------------------------------------------
static void dispatch_turnout(int id, const char* payload, int payload_len)
{
    g_cmds_dispatched++;
    char buf[64] = {};
    int  len = (payload_len < (int)sizeof(buf) - 1) ? payload_len : (int)sizeof(buf) - 1;
    memcpy(buf, payload, len);

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, buf, len) != OS_SUCCESS) {
        ESP_LOGE(TAG, "turnout %d: JSON parse failed", id);
        return;
    }

    char position[16] = {};
    if (json_obj_get_string(&jctx, "position", position, sizeof(position)) != OS_SUCCESS) {
        ESP_LOGE(TAG, "turnout %d: missing 'position' field", id);
        return;
    }

    bool straight;
    if (strcmp(position, "straight") == 0)      { straight = true;  }
    else if (strcmp(position, "diverge") == 0)  { straight = false; }
    else {
        ESP_LOGE(TAG, "turnout %d: unknown position '%s' (use straight/diverge)", id, position);
        return;
    }

    PcfCmd_t cmd = {};
    cmd.timeStamp       = xTaskGetTickCount();
    cmd.type            = PCF_CMD_TURNOUT;
    cmd.turnout.turnoutId = id;
    cmd.turnout.straight  = straight;
    xQueueSend(xPcfQueue, &cmd, pdMS_TO_TICKS(50));
}

static void dispatch_section(int id, const char* payload, int payload_len)
{
    g_cmds_dispatched++;
    char buf[64] = {};
    int  len = (payload_len < (int)sizeof(buf) - 1) ? payload_len : (int)sizeof(buf) - 1;
    memcpy(buf, payload, len);

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, buf, len) != OS_SUCCESS) {
        ESP_LOGE(TAG, "section %d: JSON parse failed", id);
        return;
    }

    int  speed     = 0;
    char direction[16] = {};
    json_obj_get_int(&jctx, "speed", &speed);
    json_obj_get_string(&jctx, "direction", direction, sizeof(direction));

    if (speed < 0)   speed = 0;
    if (speed > 100) speed = 100;

    bool forward;
    if (strcmp(direction, "forward") == 0)      { forward = true;  }
    else if (strcmp(direction, "reverse") == 0) { forward = false; }
    else {
        ESP_LOGE(TAG, "section %d: unknown direction '%s' (use forward/reverse)", id, direction);
        return;
    }

    // Direction → PCF8574 task
    PcfCmd_t dirCmd = {};
    dirCmd.timeStamp            = xTaskGetTickCount();
    dirCmd.type                 = PCF_CMD_DIRECTION;
    dirCmd.direction.sectionId  = id;
    dirCmd.direction.forward    = forward;
    xQueueSend(xPcfQueue, &dirCmd, pdMS_TO_TICKS(50));

    // Speed → motor task
    SectionCmd_t speedCmd = {};
    speedCmd.timeStamp = xTaskGetTickCount();
    speedCmd.sectionId = id;
    speedCmd.speed     = (uint8_t)speed;
    xQueueSend(xSectionQueue, &speedCmd, pdMS_TO_TICKS(50));
}

// ---------------------------------------------------------------------------
// OTA update over Wi-Fi
//
// Triggered by train/ota/controller carrying {"url":"http://host:port/path"}.
// The image is fetched over plain HTTP — fine on a trusted LAN, and it avoids
// having to embed and rotate certificates.
// ---------------------------------------------------------------------------
#define OTA_DEVICE_ID "controller"

static char          s_ota_url[192];
static volatile bool s_ota_running = false;

// Minimal JSON string extractor — enough for {"url":"..."} without involving
// json_parser for a single field. False if the key is missing or too long.
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

// Route an incoming MQTT message to the right dispatcher based on topic
static void handle_mqtt_message(const char* topic,   int topic_len,
                                const char* payload, int payload_len)
{
    // Copy topic to a null-terminated buffer for sscanf
    char topic_buf[64] = {};
    int  tlen = (topic_len < (int)sizeof(topic_buf) - 1) ? topic_len : (int)sizeof(topic_buf) - 1;
    memcpy(topic_buf, topic, tlen);

    int id = 0;
    if (sscanf(topic_buf, "train/turnout/%d", &id) == 1) {
        dispatch_turnout(id, payload, payload_len);
    } else if (sscanf(topic_buf, "train/section/%d", &id) == 1) {
        dispatch_section(id, payload, payload_len);
    } else if (strncmp(topic_buf, "train/ota/", 10) == 0) {
        // Exact device match matters more here than anywhere: this board
        // subscribes to train/# and therefore also receives the detector's
        // update command. Acting on it would flash detector firmware onto the
        // controller and take out the whole layout until someone reaches it
        // with a USB cable.
        if (strcmp(topic_buf + 10, OTA_DEVICE_ID) != 0) {
            ESP_LOGD(TAG, "ota for another device: %s", topic_buf);
            return;
        }
        if (s_ota_running) {
            ESP_LOGW(TAG, "ota already in progress, ignoring");
            return;
        }
        char payload_buf[224] = {};
        int  plen = (payload_len < (int)sizeof(payload_buf) - 1)
                  ? payload_len : (int)sizeof(payload_buf) - 1;
        memcpy(payload_buf, payload, plen);

        if (!json_string_field(payload_buf, "url", s_ota_url, sizeof(s_ota_url))) {
            ESP_LOGW(TAG, "ota payload has no usable url: %s", payload_buf);
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
static void mqtt_event_handler(void* handler_args, esp_event_base_t base,
                               int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event  = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_mqtt_connected = true;
        esp_mqtt_client_subscribe(client, "train/#", 0);
        ESP_LOGI(TAG, "subscribed to train/#");
        // Reaching the broker is the bar for calling a new image good.
        ota_confirm_image();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA:
        handle_mqtt_message(event->topic,   event->topic_len,
                            event->data,    event->data_len);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Heartbeat task — publishes alive message every 10 s
// ---------------------------------------------------------------------------
static void heartbeat_task(void* /*arg*/)
{
    // Version and build time travel with the heartbeat so a wireless update can
    // be confirmed without a serial port: uptime resets and version changes.
    const esp_app_desc_t* app = esp_app_get_description();

    char payload[384];
    for (;;) {
        if (s_mqtt_connected) {
            uint32_t uptime_s = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

            // Build scan array e.g. [32,34,56]
            char scan_buf[64] = "[";
            for (int i = 0; i < *g_scan_count; i++) {
                char tmp[8];
                snprintf(tmp, sizeof(tmp), "%s%d", i ? "," : "", g_scan_addrs[i]);
                strncat(scan_buf, tmp, sizeof(scan_buf) - strlen(scan_buf) - 1);
            }
            strncat(scan_buf, "]", sizeof(scan_buf) - strlen(scan_buf) - 1);

            snprintf(payload, sizeof(payload),
                     "{\"status\":\"alive\",\"uptime\":%lu,\"cmds\":%lu,\"i2c_mask\":%u,"
                     "\"i2c_err\":%lu,\"scan\":%s,\"version\":\"%s\",\"built\":\"%s %s\"}",
                     (unsigned long)uptime_s,
                     (unsigned long)g_cmds_dispatched,
                     (unsigned)g_i2c_ok_mask,
                     (unsigned long)g_i2c_errors,
                     scan_buf,
                     app->version, app->date, app->time);
            esp_mqtt_client_publish(mqtt_client, "train/status", payload, 0, 0, 0);
            ESP_LOGI(TAG, "heartbeat: uptime=%lu cmds=%lu i2c_mask=0x%02X i2c_err=%lu",
                     (unsigned long)uptime_s, (unsigned long)g_cmds_dispatched,
                     (unsigned)g_i2c_ok_mask, (unsigned long)g_i2c_errors);
            vTaskDelay(pdMS_TO_TICKS(10000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// ---------------------------------------------------------------------------
// MQTT start
// ---------------------------------------------------------------------------
static void mqtt_app_start(void)
{
    static esp_mqtt_client_config_t mqtt_cfg;
    mqtt_cfg.broker.address.uri              = CONFIG_BROKER_URL;
    mqtt_cfg.credentials.username            = "esp32";
    mqtt_cfg.credentials.authentication.password = "password123";
    mqtt_cfg.credentials.client_id           = "ESP";

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client,
                                   (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    ESP_LOGI(TAG, "MQTT started");
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
void app_main(void)
{
    // Queues
    xPcfQueue     = xQueueCreate(20, sizeof(PcfCmd_t));
    xSectionQueue = xQueueCreate(20, sizeof(SectionCmd_t));

    ESP_LOGI(TAG, "[APP] free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

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
    esp_log_level_set("MOTOR",       ESP_LOG_INFO);
    esp_log_level_set("PCF",         ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();

    // Optional status LED
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // Optional push button (GPIO 34 is input-only — no pull configuration needed
    // if an external pull-up resistor is fitted)
    gpio_set_direction(PUSH_BUTTON_PIN, GPIO_MODE_INPUT);

    // FreeRTOS tasks
    xTaskCreate(turnout_control_task, "pcf_task",    4096, xPcfQueue,     14, NULL);
    xTaskCreate(motor_l298n_task,     "motor_task",  4096, xSectionQueue, 13, NULL);
    // 4096, not 2048: the heartbeat payload buffer grew when version and build
    // time were added, and snprintf's own frame on top of it overflowed the old
    // stack — intermittently, which made it look like a phantom crash.
    xTaskCreate(heartbeat_task,       "heartbeat",   4096, NULL,           5, NULL);
}
