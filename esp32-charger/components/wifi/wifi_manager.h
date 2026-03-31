#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Simple structure to hold Wi-Fi credentials loaded from NVS.
// These are not hard-coded in the firmware; instead we read
// them from NVS so different credentials can be provisioned
// without recompiling.
typedef struct
{
    char ssid[32];
    char password[64];
} wifi_credentials_t;

// Initialize NVS and the Wi-Fi stack in STA mode.
// If mock_mode is true, this function only performs the
// minimal NVS init and returns ESP_OK without touching
// the real Wi-Fi hardware. This is useful when running
// the firmware without network access (e.g., lab demo).
esp_err_t wifi_manager_init(bool mock_mode);

// Load Wi-Fi credentials from NVS. Returns ESP_OK if both
// SSID and password are present. Returns ESP_ERR_NOT_FOUND
// if they have not been provisioned yet.
esp_err_t wifi_manager_load_credentials(wifi_credentials_t *creds);

// Save Wi-Fi credentials to NVS. In a real product, this
// would be called from a provisioning flow (e.g. BLE, web UI).
esp_err_t wifi_manager_save_credentials(const wifi_credentials_t *creds);

// Start Wi-Fi in station mode using the given credentials.
// In mock_mode, this will simply log what it *would* do and
// then return ESP_OK so the rest of the firmware can run.
esp_err_t wifi_manager_start_sta(const wifi_credentials_t *creds, bool mock_mode);

// Simple helper to update Wi-Fi credentials at runtime. This
// function is intentionally straightforward so beginners can
// follow it: it stores new values in NVS and restarts the
// station connection using those values.
void update_wifi_credentials(const char *ssid, const char *password);
