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
        esp_mqtt_client_subscribe(client, "train/#", 0);
        ESP_LOGI(TAG, "subscribed to train/#");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
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
}
