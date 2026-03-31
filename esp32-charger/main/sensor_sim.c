#include "sensor_sim.h"

#include <limits.h>
#include <stdint.h>
#include "esp_random.h"

// We keep internal state so that each call to sensor_sim_update
// slightly adjusts the values instead of starting from scratch.

static float s_voltage = 230.0f; // start roughly in the middle of the range
static float s_current = 0.0f;   // no current by default

void sensor_sim_init(void)
{
    s_voltage = 230.0f;
    s_current = 0.0f;
}

static float random_delta(float magnitude)
{
    // esp_random() returns a 32-bit random number.
    uint32_t r = esp_random();
    // Convert to float in [-1.0, 1.0].
    float f = (float)((int32_t)(r >> 1)) / (float)(INT32_MAX);
    return f * magnitude;
}

void sensor_sim_update(sensor_data_t *out)
{
    if (!out)
    {
        return;
    }

    // Voltage random walk in the range [220, 240] V.
    s_voltage += random_delta(0.5f); // small step
    if (s_voltage < 220.0f)
        s_voltage = 220.0f;
    if (s_voltage > 240.0f)
        s_voltage = 240.0f;

    // Current random walk in the range [0, 16] A.
    s_current += random_delta(0.5f);
    if (s_current < 0.0f)
        s_current = 0.0f;
    if (s_current > 16.0f)
        s_current = 16.0f;

    out->voltage_V = s_voltage;
    out->current_A = s_current;
}
