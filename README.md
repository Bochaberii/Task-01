# ESP32 Charge-Point Status Reporter

This project is an ESP-IDF (no Arduino) firmware for an ESP32-based "charge-point status reporter". It:

- Connects to Wi-Fi and an MQTT broker.
- Periodically publishes JSON status (voltage, current, state) to a topic.
- Listens for simple text commands on a command topic and drives a small state machine.
- Uses an RGB LED to display the current charger state (IDLE / CHARGING / FAULT).
- Uses FreeRTOS tasks and queues for clean separation of responsibilities.

> **Assumption:** The firmware is built for an ESP32 DevKit-style board. An RGB LED is connected to three GPIO pins (e.g. 25/26/27) via resistors, with common cathode tied to GND. Wi-Fi and MQTT broker are reachable from the network where the ESP32 runs.

---
The Schematic and pcb diagrams are in the esp32-charger directory as images, as well as the recording of the host mock demonstrating how the program would work.
## Kconfig Options

Project-specific Kconfig options are defined in `Kconfig.projbuild` and appear in `menuconfig` under **Charge-Point Demo Configuration**.

| Option                   | Default                      | Description                                                   |
| ------------------------ | ---------------------------- | ------------------------------------------------------------- |
| `CONFIG_WIFI_SSID`       | `"my_wifi_ssid"`             | Default Wi-Fi SSID when no credentials are stored in NVS yet. |
| `CONFIG_WIFI_PASSWORD`   | `"my_wifi_password"`         | Default Wi-Fi password when NVS is empty.                     |
| `CONFIG_MQTT_BROKER_URL` | `"mqtt://broker.hivemq.com"` | MQTT broker URI used by the demo application.                 |

**Notes / assumptions:**

- If NVS already contains Wi-Fi credentials, those take precedence over the Kconfig defaults.
- The MQTT broker must be reachable from the ESP32 (e.g. public HiveMQ broker or a LAN broker).

---

## MQTT Topic Map

The application uses a per-device topic namespace based on a `device_id` string derived from the ESP32 MAC address.

Let `<device_id>` be the device ID (e.g. `esp32_ABC123`).

### Status Topic

- **Topic:** `chaji/charger/<device_id>/status`
- **Publisher:** Firmware (MQTT task)
- **Payload:** JSON object, for example:

```json
{
  "device_id": "esp32_ABC123",
  "uptime_s": 42,
  "voltage_V": 231.5,
  "current_A": 8.2,
  "charge_state": "CHARGING"
}
```

**Fields:**

- `device_id` – String; identifies the specific ESP32.
- `uptime_s` – Integer; seconds since boot (obtained from `esp_timer_get_time`).
- `voltage_V` – Float; simulated line voltage (random walk between ~220–240V).
- `current_A` – Float; simulated current:
  - `0` in IDLE and FAULT.
  - `5–16` A when CHARGING.
- `charge_state` – String; one of `"IDLE"`, `"CHARGING"`, `"FAULT"`.

### Command Topic

- **Topic:** `chaji/charger/<device_id>/cmd`
- **Subscriber:** Firmware (MQTT task)
- **Payload:** Plain text command string, e.g. `"START"`.

**Supported commands (state machine):**

- `"START"` – IDLE → CHARGING
- `"STOP"` – CHARGING → IDLE
- `"FAULT"` – ANY → FAULT (simulate error)
- `"RESET"` – FAULT → IDLE

Invalid transitions are ignored but logged (e.g. `STOP` in IDLE).

---

## Simulation and Usage Instructions

### 1. Building the Firmware (ESP-IDF)

1. Open an ESP-IDF terminal in VS Code.
2. Change to the project folder:
   - `cd esp32-charger`
3. Select the target (once):
   - `idf.py set-target esp32`
4. Configure (optional, to change Kconfig options):
   - `idf.py menuconfig`
   - Navigate to **Charge-Point Demo Configuration** and edit Wi-Fi / MQTT settings.
5. Build the firmware:
   - `idf.py build`

Artifacts (in `build/`):

- Application firmware: `.elf` and `.bin` files.
- `sdkconfig` stores the chosen configuration.

### 2. Running on Real Hardware

1. Connect an ESP32 DevKit.
2. Wire an RGB LED:
   - LED common cathode → ESP32 GND.
   - LED R pin → resistor → GPIO25.
   - LED G pin → resistor → GPIO26.
   - LED B pin → resistor → GPIO27.
3. Flash and monitor:
   - `idf.py -p <PORT> flash monitor`
4. Observe:
   - Logs showing Wi-Fi connection, MQTT connection, state transitions, and JSON publishes.
   - RGB LED changing color / blinking according to charger state.

