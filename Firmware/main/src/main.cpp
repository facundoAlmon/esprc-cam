#include <string.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"

#include "state.h"
#include "nvs_prefs.h"
#include "camera_driver.h"
#include "webserver.h"
#include "dns_server.h"
#include "espnow_cam.h"

static const char* TAG = "Main";

CameraState camState;
NvsPrefs prefs;

static EventGroupHandle_t s_wifi_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry = 0;
static void start_ap_mode(bool fallback);

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < 10) { esp_wifi_connect(); s_retry++; }
        else xEventGroupSetBits(s_wifi_group, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
        esp_ip4addr_ntoa(&ev->ip_info.ip, camState.espIP, sizeof(camState.espIP));
        ESP_LOGI(TAG, "IP: %s", camState.espIP);
        s_retry = 0;
        xEventGroupSetBits(s_wifi_group, WIFI_CONNECTED_BIT);
    }
}

static void start_ap_mode(bool fallback) {
    esp_netif_t* ap = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wcfg = {};
    if (fallback) {
        strncpy((char*)wcfg.ap.ssid, "ESPRC-CAM", sizeof(wcfg.ap.ssid));
        wcfg.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strlcpy((char*)wcfg.ap.ssid,     camState.wifiSsid, sizeof(wcfg.ap.ssid));
        strlcpy((char*)wcfg.ap.password, camState.wifiPass, sizeof(wcfg.ap.password));
        wcfg.ap.authmode = (strlen(camState.wifiPass) < 8) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    }
    wcfg.ap.ssid_len       = strlen((char*)wcfg.ap.ssid);
    wcfg.ap.channel        = 1;
    wcfg.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(ap, &ip);
    esp_ip4addr_ntoa(&ip.ip, camState.espIP, sizeof(camState.espIP));
    strncpy(camState.wifiMode, "AP", sizeof(camState.wifiMode));
    ESP_LOGI(TAG, "AP mode%s started. IP: %s SSID: %s",
             fallback ? " (fallback)" : "", camState.espIP, (char*)wcfg.ap.ssid);
    dns_server_start(camState.espIP);
}

static void start_sta_mode(void) {
    s_wifi_group = xEventGroupCreate();
    esp_netif_t* sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h1, h2;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,    &wifi_event_handler, NULL, &h1));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &h2));

    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid,     camState.wifiSsid, sizeof(wcfg.sta.ssid));
    strlcpy((char*)wcfg.sta.password, camState.wifiPass, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        esp_netif_ip_info_t ip;
        esp_netif_get_ip_info(sta, &ip);
        esp_ip4addr_ntoa(&ip.ip, camState.espIP, sizeof(camState.espIP));
        strncpy(camState.wifiMode, "STA", sizeof(camState.wifiMode));
        ESP_LOGI(TAG, "STA connected. IP: %s", camState.espIP);
    } else {
        ESP_LOGW(TAG, "STA failed. Falling back to AP");
        esp_event_handler_instance_unregister(IP_EVENT,   IP_EVENT_STA_GOT_IP, h2);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,    h1);
        esp_wifi_stop(); esp_wifi_deinit(); esp_netif_destroy(sta);
        vEventGroupDelete(s_wifi_group);
        start_ap_mode(true);
    }
}

static void init_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (strcmp(camState.wifiMode, "STA") == 0) {
        start_sta_mode();
    } else {
        start_ap_mode(false);
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
}

