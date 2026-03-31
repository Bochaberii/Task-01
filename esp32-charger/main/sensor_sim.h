#pragma once

#include <stdint.h>

// Simple structure that represents the charger "measurements".
// In this demo we simulate them instead of reading from real ADCs.
typedef struct
{
    float voltage_V; // simulated line voltage (220 - 240 V)
    float current_A; // simulated charge current (0 - 16 A)
} sensor_data_t;

// Initialize the sensor simulation module.
void sensor_sim_init(void);

// Update the simulated values using a random-walk approach
// so that changes look gradual rather than jumping around.
void sensor_sim_update(sensor_data_t *out);
