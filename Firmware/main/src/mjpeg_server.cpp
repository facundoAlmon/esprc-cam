#include "mjpeg_server.h"
#include "camera_driver.h"
#include "led_status.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>

static const char* TAG = "MJPEG";

static volatile float    s_fps = 0.0f;
static volatile uint32_t s_bps = 0;
static volatile bool     s_client_active = false;

#define MJPEG_PORT  81
#define BOUNDARY    "ESP32CAM"
// 64 KB — SVGA JPEGs at quality 12 can exceed 32 KB on complex scenes;
// truncating a JPEG silently causes the browser to discard the frame.
#define FRAME_BUF   (64 * 1024)

static const CameraState* s_state = NULL;
static volatile bool s_running = false;
static uint8_t* s_jpg_buf = NULL;

static void handle_client(int fd) {
    // Disable Nagle — every write goes out immediately, removing up to 200 ms
    // of algorithmic delay that would otherwise buffer small MJPEG boundary
    // headers until the next ACK.
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    // Bound how long send() can block when the client's TCP window fills.
    // Without this, a slow/stalled client causes send() to block indefinitely,
    // which starves fb_get() and drops FPS to zero for the duration.
    struct timeval send_tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));

    // Drain the incoming HTTP request (GET /mjpeg HTTP/1.x …).
    // Set a 2-second timeout so we don't block forever on a slow sender.
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char req[512];
    int total = 0;
    while (total < (int)sizeof(req) - 1) {
        int n = recv(fd, req + total, sizeof(req) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    // Clear the receive timeout; we don't expect data from the client anymore.
    tv.tv_sec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static const char* HTTP_HDR =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Connection: close\r\n"
        "\r\n";
    if (send(fd, HTTP_HDR, strlen(HTTP_HDR), 0) < 0) {
        close(fd);
        return;
    }

    s_client_active = true;
    led_status_set(LED_STREAMING);

    char part_hdr[128];
    uint32_t frame_count = 0;
    uint32_t bytes_count = 0;
    int64_t  t_start = esp_timer_get_time();

    int fail_count = 0;

    while (s_running) {
        // Block while a camera reinit is in progress — deinit() races with fb_get().
        while (camera_is_paused() && s_running) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!s_running) break;

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            // cam_hal timeout — DMA/VSYNC may be stuck. After 3 consecutive
            // failures (~6 s) reinit the camera to clear the I2S DMA state.
            if (++fail_count >= 3 && !camera_is_paused()) {
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

        // Copy to the static buffer and return the DMA buffer immediately so
        // the camera can start capturing the next frame while we're on the wire.
        size_t jpg_len = (fb->len <= FRAME_BUF) ? fb->len : FRAME_BUF;
        memcpy(s_jpg_buf, fb->buf, jpg_len);
        esp_camera_fb_return(fb);

        int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
            "--" BOUNDARY "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %d\r\n\r\n",
            (int)jpg_len);

        // Three sends with TCP_NODELAY active — each goes out immediately
        // without waiting for Nagle's algorithm to coalesce.
        if (send(fd, part_hdr, (size_t)hdr_len, 0) < 0) break;
        if (send(fd, s_jpg_buf, jpg_len,         0) < 0) break;
        if (send(fd, "\r\n",   2,                0) < 0) break;

        frame_count++;
        bytes_count += jpg_len;

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
            // vTaskDelay(1) instead of taskYIELD(): gives httpd (same priority,
            // same core) a guaranteed 1-tick window to process stats/config
            // requests without starving behind a tight MJPEG send loop.
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    s_fps = 0.0f;
    s_bps = 0;
    s_client_active = false;
    led_status_set(LED_READY);

    close(fd);
    ESP_LOGI(TAG, "client disconnected");
}

static void mjpeg_task(void*) {
    // Allocate the frame copy buffer in PSRAM — keeps precious DRAM free.
    s_jpg_buf = (uint8_t*)heap_caps_malloc(FRAME_BUF, MALLOC_CAP_SPIRAM);
    if (!s_jpg_buf) {
        s_jpg_buf = (uint8_t*)malloc(FRAME_BUF);
    }
    if (!s_jpg_buf) {
        ESP_LOGE(TAG, "OOM allocating frame buffer");
        vTaskDelete(NULL);
        return;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        free(s_jpg_buf);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(MJPEG_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(server_fd, 2) < 0) {
        ESP_LOGE(TAG, "bind/listen failed: %d", errno);
        close(server_fd);
        free(s_jpg_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "MJPEG server on :%d", MJPEG_PORT);

    while (s_running) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (!s_running) break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        ESP_LOGI(TAG, "client connected");
        handle_client(client_fd);
    }

    close(server_fd);
    free(s_jpg_buf);
    s_jpg_buf = NULL;
    vTaskDelete(NULL);
}

void mjpeg_server_start(const CameraState* state) {
    s_state   = state;
    s_running = true;
    // Pin to core 0 — same core as the lwIP/TCP stack, minimising context switches
    // on the network send path.
    xTaskCreatePinnedToCore(mjpeg_task, "mjpeg_srv", 4096, NULL, 5, NULL, 0);
}

void mjpeg_server_stop(void) {
    s_running = false;
}

void mjpeg_server_get_stats(float* fps, uint32_t* bps) {
    if (fps) *fps = s_fps;
    if (bps) *bps = s_bps;
}

bool mjpeg_server_has_client(void) {
    return s_client_active;
}
