#include "app_tasks.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sensor_sim.h"
#include "led.h"

static const char *TAG = "app_tasks";

// Queues used for communication between tasks. Note that we
// only send small "messages" over these queues (copies of
// structs). The actual state lives inside the tasks.
static QueueHandle_t s_sensor_to_mqtt_q = NULL; // sensor_task -> mqtt_task
static QueueHandle_t s_mqtt_to_sensor_q = NULL; // mqtt_task -> sensor_task

// Parameters passed to each task at creation time. We use
// a small struct instead of globals so it is clear which
// data each task depends on.
typedef struct
{
    QueueHandle_t sensor_to_mqtt;
    QueueHandle_t mqtt_to_sensor;
} sensor_task_params_t;

typedef struct
{
    QueueHandle_t sensor_to_mqtt;
    QueueHandle_t mqtt_to_sensor;
    char device_id[32];
} mqtt_task_params_t;

static sensor_task_params_t s_sensor_params;
static mqtt_task_params_t s_mqtt_params;

// MQTT client objects and topics are kept local to this
// module. They are not shared directly with other modules.
static esp_mqtt_client_handle_t s_client = NULL;
static char s_status_topic[128];
static char s_cmd_topic[128];

// Forward declarations of the tasks.
static void sensor_task(void *pvParameters);
static void mqtt_task(void *pvParameters);

