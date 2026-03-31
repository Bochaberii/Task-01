#include "mqtt_client_app.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "charger_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sensor_sim.h"

static const char *TAG = "mqtt_app";

static esp_mqtt_client_handle_t s_client = NULL;
static char s_status_topic[128];
static char s_cmd_topic[128];

// We send status every 5 seconds from a dedicated FreeRTOS task
// so that the MQTT event handler remains small and focused.
static void status_publish_task(void *pv);

// Helper to build the JSON payload using cJSON.
static char *build_status_json(const char *device_id, const sensor_data_t *data);

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, s_cmd_topic, 1);
        ESP_LOGI(TAG, "Subscribed to command topic: %s", s_cmd_topic);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_DATA:
    {
        // Copy the incoming payload to a null-terminated buffer.
        char topic[128];
        char payload[128];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';

        int data_len = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
        memcpy(payload, event->data, data_len);
        payload[data_len] = '\0';

        ESP_LOGI(TAG, "MQTT cmd topic=%s payload=%s", topic, payload);

        if (strcmp(topic, s_cmd_topic) == 0)
        {
            // Pass the plain text command to the state machine.
            charger_state_handle_command(payload);
        }
        break;
    }

    default:
        break;
    }

    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_id;
    mqtt_event_handler_cb((esp_mqtt_event_handle_t)event_data);
}

esp_err_t mqtt_client_app_start(const char *device_id, bool mock_mode)
{
    (void)mock_mode; // Currently unused but kept for API symmetry.

    if (!device_id)
    {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_status_topic, sizeof(s_status_topic), "chaji/charger/%s/status", device_id);
    snprintf(s_cmd_topic, sizeof(s_cmd_topic), "chaji/charger/%s/cmd", device_id);

    ESP_LOGI(TAG, "Status topic: %s", s_status_topic);
    ESP_LOGI(TAG, "Command topic: %s", s_cmd_topic);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    // Start publisher task.
    xTaskCreate(status_publish_task, "status_pub", 4096, (void *)device_id, 5, NULL);
    return ESP_OK;
}

static char *build_status_json(const char *device_id, const sensor_data_t *data)
{
    if (!device_id || !data)
    {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return NULL;
    }

    // Uptime in seconds since boot. esp_timer_get_time() returns
    // microseconds, so we divide by 1,000,000.
    int64_t uptime_us = esp_timer_get_time();
    int64_t uptime_s = uptime_us / 1000000LL;

    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    cJSON_AddNumberToObject(root, "voltage_V", data->voltage_V);
    cJSON_AddNumberToObject(root, "current_A", data->current_A);
    cJSON_AddStringToObject(root, "charge_state", charger_state_to_string(charger_state_get()));

    // Print as minified JSON string. Caller must free.
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static void status_publish_task(void *pv)
{
    const char *device_id = (const char *)pv;
    sensor_data_t data;

    sensor_sim_init();

    while (1)
    {
        sensor_sim_update(&data);

        char *json = build_status_json(device_id, &data);
        if (json && s_client)
        {
            int msg_id = esp_mqtt_client_publish(s_client, s_status_topic, json, 0, 1, 0);
            ESP_LOGI(TAG, "Published status msg_id=%d: %s", msg_id, json);
        }

        if (json)
        {
            cJSON_free(json);
        }

        // Wait 5 seconds before next update. vTaskDelay expects ticks.
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