static void init_preferences(void) {
    static const char* NS = "esprc-cam";
    prefs.begin(NS, false);

    // WiFi
    auto load_str = [&](const char* key, char* dst, size_t sz, const char* def) {
        if (prefs.isKey(key)) strncpy(dst, prefs.getString(key).c_str(), sz);
        else { strncpy(dst, def, sz); prefs.putString(key, def); }
    };
    load_str("wifiSsid", camState.wifiSsid, sizeof(camState.wifiSsid), "ESPRC-CAM");
    load_str("wifiPass",  camState.wifiPass,  sizeof(camState.wifiPass),  "");
    load_str("wifiMode",  camState.wifiMode,  sizeof(camState.wifiMode),  "AP");
    load_str("hostname",  camState.hostname,  sizeof(camState.hostname),  "esprc-cam");

    // Stream
    camState.framesize   = (uint8_t)prefs.getUInt("framesize",   FRAMESIZE_QVGA);
    camState.jpegQuality = (uint8_t)prefs.getUInt("jpegQuality", 12);
    camState.fpsLimit    = (uint8_t)prefs.getUInt("fpsLimit",    0);

    // Sensor (signed values stored offset by 128 in NVS)
    camState.brightness   = (int8_t)((int)prefs.getUInt("brightness",   128) - 128);
    camState.contrast     = (int8_t)((int)prefs.getUInt("contrast",     128) - 128);
    camState.saturation   = (int8_t)((int)prefs.getUInt("saturation",   128) - 128);
    camState.aeLevel      = (int8_t)((int)prefs.getUInt("aeLevel",      128) - 128);
    camState.exposure     = (uint16_t)prefs.getUInt("exposure",     0);
    camState.agcGain      = (uint8_t)prefs.getUInt("agcGain",      0);
    camState.gainCeiling  = (uint8_t)prefs.getUInt("gainCeiling",  0);
    camState.effect       = (uint8_t)prefs.getUInt("effect",       0);
    camState.wbMode       = (uint8_t)prefs.getUInt("wbMode",       0);

    camState.awb           = prefs.getBool("awb",           true);
    camState.awbGain       = prefs.getBool("awbGain",       true);
    camState.aecSensor     = prefs.getBool("aecSensor",     true);
    camState.aecDsp        = prefs.getBool("aecDsp",        true);
    camState.agc           = prefs.getBool("agc",           true);
    camState.bpc           = prefs.getBool("bpc",           true);
    camState.wpc           = prefs.getBool("wpc",           true);
    camState.rawGma        = prefs.getBool("rawGma",        true);
    camState.lensCorrection= prefs.getBool("lensCorrection",true);
    camState.hMirror       = prefs.getBool("hMirror",       false);
    camState.vFlip         = prefs.getBool("vFlip",         false);
    camState.dcw           = prefs.getBool("dcw",           true);
    camState.colorBar      = prefs.getBool("colorBar",      false);
    camState.statsEnabled  = prefs.getBool("statsEnabled",  false);

    prefs.end();
}

static void main_task(void*) {
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    // Camera initialised BEFORE init_preferences() and WiFi.
    //
    // Why order matters:
    //  1. NVS operations (nvs_open, nvs_get_*) allocate from DMA-capable DRAM.
    //     With less free DRAM the camera HAL computes a smaller frame_copy_cnt
    //     (total_cnt=2 instead of 3), which shrinks the PSRAM frame buffer from
    //     ~15 KB to ~11 KB. In that state the DMA ring does not capture a
    //     complete JPEG → NO-SOI on every frame.
    //  2. WiFi ISRs run at interrupt level-3; the VSYNC ISR at level-1. If WiFi
    //     is up during camera init the VSYNC ISR gets preempted and the DMA
    //     misses the JPEG SOI bytes.
    //
    // Solution: set camera defaults directly, init camera, let it capture a few
    // warm-up frames (DMA in steady state), then load NVS preferences and WiFi.

    camState.framesize   = FRAMESIZE_QVGA;
    camState.jpegQuality = 12;
    camState.fpsLimit    = 0;

    if (camera_init(&camState) != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed — halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Drain a few frames so the DMA ring is in steady state before anything
    // else touches DRAM (NVS / WiFi).
    for (int i = 0; i < 5; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            ESP_LOGI(TAG, "Warmup frame %d: %zu B  SOI:%s",
                     i + 1, fb->len,
                     (fb->len >= 3 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) ? "OK" : "MISS");
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGW(TAG, "Warmup frame %d: NULL", i + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    // Now it is safe to load NVS preferences and start WiFi.
    init_preferences();
    // Apply any settings that differ from the defaults used at init.
    camera_apply_image_settings(&camState);

    init_wifi();

    wifi_interface_t espnow_iface = (strcmp(camState.wifiMode, "AP") == 0) ? WIFI_IF_AP : WIFI_IF_STA;
    espnow_cam_init(&camState, espnow_iface);

    webserver_start(&camState);


    // Mark OTA image valid
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (running && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    ESP_LOGI(TAG, "esprc-cam ready. IP: %s  mDNS: %s.local", camState.espIP, camState.hostname);



    // Nothing to loop — streaming runs in dedicated task
    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
}

extern "C" void app_task_start(void) {
    xTaskCreatePinnedToCore(main_task, "main_task", 8192, NULL, 5, NULL, tskNO_AFFINITY);
}
