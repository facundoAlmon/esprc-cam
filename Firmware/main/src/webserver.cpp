#include "webserver.h"
#include "mjpeg_server.h"
#include "streamer.h"
#include "camera_driver.h"
#include "led_status.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_client.h"
#include <ArduinoJson.h>
#include <string.h>
#include <string>

static const char* TAG = "WebServer";
static const char* NVS_NS = "esprc-cam";

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

// ---- NVS helpers ----

static bool nvs_put_u32(const char* key, uint32_t v) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_u32(h, key, v) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h); return ok;
}

static bool nvs_put_bool(const char* key, bool v) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_u8(h, key, v ? 1 : 0) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h); return ok;
}

static bool nvs_put_str(const char* key, const char* value) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_str(h, key, value ? value : "") == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h); return ok;
}

static bool nvs_clear_all() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_erase_all(h) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h); return ok;
}

// ---- Server state ----

static httpd_handle_t s_httpd = NULL;
static CameraState* g_state = NULL;

// ---- Config JSON builder ----

static std::string build_config_json() {
    JsonDocument doc;
    doc["wifiSsid"]        = g_state->wifiSsid;
    doc["wifiPass"]        = g_state->wifiPass;
    doc["wifiMode"]        = g_state->wifiMode;
    doc["ip"]              = g_state->espIP;
    doc["hostname"]        = g_state->hostname;
    doc["framesize"]       = g_state->framesize;
    doc["camQuality"]      = g_state->jpegQuality;
    doc["fpsLimit"]        = g_state->fpsLimit;
    doc["camBright"]       = g_state->brightness;
    doc["camContrast"]     = g_state->contrast;
    doc["camSaturation"]   = g_state->saturation;
    doc["camAELevel"]      = g_state->aeLevel;
    doc["camExpo"]         = g_state->exposure;
    doc["camAGCGain"]      = g_state->agcGain;
    doc["camGainCeiling"]  = g_state->gainCeiling;
    doc["camEffect"]       = g_state->effect;
    doc["camWBMode"]       = g_state->wbMode;
    doc["camAwb"]          = g_state->awb          ? 1 : 0;
    doc["camAwbGain"]      = g_state->awbGain      ? 1 : 0;
    doc["camAECSensor"]    = g_state->aecSensor     ? 1 : 0;
    doc["camAECDSP"]       = g_state->aecDsp        ? 1 : 0;
    doc["camAGC"]          = g_state->agc           ? 1 : 0;
    doc["camBPC"]          = g_state->bpc           ? 1 : 0;
    doc["camWPC"]          = g_state->wpc           ? 1 : 0;
    doc["camRAWGCMA"]      = g_state->rawGma        ? 1 : 0;
    doc["camLensCorr"]     = g_state->lensCorrection? 1 : 0;
    doc["camHmirror"]      = g_state->hMirror       ? 1 : 0;
    doc["camVFlip"]        = g_state->vFlip         ? 1 : 0;
    doc["camDCW"]          = g_state->dcw           ? 1 : 0;
    doc["camColorBar"]     = g_state->colorBar      ? 1 : 0;
    doc["statsEnabled"]    = g_state->statsEnabled  ? 1 : 0;
    std::string out;
    serializeJson(doc, out);
    return out;
}

// ---- Handlers ----

static esp_err_t get_index_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    extern const uint8_t html_start[] asm("_binary_index_html_start");
    extern const uint8_t html_end[]   asm("_binary_index_html_end");
    return httpd_resp_send(req, (const char*)html_start, html_end - html_start);
}

static esp_err_t cors_handler(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

#define CORS(req) do { \
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); \
} while(0)

