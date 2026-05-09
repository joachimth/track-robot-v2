/**
 * @file controller_http.c
 * @brief HTTP (WiFi) controller — AP+STA fallback, web UI, REST API
 *
 * REST API:
 * - GET  /          Web UI (tab layout: Control / WiFi / Config / Status)
 * - POST /control   {"throttle": 0.5, "steering": -0.2, "slow_mode": false}
 * - POST /estop     Trigger emergency stop
 * - POST /arm       Arm the system
 * - GET  /status    JSON system status
 * - POST /wifi      {"ssid":"...", "password":"..."}  Save STA credentials to NVS
 * - GET  /config    Return current robot config (NVS overrides or Kconfig defaults)
 * - POST /config    {"deadzone":5,"expo":30,"max_speed":100,"slow_factor":50}
 *                   Save robot config to NVS — takes effect after reboot
 */

#include "controller_http.h"
#include "control_manager.h"
#include "control_frame.h"
#include "safety_failsafe.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ctrl_http";

#define WIFI_NVS_NAMESPACE     "wifi_cfg"
#define WIFI_NVS_KEY_SSID      "ssid"
#define WIFI_NVS_KEY_PASSWORD  "password"
#define ROBOT_CFG_NVS_NS       "robot_cfg"
#define WIFI_AP_SSID           "TrackRobot-Setup"
#define WIFI_AP_PASSWORD       "trackrobot"
#define WIFI_AP_CHANNEL        CONFIG_ROBOT_WIFI_CHANNEL
#define WIFI_AP_MAX_CONN       CONFIG_ROBOT_WIFI_MAX_CONN
#define WIFI_STA_TIMEOUT_MS    15000
#define WIFI_RECONNECT_MS      5000

static httpd_handle_t server = NULL;
static bool sta_connected = false;
static bool sta_connecting = false;
static bool ap_started = false;
static char active_sta_ssid[33] = {0};
static esp_timer_handle_t fallback_timer = NULL;
static esp_timer_handle_t reconnect_timer = NULL;

static esp_err_t start_fallback_ap(void);
static esp_err_t connect_sta_from_saved_config(void);

static void safe_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    size_t n = strnlen(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool nvs_read_string(const char *key, char *out, size_t out_len) {
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) return false;

    size_t len = out_len;
    ret = nvs_get_str(nvs, key, out, &len);
    nvs_close(nvs);

    return ret == ESP_OK && out[0] != '\0';
}

static esp_err_t nvs_write_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(nvs, WIFI_NVS_KEY_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, WIFI_NVS_KEY_PASSWORD, password ? password : "");
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return ret;
}

static void fallback_timer_cb(void *arg) {
    if (!sta_connected) {
        ESP_LOGW(TAG, "STA connection timeout — keeping/starting fallback AP");
        start_fallback_ap();
    }
}

static void reconnect_timer_cb(void *arg) {
    if (!sta_connected && active_sta_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Retrying STA connection to %s", active_sta_ssid);
        esp_wifi_connect();
    }
}

