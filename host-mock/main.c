/*
 * ESP32 Charge-Point Status Reporter (Host Mock Version)
 * ------------------------------------------------------
 * This is a pure C, host-based simulation of your firmware.
 *
 * It does NOT use ESP-IDF. Instead, it:
 *  - Mocks Wi-Fi connect
 *  - Mocks MQTT publish
 *  - Implements the same charger state machine
 *  - Simulates sensor data
 *  - Simulates two "tasks" that communicate via a simple queue
 *
 * This lets you run and understand the logic on your PC:
 *   gcc main.c -o app
 *   ./app        (or app.exe on Windows)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------
 * Cross‑platform sleep helper
 * -------------------------------------------------- */
static void sleep_seconds(int seconds)
{
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000));
#else
    sleep(seconds);
#endif
}

/* --------------------------------------------------
 * Charger state machine
 * -------------------------------------------------- */

typedef enum
{
    CHARGER_STATE_IDLE = 0,
    CHARGER_STATE_CHARGING,
    CHARGER_STATE_FAULT
} charger_state_t;

static const char *charger_state_to_string(charger_state_t state)
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

/*
 * Handle high-level commands that would normally come from MQTT:
 *   "START"  - IDLE -> CHARGING
 *   "STOP"   - CHARGING -> IDLE
 *   "FAULT"  - ANY -> FAULT (simulate error)
 *   "RESET"  - FAULT -> IDLE
 *
 * Logs all transitions using printf.
 */
static void charger_state_handle_command(const char *cmd, charger_state_t *state)
{
    if (!cmd || !state)
    {
        return;
    }

    printf("[STATE] Command received: %s\n", cmd);

    charger_state_t old_state = *state;
    charger_state_t new_state = *state;

    /* 🔒 LOCK FAULT STATE (very important for real systems) */
    if (*state == CHARGER_STATE_FAULT)
    {
        if (strcmp(cmd, "RESET") == 0)
        {
            new_state = CHARGER_STATE_IDLE;
        }
        else
        {
            printf("[STATE] Command ignored in FAULT state\n");
            return;
        }
    }
    else if (strcmp(cmd, "START") == 0)
    {
        if (*state == CHARGER_STATE_IDLE)
        {
            new_state = CHARGER_STATE_CHARGING;
        }
        else
        {
            printf("[STATE] START ignored in state %s\n",
                   charger_state_to_string(*state));
        }
    }
    else if (strcmp(cmd, "STOP") == 0)
    {
        if (*state == CHARGER_STATE_CHARGING)
        {
            new_state = CHARGER_STATE_IDLE;
        }
        else
        {
            printf("[STATE] STOP ignored in state %s\n",
                   charger_state_to_string(*state));
        }
    }
    else if (strcmp(cmd, "FAULT") == 0)
    {
        new_state = CHARGER_STATE_FAULT;
    }
    else if (strcmp(cmd, "RESET") == 0)
    {
        printf("[STATE] RESET ignored in state %s\n",
               charger_state_to_string(*state));
    }
    else
    {
        printf("[STATE] Unknown command: %s\n", cmd);
    }

    if (new_state != old_state)
    {
        printf("[STATE] State changed: %s -> %s\n",
               charger_state_to_string(old_state),
               charger_state_to_string(new_state));
        *state = new_state;
    }
}
/* --------------------------------------------------
 * Simulated sensor data
 * -------------------------------------------------- */

/*
 * In the real ESP32 firmware, sensor values would come
 * from ADCs or other peripherals.
 *
 * Here we simulate:
 *  - voltage_V: random walk between 220–240 V
 *  - current_A:
 *        0 when IDLE
 *        5–16 when CHARGING
 *        0 when FAULT
 */

typedef struct
{
    float voltage_V;
    float current_A;
} sensor_data_t;

static void sensor_init(sensor_data_t *s)
{
    if (!s)
        return;
    s->voltage_V = 230.0f; /* start in the middle of the range */
    s->current_A = 0.0f;
}

static float random_float(float min, float max)
{
    float r = (float)rand() / (float)RAND_MAX;
    return min + r * (max - min);
}

