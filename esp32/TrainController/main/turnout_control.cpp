#include "turnout_control.h"
#include "train_config.h"
#include "PCF8574.h"
#include "esp_log.h"

static const char* TAG = "PCF";

// One PCF8574 object per expander, indexed by (address - EXPANDER_BASE_ADDR)
static PCF8574* expanders[NUM_EXPANDERS];

// Bitmask of which expanders responded on I2C at startup (bit 0 = 0x20, bit 6 = 0x26)
uint8_t g_i2c_ok_mask = 0;

// Full I2C bus scan results (addresses of all responding devices)
static uint8_t s_scan_addrs[16];
static int     s_scan_count = 0;
uint8_t* g_scan_addrs = s_scan_addrs;
int*     g_scan_count = &s_scan_count;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static PCF8574* get_expander(uint8_t addr)
{
    int idx = addr - EXPANDER_BASE_ADDR;
    if (idx < 0 || idx >= NUM_EXPANDERS) {
        ESP_LOGE(TAG, "expander address 0x%02X is out of range (0x%02X–0x%02X)",
                 addr, EXPANDER_BASE_ADDR, EXPANDER_BASE_ADDR + NUM_EXPANDERS - 1);
        return nullptr;
    }
    return expanders[idx];
}

// Set all 56 outputs HIGH on every expander (relay off = coil safe).
static void all_relays_off()
{
    for (int i = 0; i < NUM_EXPANDERS; i++) {
        expanders[i]->write(0xFF);
    }
    ESP_LOGI(TAG, "all relays off");
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static void handle_turnout(const PcfCmd_t& cmd)
{
    int id = cmd.turnout.turnoutId;
    if (id < 1 || id > NUM_TURNOUTS) {
        ESP_LOGW(TAG, "turnout id %d out of range (1–%d)", id, NUM_TURNOUTS);
        return;
    }

    const TurnoutConfig_t& cfg = TURNOUT_CONFIG[id - 1];
    uint8_t activePin = cmd.turnout.straight ? cfg.straightPin : cfg.divergePin;
    const char* posLabel = cmd.turnout.straight ? "straight" : "diverge";

    PCF8574* exp = get_expander(cfg.i2cAddr);
    if (!exp) return;

    ESP_LOGI(TAG, "turnout %d → %-8s  [expander 0x%02X  pin %d  pulse %d ms]",
             id, posLabel, cfg.i2cAddr, activePin, SOLENOID_PULSE_MS);

    exp->writeBit(activePin, false);                    // energise coil (relay ON)
    vTaskDelay(pdMS_TO_TICKS(SOLENOID_PULSE_MS));
    exp->writeBit(activePin, true);                     // de-energise (relay OFF)
}

static void handle_direction(const PcfCmd_t& cmd)
{
    int id = cmd.direction.sectionId;
    if (id < 1 || id > NUM_SECTIONS) {
        ESP_LOGW(TAG, "section id %d out of range (1–%d)", id, NUM_SECTIONS);
        return;
    }

    const SectionConfig_t& cfg = SECTION_CONFIG[id - 1];
    PCF8574* exp = get_expander(cfg.dirAddr);
    if (!exp) return;

    bool fwd = cmd.direction.forward;

    ESP_LOGI(TAG, "section %d direction → %-7s  [expander 0x%02X  fwd pin %d  rev pin %d]",
             id, fwd ? "forward" : "reverse", cfg.dirAddr, cfg.fwdPin, cfg.revPin);

    // Only one direction pin is active (LOW) at a time
    exp->writeBit(cfg.fwdPin, !fwd);
    exp->writeBit(cfg.revPin,  fwd);
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

void turnout_control_task(void* pvQueue)
{
    if (!pvQueue) {
        ESP_LOGE(TAG, "queue handle is null — task aborted");
        vTaskDelete(NULL);
        return;
    }
    QueueHandle_t queue = (QueueHandle_t)pvQueue;

    // Install I2C driver via first expander (init() is a no-op for i > 0 now)
    for (int i = 0; i < NUM_EXPANDERS; i++) {
        expanders[i] = new PCF8574(EXPANDER_BASE_ADDR + i);
        expanders[i]->init(TRAIN_I2C_SDA, TRAIN_I2C_SCL);
    }

    // Full bus scan BEFORE pinging specific addresses — avoids failed-ACK
    // transactions corrupting the bus state before the scan runs
    {
        I2C scanner;
        for (uint8_t addr = 0x08; addr < 0x78 && s_scan_count < 16; addr++) {
            if (scanner.slavePresent(addr)) {
                s_scan_addrs[s_scan_count++] = addr;
                ESP_LOGI(TAG, "I2C scan: device at 0x%02X", addr);
            }
        }
        ESP_LOGI(TAG, "I2C scan complete: %d device(s) found", s_scan_count);
    }

    // Ping expected expander addresses
    for (int i = 0; i < NUM_EXPANDERS; i++) {
        bool found = expanders[i]->ping();
        if (found) g_i2c_ok_mask |= (1 << i);
        ESP_LOGI(TAG, "expander 0x%02X %s", EXPANDER_BASE_ADDR + i, found ? "OK" : "NOT FOUND");
    }

    all_relays_off();

    ESP_LOGI(TAG, "PCF8574 task ready");

    PcfCmd_t cmd;
    while (true) {
        // Block until a command arrives (no busy-wait)
        if (xQueueReceive(queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case PCF_CMD_TURNOUT:   handle_turnout(cmd);   break;
                case PCF_CMD_DIRECTION: handle_direction(cmd); break;
                default:
                    ESP_LOGW(TAG, "unknown command type %d", (int)cmd.type);
                    break;
            }
        }
    }
}
