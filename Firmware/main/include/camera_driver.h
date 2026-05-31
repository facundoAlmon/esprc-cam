#pragma once
#include "state.h"
#include "esp_camera.h"

// Initialize OV2640 with settings from CameraState.
// Returns ESP_OK on success.
esp_err_t camera_init(const CameraState* st);

// Apply only image-processing parameters (brightness, AWB, etc.) without
// restarting the DMA. Safe to call while streaming.
void camera_apply_image_settings(const CameraState* st);

// Apply all sensor parameters including framesize/quality (restarts DMA).
// Call after changing resolution or quality.
void camera_apply_settings(const CameraState* st);

// Re-initialize camera with new framesize/quality from CameraState.
// Call after changing framesize or jpegQuality.
esp_err_t camera_reinit(const CameraState* st);

// Shut down camera (call before OTA or full restart).
void camera_deinit(void);