static void sensor_update(sensor_data_t *s, charger_state_t state)
{
    if (!s)
        return;

    /* Random walk for voltage, clamped to 220–240 V */
    float delta = random_float(-1.0f, 1.0f);
    s->voltage_V += delta;
    if (s->voltage_V < 220.0f)
        s->voltage_V = 220.0f;
    if (s->voltage_V > 240.0f)
        s->voltage_V = 240.0f;

    /* Current depends on state */
    if (state == CHARGER_STATE_IDLE || state == CHARGER_STATE_FAULT)
    {
        s->current_A = 0.0f;
    }
    else if (state == CHARGER_STATE_CHARGING)
    {
        /* 5–16 A range */
        s->current_A = random_float(5.0f, 16.0f);
    }
}

/* --------------------------------------------------
 * Queue for inter‑task communication
 * --------------------------------------------------
 *
 * In the real ESP32 firmware we used FreeRTOS queues so
 * tasks didn't share globals directly.
 *
 * Here we simulate a very simple single‑producer,
 * single‑consumer queue. This keeps the design concept:
 *  - sensor_task produces status messages
 *  - mqtt_task consumes status messages
 */

typedef struct
{
    char device_id[32];
    int uptime_s;
    float voltage_V;
    float current_A;
    charger_state_t state;
} status_msg_t;

#define STATUS_QUEUE_SIZE 8

