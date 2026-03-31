#include "charger_state.h"

#include <ctype.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "charger_state";

static charger_state_t s_state = CHARGER_STATE_IDLE;

void charger_state_init(void)
{
    // On boot we start in IDLE. Real products might
    // restore the last state from NVS or perform
    // additional safety checks here.
    s_state = CHARGER_STATE_IDLE;
}

charger_state_t charger_state_get(void)
{
    return s_state;
}

const char *charger_state_to_string(charger_state_t state)
{
    switch (state)
    {
    case CHARGER_STATE_IDLE:
        return "IDLE";
    case CHARGER_STATE_CHARGING:
        return "CHARGING";
    case CHARGER_STATE_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}

// Helper to convert a string in-place to upper case.
static void str_to_upper(char *s)
{
    while (*s)
    {
        *s = (char)toupper((unsigned char)*s);
        ++s;
    }
}

void charger_state_handle_command(const char *cmd_string)
{
    if (!cmd_string)
    {
        return;
    }

    char buf[32];
    size_t len = strnlen(cmd_string, sizeof(buf) - 1);
    memcpy(buf, cmd_string, len);
    buf[len] = '\0';

    str_to_upper(buf);

    ESP_LOGI(TAG, "Received command: %s (current state=%s)", buf, charger_state_to_string(s_state));

    if (strcmp(buf, "START") == 0)
    {
        if (s_state == CHARGER_STATE_IDLE)
        {
            s_state = CHARGER_STATE_CHARGING;
            ESP_LOGI(TAG, "Transition: IDLE -> CHARGING");
        }
    }
    else if (strcmp(buf, "STOP") == 0)
    {
        if (s_state == CHARGER_STATE_CHARGING)
        {
            s_state = CHARGER_STATE_IDLE;
            ESP_LOGI(TAG, "Transition: CHARGING -> IDLE");
        }
    }
    else if (strcmp(buf, "FAULT_RESET") == 0)
    {
        if (s_state == CHARGER_STATE_FAULT)
        {
            s_state = CHARGER_STATE_IDLE;
            ESP_LOGI(TAG, "Transition: FAULT -> IDLE");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Unknown command, ignoring");
    }
}
