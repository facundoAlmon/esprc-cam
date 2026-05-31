#pragma once

#include "state.h"
#include "esp_wifi_types.h"

// Call once after init_wifi() / esp_wifi_start().
void espnow_cam_init(CameraState* state, wifi_interface_t iface);
