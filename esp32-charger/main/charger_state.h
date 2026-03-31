#pragma once

#include <stdint.h>

// Simple charge state machine for an EV charge point.
// We keep it small and easy to understand on purpose.

typedef enum
{
    CHARGER_STATE_IDLE = 0,
    CHARGER_STATE_CHARGING,
    CHARGER_STATE_FAULT
} charger_state_t;

// Initialize the charger state machine.
void charger_state_init(void);

// Get the current charger state.
charger_state_t charger_state_get(void);

// Helper that returns a human-readable string for the state.
const char *charger_state_to_string(charger_state_t state);

// Handle a high-level command received over MQTT.
// Accepted commands (case-insensitive examples):
//   "START"       -> move to CHARGING from IDLE
//   "STOP"        -> move to IDLE from CHARGING
//   "FAULT_RESET" -> clear FAULT back to IDLE
// Any unknown command is ignored.
void charger_state_handle_command(const char *cmd_string);

// Simple automatic transitions could be added here (for example,
// moving into FAULT when current exceeds a threshold). For this
// demo we keep the logic inside the command handler so the code
// is easy to follow for beginners.
