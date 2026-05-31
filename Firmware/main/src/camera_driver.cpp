#include "camera_driver.h"
#include "esp_log.h"

static const char* TAG = "Camera";

// AI-Thinker ESP32-CAM pin assignment
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

esp_err_t camera_init(const CameraState* st) {
    camera_config_t config = {};
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_pclk     = CAM_PIN_PCLK;

    // Match ESPHome's working configuration exactly.
    config.xclk_freq_hz = 20000000;
    config.ledc_timer   = LEDC_TIMER_0;   // ESPHome default
    config.ledc_channel = LEDC_CHANNEL_0; // ESPHome default

    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = (framesize_t)st->framesize;
    config.jpeg_quality = st->jpegQuality;

    // fb_count=1: capture one frame, wait for consumer, repeat.
    // Sequential mode avoids the fb_count=2 pipelining bug in esp32-camera v2.x
    // on ESP32 where the HAL fills both buffers before anyone reads, then
    // the VSYNC semaphore state becomes inconsistent.
    //
    // CAMERA_FB_IN_DRAM: The cam_hal always has "PSRAM DMA mode disabled" on
    // ESP32 because PSRAM shares the SPI bus and DMA can't write there directly.
    // With CAMERA_FB_IN_PSRAM the HAL copies each DMA node (2 KB) to PSRAM over
    // the SPI bus — ~3 ms/frame of extra latency at 40 MHz SPIRAM.
    // With CAMERA_FB_IN_DRAM the copy is DRAM→DRAM through cache, ~10× faster.
    // QVGA JPEG = ~15 KB; available DRAM heap = 141 KB → fits comfortably.
    config.fb_count    = 1;
    config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_DRAM;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Do NOT call any sensor setters here. esp_camera_init() already configured
    // framesize and quality. Calling set_framesize() after init restarts the I2S
    // DMA and can leave it in a state where the VSYNC interrupt never re-fires.
    // Apply image settings only after the first successful fb_get/fb_return cycle.

    ESP_LOGI(TAG, "Camera ready. Framesize=%d Quality=%d", st->framesize, st->jpegQuality);
    return ESP_OK;
}

void camera_apply_image_settings(const CameraState* st) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    s->set_brightness(s, st->brightness);
    s->set_contrast(s, st->contrast);
    s->set_saturation(s, st->saturation);
    s->set_special_effect(s, st->effect);
    s->set_whitebal(s, st->awb ? 1 : 0);
    s->set_awb_gain(s, st->awbGain ? 1 : 0);
    s->set_wb_mode(s, st->wbMode);
    s->set_exposure_ctrl(s, st->aecSensor ? 1 : 0);
    s->set_aec2(s, st->aecDsp ? 1 : 0);
    s->set_ae_level(s, st->aeLevel);
    s->set_aec_value(s, st->exposure);
    s->set_gain_ctrl(s, st->agc ? 1 : 0);
    s->set_agc_gain(s, st->agcGain);
    s->set_gainceiling(s, (gainceiling_t)st->gainCeiling);
    s->set_bpc(s, st->bpc ? 1 : 0);
    s->set_wpc(s, st->wpc ? 1 : 0);
    s->set_raw_gma(s, st->rawGma ? 1 : 0);
    s->set_lenc(s, st->lensCorrection ? 1 : 0);
    s->set_hmirror(s, st->hMirror ? 1 : 0);
    s->set_vflip(s, st->vFlip ? 1 : 0);
    s->set_dcw(s, st->dcw ? 1 : 0);
    s->set_colorbar(s, st->colorBar ? 1 : 0);
}

void camera_apply_settings(const CameraState* st) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_framesize(s, (framesize_t)st->framesize);
    s->set_quality(s, st->jpegQuality);
    camera_apply_image_settings(st);
}

esp_err_t camera_reinit(const CameraState* st) {
    camera_deinit();
    return camera_init(st);
}

void camera_deinit(void) {
    esp_camera_deinit();
    ESP_LOGI(TAG, "Camera deinitialized");
}
