#include "espnow_cam.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"

static const char* TAG = "ESPNOW_CAM";

// ---- Protocol (must match espnow_manager.cpp on the brain) ----
#define MSG_BEACON    0x01
#define MSG_CAM_HELLO 0x02

typedef struct __attribute__((packed)) {
    uint8_t type;
    char    ssid[32];
    char    pass[64];
    uint8_t channel;
} espnow_beacon_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    char    ip[16];
} espnow_cam_hello_t;

// ---- Internal state ----
typedef struct {
    uint8_t mac[6];
    uint8_t data[250];
    int     len;
} recv_event_t;

static CameraState*    s_state  = nullptr;
static wifi_interface_t s_iface = WIFI_IF_AP;
static QueueHandle_t   s_queue  = nullptr;

// Hop through non-overlapping channels first, then the rest.
static const uint8_t HOP_CHANNELS[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const int NUM_HOP_CHANNELS = (int)(sizeof(HOP_CHANNELS));

// ---- Recv callback (runs in WiFi task — must be fast) ----
static void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len < 1 || s_queue == nullptr) return;
    recv_event_t evt;
    memcpy(evt.mac, info->src_addr, 6);
    int copy_len = len < 250 ? len : 250;
    memcpy(evt.data, data, copy_len);
    evt.len = copy_len;
    xQueueSend(s_queue, &evt, 0);
}

// ---- Persist WiFi credentials and mode, then restart ----
static void apply_brain_creds_and_restart(const char* ssid, const char* pass) {
    nvs_handle_t h;
    if (nvs_open("esprc-cam", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "wifiSsid", ssid);
    nvs_set_str(h, "wifiPass", pass);
    nvs_set_str(h, "wifiMode", "STA");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Brain creds saved (ssid=%s). Restarting in STA mode...", ssid);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

// ---- Send CAM_HELLO to brain ----
static void send_cam_hello(const uint8_t* brain_mac) {
    espnow_cam_hello_t msg = {};
    msg.type = MSG_CAM_HELLO;
    strncpy(msg.ip, s_state->espIP, sizeof(msg.ip));

    if (!esp_now_is_peer_exist(brain_mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, brain_mac, 6);
        peer.channel = 0;
        peer.ifidx   = s_iface;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    esp_now_send(brain_mac, (uint8_t*)&msg, sizeof(msg));
    ESP_LOGI(TAG, "CAM_HELLO sent to brain (ip=%s)", s_state->espIP);
}

// ---- Process incoming beacon ----
static void handle_beacon(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(espnow_beacon_t)) return;
    const espnow_beacon_t* beacon = (const espnow_beacon_t*)data;

    ESP_LOGI(TAG, "Beacon from " MACSTR " ssid=%s ch=%d",
             MAC2STR(mac), beacon->ssid, beacon->channel);

    // Always reply with our IP — lets brain update cameraIP without mDNS.
    send_cam_hello(mac);

    // If already on the brain's WiFi, nothing more to do.
    if (strcmp(s_state->wifiMode, "STA") == 0 &&
        strcmp(s_state->wifiSsid, beacon->ssid) == 0) {
        return;
    }

    // New credentials — save and restart into STA mode.
    apply_brain_creds_and_restart(beacon->ssid, beacon->pass);
}

// ---- Main ESP-NOW task ----
static void espnow_cam_task(void*) {
    bool in_ap_mode = (strcmp(s_state->wifiMode, "AP") == 0);
    int  hop_idx    = 0;

    recv_event_t evt;

    // In STA mode: send CAM_HELLO immediately (brain might have missed mDNS).
    // We can't send to a specific MAC yet (don't know brain's MAC), so we wait
    // for the brain's first beacon to arrive and reply to it.
    // Meanwhile, also send periodic keepalives once the brain MAC is known.
    uint8_t brain_mac[6] = {};
    bool    brain_known  = false;
    int64_t last_hello_us = 0;
    const int64_t HELLO_INTERVAL_US = 30000000LL; // 30 s

    for (;;) {
        // Channel hopping (AP mode only, while no beacon received yet).
        if (in_ap_mode && !brain_known) {
            uint8_t target_ch = HOP_CHANNELS[hop_idx];
            esp_wifi_set_channel(target_ch, WIFI_SECOND_CHAN_NONE);
            hop_idx = (hop_idx + 1) % NUM_HOP_CHANNELS;
        }

        // Wait for recv event (300 ms = one hop dwell).
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(300)) == pdTRUE) {
            if (evt.len < 1) continue;
            if (evt.data[0] == MSG_BEACON) {
                if (!brain_known) {
                    memcpy(brain_mac, evt.mac, 6);
                    brain_known = true;
                }
                handle_beacon(evt.mac, evt.data, evt.len);
                // handle_beacon may restart the device (if creds changed).
                // If we reach here we're already on brain's WiFi → stop hopping.
                in_ap_mode = false;
            }
        }

        // Periodic CAM_HELLO once brain is known (STA mode keepalive).
        if (brain_known && !in_ap_mode) {
            int64_t now = esp_timer_get_time();
            if (now - last_hello_us >= HELLO_INTERVAL_US) {
                last_hello_us = now;
                send_cam_hello(brain_mac);
            }
        }
    }
}

// ---- Public API ----
void espnow_cam_init(CameraState* state, wifi_interface_t iface) {
    s_state = state;
    s_iface = iface;
    s_queue = xQueueCreate(8, sizeof(recv_event_t));

    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_recv_cb(on_recv);

    xTaskCreate(espnow_cam_task, "espnow_cam", 4096, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "ESP-NOW cam initialized (mode=%s, iface=%d)",
             state->wifiMode, iface);
}
