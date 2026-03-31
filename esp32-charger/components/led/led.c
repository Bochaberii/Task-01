#include "led.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

// We intentionally keep this module small and self-contained.
// The only public call is update_led(state), which is invoked
// from the sensor_task after the state machine runs.
//
// Internally we use an esp_timer to implement non-blocking
// blinking in the FAULT state. Using a timer instead of
// vTaskDelay allows the LED to blink without slowing down the
// main sensor loop.

static const char *STATE_TAG = "STATE_MACHINE";
static const char *LED_TAG = "LED";

static charger_state_t s_current_state = CHARGER_STATE_IDLE;
static charger_state_t s_prev_state = CHARGER_STATE_IDLE;

static esp_timer_handle_t s_blink_timer = NULL;
static bool s_led_on = false; // simulated on/off for logging

static void blink_timer_cb(void *arg)
{
    (void)arg;
    // Toggle the simulated LED. In real hardware this is where
    // you would flip a GPIO pin.
    s_led_on = !s_led_on;
    ESP_LOGI(LED_TAG, "RED %s (FAULT blink)", s_led_on ? "ON" : "OFF");
}

void led_init(void)
{
    // Create the timer used for FAULT blinking. It will not
    // start running until we enter the FAULT state.
    const esp_timer_create_args_t blink_args = {
        .callback = &blink_timer_cb,
        .arg = NULL,
        .name = "led_blink"};

    esp_err_t err = esp_timer_create(&blink_args, &s_blink_timer);
    if (err != ESP_OK)
    {
        ESP_LOGE(LED_TAG, "Failed to create blink timer: %s", esp_err_to_name(err));
    }
}

static void stop_blink_timer(void)
{
    if (s_blink_timer && esp_timer_is_active(s_blink_timer))
    {
        esp_timer_stop(s_blink_timer);
    }
}

static void start_fault_blink(void)
{
    if (!s_blink_timer)
    {
        return;
    }

    // 2 Hz blinking means a period of 0.5 seconds.
    const int64_t period_us = 500000; // 500 ms
    s_led_on = false;
    esp_timer_start_periodic(s_blink_timer, period_us);
}

void update_led(charger_state_t state)
{
    s_current_state = state;

    // Log state transitions with a clear tag. Keeping this
    // here (rather than spread across tasks) ensures we
    // always log the old and new state in one place.
    if (s_current_state != s_prev_state)
    {
        ESP_LOGI(STATE_TAG, "State changed: %s -> %s",
                 charger_state_to_string(s_prev_state),
                 charger_state_to_string(s_current_state));
        s_prev_state = s_current_state;
    }

    switch (s_current_state)
    {
    case CHARGER_STATE_IDLE:
        stop_blink_timer();
        s_led_on = true;
        ESP_LOGI(LED_TAG, "BLUE ON (IDLE)");
        break;
    case CHARGER_STATE_CHARGING:
        stop_blink_timer();
        s_led_on = true;
        ESP_LOGI(LED_TAG, "GREEN ON (CHARGING)");
        break;
    case CHARGER_STATE_FAULT:
        ESP_LOGI(LED_TAG, "RED BLINK (FAULT)");
        start_fault_blink();
        break;
    default:
        stop_blink_timer();
        ESP_LOGW(LED_TAG, "Unknown state for LED");
        break;
    }
}
