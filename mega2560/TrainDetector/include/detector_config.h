#pragma once

// ---------------------------------------------------------------------------
// ACS712 analog input pins (one per section, A0–A13)
// ---------------------------------------------------------------------------
static const uint8_t SENSOR_PINS[14] = {
    A0,  A1,  A2,  A3,  A4,  A5,  A6,
    A7,  A8,  A9,  A10, A11, A12, A13
};

// ---------------------------------------------------------------------------
// Digital output pins → PCF8574 0x20 (sections 1–8) and 0x21 (sections 9–14)
// LOW  = train present  (pulls PCF8574 pin LOW, ESP32 reads 0)
// HIGH = track clear    (PCF8574 internal pull-up holds pin HIGH, ESP32 reads 1)
// ---------------------------------------------------------------------------
static const uint8_t OUTPUT_PINS[14] = {
    52, 50, 48, 46, 44, 42, 40, 38,   // sections 1–8  → 0x20 pins 0–7
    36, 34, 32, 30, 28, 26            // sections 9–14 → 0x21 pins 0–5
};

// ---------------------------------------------------------------------------
// Detection threshold
// ACS712 idle output ≈ 2.5 V → ADC ≈ 512 (10-bit, 5 V reference).
// A deviation larger than this indicates current flow (train present).
// Increase if you get false positives from noise; decrease for sensitivity.
// ---------------------------------------------------------------------------
#define DETECTION_THRESHOLD  15   // ADC counts (~73 mV, ~15 mA for ACS712-5A)

// Baseline is sampled once at startup and re-sampled every RECAL_INTERVAL_MS
// to compensate for thermal drift.
#define RECAL_INTERVAL_MS   60000UL   // recalibrate baseline every 60 s

// Number of analog samples averaged per reading (noise reduction)
#define SAMPLE_COUNT  8
