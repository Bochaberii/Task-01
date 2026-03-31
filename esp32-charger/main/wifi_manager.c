#include "wifi_manager.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_manager";

#define WIFI_NAMESPACE "wifi_cfg"
#define WIFI_KEY_SSID "ssid"
#define WIFI_KEY_PASS "pass"

static bool s_wifi_initialized = false;

static esp_err_t wifi_nvs_init(void)
{
    // NVS is used to store configuration data such as Wi-Fi
    // credentials. We must initialize it before we can read
    // or write any key-value pairs.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t wifi_manager_init(bool mock_mode)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi manager (mock_mode=%d)", mock_mode);

    ESP_ERROR_CHECK(wifi_nvs_init());

    if (mock_mode)
    {
        // In mock mode we do not touch the real Wi-Fi hardware.
        // This allows the rest of the firmware (MQTT, JSON, state
        // machine) to be demonstrated without a real network.
        ESP_LOGW(TAG, "Wi-Fi running in MOCK mode - no real connection will be made");
        return ESP_OK;
    }

    if (!s_wifi_initialized)
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        s_wifi_initialized = true;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_load_credentials(wifi_credentials_t *creds)
{
    if (!creds)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t ssid_len = sizeof(creds->ssid);
    size_t pass_len = sizeof(creds->password);

    err = nvs_get_str(nvs, WIFI_KEY_SSID, creds->ssid, &ssid_len);
    if (err != ESP_OK)
    {
        nvs_close(nvs);
        return err;
    }

    err = nvs_get_str(nvs, WIFI_KEY_PASS, creds->password, &pass_len);
    nvs_close(nvs);
    return err;
}

esp_err_t wifi_manager_save_credentials(const wifi_credentials_t *creds)
{
    if (!creds)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(nvs, WIFI_KEY_SSID, creds->ssid);
    if (err == ESP_OK)
    {
        err = nvs_set_str(nvs, WIFI_KEY_PASS, creds->password);
    }

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t wifi_manager_start_sta(const wifi_credentials_t *creds, bool mock_mode)
{
    if (mock_mode)
    {
        // For simulation we simply pretend Wi-Fi connected.
        ESP_LOGI(TAG, "[MOCK] Pretending to connect to SSID: %s", creds ? creds->ssid : "<none>");
        return ESP_OK;
    }

    if (!creds)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, creds->ssid, sizeof(wifi_cfg.sta.ssid));
    strncpy((char *)wifi_cfg.sta.password, creds->password, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi started, connecting to SSID: %s", creds->ssid);
    ESP_ERROR_CHECK(esp_wifi_connect());
    return ESP_OK;
}
