#pragma once
#include "esp_http_server.h"
#include "state.h"

// Start the WebSocket streaming task. Call once after httpd is up.
void streamer_start(httpd_handle_t server, const CameraState* st);

// Stop the streaming task (called before OTA).
void streamer_stop(void);

// Get current measured FPS and bytes/sec.
void streamer_get_stats(float* fps, uint32_t* bps);

// Register/unregister a WebSocket client file descriptor.
void streamer_add_client(int fd);
void streamer_remove_client(int fd);
