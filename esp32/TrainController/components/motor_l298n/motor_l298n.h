#ifndef MOTOR_L298N_H
#define MOTOR_L298N_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
// Speed command for a single track section.
// Direction is sent separately to the PCF8574 task (turnout_control_task)
// because all PCF8574 access is centralised there.
// ---------------------------------------------------------------------------
typedef struct {
    TickType_t timeStamp;
    int        sectionId; // 1 – NUM_SECTIONS
    uint8_t    speed;     // 0–100 percent
} SectionCmd_t;

// Task entry point. pvQueue must be a QueueHandle_t for SectionCmd_t messages.
void motor_l298n_task(void* pvQueue);

#endif // MOTOR_L298N_H