// GET /api/config  (also aliased at /cam-config for brain compatibility)
static esp_err_t get_config_handler(httpd_req_t* req) {
    std::string json = build_config_json();
    CORS(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

#define MAX_POST 2048

static esp_err_t post_config_handler(httpd_req_t* req) {
    char buf[MAX_POST];
    int ret = httpd_req_recv(req, buf, MIN((int)req->content_len, MAX_POST - 1));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    JsonDocument doc;
    deserializeJson(doc, buf);

    bool cam_changed = false;

    if (doc.containsKey("framesize")) {
        uint8_t fs = doc["framesize"];
        if (fs != g_state->framesize) { g_state->framesize = fs; cam_changed = true; }
        nvs_put_u32("framesize", fs);
    }
    if (doc.containsKey("camQuality")) {
        uint8_t q = doc["camQuality"];
        if (q != g_state->jpegQuality) { g_state->jpegQuality = q; cam_changed = true; }
        nvs_put_u32("jpegQuality", q);
    }
    if (doc.containsKey("fpsLimit")) {
        g_state->fpsLimit = doc["fpsLimit"];
        nvs_put_u32("fpsLimit", g_state->fpsLimit);
    }
    if (doc.containsKey("hostname")) {
        strncpy(g_state->hostname, doc["hostname"] | g_state->hostname, sizeof(g_state->hostname));
        nvs_put_str("hostname", g_state->hostname);
    }

    // Sensor params — apply live without reinit
#define APPLY_I8(key, field) if (doc.containsKey(key)) { g_state->field = (int8_t)(int)doc[key]; nvs_put_u32(#field, (uint32_t)((int8_t)g_state->field + 128)); }
#define APPLY_U8(key, field) if (doc.containsKey(key)) { g_state->field = (uint8_t)(int)doc[key]; nvs_put_u32(#field, g_state->field); }
#define APPLY_U16(key, field) if (doc.containsKey(key)) { g_state->field = (uint16_t)(int)doc[key]; nvs_put_u32(#field, g_state->field); }
#define APPLY_BOOL(key, field) if (doc.containsKey(key)) { g_state->field = doc[key].as<int>() != 0; nvs_put_bool(#field, g_state->field); }

    APPLY_I8("camBright",    brightness)
    APPLY_I8("camContrast",  contrast)
    APPLY_I8("camSaturation",saturation)
    APPLY_I8("camAELevel",   aeLevel)
    APPLY_U16("camExpo",     exposure)
    APPLY_U8("camAGCGain",   agcGain)
    APPLY_U8("camGainCeiling",gainCeiling)
    APPLY_U8("camEffect",    effect)
    APPLY_U8("camWBMode",    wbMode)
    APPLY_BOOL("camAwb",     awb)
    APPLY_BOOL("camAwbGain", awbGain)
    APPLY_BOOL("camAECSensor",aecSensor)
    APPLY_BOOL("camAECDSP",  aecDsp)
    APPLY_BOOL("camAGC",     agc)
    APPLY_BOOL("camBPC",     bpc)
    APPLY_BOOL("camWPC",     wpc)
    APPLY_BOOL("camRAWGCMA", rawGma)
    APPLY_BOOL("camLensCorr",lensCorrection)
    APPLY_BOOL("camHmirror", hMirror)
    APPLY_BOOL("camVFlip",   vFlip)
    APPLY_BOOL("camDCW",     dcw)
    APPLY_BOOL("camColorBar",colorBar)
    APPLY_BOOL("statsEnabled",statsEnabled)

    if (cam_changed) {
        // Pause streaming tasks so that deinit() does not race with a concurrent
        // fb_get() — the VSYNC semaphore becomes corrupt if deinit() destroys it
        // while another task is blocked waiting on it.
        led_status_set(LED_RETRY);
        camera_pause();
        vTaskDelay(pdMS_TO_TICKS(200)); // wait for any in-flight fb_get to finish
        camera_reinit(g_state);
        vTaskDelay(pdMS_TO_TICKS(200)); // let DMA settle before tasks resume
        camera_resume();
        led_status_set(mjpeg_server_has_client() ? LED_STREAMING : LED_READY);
    } else {
        // Image-only change: apply without touching framesize/quality.
        // camera_apply_settings() always calls set_framesize() which restarts
        // the I2S DMA and can corrupt the VSYNC semaphore → fb_get hangs.
        camera_apply_image_settings(g_state);
    }

    CORS(req);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// GET/POST /api/wifi
static esp_err_t get_wifi_handler(httpd_req_t* req) {
    JsonDocument doc;
    doc["wifiSsid"] = g_state->wifiSsid;
    doc["wifiPass"]  = g_state->wifiPass;
    doc["wifiMode"]  = g_state->wifiMode;
    doc["ip"]        = g_state->espIP;
    char json[256];
    serializeJson(doc, json, sizeof(json));
    CORS(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t post_wifi_handler(httpd_req_t* req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, MIN((int)req->content_len, 511));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';
    JsonDocument doc;
    deserializeJson(doc, buf);
    snprintf(g_state->wifiSsid, sizeof(g_state->wifiSsid), "%s", (const char*)(doc["wifiSsid"] | ""));
    snprintf(g_state->wifiPass, sizeof(g_state->wifiPass), "%s", (const char*)(doc["wifiPass"]  | ""));
    snprintf(g_state->wifiMode, sizeof(g_state->wifiMode), "%s", (const char*)(doc["wifiMode"]  | "AP"));
    nvs_put_str("wifiSsid", g_state->wifiSsid);
    nvs_put_str("wifiPass",  g_state->wifiPass);
    nvs_put_str("wifiMode",  g_state->wifiMode);
    CORS(req);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// GET /api/stats
static esp_err_t get_stats_handler(httpd_req_t* req) {
    if (!g_state->statsEnabled) {
        CORS(req);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"enabled\":false}", 17);
        return ESP_OK;
    }
    // Prefer MJPEG stats (primary stream); fall back to WS streamer stats.
    float fps; uint32_t bps;
    mjpeg_server_get_stats(&fps, &bps);
    if (fps == 0.0f) streamer_get_stats(&fps, &bps);
    char json[64];
    snprintf(json, sizeof(json), "{\"enabled\":true,\"fps\":%.1f,\"bps\":%lu}", fps, (unsigned long)bps);
    CORS(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// GET /api/scan-brain — queries mDNS for brain, returns its WiFi config
static esp_err_t get_scan_brain_handler(httpd_req_t* req) {
    mdns_result_t* results = NULL;
    char brain_ip[16] = "";

    // Try to resolve brain hostname
    esp_ip4_addr_t addr;
    if (mdns_query_a("ecar", 3000, &addr) == ESP_OK) {
        esp_ip4addr_ntoa(&addr, brain_ip, sizeof(brain_ip));
    }

    if (brain_ip[0] == '\0') {
        const char* resp = "{\"found\":false}";
        CORS(req);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    // Fetch WiFi config from brain
    char url[64];
    snprintf(url, sizeof(url), "http://%s/wifi", brain_ip);

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char resp_buf[512] = "";
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int total = 0;
        while (total < (int)sizeof(resp_buf) - 1) {
            int n = esp_http_client_read(client, resp_buf + total, sizeof(resp_buf) - 1 - total);
            if (n <= 0) break;
            total += n;
        }
        resp_buf[total] = '\0';
    }
    esp_http_client_cleanup(client);

    JsonDocument brain_doc;
    JsonDocument out_doc;
    out_doc["found"] = true;
    out_doc["ip"] = brain_ip;

    if (deserializeJson(brain_doc, resp_buf) == DeserializationError::Ok) {
        out_doc["wifiSsid"] = brain_doc["wifiName"];
        out_doc["wifiPass"] = brain_doc["wifiPass"];
    }

    std::string out;
    serializeJson(out_doc, out);
    CORS(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out.c_str(), out.length());
    return ESP_OK;
}

// POST /manage
static esp_err_t post_manage_handler(httpd_req_t* req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, MIN((int)req->content_len, 127));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';
    JsonDocument doc;
    deserializeJson(doc, buf);
    if (doc["clearPreferences"]) nvs_clear_all();
    CORS(req);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    if (doc["restartESP"]) { vTaskDelay(pdMS_TO_TICKS(1000)); esp_restart(); }
    return ESP_OK;
}

// GET /api/ota/info
static esp_err_t get_ota_info_handler(httpd_req_t* req) {
    JsonDocument doc;
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next    = esp_ota_get_next_update_partition(NULL);
    if (running) { doc["running"]["label"] = running->label; }
    if (next)    { doc["next"]["label"]    = next->label; doc["next"]["size"] = next->size; }
    const esp_app_desc_t* ad = esp_app_get_description();
    if (ad) {
        doc["app"]["version"]      = ad->version;
        doc["app"]["project_name"] = ad->project_name;
        doc["app"]["idf_version"]  = ad->idf_ver;
    }
    std::string out;
    serializeJson(doc, out);
    CORS(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out.c_str(), out.length());
    return ESP_OK;
}

// POST /api/ota
#define OTA_BUF 4096
static esp_err_t post_ota_handler(httpd_req_t* req) {
    if (req->content_len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content"); return ESP_FAIL; }
    streamer_stop();
    camera_deinit();
    const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition"); return ESP_FAIL; }
    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed"); return ESP_FAIL;
    }
    char* buf = (char*)malloc(OTA_BUF);
    if (!buf) { esp_ota_abort(ota); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }
    int remaining = req->content_len;
    bool first = true;
    while (remaining > 0) {
        int to_read = remaining < OTA_BUF ? remaining : OTA_BUF;
        int n = httpd_req_recv(req, buf, to_read);
        if (n <= 0) { if (n == HTTPD_SOCK_ERR_TIMEOUT) continue; break; }
        if (first && (uint8_t)buf[0] != 0xE9) {
            esp_ota_abort(ota); free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a valid binary"); return ESP_FAIL;
        }
        first = false;
        if (esp_ota_write(ota, buf, n) != ESP_OK) break;
        remaining -= n;
    }
    free(buf);
    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA finalize failed"); return ESP_FAIL;
    }
    CORS(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", 15);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// WS /ws — WebSocket binary JPEG stream
static esp_err_t ws_handler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        streamer_add_client(fd);
        ESP_LOGI(TAG, "WS client connected fd=%d", fd);
        return ESP_OK;
    }
    // Handle close / error frames
    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        int fd = httpd_req_to_sockfd(req);
        streamer_remove_client(fd);
    }
    return ESP_OK;
}

// GET /mjpeg — redirect to the dedicated MJPEG server on port 81.
// Keeping MJPEG inside the httpd task would block the single httpd worker
// forever, making all other endpoints (stats, config, …) unresponsive.
// Port 81 runs a raw TCP server in its own FreeRTOS task.
static esp_err_t mjpeg_redirect_handler(httpd_req_t* req) {
    char location[48];
    snprintf(location, sizeof(location), "http://%s:81/mjpeg", g_state->espIP);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---- Register all URIs ----

#define REG(u, m, h, ws) do { \
    httpd_uri_t _u = {.uri = (u), .method = (m), .handler = (h), .is_websocket = (ws)}; \
    httpd_register_uri_handler(s_httpd, &_u); \
} while(0)

void webserver_start(CameraState* st) {
    g_state = st;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size          = 12288;
    cfg.max_uri_handlers    = 20;
    cfg.max_open_sockets    = 7;
    cfg.recv_wait_timeout   = 5;
    cfg.send_wait_timeout   = 10;
    cfg.lru_purge_enable    = true;
    cfg.uri_match_fn        = httpd_uri_match_wildcard;
    cfg.global_user_ctx     = g_state;

    // mDNS
    mdns_init();
    mdns_hostname_set(g_state->hostname);
    mdns_service_add("ESPRC-CAM", "_esprc-cam", "_tcp", 80, NULL, 0);
    mdns_service_add("ESPRC-CAM-HTTP", "_http", "_tcp", 80, NULL, 0);

    if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
        REG("/",              HTTP_GET,     get_index_handler,   false);
        REG("*",              HTTP_OPTIONS, cors_handler,        false);
        REG("/api/config",    HTTP_GET,     get_config_handler,  false);
        REG("/api/config",    HTTP_POST,    post_config_handler, false);
        REG("/cam-config",    HTTP_GET,     get_config_handler,  false);  // brain compat
        REG("/cam-config",    HTTP_POST,    post_config_handler, false);  // brain compat
        REG("/api/wifi",      HTTP_GET,     get_wifi_handler,    false);
        REG("/api/wifi",      HTTP_POST,    post_wifi_handler,   false);
        REG("/api/stats",     HTTP_GET,     get_stats_handler,   false);
        REG("/api/scan-brain",HTTP_GET,     get_scan_brain_handler, false);
        REG("/manage",        HTTP_POST,    post_manage_handler, false);
        REG("/api/ota/info",  HTTP_GET,     get_ota_info_handler,false);
        REG("/api/ota",       HTTP_POST,    post_ota_handler,    false);
        REG("/mjpeg",         HTTP_GET,     mjpeg_redirect_handler, false);
        REG("/ws",            HTTP_GET,     ws_handler,             true);

        streamer_start(s_httpd, g_state);
        mjpeg_server_start(g_state);
        ESP_LOGI(TAG, "HTTP server started. mDNS: %s.local", g_state->hostname);
    }
}

void webserver_stop(void) {
    mjpeg_server_stop();
    streamer_stop();
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
}
