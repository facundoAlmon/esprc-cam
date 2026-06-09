#include "streamer.h"
#include "mjpeg_server.h"
#include "camera_driver.h"
#include "led_status.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "Streamer";

#define MAX_WS_CLIENTS 4

static httpd_handle_t s_server = NULL;
static const CameraState* s_state = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;

static int s_clients[MAX_WS_CLIENTS];
static int s_client_count = 0;
static SemaphoreHandle_t s_clients_mutex = NULL;

// Stats
static volatile float s_fps = 0.0f;
static volatile uint32_t s_bps = 0;

static void stream_task(void* arg) {
    // Give camera DMA time to stabilize before first grab
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Warm up: grab and discard the first frame — OV2640 often produces a
    // partial/corrupted frame right after init while AGC/AWB settle.
    {
        camera_fb_t* warmup = esp_camera_fb_get();
        if (warmup) esp_camera_fb_return(warmup);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    uint32_t frame_count = 0;
    uint32_t bytes_count = 0;
    int64_t t_start = esp_timer_get_time();
    int fail_count = 0;

    while (s_running) {
        // Don't grab frames when no WS clients — avoids starving the MJPEG handler.
        xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
        int client_count = s_client_count;
        xSemaphoreGive(s_clients_mutex);

        if (client_count == 0) {
            fail_count = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Yield the single camera frame buffer to the MJPEG task while it has
        // an active client. Both tasks share fb_count=1 — competing for the
        // same buffer causes mutual blocking spikes and FPS drops on both streams.
        if (mjpeg_server_has_client()) {
            fail_count = 0;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Block while a camera reinit is in progress.
        if (camera_is_paused()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            // DMA/VSYNC stuck — recover only when MJPEG has no client (if MJPEG
            // is active it will handle recovery itself via its own fail_count).
            if (++fail_count >= 3 && !mjpeg_server_has_client() && !camera_is_paused()) {
                ESP_LOGW(TAG, "Camera DMA stuck (%d timeouts) — reiniting", fail_count);
                led_status_set(LED_RETRY);
                camera_pause();
                vTaskDelay(pdMS_TO_TICKS(200));
                camera_reinit(s_state);
                vTaskDelay(pdMS_TO_TICKS(200));
                camera_resume();
                led_status_set(LED_STREAMING);
                fail_count = 0;
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        fail_count = 0;

        xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
        if (s_client_count > 0) {
            httpd_ws_frame_t pkt = {};
            pkt.final     = true;
            pkt.type      = HTTPD_WS_TYPE_BINARY;
            pkt.payload   = fb->buf;
            pkt.len       = fb->len;

            for (int i = 0; i < s_client_count; i++) {
                esp_err_t err = httpd_ws_send_frame_async(s_server, s_clients[i], &pkt);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "WS client fd=%d dropped: %s", s_clients[i], esp_err_to_name(err));
                    s_clients[i] = s_clients[--s_client_count];
                    i--;
                }
            }
            bytes_count += fb->len;
        }
        xSemaphoreGive(s_clients_mutex);

        esp_camera_fb_return(fb);
        frame_count++;

        // Update stats every second
        int64_t elapsed = esp_timer_get_time() - t_start;
        if (elapsed >= 1000000LL) {
            s_fps = (float)frame_count * 1e6f / (float)elapsed;
            s_bps = bytes_count;
            frame_count = 0;
            bytes_count = 0;
            t_start = esp_timer_get_time();
        }

        if (s_state->fpsLimit > 0) {
            vTaskDelay(pdMS_TO_TICKS(1000 / s_state->fpsLimit));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1)); // yield
        }
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

void streamer_start(httpd_handle_t server, const CameraState* st) {
    s_server = server;
    s_state  = st;
    s_clients_mutex = xSemaphoreCreateMutex();
    s_running = true;
    xTaskCreatePinnedToCore(stream_task, "streamer", 4096, NULL, 6, &s_task, 1);
    ESP_LOGI(TAG, "WebSocket streamer started");
}

void streamer_stop(void) {
    s_running = false;
    if (s_task) {
        // Give task time to exit
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void streamer_get_stats(float* fps, uint32_t* bps) {
    if (fps) *fps = s_fps;
    if (bps) *bps = s_bps;
}

void streamer_add_client(int fd) {
    if (!s_clients_mutex) return;
    xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
    if (s_client_count < MAX_WS_CLIENTS) {
        s_clients[s_client_count++] = fd;
        ESP_LOGI(TAG, "WS client added fd=%d, total=%d", fd, s_client_count);
        if (s_client_count == 1) led_status_set(LED_STREAMING);
    }
    xSemaphoreGive(s_clients_mutex);
}

void streamer_remove_client(int fd) {
    if (!s_clients_mutex) return;
    xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i] == fd) {
            s_clients[i] = s_clients[--s_client_count];
            ESP_LOGI(TAG, "WS client removed fd=%d, total=%d", fd, s_client_count);
            if (s_client_count == 0) led_status_set(LED_READY);
            break;
        }
    }
    xSemaphoreGive(s_clients_mutex);
}
