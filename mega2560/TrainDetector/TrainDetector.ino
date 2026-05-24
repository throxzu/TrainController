#include "detector_config.h"

static int      baseline[14];
static bool     occupied[14];
static uint32_t lastRecal = 0;

// ---------------------------------------------------------------------------
// Average SAMPLE_COUNT analog reads to reduce noise
// ---------------------------------------------------------------------------
static int averagedRead(uint8_t pin)
{
    long sum = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        sum += analogRead(pin);
        delayMicroseconds(200);
    }
    return (int)(sum / SAMPLE_COUNT);
}

// ---------------------------------------------------------------------------
// Sample baseline for all sections (call at startup and periodically)
// ---------------------------------------------------------------------------
static void calibrate()
{
    for (int i = 0; i < 14; i++) {
        baseline[i] = averagedRead(SENSOR_PINS[i]);
    }
    Serial.println("Calibration done");
}

// ---------------------------------------------------------------------------
// Update one section: read sensor, compare to baseline, drive output pin
// ---------------------------------------------------------------------------
static void updateSection(int idx)
{
    int reading  = averagedRead(SENSOR_PINS[idx]);
    int delta    = abs(reading - baseline[idx]);
    bool present = delta > DETECTION_THRESHOLD;

    if (present != occupied[idx]) {
        occupied[idx] = present;
        digitalWrite(OUTPUT_PINS[idx], present ? LOW : HIGH);
        Serial.print("Section ");
        Serial.print(idx + 1);
        Serial.println(present ? ": OCCUPIED" : ": clear");
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    for (int i = 0; i < 14; i++) {
        pinMode(OUTPUT_PINS[i], OUTPUT);
        digitalWrite(OUTPUT_PINS[i], HIGH);   // default: track clear
        occupied[i] = false;
    }

    delay(500);   // let ACS712 outputs settle before sampling baseline
    calibrate();
    lastRecal = millis();

    Serial.println("TrainDetector ready");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop()
{
    // Periodic recalibration (only safe while all sections are clear,
    // but we do it anyway — worst case a brief misread, self-corrects next cycle)
    if (millis() - lastRecal >= RECAL_INTERVAL_MS) {
        calibrate();
        lastRecal = millis();
    }

    for (int i = 0; i < 14; i++) {
        updateSection(i);
    }
}
