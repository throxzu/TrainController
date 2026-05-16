#include "motor_l298n.h"
#include "train_config.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char* TAG = "MOTOR";

// Maximum duty cycle as a percentage of full scale.
// Reduce if your supply voltage exceeds the motor voltage rating.
static const uint8_t DUTY_LIMIT_PCT = 90;

// ---------------------------------------------------------------------------
// LEDC initialisation
// ---------------------------------------------------------------------------

static void ledc_init()
{
    // High-speed timer — used by sections 1–8 (LEDC channels 0–7)
    ledc_timer_config_t hs_timer;
    hs_timer.speed_mode      = LEDC_HIGH_SPEED_MODE;
    hs_timer.duty_resolution = TRAIN_LEDC_RESOLUTION;
    hs_timer.timer_num       = TRAIN_LEDC_TIMER;
    hs_timer.freq_hz         = TRAIN_LEDC_FREQ_HZ;
    hs_timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&hs_timer);

    // Low-speed timer — used by sections 9–14 (LEDC channels 0–5, low-speed)
    ledc_timer_config_t ls_timer;
    ls_timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    ls_timer.duty_resolution = TRAIN_LEDC_RESOLUTION;
    ls_timer.timer_num       = TRAIN_LEDC_TIMER;
    ls_timer.freq_hz         = TRAIN_LEDC_FREQ_HZ;
    ls_timer.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&ls_timer);

    // Bind one LEDC channel to each section's enable GPIO
    for (int i = 0; i < NUM_SECTIONS; i++) {
        const SectionConfig_t& cfg = SECTION_CONFIG[i];

        ledc_channel_config_t ch = {};
        ch.gpio_num   = cfg.enablePin;
        ch.speed_mode = cfg.ledcMode;
        ch.channel    = cfg.ledcChannel;
        ch.intr_type  = LEDC_INTR_DISABLE;
        ch.timer_sel  = TRAIN_LEDC_TIMER;
        ch.duty       = 0;
        ch.hpoint     = 0;
        ledc_channel_config(&ch);

        ESP_LOGI(TAG, "section %2d  GPIO %-2d  LEDC %s ch %d",
                 i + 1, cfg.enablePin,
                 cfg.ledcMode == LEDC_HIGH_SPEED_MODE ? "HS" : "LS",
                 cfg.ledcChannel);
    }
    ESP_LOGI(TAG, "LEDC ready — %d sections", NUM_SECTIONS);
}

// ---------------------------------------------------------------------------
// Speed update
// ---------------------------------------------------------------------------

static void set_speed(int sectionId, uint8_t speedPct)
{
    if (sectionId < 1 || sectionId > NUM_SECTIONS) {
        ESP_LOGW(TAG, "section %d out of range (1–%d)", sectionId, NUM_SECTIONS);
        return;
    }

    const SectionConfig_t& cfg = SECTION_CONFIG[sectionId - 1];

    // Clamp to duty limit and convert 0–100 → 0–1023 (10-bit)
    uint8_t  clamped = (speedPct > DUTY_LIMIT_PCT) ? DUTY_LIMIT_PCT : speedPct;
    uint32_t duty    = (uint32_t)clamped * ((1 << TRAIN_LEDC_RESOLUTION) - 1) / 100;

    ledc_set_duty(cfg.ledcMode, cfg.ledcChannel, duty);
    ledc_update_duty(cfg.ledcMode, cfg.ledcChannel);

    ESP_LOGI(TAG, "section %2d  speed %3d%%  duty %4lu  [GPIO %d LEDC %s ch %d]",
             sectionId, clamped, duty, cfg.enablePin,
             cfg.ledcMode == LEDC_HIGH_SPEED_MODE ? "HS" : "LS",
             cfg.ledcChannel);
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

void motor_l298n_task(void* pvQueue)
{
    if (!pvQueue) {
        ESP_LOGE(TAG, "queue handle is null — task aborted");
        vTaskDelete(NULL);
        return;
    }
    QueueHandle_t queue = (QueueHandle_t)pvQueue;

    ledc_init();

    SectionCmd_t cmd;
    while (true) {
        if (xQueueReceive(queue, &cmd, portMAX_DELAY) == pdTRUE) {
            set_speed(cmd.sectionId, cmd.speed);
        }
    }
}
