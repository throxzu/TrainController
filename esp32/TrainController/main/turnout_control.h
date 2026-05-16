#ifndef TURNOUT_CONTROL_H
#define TURNOUT_CONTROL_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
// Command types handled by the PCF8574 task.
//
// All PCF8574 access is funnelled through this single task to avoid
// concurrent I2C transactions from multiple FreeRTOS tasks.
// ---------------------------------------------------------------------------
typedef enum {
    PCF_CMD_TURNOUT,    // pulse one solenoid coil to throw a turnout
    PCF_CMD_DIRECTION,  // set direction pins for a track section (immediate)
} PcfCmdType_t;

typedef struct {
    TickType_t   timeStamp;
    PcfCmdType_t type;
    union {
        struct {
            int  turnoutId; // 1 – NUM_TURNOUTS
            bool straight;  // true = straight, false = diverge
        } turnout;
        struct {
            int  sectionId; // 1 – NUM_SECTIONS
            bool forward;   // true = forward, false = reverse
        } direction;
    };
} PcfCmd_t;

// Task entry point. pvQueue must be a QueueHandle_t for PcfCmd_t messages.
void turnout_control_task(void* pvQueue);

#endif // TURNOUT_CONTROL_H