static void schedule_reconnect(void) {
    if (!reconnect_timer) return;
    esp_timer_stop(reconnect_timer);
    esp_timer_start_once(reconnect_timer, WIFI_RECONNECT_MS * 1000ULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_START) {
            ap_started = true;
            ESP_LOGI(TAG, "Fallback AP started");
            ESP_LOGI(TAG, "  SSID: %s", WIFI_AP_SSID);
            ESP_LOGI(TAG, "  Password: %s", WIFI_AP_PASSWORD);
            ESP_LOGI(TAG, "  IP: 192.168.4.1");
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station "MACSTR" joined setup AP", MAC2STR(event->mac));
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Station "MACSTR" left setup AP", MAC2STR(event->mac));
        } else if (event_id == WIFI_EVENT_STA_START) {
            if (active_sta_ssid[0] != '\0') {
                ESP_LOGI(TAG, "STA started — connecting to %s", active_sta_ssid);
                sta_connecting = true;
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            sta_connected = false;
            sta_connecting = false;
            ESP_LOGW(TAG, "STA disconnected");
            start_fallback_ap();
            schedule_reconnect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sta_connected = true;
        sta_connecting = false;
        ESP_LOGI(TAG, "STA connected, got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t start_fallback_ap(void) {
    if (ap_started) return ESP_OK;  // already running, avoid double DHCP restart

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    if (strlen(WIFI_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_LOGI(TAG, "Starting fallback setup AP...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    return ESP_OK;
}

static esp_err_t connect_sta_from_saved_config(void) {
    char ssid[33] = {0};
    char password[65] = {0};

    bool has_saved_ssid = nvs_read_string(WIFI_NVS_KEY_SSID, ssid, sizeof(ssid));
    if (!has_saved_ssid) {
#ifdef CONFIG_ROBOT_WIFI_MODE_STA
        safe_copy(ssid, sizeof(ssid), CONFIG_ROBOT_WIFI_SSID);
        safe_copy(password, sizeof(password), CONFIG_ROBOT_WIFI_PASSWORD);
        has_saved_ssid = ssid[0] != '\0';
#endif
    } else {
        nvs_read_string(WIFI_NVS_KEY_PASSWORD, password, sizeof(password));
    }

    if (!has_saved_ssid) {
        ESP_LOGW(TAG, "No saved STA WiFi credentials — using setup AP only");
        return start_fallback_ap();
    }

    safe_copy(active_sta_ssid, sizeof(active_sta_ssid), ssid);

    wifi_config_t sta_config = {0};
    safe_copy((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), ssid);
    safe_copy((char *)sta_config.sta.password, sizeof(sta_config.sta.password), password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_LOGI(TAG, "Configuring STA WiFi: %s", ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    sta_connecting = true;
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Initial STA connect failed: %s", esp_err_to_name(ret));
        sta_connecting = false;
    }

    if (fallback_timer) {
        esp_timer_stop(fallback_timer);
        esp_timer_start_once(fallback_timer, WIFI_STA_TIMEOUT_MS * 1000ULL);
    }

    return ret == ESP_OK ? ESP_OK : start_fallback_ap();
}

static esp_err_t init_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    esp_timer_create_args_t fallback_args = {
        .callback = &fallback_timer_cb,
        .name = "wifi_fallback"
    };
    ESP_ERROR_CHECK(esp_timer_create(&fallback_args, &fallback_timer));

    esp_timer_create_args_t reconnect_args = {
        .callback = &reconnect_timer_cb,
        .name = "wifi_reconnect"
    };
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_args, &reconnect_timer));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(start_fallback_ap());
    ESP_ERROR_CHECK(esp_wifi_start());
    connect_sta_from_saved_config();

    return ESP_OK;
}

static esp_err_t control_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    control_frame_t frame = {0};
    frame.timestamp = xTaskGetTickCount();

    cJSON *throttle = cJSON_GetObjectItem(root, "throttle");
    if (throttle) frame.throttle = control_clamp((float)throttle->valuedouble);

    cJSON *steering = cJSON_GetObjectItem(root, "steering");
    if (steering) frame.steering = control_clamp((float)steering->valuedouble);

    cJSON *slow = cJSON_GetObjectItem(root, "slow_mode");
    if (slow) frame.slow_mode = cJSON_IsTrue(slow);

    cJSON_Delete(root);
    control_manager_submit(CONTROL_SOURCE_HTTP, &frame);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t wifi_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    const char *pass = cJSON_IsString(password) ? password->valuestring : "";
    esp_err_t save_ret = nvs_write_wifi_credentials(ssid->valuestring, pass);
    if (save_ret != ESP_OK) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save WiFi credentials");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved new WiFi credentials for SSID: %s", ssid->valuestring);
    cJSON_Delete(root);

    connect_sta_from_saved_config();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"saved\",\"message\":\"WiFi saved, connecting now\"}");
    return ESP_OK;
}

static esp_err_t estop_post_handler(httpd_req_t *req) {
    control_frame_t frame = {0};
    frame.estop = true;
    frame.timestamp = xTaskGetTickCount();
    control_manager_submit(CONTROL_SOURCE_HTTP, &frame);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"estop\"}");
    return ESP_OK;
}

