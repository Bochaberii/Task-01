#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "charger_state.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_tasks.h"
#include "wifi_manager.h"

static const char *TAG = "main";

// In a real product, whether we run Wi-Fi and MQTT in mock mode
// would typically come from a compile-time flag or configuration.
// For this beginner demo we use a simple constant so the code is
// easy to read. Set to true if you want to run without real Wi-Fi.
static const bool USE_MOCK_WIFI = false;

// Helper that builds a device ID string based on the ESP32 MAC
// address. This guarantees that every board publishes to its own
// MQTT topic without any manual configuration.
static void build_device_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "ESP32_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Charge-Point Status Reporter starting up");

    // Initialize our small state machine first. This makes the
    // rest of the code easier to reason about because there is
    // a single source of truth for the charger state.
    charger_state_init();

    // Initialize Wi-Fi (real or mock) and load credentials.
    ESP_ERROR_CHECK(wifi_manager_init(USE_MOCK_WIFI));

    wifi_credentials_t creds = {0};
    esp_err_t err = wifi_manager_load_credentials(&creds);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "No Wi-Fi credentials in NVS (err=%s)", esp_err_to_name(err));
        ESP_LOGW(TAG, "Falling back to Kconfig defaults (if provided) for this build.");

#ifdef CONFIG_WIFI_SSID
        if (!USE_MOCK_WIFI)
        {
            strncpy(creds.ssid, CONFIG_WIFI_SSID, sizeof(creds.ssid) - 1);
            creds.ssid[sizeof(creds.ssid) - 1] = '\0';
        }
#endif

#ifdef CONFIG_WIFI_PASSWORD
        if (!USE_MOCK_WIFI)
        {
            strncpy(creds.password, CONFIG_WIFI_PASSWORD, sizeof(creds.password) - 1);
            creds.password[sizeof(creds.password) - 1] = '\0';
        }
#endif
    }

    ESP_ERROR_CHECK(wifi_manager_start_sta(&creds, USE_MOCK_WIFI));

    // Build a unique device ID from the MAC address.
    char device_id[32];
    build_device_id(device_id, sizeof(device_id));
    ESP_LOGI(TAG, "Device ID: %s", device_id);

    // Initialize the demo tasks. init_tasks() will:
    //   - create the queues used for communication
    //   - start sensor_task() and mqtt_task()
    // Each task then runs independently under FreeRTOS and
    // communicates only through those queues.
    ESP_ERROR_CHECK(init_tasks(device_id));

    // app_main can now return, or it can perform additional work.
    // In this simple demo we just sleep forever; all useful work
    // is done in the FreeRTOS tasks created above.
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