### 3. Wokwi Simulation (ESP32 + RGB LED + Virtual Wi-Fi)

**Assumptions:**

- You have built the project locally and have the `.elf` firmware in `build/`.
- Wokwi ESP32 DevKitC supports loading custom ESP-IDF firmware.

Steps:

1. Go to <https://wokwi.com> → **New Project** → select **ESP32 DevKit** template.
2. Add components:
   - ESP32 DevKit (if not already present).
   - RGB LED.
   - 3× resistors (e.g. 220 Ω).
3. Wire the RGB LED (matching the firmware pins):
   - LED `R` pin → resistor → ESP32 GPIO25.
   - LED `G` pin → resistor → ESP32 GPIO26.
   - LED `B` pin → resistor → ESP32 GPIO27.
   - LED common `GND` → ESP32 GND.
4. Load custom firmware:
   - In Wokwi, open the firmware/board settings.
   - Choose **Load/Change Firmware**.
   - Select the application `.elf` from the local `build/` folder (e.g. `esp32-charger.elf`).
5. Run the simulation:
   - Click the **Run** button.
   - Open the serial monitor to view ESP-IDF logs.
   - Observe the RGB LED reflecting charger state.
6. Optional: interact via MQTT
   - Use an MQTT client (e.g. HiveMQ WebSocket UI).
   - Connect to `CONFIG_MQTT_BROKER_URL`.
   - Subscribe to `chaji/charger/<device_id>/status` to see JSON.
   - Publish commands (`START`, `STOP`, `FAULT`, `RESET`) to `chaji/charger/<device_id>/cmd` and watch state/LED changes.

### 4. Host-Based Mock Simulation (PC only, no ESP-IDF)

For easier debugging and panel demos without hardware, a separate host-only C program is provided in the `host-mock` folder.

**Assumptions:**

- You have a standard C compiler installed (e.g. `gcc` / MinGW on Windows).
- This program does **not** use ESP-IDF; it is a logical mock.

Steps:

1. Change directory:
   - `cd host-mock`
2. Compile:
   - `gcc main.c -o app`
3. Run:
   - On Windows: `./app` or `app.exe`
4. Observe:
   - Console output simulating Wi-Fi connect and MQTT publishes.
   - State transitions (`IDLE`, `CHARGING`, `FAULT`) logged as they change.
   - JSON payloads printed to stdout instead of being sent to a real broker.

This mock uses the same state machine concepts (commands and transitions) as the firmware but is designed to be portable and easy to run in a VS Code terminal.

---

## Design Notes

- **State Machine**
  - Implemented in a dedicated `charger_state` module.
  - States: `IDLE`, `CHARGING`, `FAULT`.
  - Transitions are driven by string commands (`START`, `STOP`, `FAULT`, `RESET`) received over MQTT.
  - All state changes are logged as `"State changed: OLD -> NEW"` to make behavior easy to trace.

- **Tasks and Queues (FreeRTOS)**
  - Two main tasks:
    - `sensor_task` – owns the state machine and simulated sensors, updates LED and produces status messages.
    - `mqtt_task` – owns the MQTT client, publishes JSON, and forwards incoming commands.
  - Two FreeRTOS queues:
    - `sensor_to_mqtt` – carries status structs from sensor → MQTT.
    - `mqtt_to_sensor` – carries command structs from MQTT → sensor.
  - Using queues avoids sharing mutable global variables between tasks and makes data flow explicit.

- **Sensor Simulation**
  - Voltage is a bounded random walk between roughly 220–240V.
  - Current is 0 A in `IDLE` and `FAULT`, and 5–16 A in `CHARGING`.
  - This decouples the firmware logic from real hardware and keeps the demo predictable.

- **LED Behavior**
  - Implemented in a separate `led` component.
  - Colors/blinking are derived solely from the logical charger state.
  - Uses `esp_timer` to implement non-blocking blinking in FAULT state.

- **Configuration via Kconfig**
  - Wi-Fi SSID/password and MQTT broker URL are configurable without editing C source files.
  - This matches typical ESP-IDF best practices and simplifies switching networks/brokers.

- **Host Mock vs. Firmware**
  - The host mock program mirrors the firmware logic at a high level but runs as a normal C program.
  - It replaces hardware-specific calls with `printf`-based mocks, which is useful for understanding and presenting the design without requiring an ESP32 board.

## USE OF AI
AI was primarily used to assist in developing the firmware, as this domain was entirely new to me. It helped guide the structure, implementation, and best practices, while I focused on understanding the concepts, refining the logic, and ensuring the system behaved as intended.
