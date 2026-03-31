#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "charger_state.h"

// This header defines the types and initialization function
// for the simple FreeRTOS architecture used by this demo.
//
// We explicitly do NOT share sensor values or charger state
// using global variables. Instead, we use FreeRTOS queues to
// pass small "messages" between tasks. Each message contains
// a snapshot of the data at one point in time.
//
// This makes the code easier to reason about for beginners:
//  - each task owns its own local variables
//  - communication is explicit through queue send/receive
//  - we avoid hard-to-debug race conditions.

// Message sent from the sensor_task to the mqtt_task.
// It contains everything the MQTT side needs to publish
// a charger status update.
typedef struct
{
    charger_state_t state; // logical charger state (IDLE/CHARGING/FAULT)
    float voltage_V;       // simulated line voltage
    float current_A;       // simulated charge current
} charger_status_msg_t;

// Message sent from the mqtt_task to the sensor_task.
// It represents a high-level command received over MQTT
// such as "START", "STOP" or "FAULT_RESET".
typedef struct
{
    char command[16];
} charger_command_msg_t;

// Initialize the FreeRTOS queues and create the two main
// tasks used by this example:
//   - sensor_task: owns the charger state machine and LEDs
//   - mqtt_task:   owns the MQTT connection and JSON publish
//
// device_id must point to a zero-terminated string that
// uniquely identifies this ESP32 (we build one from the
// MAC address in main.c). The function copies the id
// internally so the caller's buffer can be on the stack.
esp_err_t init_tasks(const char *device_id);
