#pragma once

#include "charger_state.h"

// This small component is responsible for "driving" an RGB LED
// based on the charger state. In this beginner demo we do not
// control real GPIOs; instead we log what the LED would do.
//
// We keep the API very small on purpose: the sensor_task owns
// the state machine and simply calls update_led() whenever the
// logical state changes.

void led_init(void);

// Update LED behavior according to the current charger state.
//
// Mapping:
//   IDLE     -> BLUE  (solid)
//   CHARGING -> GREEN (solid)
//   FAULT    -> RED   (blinking at 2 Hz using esp_timer)
//
// The function also logs all state transitions using the
// "STATE_MACHINE" tag, for example:
//   "State changed: IDLE -> CHARGING"
void update_led(charger_state_t state);