static esp_err_t arm_post_handler(httpd_req_t *req) {
    control_frame_t frame = {0};
    frame.arm = true;
    frame.timestamp = xTaskGetTickCount();
    control_manager_submit(CONTROL_SOURCE_HTTP, &frame);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"armed\"}");
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    char json[384];
    snprintf(json, sizeof(json),
             "{\"armed\":%s,\"source\":%d,\"wifi\":{\"ap\":%s,\"sta_connected\":%s,\"sta_connecting\":%s,\"sta_ssid\":\"%s\",\"setup_ip\":\"192.168.4.1\"}}",
             safety_is_armed() ? "true" : "false",
             control_manager_get_active_source(),
             ap_started ? "true" : "false",
             sta_connected ? "true" : "false",
             sta_connecting ? "true" : "false",
             active_sta_ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
//  Helpers: NVS robot config read/write
// ---------------------------------------------------------------------------

static int robot_cfg_read_int(const char *key, int def) {
    nvs_handle_t h;
    if (nvs_open(ROBOT_CFG_NVS_NS, NVS_READONLY, &h) != ESP_OK) return def;
    int32_t v;
    esp_err_t ret = nvs_get_i32(h, key, &v);
    nvs_close(h);
    return (ret == ESP_OK) ? (int)v : def;
}

static esp_err_t robot_cfg_write_int(const char *key, int val) {
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ROBOT_CFG_NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_i32(h, key, (int32_t)val);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

// ---------------------------------------------------------------------------
//  GET /config
// ---------------------------------------------------------------------------

static esp_err_t config_get_handler(httpd_req_t *req) {
    int dz  = robot_cfg_read_int("deadzone",   CONFIG_ROBOT_DRIVE_DEADZONE);
    int ex  = robot_cfg_read_int("expo",        CONFIG_ROBOT_DRIVE_EXPO);
    int ms  = robot_cfg_read_int("max_speed",  CONFIG_ROBOT_DRIVE_MAX_SPEED);
    int sf  = robot_cfg_read_int("slow_factor", CONFIG_ROBOT_DRIVE_SLOW_MODE_FACTOR);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"deadzone\":%d,\"expo\":%d,\"max_speed\":%d,\"slow_factor\":%d,"
        "\"note\":\"POST /config with same fields to update. Reboot to apply.\"}",
        dz, ex, ms, sf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
//  POST /config  {"deadzone":5,"expo":30,"max_speed":100,"slow_factor":50}
// ---------------------------------------------------------------------------

static esp_err_t config_post_handler(httpd_req_t *req) {
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "deadzone")) && cJSON_IsNumber(item)) {
        int v = item->valueint;
        if (v >= 0 && v <= 20) robot_cfg_write_int("deadzone", v);
    }
    if ((item = cJSON_GetObjectItem(root, "expo")) && cJSON_IsNumber(item)) {
        int v = item->valueint;
        if (v >= 0 && v <= 100) robot_cfg_write_int("expo", v);
    }
    if ((item = cJSON_GetObjectItem(root, "max_speed")) && cJSON_IsNumber(item)) {
        int v = item->valueint;
        if (v >= 10 && v <= 100) robot_cfg_write_int("max_speed", v);
    }
    if ((item = cJSON_GetObjectItem(root, "slow_factor")) && cJSON_IsNumber(item)) {
        int v = item->valueint;
        if (v >= 10 && v <= 100) robot_cfg_write_int("slow_factor", v);
    }

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"status\":\"saved\",\"message\":\"Config saved to NVS. Reboot to apply.\"}");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
//  GET /  — tab-based web UI
// ---------------------------------------------------------------------------

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");

    // Part 1 — head + CSS
    httpd_resp_send_chunk(req,
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Tracked Robot</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:system-ui,Arial,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh}"
        "header{background:#1e293b;padding:16px 20px;border-bottom:1px solid #334155}"
        "header h1{font-size:1.3em;color:#f1f5f9}"
        "header small{color:#94a3b8;font-size:0.85em}"
        ".tabs{display:flex;background:#1e293b;border-bottom:2px solid #334155;overflow-x:auto}"
        ".tab-btn{padding:12px 20px;border:none;background:none;color:#94a3b8;cursor:pointer;"
        "font-size:0.95em;white-space:nowrap;border-bottom:2px solid transparent;margin-bottom:-2px}"
        ".tab-btn.active{color:#38bdf8;border-bottom-color:#38bdf8}"
        ".tab-pane{display:none;padding:20px;max-width:700px;margin:0 auto}"
        ".tab-pane.active{display:block}"
        ".card{background:#1e293b;border-radius:10px;padding:16px;margin-bottom:16px;border:1px solid #334155}"
        ".card h2{font-size:1em;color:#94a3b8;margin-bottom:14px;text-transform:uppercase;letter-spacing:.05em}"
        "label{display:block;font-size:0.9em;color:#94a3b8;margin-bottom:4px;margin-top:10px}"
        "input[type=text],input[type=password],input[type=number]"
        "{width:100%;padding:10px;background:#0f172a;border:1px solid #475569;"
        "border-radius:6px;color:#e2e8f0;font-size:0.95em}"
        "input[type=range]{width:100%;accent-color:#38bdf8}"
        ".val{font-size:0.85em;color:#38bdf8;margin-left:8px}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}"
        "button{padding:10px 18px;border:none;border-radius:6px;cursor:pointer;"
        "font-size:0.9em;font-weight:600;transition:opacity .15s}"
        "button:hover{opacity:.85}"
        ".btn-arm{background:#16a34a;color:#fff}"
        ".btn-stop{background:#dc2626;color:#fff}"
        ".btn-primary{background:#0284c7;color:#fff}"
        ".btn-neutral{background:#334155;color:#e2e8f0}"
        ".msg{margin-top:10px;padding:8px 12px;border-radius:6px;font-size:0.9em}"
        ".msg-ok{background:#14532d;color:#86efac}"
        ".msg-err{background:#7f1d1d;color:#fca5a5}"
        ".msg-info{background:#0c4a6e;color:#7dd3fc}"
        "pre{background:#0f172a;border:1px solid #334155;border-radius:8px;"
        "padding:14px;font-size:0.82em;overflow-x:auto;white-space:pre-wrap}"
        ".wifi-status{display:inline-block;padding:3px 10px;border-radius:99px;font-size:0.8em;font-weight:600}"
        ".ws-ap{background:#1e3a5f;color:#7dd3fc}"
        ".ws-sta{background:#14532d;color:#86efac}"
        ".ws-off{background:#2d2d2d;color:#9ca3af}"
        "hr{border:none;border-top:1px solid #334155;margin:14px 0}"
        "</style></head><body>", -1);

    // Part 2 — header + tabs
    httpd_resp_send_chunk(req,
        "<header>"
        "<h1>Tracked Robot</h1>"
        "<small>Connect PS4 controller &rarr; press Options to arm &rarr; drive with left stick</small>"
        "</header>"
        "<div class='tabs'>"
        "<button class='tab-btn active' onclick=\"showTab('control')\">Control</button>"
        "<button class='tab-btn' onclick=\"showTab('wifi')\">WiFi</button>"
        "<button class='tab-btn' onclick=\"showTab('config')\">Config</button>"
        "<button class='tab-btn' onclick=\"showTab('status')\">Status</button>"
        "</div>", -1);

    // Part 3 — Control tab
    httpd_resp_send_chunk(req,
        "<div class='tab-pane active' id='pane-control'>"
        "<div class='card'>"
        "<h2>Safety</h2>"
        "<div class='row'>"
        "<button class='btn-arm' onclick='armRobot()'>ARM</button>"
        "<button class='btn-stop' onclick='estopRobot()'>E-STOP</button>"
        "</div>"
        "<div id='ctrl-msg'></div>"
        "</div>"
        "<div class='card'>"
        "<h2>Manual drive (HTTP)</h2>"
        "<label>Throttle <span class='val' id='tval'>0%</span></label>"
        "<input type='range' id='thr' min='-100' max='100' value='0' oninput=\"document.getElementById('tval').textContent=this.value+'%'\">"
        "<label>Steering <span class='val' id='sval'>0%</span></label>"
        "<input type='range' id='str' min='-100' max='100' value='0' oninput=\"document.getElementById('sval').textContent=this.value+'%'\">"
        "<div class='row' style='margin-top:12px'>"
        "<button class='btn-primary' onclick='sendDrive()'>Send</button>"
        "<button class='btn-neutral' onclick='zeroDrive()'>Zero</button>"
        "</div>"
        "</div>"
        "</div>", -1);

    // Part 4 — WiFi tab
    httpd_resp_send_chunk(req,
        "<div class='tab-pane' id='pane-wifi'>"
        "<div class='card'>"
        "<h2>Setup AP (always active)</h2>"
        "<p style='font-size:.9em;color:#94a3b8'>"
        "A fallback access point is always running so you can reach this UI "
        "even if home WiFi is unavailable.</p>"
        "<p style='margin-top:10px'>"
        "<span class='wifi-status ws-ap'>AP</span>&nbsp;"
        "<strong>TrackRobot-Setup</strong> &nbsp;&bull;&nbsp; password: <strong>trackrobot</strong>"
        "</p>"
        "<p style='margin-top:6px;color:#94a3b8;font-size:.85em'>IP: 192.168.4.1 &nbsp;&bull;&nbsp;"
        "http://192.168.4.1/</p>"
        "</div>"
        "<div class='card'>"
        "<h2>Home WiFi (optional)</h2>"
        "<p style='font-size:.9em;color:#94a3b8'>Connect to your home network so the robot is "
        "reachable via your router. Leave blank to stay in AP-only mode.</p>"
        "<label>SSID</label>"
        "<input type='text' id='w-ssid' placeholder='Your home WiFi name'>"
        "<label>Password</label>"
        "<input type='password' id='w-pass' placeholder='WiFi password'>"
        "<div class='row' style='margin-top:12px'>"
        "<button class='btn-primary' onclick='saveWifi()'>Save &amp; Connect</button>"
        "<button class='btn-neutral' onclick='clearWifi()'>Clear (AP only)</button>"
        "</div>"
        "<div id='wifi-msg'></div>"
        "</div>"
        "<div class='card'><h2>Current status</h2><pre id='wifi-status-pre'>Loading...</pre></div>"
        "</div>", -1);

    // Part 5 — Config tab
    httpd_resp_send_chunk(req,
        "<div class='tab-pane' id='pane-config'>"
        "<div class='card'>"
        "<h2>Drive parameters</h2>"
        "<p style='font-size:.9em;color:#94a3b8'>Saved to NVS. "
        "Reboot the ESP32 to apply changes.</p>"
        "<label>Deadzone (0&ndash;20 %)"
        "<span class='val' id='dz-val'></span></label>"
        "<input type='number' id='cfg-deadzone' min='0' max='20' value='5'>"
        "<label>Expo curve (0&ndash;100 %)"
        "<span class='val' id='ex-val'></span></label>"
        "<input type='number' id='cfg-expo' min='0' max='100' value='30'>"
        "<label>Max speed (10&ndash;100 %)"
        "<span class='val' id='ms-val'></span></label>"
        "<input type='number' id='cfg-maxspeed' min='10' max='100' value='100'>"
        "<label>Slow-mode factor (10&ndash;100 %)"
        "<span class='val' id='sf-val'></span></label>"
        "<input type='number' id='cfg-slowfactor' min='10' max='100' value='50'>"
        "<div class='row' style='margin-top:14px'>"
        "<button class='btn-primary' onclick='saveConfig()'>Save config</button>"
        "<button class='btn-neutral' onclick='loadConfig()'>Reload</button>"
        "</div>"
        "<div id='cfg-msg'></div>"
        "</div>"
        "<div class='card'>"
        "<h2>Reboot</h2>"
        "<p style='font-size:.9em;color:#94a3b8'>Apply saved config changes.</p>"
        "<button class='btn-neutral' style='margin-top:8px' onclick='rebootRobot()'>Reboot ESP32</button>"
        "<div id='reboot-msg'></div>"
        "</div>"
        "</div>", -1);

    // Part 6 — Status tab
    httpd_resp_send_chunk(req,
        "<div class='tab-pane' id='pane-status'>"
        "<div class='card'>"
        "<h2>Live status <small style='font-weight:normal;color:#475569'>(auto-refresh 3 s)</small></h2>"
        "<pre id='status-pre'>Loading...</pre>"
        "<button class='btn-neutral' style='margin-top:8px' onclick='loadStatus()'>Refresh now</button>"
        "</div>"
        "</div>", -1);

    // Part 7 — JavaScript
    httpd_resp_send_chunk(req,
        "<script>"
        // Tab switching
        "function showTab(t){"
        "document.querySelectorAll('.tab-pane').forEach(p=>p.classList.remove('active'));"
        "document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));"
        "document.getElementById('pane-'+t).classList.add('active');"
        "event.target.classList.add('active');"
        "if(t==='status')loadStatus();"
        "if(t==='wifi')loadWifiStatus();"
        "if(t==='config')loadConfig();"
        "}"

        // Control helpers
        "function msg(id,text,cls){"
        "var el=document.getElementById(id);"
        "el.innerHTML=\"<div class='msg \"+cls+\"'>\"+text+\"</div>\";}"

        "async function armRobot(){"
        "await fetch('/arm',{method:'POST'});"
        "msg('ctrl-msg','System armed','msg-ok');}"

        "async function estopRobot(){"
        "await fetch('/estop',{method:'POST'});"
        "msg('ctrl-msg','E-STOP triggered','msg-err');}"

        "async function sendDrive(){"
        "var t=parseInt(document.getElementById('thr').value)/100;"
        "var s=parseInt(document.getElementById('str').value)/100;"
        "await fetch('/control',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({throttle:t,steering:s})});}"

        "function zeroDrive(){"
        "document.getElementById('thr').value=0;"
        "document.getElementById('str').value=0;"
        "document.getElementById('tval').textContent='0%';"
        "document.getElementById('sval').textContent='0%';"
        "sendDrive();}"

        // WiFi helpers
        "async function loadWifiStatus(){"
        "try{var r=await fetch('/status');var d=await r.json();"
        "document.getElementById('wifi-status-pre').textContent=JSON.stringify(d.wifi,null,2);"
        "}catch(e){document.getElementById('wifi-status-pre').textContent='Error loading status';}}"

        "async function saveWifi(){"
        "var ssid=document.getElementById('w-ssid').value.trim();"
        "var pass=document.getElementById('w-pass').value;"
        "if(!ssid){msg('wifi-msg','SSID is required','msg-err');return;}"
        "try{"
        "var r=await fetch('/wifi',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({ssid:ssid,password:pass})});"
        "var d=await r.json();"
        "msg('wifi-msg',d.message||'Saved','msg-ok');"
        "setTimeout(loadWifiStatus,2000);"
        "}catch(e){msg('wifi-msg','Error saving WiFi','msg-err');}}"

        "async function clearWifi(){"
        "try{"
        "await fetch('/wifi',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({ssid:'',password:''})});"
        "msg('wifi-msg','WiFi cleared — AP-only mode on next boot','msg-info');"
        "}catch(e){msg('wifi-msg','Error','msg-err');}}"

        // Config helpers
        "async function loadConfig(){"
        "try{"
        "var r=await fetch('/config');var d=await r.json();"
        "document.getElementById('cfg-deadzone').value=d.deadzone;"
        "document.getElementById('cfg-expo').value=d.expo;"
        "document.getElementById('cfg-maxspeed').value=d.max_speed;"
        "document.getElementById('cfg-slowfactor').value=d.slow_factor;"
        "}catch(e){msg('cfg-msg','Error loading config','msg-err');}}"

        "async function saveConfig(){"
        "var body={"
        "deadzone:parseInt(document.getElementById('cfg-deadzone').value),"
        "expo:parseInt(document.getElementById('cfg-expo').value),"
        "max_speed:parseInt(document.getElementById('cfg-maxspeed').value),"
        "slow_factor:parseInt(document.getElementById('cfg-slowfactor').value)"
        "};"
        "try{"
        "var r=await fetch('/config',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(body)});"
        "var d=await r.json();"
        "msg('cfg-msg',d.message,'msg-ok');"
        "}catch(e){msg('cfg-msg','Error saving config','msg-err');}}"

        // Reboot (requires POST /reboot endpoint — sends best-effort)
        "async function rebootRobot(){"
        "msg('reboot-msg','Rebooting...','msg-info');"
        "fetch('/reboot',{method:'POST'}).catch(()=>{});"
        "setTimeout(()=>msg('reboot-msg','ESP32 rebooting — reconnect in ~5 s','msg-info'),500);}"

        // Status helpers
        "async function loadStatus(){"
        "try{var r=await fetch('/status');var d=await r.json();"
        "document.getElementById('status-pre').textContent=JSON.stringify(d,null,2);"
        "}catch(e){document.getElementById('status-pre').textContent='Error loading status';}}"

        "loadStatus();"
        "setInterval(()=>{"
        "var active=document.querySelector('.tab-pane.active');"
        "if(active&&active.id==='pane-status')loadStatus();"
        "},3000);"
        "</script></body></html>", -1);

    httpd_resp_send_chunk(req, NULL, 0);  // end chunked response
    return ESP_OK;
}

static esp_err_t reboot_post_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;

    if (server) return ESP_OK;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        {.uri = "/control", .method = HTTP_POST, .handler = control_post_handler},
        {.uri = "/wifi",    .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/estop",   .method = HTTP_POST, .handler = estop_post_handler},
        {.uri = "/arm",     .method = HTTP_POST, .handler = arm_post_handler},
        {.uri = "/status",  .method = HTTP_GET,  .handler = status_get_handler},
        {.uri = "/config",  .method = HTTP_GET,  .handler = config_get_handler},
        {.uri = "/config",  .method = HTTP_POST, .handler = config_post_handler},
        {.uri = "/reboot",  .method = HTTP_POST, .handler = reboot_post_handler},
        {.uri = "/",        .method = HTTP_GET,  .handler = index_get_handler},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "HTTP server started — 9 endpoints registered");
    ESP_LOGI(TAG, "Control UI: http://192.168.4.1/ (connect to TrackRobot-Setup AP first)");
    return ESP_OK;
}

esp_err_t controller_http_init(void) {
    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(start_webserver());
    return ESP_OK;
}
