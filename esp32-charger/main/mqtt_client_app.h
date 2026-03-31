#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Initialize and start the MQTT client.
//
// Parameters:
//   device_id  - unique textual identifier for this charger.
//   mock_mode  - when true, we still start the MQTT client but the
//                expectation is that Wi-Fi may be mocked. The code
//                itself does not behave differently, but this flag is
//                kept to mirror the Wi-Fi API and make the flow easy
//                to follow.
//
// The MQTT client:
//   - Connects to broker.hivemq.com
//   - Publishes status to: chaji/charger/<device_id>/status
//   - Subscribes to:       chaji/charger/<device_id>/cmd
//   - For every incoming command, it calls the charger state
//     machine command handler.
esp_err_t mqtt_client_app_start(const char *device_id, bool mock_mode);