typedef struct
{
    status_msg_t buffer[STATUS_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} status_queue_t;

static void status_queue_init(status_queue_t *q)
{
    if (!q)
        return;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

static bool status_queue_push(status_queue_t *q, const status_msg_t *msg)
{
    if (!q || !msg)
        return false;

    if (q->count == STATUS_QUEUE_SIZE)
    {
        /* Queue full: drop the oldest message (simple strategy) */
        q->head = (q->head + 1) % STATUS_QUEUE_SIZE;
        q->count--;
    }

    q->buffer[q->tail] = *msg;
    q->tail = (q->tail + 1) % STATUS_QUEUE_SIZE;
    q->count++;
    return true;
}

static bool status_queue_pop(status_queue_t *q, status_msg_t *out)
{
    if (!q || !out)
        return false;

    if (q->count == 0)
    {
        return false; /* empty */
    }

    *out = q->buffer[q->head];
    q->head = (q->head + 1) % STATUS_QUEUE_SIZE;
    q->count--;
    return true;
}

/* --------------------------------------------------
 * Mock Wi‑Fi and MQTT
 * --------------------------------------------------
 *
 * These replace ESP‑IDF APIs:
 *
 *   esp_wifi_connect()          -> wifi_connect_mock()
 *   esp_mqtt_client_publish()   -> mqtt_publish_mock()
 *
 * They just print to stdout so you can see what would
 * happen on a real device.
 */

static void wifi_connect_mock(void)
{
    printf("[WiFi] Connecting to WiFi (mock)...\n");
    /* In real ESP‑IDF: esp_wifi_connect(); */
    printf("[WiFi] WiFi connected (mock)\n");
}

static void mqtt_publish_mock(const char *topic, const char *payload)
{
    /* In real ESP‑IDF: esp_mqtt_client_publish(client, topic, payload, ...) */
    printf("[MQTT MOCK] PUBLISH\n");
    printf("  Topic  : %s\n", topic);
    printf("  Payload: %s\n\n", payload);
}

/* --------------------------------------------------
 * Task‑like behavior
 * --------------------------------------------------
 *
 * We simulate two "tasks":
 *
 *   sensor_task_step():
 *     - updates state machine (commands)
 *     - updates sensors
 *     - pushes latest status into the queue
 *
 *   mqtt_task_step():
 *     - every 5 seconds pops a message from the queue
 *       and publishes JSON via mqtt_publish_mock()
 *
 * In real ESP32 code these would be FreeRTOS tasks.
 * Here we simply call them from the main loop to keep
 * things beginner‑friendly and portable.
 */

static void sensor_task_step(const char *device_id,
                             int uptime_s,
                             charger_state_t *state,
                             sensor_data_t *sensor,
                             status_queue_t *queue)
{
    if (!device_id || !state || !sensor || !queue)
        return;

    /* -----------------------------------------------------------------
     * Demo commands:
     *  - At 3 s:  START
     *  - At 15 s: FAULT (simulate error)
     *  - At 20 s: RESET (recover from FAULT)
     *  - At 30 s: STOP
     *
     * Additionally, while CHARGING we occasionally inject a random
     * FAULT to show the ANY -> FAULT transition.
     * ----------------------------------------------------------------- */
    if (uptime_s == 3)
    {
        charger_state_handle_command("START", state);
    }
    else if (uptime_s == 15)
    {
        charger_state_handle_command("FAULT", state);
    }
    else if (uptime_s == 20)
    {
        charger_state_handle_command("RESET", state);
    }
    else if (uptime_s == 30)
    {
        charger_state_handle_command("STOP", state);
    }
    else
    {
        /* Random fault injection while charging */
        if (*state == CHARGER_STATE_CHARGING)
        {
            int r = rand() % 50; /* ~2% chance per second */
            if (r == 0)
            {
                charger_state_handle_command("FAULT", state);
            }
        }
    }

    /* Update sensor readings based on current state */
    sensor_update(sensor, *state);

    /* Build a status message and push it into the queue */
    status_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.device_id, device_id, sizeof(msg.device_id) - 1);
    msg.uptime_s = uptime_s;
    msg.voltage_V = sensor->voltage_V;
    msg.current_A = sensor->current_A;
    msg.state = *state;

    (void)status_queue_push(queue, &msg);
}

static void mqtt_task_step(const char *device_id,
                           int uptime_s,
                           status_queue_t *queue)
{
    static int last_publish_time = -5; /* so we publish at t = 0 as well */

    if (!device_id || !queue)
        return;

    /* Publish every 5 seconds */
    if (uptime_s - last_publish_time < 5)
    {
        return;
    }

    status_msg_t msg;
    if (!status_queue_pop(queue, &msg))
    {
        /* Nothing to publish yet */
        return;
    }

    last_publish_time = uptime_s;

    /* Build JSON payload (manually, no JSON library) */
    char json[256];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"device_id\": \"%s\",\n"
             "  \"uptime_s\": %d,\n"
             "  \"voltage_V\": %.2f,\n"
             "  \"current_A\": %.2f,\n"
             "  \"charge_state\": \"%s\"\n"
             "}",
             msg.device_id,
             msg.uptime_s,
             msg.voltage_V,
             msg.current_A,
             charger_state_to_string(msg.state));

    /* Build topic similar to the real firmware */
    char topic[128];
    snprintf(topic, sizeof(topic),
             "chaji/charger/%s/status", device_id);

    mqtt_publish_mock(topic, json);
}

/* --------------------------------------------------
 * main()
 * -------------------------------------------------- */

int main(void)
{
    const char *device_id = "esp32_mock_001";

    /* Seed RNG for sensor randomness and random faults */
    srand((unsigned int)time(NULL));

    printf("ESP32 Charge-Point Status Reporter (Host Mock)\n");
    printf("------------------------------------------------\n");
    printf("Device ID: %s\n\n", device_id);

    /* 1) Wi‑Fi connect (mock) */
    wifi_connect_mock();

    /* Initial state and sensor data */
    charger_state_t state = CHARGER_STATE_IDLE;
    sensor_data_t sensor;
    sensor_init(&sensor);

    /* Initialize the status queue used between our 'tasks' */
    status_queue_t queue;
    status_queue_init(&queue);

    /* 2) Main loop mimicking firmware behavior */
    int uptime_s = 0;

    while (1)
    {
        /* sensor_task: update state machine and sensors, push status */
        sensor_task_step(device_id, uptime_s, &state, &sensor, &queue);

        /* mqtt_task: every 5 s, pop and 'publish' a JSON status */
        mqtt_task_step(device_id, uptime_s, &queue);

        /* Wait 1 second to simulate real time passing */
        sleep_seconds(1);
        uptime_s++;
    }

    return 0;
}