// Helper to configure the two FreeRTOS queues. Keeping this
// in a separate function makes the overall flow easier to
// read and mirrors the logical design step: first we define
// the communication channels, then we create the tasks.
static esp_err_t queue_init(void)
{
    // Queue that carries sensor -> MQTT status messages.
    s_sensor_to_mqtt_q = xQueueCreate(10, sizeof(charger_status_msg_t));

    // Queue that carries MQTT -> sensor command messages.
    s_mqtt_to_sensor_q = xQueueCreate(5, sizeof(charger_command_msg_t));

    if (!s_sensor_to_mqtt_q || !s_mqtt_to_sensor_q)
    {
        ESP_LOGE(TAG, "Failed to create queues");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// LED behavior is implemented in the separate led component.
// The sensor_task simply tells that component what the current
// logical state is, and the LED code takes care of colors and
// blinking using esp_timer.

// Build the JSON payload that will be sent over MQTT. We do
// this on the MQTT side because it already knows the device id
// and uptime. The sensor side only cares about the physical
// measurements and logical state.
static char *build_status_json(const char *device_id, const charger_status_msg_t *msg)
{
    if (!device_id || !msg)
    {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return NULL;
    }

    int64_t uptime_us = esp_timer_get_time();
    int64_t uptime_s = uptime_us / 1000000LL;

    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);
    cJSON_AddNumberToObject(root, "voltage_V", msg->voltage_V);
    cJSON_AddNumberToObject(root, "current_A", msg->current_A);
    cJSON_AddStringToObject(root, "charge_state", charger_state_to_string(msg->state));

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// MQTT event handler used by the esp-mqtt component. This runs
// in the context of the MQTT task created by the library, not
// in our mqtt_task() FreeRTOS task. Its job is very small:
// - when data arrives on the command topic, push a command
//   message into the mqtt->sensor queue so that sensor_task
//   can update the state machine.
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    (void)client;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(event->client, s_cmd_topic, 1);
        ESP_LOGI(TAG, "Subscribed to command topic: %s", s_cmd_topic);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_DATA:
    {
        char topic[128];
        char payload[16];

        int topic_len = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, topic_len);
        topic[topic_len] = '\0';

        int data_len = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
        memcpy(payload, event->data, data_len);
        payload[data_len] = '\0';

        ESP_LOGI(TAG, "MQTT cmd topic=%s payload=%s", topic, payload);

        if (strcmp(topic, s_cmd_topic) == 0 && s_mqtt_to_sensor_q)
        {
            charger_command_msg_t cmd = {0};
            strncpy(cmd.command, payload, sizeof(cmd.command) - 1);
            // We use a queue instead of a global string here so
            // the sensor task receives a clear, self-contained
            // message and we avoid sharing mutable state.
            xQueueSend(s_mqtt_to_sensor_q, &cmd, 0);
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

// This task owns the charger state machine and the simulated
// sensors. It is the ONLY place that is allowed to change the
// state. Other tasks (like mqtt_task) can only request changes
// indirectly by sending commands over the queue.
static void sensor_task(void *pvParameters)
{
    sensor_task_params_t *params = (sensor_task_params_t *)pvParameters;
    charger_state_init();
    sensor_sim_init();
    led_init();

    charger_state_t current_state = charger_state_get();
    sensor_data_t sensor_values;

    while (1)
    {
        // 1) Handle any pending command from MQTT.
        charger_command_msg_t cmd_msg;
        if (xQueueReceive(params->mqtt_to_sensor, &cmd_msg, 0) == pdPASS)
        {
            // The state machine itself lives in charger_state.c,
            // but ONLY this task calls it. This keeps all state
            // transitions in a single place, which is easier to
            // reason about and avoids concurrency bugs.
            charger_state_handle_command(cmd_msg.command);
            current_state = charger_state_get();
        }

        // 2) Update simulated sensor readings.
        sensor_sim_update(&sensor_values);

        // 3) Update LED behavior based on the current state.
        //    LED logic lives in the led component, but the
        //    sensor task is the only place that calls it. This
        //    keeps responsibility for visual feedback with the
        //    same task that owns the state machine.
        update_led(current_state);

        // 4) Send a status snapshot to the MQTT task.
        charger_status_msg_t status = {
            .state = current_state,
            .voltage_V = sensor_values.voltage_V,
            .current_A = sensor_values.current_A,
        };

        xQueueSend(params->sensor_to_mqtt, &status, 0);

        // Run this loop once per second. This is slow enough to
        // be easy to observe in the logs but fast enough to feel
        // "live" in a demo.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// This task owns the MQTT connection. It does not know how to
// update the charger state itself; instead it:
//   - receives status messages from the sensor task
//   - publishes them as JSON
//   - forwards incoming commands to the sensor task via queue
static void mqtt_task(void *pvParameters)
{
    mqtt_task_params_t *params = (mqtt_task_params_t *)pvParameters;

    // Build topic strings once. We use the device id passed from
    // main.c so that each board has its own topic namespace.
    snprintf(s_status_topic, sizeof(s_status_topic), "chaji/charger/%s/status", params->device_id);
    snprintf(s_cmd_topic, sizeof(s_cmd_topic), "chaji/charger/%s/cmd", params->device_id);

    ESP_LOGI(TAG, "Status topic: %s", s_status_topic);
    ESP_LOGI(TAG, "Command topic: %s", s_cmd_topic);

    // Use the broker URL from Kconfig (menuconfig). This makes it
    // easy to point the demo at a different broker without
    // touching the C source code. If the Kconfig option is not
    // present for some reason, fall back to a reasonable default.
    esp_mqtt_client_config_t mqtt_cfg = {
#ifdef CONFIG_MQTT_BROKER_URL
        .broker.address.uri = CONFIG_MQTT_BROKER_URL,
#else
        .broker.address.uri = "mqtt://broker.hivemq.com",
#endif
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client)
    {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        vTaskDelete(NULL);
        return;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Main loop: wait for status updates from the sensor task
    // and publish each one as a JSON message.
    charger_status_msg_t status;

    while (1)
    {
        if (xQueueReceive(params->sensor_to_mqtt, &status, portMAX_DELAY) == pdPASS)
        {
            char *json = build_status_json(params->device_id, &status);
            if (json)
            {
                int msg_id = esp_mqtt_client_publish(s_client, s_status_topic, json, 0, 1, 0);
                ESP_LOGI(TAG, "Published status msg_id=%d: %s", msg_id, json);
                cJSON_free(json);
            }
        }
    }
}

esp_err_t init_tasks(const char *device_id)
{
    if (!device_id)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing tasks and queues");

    esp_err_t err = queue_init();
    if (err != ESP_OK)
    {
        return err;
    }

    // Fill task parameter structures.
    s_sensor_params.sensor_to_mqtt = s_sensor_to_mqtt_q;
    s_sensor_params.mqtt_to_sensor = s_mqtt_to_sensor_q;

    s_mqtt_params.sensor_to_mqtt = s_sensor_to_mqtt_q;
    s_mqtt_params.mqtt_to_sensor = s_mqtt_to_sensor_q;
    memset(s_mqtt_params.device_id, 0, sizeof(s_mqtt_params.device_id));
    strncpy(s_mqtt_params.device_id, device_id, sizeof(s_mqtt_params.device_id) - 1);

    // Create tasks. Both run independently under FreeRTOS. The
    // RTOS scheduler switches between them automatically; each
    // task cooperates by using vTaskDelay or queue operations
    // that can block.
    xTaskCreate(sensor_task, "sensor_task", 4096, &s_sensor_params, 5, NULL);
    xTaskCreate(mqtt_task, "mqtt_task", 4096, &s_mqtt_params, 5, NULL);

    return ESP_OK;
}
