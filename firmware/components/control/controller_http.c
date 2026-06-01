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
#include "motor_bts7960.h"
#include "motor_monitor.h"
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
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

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

// Captive portal: all DNS queries resolve to the AP gateway (192.168.4.1) and
// every unknown HTTP path 302-redirects there, so connecting devices pop the
// robot's web UI automatically. The IP matches the esp_netif AP default.
#define CAPTIVE_REDIRECT_URL   "http://192.168.4.1/"
#define DNS_PORT               53
#define DNS_BUF_LEN            256
static const uint8_t CAPTIVE_IP[4] = {192, 168, 4, 1};

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

static esp_err_t estop_reset_post_handler(httpd_req_t *req) {
    esp_err_t ret = safety_estop_reset();
    httpd_resp_set_type(req, "application/json");
    if (ret == ESP_OK) {
        httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"E-STOP cleared — re-arm to continue\"}");
    } else {
        httpd_resp_sendstr(req, "{\"status\":\"err\",\"message\":\"Not in E-STOP state\"}");
    }
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

static const char *state_name(safety_state_t s) {
    switch (s) {
        case SAFETY_STATE_ARMED:    return "ARMED";
        case SAFETY_STATE_ESTOP:    return "ESTOP";
        default:                    return "DISARMED";
    }
}

static const char *source_name(control_source_t s) {
    switch (s) {
        case CONTROL_SOURCE_PS4:    return "PS4";
        case CONTROL_SOURCE_SERIAL: return "SERIAL";
        case CONTROL_SOURCE_HTTP:   return "HTTP";
        default:                    return "NONE";
    }
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    control_status_t cs;
    control_manager_get_status(&cs);

    float lt = 0, rt = 0, la = 0, ra = 0;
    motor_get_speeds(&lt, &rt, &la, &ra);

    safety_state_t st = safety_get_state();

    monitor_status_t mon;
    motor_monitor_get_status(&mon);

    char json[1024];
    snprintf(json, sizeof(json),
        "{"
        "\"state\":\"%s\","
        "\"armed\":%s,"
        "\"source\":\"%s\","
        "\"input\":{"
          "\"throttle\":%.3f,"
          "\"steering\":%.3f,"
          "\"slow_mode\":%s,"
          "\"estop\":%s,"
          "\"arm\":%s"
        "},"
        "\"output\":{"
          "\"left_target\":%.3f,"
          "\"right_target\":%.3f,"
          "\"left_actual\":%.3f,"
          "\"right_actual\":%.3f"
        "},"
        "\"monitor\":{"
          "\"left_ma\":%lu,"
          "\"right_ma\":%lu,"
          "\"overcurrent\":%s,"
          "\"battery_enabled\":%s,"
          "\"battery_mv\":%lu,"
          "\"battery_low\":%s"
        "},"
        "\"wifi\":{"
          "\"ap\":%s,"
          "\"sta_connected\":%s,"
          "\"sta_connecting\":%s,"
          "\"sta_ssid\":\"%s\","
          "\"setup_ip\":\"192.168.4.1\""
        "}"
        "}",
        state_name(st),
        (st == SAFETY_STATE_ARMED) ? "true" : "false",
        source_name(cs.source),
        cs.frame.throttle,
        cs.frame.steering,
        cs.frame.slow_mode ? "true" : "false",
        cs.frame.estop     ? "true" : "false",
        cs.frame.arm       ? "true" : "false",
        lt, rt, la, ra,
        (unsigned long)mon.left_ma,
        (unsigned long)mon.right_ma,
        mon.overcurrent     ? "true" : "false",
        mon.battery_enabled ? "true" : "false",
        (unsigned long)mon.battery_mv,
        mon.battery_low     ? "true" : "false",
        ap_started     ? "true" : "false",
        sta_connected  ? "true" : "false",
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

    // Part 1 — head + favicon + CSS (base)
    httpd_resp_send_chunk(req,
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
        "<title>TrackRobot</title>"
        "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' "
        "viewBox='0 0 100 100'><text y='.9em' font-size='90'>\xF0\x9F\xA4\x96</text></svg>\">"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:system-ui,Arial,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh}"
        "body.estop{animation:eg 1s ease-in-out infinite}"
        "@keyframes eg{0%,100%{box-shadow:inset 0 0 0 2px #dc2626}"
        "50%{box-shadow:inset 0 0 44px 8px rgba(220,38,38,.7)}}"
        "header{background:#1e293b;padding:14px 18px;border-bottom:1px solid #334155;"
        "display:flex;justify-content:space-between;align-items:center;gap:10px}"
        "header h1{font-size:1.25em;color:#f1f5f9}"
        "header small{color:#94a3b8;font-size:0.8em}"
        ".conn{font-size:.85em;font-weight:600;white-space:nowrap;color:#94a3b8}"
        ".tabs{display:flex;background:#1e293b;border-bottom:2px solid #334155;overflow-x:auto}"
        ".tab-btn{padding:12px 20px;border:none;background:none;color:#94a3b8;cursor:pointer;"
        "font-size:0.95em;white-space:nowrap;border-bottom:2px solid transparent;margin-bottom:-2px}"
        ".tab-btn.active{color:#38bdf8;border-bottom-color:#38bdf8}"
        ".tab-pane{display:none;padding:18px;max-width:760px;margin:0 auto}"
        ".tab-pane.active{display:block}"
        ".card{background:#1e293b;border-radius:10px;padding:16px;margin-bottom:16px;border:1px solid #334155}"
        ".card h2{font-size:1em;color:#94a3b8;margin-bottom:14px;text-transform:uppercase;letter-spacing:.05em}"
        "label{display:block;font-size:0.9em;color:#94a3b8;margin-bottom:4px;margin-top:10px}"
        "input[type=text],input[type=password]"
        "{width:100%;padding:10px;background:#0f172a;border:1px solid #475569;"
        "border-radius:6px;color:#e2e8f0;font-size:0.95em}"
        "input[type=range]{width:100%;accent-color:#38bdf8;height:28px}"
        ".val{font-size:0.85em;color:#38bdf8;margin-left:8px;font-weight:700}"
        ".row{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}"
        "button{padding:10px 18px;border:none;border-radius:6px;cursor:pointer;"
        "font-size:0.9em;font-weight:600;transition:opacity .15s}"
        "button:hover{opacity:.88}"
        ".btn-arm{background:#16a34a;color:#fff}"
        ".btn-stop{background:#dc2626;color:#fff}"
        ".btn-primary{background:#0284c7;color:#fff}"
        ".btn-neutral{background:#334155;color:#e2e8f0}", -1);

    // Part 2 — CSS (control widgets + responsive)
    httpd_resp_send_chunk(req,
        ".msg{margin-top:10px;padding:8px 12px;border-radius:6px;font-size:0.9em}"
        ".msg-ok{background:#14532d;color:#86efac}"
        ".msg-err{background:#7f1d1d;color:#fca5a5}"
        ".msg-info{background:#0c4a6e;color:#7dd3fc}"
        "pre{background:#0f172a;border:1px solid #334155;border-radius:8px;"
        "padding:14px;font-size:0.82em;overflow-x:auto;white-space:pre-wrap}"
        "details{margin-top:10px}summary{cursor:pointer;color:#94a3b8;font-size:.85em}"
        ".wifi-status{display:inline-block;padding:3px 10px;border-radius:99px;font-size:0.8em;font-weight:600}"
        ".ws-ap{background:#1e3a5f;color:#7dd3fc}"
        ".ws-sta{background:#14532d;color:#86efac}"
        ".ws-off{background:#2d2d2d;color:#9ca3af}"
        ".badge{display:inline-block;padding:5px 16px;border-radius:99px;font-size:1.05em;"
        "font-weight:800;letter-spacing:.06em}"
        ".ws-armed{background:#14532d;color:#86efac}"
        ".ws-disarmed{background:#334155;color:#cbd5e1}"
        ".ws-estop{background:#7f1d1d;color:#fca5a5}"
        ".bar-wrap{position:relative;height:22px;background:#0f172a;border:1px solid #334155;"
        "border-radius:4px;overflow:hidden;margin-bottom:4px}"
        ".bar-fill{position:absolute;top:0;height:100%;width:0;transition:width .08s ease,left .08s ease;"
        "border-radius:3px}"
        ".bar-lbl{position:absolute;right:6px;top:2px;font-size:.8em;color:#e2e8f0;pointer-events:none}"
        ".ind{display:inline-block;padding:3px 8px;border-radius:4px;font-size:.75em;font-weight:700;"
        "background:#1e293b;color:#475569;border:1px solid #334155}"
        ".ind.active{background:#7f1d1d;color:#fca5a5;border-color:#dc2626}"
        ".ind.active-ok{background:#14532d;color:#86efac;border-color:#16a34a}"
        "hr{border:none;border-top:1px solid #334155;margin:14px 0}"
        ".hint{background:#0c4a6e;color:#7dd3fc;padding:12px 14px;border-radius:8px;"
        "margin-bottom:14px;font-size:.9em}"
        ".ctrl-wrap{display:flex;gap:16px;flex-wrap:wrap}"
        ".ctrl-main{flex:1 1 300px;min-width:280px}"
        ".ctrl-side{flex:1 1 260px;min-width:240px}"
        "#joy{touch-action:none;display:block;margin:8px auto;width:100%;max-width:300px;"
        "height:auto;cursor:grab}"
        ".joy-readout{text-align:center;font-size:1.15em;color:#38bdf8;margin:6px 0;"
        "font-variant-numeric:tabular-nums}"
        ".hintsmall{text-align:center;font-size:.82em;color:#94a3b8;margin-bottom:10px}"
        ".btn-estop-big{background:#dc2626;color:#fff;font-size:1.2em;padding:16px;flex:2;"
        "border-radius:10px;font-weight:800;letter-spacing:.08em}"
        ".arm-row button{flex:1;padding:14px}"
        "#toast{position:fixed;left:50%;bottom:86px;transform:translateX(-50%);background:#0284c7;"
        "color:#fff;padding:10px 18px;border-radius:8px;opacity:0;transition:opacity .3s;"
        "pointer-events:none;z-index:60;font-weight:700;box-shadow:0 4px 12px rgba(0,0,0,.4)}"
        ".fab-estop{display:none}"
        "@media(max-width:640px){"
        ".tab-pane{padding:12px}"
        ".ctrl-side{order:3}"
        ".fab-estop{display:flex;position:fixed;bottom:18px;right:18px;width:66px;height:66px;"
        "border-radius:50%;background:#dc2626;color:#fff;font-weight:800;font-size:.78em;"
        "align-items:center;justify-content:center;box-shadow:0 4px 14px rgba(0,0,0,.55);"
        "z-index:50;border:3px solid #fff}}"
        "</style></head><body>", -1);

    // Part 3 — header + tabs + toast
    httpd_resp_send_chunk(req,
        "<header>"
        "<div><h1>\xF0\x9F\xA4\x96 TrackRobot</h1>"
        "<small>AP: TrackRobot-Setup / trackrobot &middot; 192.168.4.1</small></div>"
        "<span id='conn' class='conn'>&#9679; connecting</span>"
        "</header>"
        "<div class='tabs'>"
        "<button class='tab-btn active' onclick=\"showTab('control')\">Control</button>"
        "<button class='tab-btn' onclick=\"showTab('wifi')\">WiFi</button>"
        "<button class='tab-btn' onclick=\"showTab('config')\">Config</button>"
        "<button class='tab-btn' onclick=\"showTab('status')\">Status</button>"
        "</div>"
        "<div id='toast'></div>", -1);

    // Part 4 — Control tab (header + safety + joystick)
    httpd_resp_send_chunk(req,
        "<div class='tab-pane active' id='pane-control'>"
        "<div id='hint-card' class='hint'>Robot is disarmed &mdash; press "
        "<strong>ARM</strong> to enable motors (or just move the joystick to auto-arm).</div>"
        "<div class='ctrl-wrap'>"
        "<div class='ctrl-main'>"
        "<div class='card'>"
        "<div style='text-align:center;margin-bottom:10px'>"
        "<span id='cs-state' class='badge ws-off'>---</span>"
        "<div id='cs-source' style='color:#94a3b8;font-size:.85em;margin-top:6px'>Source: &mdash;</div>"
        "</div>"
        "<div class='row arm-row'>"
        "<button class='btn-arm' onclick='armRobot()'>ARM</button>"
        "<button id='btn-slow' class='btn-neutral' onclick='toggleSlow()'>SLOW: OFF</button>"
        "<button id='btn-estop-reset' class='btn-neutral' onclick='resetEstop()'"
        " style='display:none'>RESET</button>"
        "</div>"
        "<canvas id='joy' width='300' height='300'></canvas>"
        "<div class='joy-readout'>T <span id='jt'>0.00</span> &nbsp;&bull;&nbsp; "
        "S <span id='js'>0.00</span></div>"
        "<div class='hintsmall'>WASD or &uarr;&larr;&darr;&rarr; to drive &nbsp;"
        "<span id='gp-ind' style='display:none'>&nbsp;&#127918; Gamepad connected</span></div>"
        "<div class='row'>"
        "<button class='btn-estop-big' onclick='estopRobot()'>E-STOP</button>"
        "<button class='btn-neutral' style='flex:1' onclick='stopZero()'>Stop (zero)</button>"
        "</div>"
        "<div id='ctrl-msg'></div>"
        "</div>"
        "</div>", -1);

    // Part 5 — Control tab (live status side panel) + close wrap + FAB
    httpd_resp_send_chunk(req,
        "<div class='ctrl-side'>"
        "<div class='card'>"
        "<h2>Live status</h2>"
        "<label>Left motor (actual)</label>"
        "<div class='bar-wrap'><div id='cs-bar-ml' class='bar-fill' style='background:#334155'></div>"
        "<span id='cs-lbl-ml' class='bar-lbl'>0.00</span></div>"
        "<label>Right motor (actual)</label>"
        "<div class='bar-wrap'><div id='cs-bar-mr' class='bar-fill' style='background:#334155'></div>"
        "<span id='cs-lbl-mr' class='bar-lbl'>0.00</span></div>"
        "<div style='margin-top:10px;font-size:.82em;color:#94a3b8'>"
        "Current: L <span id='cs-cur-l'>--</span> A &nbsp;&bull;&nbsp; "
        "R <span id='cs-cur-r'>--</span> A<br>"
        "Battery <span id='cs-batt'>--</span></div>"
        "</div>"
        "</div>"
        "</div>"
        "<button class='fab-estop' onclick='estopRobot()'>E-STOP</button>"
        "</div>", -1);

    // Part 6 — WiFi tab
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

    // Part 7 — Config tab
    httpd_resp_send_chunk(req,
        "<div class='tab-pane' id='pane-config'>"
        "<div class='card'>"
        "<h2>Drive parameters</h2>"
        "<p style='font-size:.9em;color:#94a3b8'>Saved to NVS. "
        "Reboot the ESP32 to apply changes.</p>"
        "<label>Deadzone <span class='val' id='dz-val'>5%</span></label>"
        "<input type='range' id='cfg-deadzone' min='0' max='20' step='1' value='5'"
        " oninput='cfgVal()'>"
        "<label>Expo curve <span class='val' id='ex-val'>30%</span></label>"
        "<input type='range' id='cfg-expo' min='0' max='100' step='1' value='30'"
        " oninput='cfgVal()'>"
        "<canvas id='expo-canvas' width='120' height='80'"
        " style='display:block;margin:10px auto;background:#0f172a;border:1px solid #334155;"
        "border-radius:6px'></canvas>"
        "<label>Max speed <span class='val' id='ms-val'>100%</span></label>"
        "<input type='range' id='cfg-maxspeed' min='10' max='100' step='5' value='100'"
        " oninput='cfgVal()'>"
        "<label>Slow-mode factor <span class='val' id='sf-val'>50%</span></label>"
        "<input type='range' id='cfg-slowfactor' min='10' max='100' step='5' value='50'"
        " oninput='cfgVal()'>"
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

    // Part 8 — Status tab
    httpd_resp_send_chunk(req,
        "<div class='tab-pane' id='pane-status'>"
        "<div class='card'>"
        "<h2>System <small style='font-weight:normal;color:#475569'>(auto-refresh 1 s)</small></h2>"
        "<div style='display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:12px'>"
        "<span id='st-state' class='badge ws-off'>?</span>"
        "<span id='st-source' style='color:#94a3b8;font-size:.9em'>Source: &mdash;</span>"
        "<span id='st-slow'   style='color:#94a3b8;font-size:.9em'></span>"
        "</div>"
        "</div>"

        "<div class='card'>"
        "<h2>Controller input</h2>"
        "<label>Throttle</label>"
        "<div class='bar-wrap'><div id='bar-thr' class='bar-fill' style='background:#38bdf8'></div>"
        "<span id='lbl-thr' class='bar-lbl'>0.00</span></div>"
        "<label>Steering</label>"
        "<div class='bar-wrap'><div id='bar-str' class='bar-fill' style='background:#a78bfa'></div>"
        "<span id='lbl-str' class='bar-lbl'>0.00</span></div>"
        "<div style='margin-top:10px;display:flex;gap:8px'>"
        "<span id='ind-slow'  class='ind'>SLOW</span>"
        "<span id='ind-arm'   class='ind'>ARM</span>"
        "<span id='ind-estop' class='ind'>ESTOP</span>"
        "</div>"
        "</div>"

        "<div class='card'>"
        "<h2>Motor output</h2>"
        "<label>Left motor (actual)</label>"
        "<div class='bar-wrap'><div id='bar-ml' class='bar-fill' style='background:#4ade80'></div>"
        "<span id='lbl-ml' class='bar-lbl'>0.00</span></div>"
        "<label>Right motor (actual)</label>"
        "<div class='bar-wrap'><div id='bar-mr' class='bar-fill' style='background:#4ade80'></div>"
        "<span id='lbl-mr' class='bar-lbl'>0.00</span></div>"
        "<details><summary>Raw JSON</summary>"
        "<pre id='status-pre' style='font-size:.78em'>Loading...</pre></details>"
        "</div>"
        "</div>", -1);

    // Part 9 — JS: tabs, drive sources, joystick
    httpd_resp_send_chunk(req,
        "<script>"
        "function showTab(t){"
        "document.querySelectorAll('.tab-pane').forEach(p=>p.classList.remove('active'));"
        "document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));"
        "document.getElementById('pane-'+t).classList.add('active');"
        "event.target.classList.add('active');"
        "if(t==='wifi')loadWifiStatus();"
        "if(t==='config')loadConfig();}"

        "function msg(id,text,cls){var el=document.getElementById(id);"
        "el.innerHTML=\"<div class='msg \"+cls+\"'>\"+text+\"</div>\";}"
        "function setText(id,v){var e=document.getElementById(id);if(e)e.textContent=v;}"
        "function clamp1(v){return Math.max(-1,Math.min(1,v));}"

        "var curState='',slowMode=false,armPending=false,wasActive=false;"
        "var joyActive=false,joyT=0,joyS=0;"
        "var keys={u:0,d:0,l:0,r:0},gpIndex=null;"

        // Joystick
        "var joy=document.getElementById('joy'),jc=joy.getContext('2d');"
        "var R=joy.width/2,knobR=R*0.28,maxR=R-knobR-4,jx=0,jy=0;"
        "function drawJoy(){var w=joy.width,h=joy.height,cx=w/2,cy=h/2;jc.clearRect(0,0,w,h);"
        "jc.beginPath();jc.arc(cx,cy,R-2,0,6.2832);jc.fillStyle='#0f172a';jc.fill();"
        "jc.lineWidth=3;jc.strokeStyle='#334155';jc.stroke();"
        "jc.strokeStyle='#1e293b';jc.lineWidth=1;jc.beginPath();"
        "jc.moveTo(cx-maxR,cy);jc.lineTo(cx+maxR,cy);jc.moveTo(cx,cy-maxR);jc.lineTo(cx,cy+maxR);jc.stroke();"
        "jc.beginPath();jc.arc(cx+jx,cy+jy,knobR,0,6.2832);"
        "jc.fillStyle=joyActive?'#38bdf8':'#0ea5e9';jc.fill();"
        "jc.lineWidth=2;jc.strokeStyle='#7dd3fc';jc.stroke();}"
        "function joyMove(cxp,cyp){var r=joy.getBoundingClientRect();"
        "var x=(cxp-r.left)*(joy.width/r.width)-joy.width/2;"
        "var y=(cyp-r.top)*(joy.height/r.height)-joy.height/2;"
        "var d=Math.hypot(x,y);if(d>maxR){x=x/d*maxR;y=y/d*maxR;}"
        "jx=x;jy=y;joyS=x/maxR;joyT=-y/maxR;drawJoy();}"
        "function pt(e){if(e.touches&&e.touches[0])return{x:e.touches[0].clientX,y:e.touches[0].clientY};"
        "return{x:e.clientX,y:e.clientY};}"
        "function joyStart(e){joyActive=true;var p=pt(e);joyMove(p.x,p.y);e.preventDefault();}"
        "function joyEnd(){if(!joyActive)return;joyActive=false;jx=0;jy=0;joyT=0;joyS=0;drawJoy();}"
        "joy.addEventListener('mousedown',joyStart);"
        "joy.addEventListener('touchstart',joyStart,{passive:false});"
        "window.addEventListener('mousemove',e=>{if(joyActive)joyMove(e.clientX,e.clientY);});"
        "window.addEventListener('touchmove',e=>{if(joyActive){var p=pt(e);joyMove(p.x,p.y);"
        "e.preventDefault();}},{passive:false});"
        "window.addEventListener('mouseup',joyEnd);"
        "window.addEventListener('touchend',joyEnd);"
        "window.addEventListener('touchcancel',joyEnd);"
        "drawJoy();", -1);

    // Part 10 — JS: keyboard, gamepad, drive loop
    httpd_resp_send_chunk(req,
        "function typing(){var a=document.activeElement;"
        "return a&&/INPUT|TEXTAREA|SELECT/.test(a.tagName);}"
        "document.addEventListener('keydown',e=>{if(typing())return;var k=e.key.toLowerCase();"
        "if(k==='w'||k==='arrowup')keys.u=1;else if(k==='s'||k==='arrowdown')keys.d=1;"
        "else if(k==='a'||k==='arrowleft')keys.l=1;else if(k==='d'||k==='arrowright')keys.r=1;"
        "else return;e.preventDefault();});"
        "document.addEventListener('keyup',e=>{var k=e.key.toLowerCase();"
        "if(k==='w'||k==='arrowup')keys.u=0;else if(k==='s'||k==='arrowdown')keys.d=0;"
        "else if(k==='a'||k==='arrowleft')keys.l=0;else if(k==='d'||k==='arrowright')keys.r=0;});"

        "window.addEventListener('gamepadconnected',e=>{gpIndex=e.gamepad.index;"
        "document.getElementById('gp-ind').style.display='';});"
        "window.addEventListener('gamepaddisconnected',e=>{if(gpIndex===e.gamepad.index){"
        "gpIndex=null;document.getElementById('gp-ind').style.display='none';}});"
        "function getGp(){if(gpIndex===null)return null;"
        "var g=navigator.getGamepads?navigator.getGamepads():[];return g[gpIndex]||null;}"

        "function setReadout(t,s){setText('jt',(t>=0?'+':'')+t.toFixed(2));"
        "setText('js',(s>=0?'+':'')+s.toFixed(2));}"
        "function sendControl(t,s){fetch('/control',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({throttle:t,steering:s,slow_mode:slowMode})}).catch(()=>{});}"
        "function autoArm(){if(armPending)return;armPending=true;"
        "fetch('/arm',{method:'POST'}).then(()=>toast('Auto-armed')).catch(()=>{})"
        ".finally(()=>setTimeout(()=>armPending=false,600));}"
        "function toast(m){var t=document.getElementById('toast');t.textContent=m;"
        "t.style.opacity='1';clearTimeout(t._h);t._h=setTimeout(()=>t.style.opacity='0',1500);}"

        "function driveTick(){var t=0,s=0,active=false;"
        "if(joyActive){t=joyT;s=joyS;active=true;}"
        "else{var kt=keys.u-keys.d,ks=keys.r-keys.l;"
        "if(kt||ks){var m=Math.hypot(kt,ks);if(m>1){kt/=m;ks/=m;}t=kt;s=ks;active=true;}"
        "else{var g=getGp();if(g){var gx=g.axes[0]||0,gy=g.axes[1]||0;"
        "if(Math.abs(gx)>=.08||Math.abs(gy)>=.08){t=-gy;s=gx;active=true;}}}}"
        "t=clamp1(t);s=clamp1(s);"
        "if(active){if(curState==='DISARMED')autoArm();sendControl(t,s);setReadout(t,s);}"
        "else if(wasActive){sendControl(0,0);setReadout(0,0);}"
        "wasActive=active;}"
        "setInterval(driveTick,100);"

        "function stopZero(){joyActive=false;jx=0;jy=0;joyT=0;joyS=0;"
        "keys={u:0,d:0,l:0,r:0};drawJoy();sendControl(0,0);setReadout(0,0);wasActive=false;}"
        "function toggleSlow(){slowMode=!slowMode;var b=document.getElementById('btn-slow');"
        "b.textContent='SLOW: '+(slowMode?'ON':'OFF');"
        "b.className=slowMode?'btn-primary':'btn-neutral';}", -1);

    // Part 11 — JS: safety actions, wifi, config
    httpd_resp_send_chunk(req,
        "async function armRobot(){await fetch('/arm',{method:'POST'}).catch(()=>{});"
        "msg('ctrl-msg','ARM sent','msg-ok');setTimeout(pollStatus,150);}"
        "async function estopRobot(){await fetch('/estop',{method:'POST'}).catch(()=>{});"
        "stopZero();msg('ctrl-msg','E-STOP triggered','msg-err');setTimeout(pollStatus,150);}"
        "async function resetEstop(){var r=await fetch('/estop-reset',{method:'POST'});"
        "var d=await r.json();msg('ctrl-msg',d.message,d.status==='ok'?'msg-ok':'msg-err');"
        "setTimeout(pollStatus,150);}"

        "async function loadWifiStatus(){try{var r=await fetch('/status');var d=await r.json();"
        "document.getElementById('wifi-status-pre').textContent=JSON.stringify(d.wifi,null,2);"
        "}catch(e){document.getElementById('wifi-status-pre').textContent='Error loading status';}}"
        "async function saveWifi(){var ssid=document.getElementById('w-ssid').value.trim();"
        "var pass=document.getElementById('w-pass').value;"
        "if(!ssid){msg('wifi-msg','SSID is required','msg-err');return;}"
        "try{var r=await fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({ssid:ssid,password:pass})});var d=await r.json();"
        "msg('wifi-msg',d.message||'Saved','msg-ok');setTimeout(loadWifiStatus,2000);"
        "}catch(e){msg('wifi-msg','Error saving WiFi','msg-err');}}"
        "async function clearWifi(){try{await fetch('/wifi',{method:'POST',"
        "headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:'',password:''})});"
        "msg('wifi-msg','WiFi cleared — AP-only mode on next boot','msg-info');"
        "}catch(e){msg('wifi-msg','Error','msg-err');}}"

        "function cfgVal(){"
        "setText('dz-val',document.getElementById('cfg-deadzone').value+'%');"
        "setText('ex-val',document.getElementById('cfg-expo').value+'%');"
        "setText('ms-val',document.getElementById('cfg-maxspeed').value+'%');"
        "setText('sf-val',document.getElementById('cfg-slowfactor').value+'%');"
        "drawExpo();}"
        "function drawExpo(){var c=document.getElementById('expo-canvas');if(!c)return;"
        "var x=c.getContext('2d'),e=parseInt(document.getElementById('cfg-expo').value)/100;"
        "var w=c.width,h=c.height;x.clearRect(0,0,w,h);"
        "x.strokeStyle='#334155';x.lineWidth=1;x.beginPath();"
        "x.moveTo(0,h/2);x.lineTo(w,h/2);x.moveTo(w/2,0);x.lineTo(w/2,h);x.stroke();"
        "x.strokeStyle='#38bdf8';x.lineWidth=2;x.beginPath();"
        "for(var i=0;i<=w;i++){var xx=(i/w)*2-1;var yy=e*xx*xx*xx+(1-e)*xx;"
        "var py=h/2-yy*(h/2-3);if(i===0)x.moveTo(i,py);else x.lineTo(i,py);}x.stroke();}"
        "async function loadConfig(){try{var r=await fetch('/config');var d=await r.json();"
        "document.getElementById('cfg-deadzone').value=d.deadzone;"
        "document.getElementById('cfg-expo').value=d.expo;"
        "document.getElementById('cfg-maxspeed').value=d.max_speed;"
        "document.getElementById('cfg-slowfactor').value=d.slow_factor;cfgVal();"
        "}catch(e){msg('cfg-msg','Error loading config','msg-err');}}"
        "async function saveConfig(){var body={"
        "deadzone:parseInt(document.getElementById('cfg-deadzone').value),"
        "expo:parseInt(document.getElementById('cfg-expo').value),"
        "max_speed:parseInt(document.getElementById('cfg-maxspeed').value),"
        "slow_factor:parseInt(document.getElementById('cfg-slowfactor').value)};"
        "try{var r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(body)});var d=await r.json();msg('cfg-msg',d.message,'msg-ok');"
        "}catch(e){msg('cfg-msg','Error saving config','msg-err');}}"
        "async function rebootRobot(){msg('reboot-msg','Rebooting...','msg-info');"
        "fetch('/reboot',{method:'POST'}).catch(()=>{});"
        "setTimeout(()=>msg('reboot-msg','ESP32 rebooting — reconnect in ~5 s','msg-info'),500);}", -1);

    // Part 12 — JS: status poll, connection health, bars
    httpd_resp_send_chunk(req,
        "function setBar(id,lblId,v){var el=document.getElementById(id);if(!el)return;"
        "var lbl=document.getElementById(lblId);var pct=Math.abs(v)*50;"
        "if(v>=0){el.style.left='50%';el.style.width=pct+'%';}"
        "else{el.style.left=(50-pct)+'%';el.style.width=pct+'%';}"
        "if(lbl)lbl.textContent=(v>=0?'+':'')+v.toFixed(2);}"
        "function setInd(id,on,ok){var el=document.getElementById(id);if(!el)return;"
        "el.classList.remove('active','active-ok');if(on)el.classList.add(ok?'active-ok':'active');}"
        "function setBadge(id,st){var e=document.getElementById(id);if(!e)return;"
        "e.textContent=st||'?';e.className='badge '+(st==='ARMED'?'ws-armed':"
        "st==='ESTOP'?'ws-estop':'ws-disarmed');}"
        "function motColor(id,v){var e=document.getElementById(id);"
        "if(e)e.style.background=Math.abs(v)>.05?'#4ade80':'#334155';}"

        "var lastOk=0,lastLat=0;"
        "async function pollStatus(){var t0=performance.now();"
        "try{var r=await fetch('/status');var d=await r.json();"
        "lastLat=Math.round(performance.now()-t0);lastOk=performance.now();applyStatus(d);"
        "}catch(e){}}"
        "function applyStatus(d){curState=d.state||'';"
        "document.body.classList.toggle('estop',curState==='ESTOP');"
        "var hc=document.getElementById('hint-card');"
        "if(hc)hc.style.display=curState==='DISARMED'?'':'none';"
        "setBadge('cs-state',d.state);setBadge('st-state',d.state);"
        "var src='Source: '+(d.source||'NONE');setText('cs-source',src);setText('st-source',src);"
        "setText('st-slow',d.input&&d.input.slow_mode?'SLOW MODE':'');"
        "var rb=document.getElementById('btn-estop-reset');"
        "if(rb)rb.style.display=d.state==='ESTOP'?'':'none';"
        "if(d.output){var la=d.output.left_actual||0,ra=d.output.right_actual||0;"
        "setBar('cs-bar-ml','cs-lbl-ml',la);setBar('cs-bar-mr','cs-lbl-mr',ra);"
        "setBar('bar-ml','lbl-ml',la);setBar('bar-mr','lbl-mr',ra);"
        "motColor('cs-bar-ml',la);motColor('cs-bar-mr',ra);"
        "motColor('bar-ml',la);motColor('bar-mr',ra);}"
        "if(d.input){setBar('bar-thr','lbl-thr',d.input.throttle||0);"
        "setBar('bar-str','lbl-str',d.input.steering||0);"
        "setInd('ind-slow',d.input.slow_mode,true);setInd('ind-arm',d.input.arm,true);"
        "setInd('ind-estop',d.input.estop,false);}"
        "if(d.monitor){setText('cs-cur-l',(d.monitor.left_ma/1000).toFixed(1));"
        "setText('cs-cur-r',(d.monitor.right_ma/1000).toFixed(1));"
        "var be=document.getElementById('cs-batt');if(be){be.textContent=d.monitor.battery_enabled?"
        "(d.monitor.battery_mv/1000).toFixed(2)+' V'+(d.monitor.battery_low?' (LOW)':''):'n/a';}}"
        "var pre=document.getElementById('status-pre');"
        "if(pre)pre.textContent=JSON.stringify(d,null,2);}"

        "function updateConn(){var el=document.getElementById('conn');if(!el)return;"
        "if(!lastOk){el.textContent='\\u25CF connecting';el.style.color='#94a3b8';return;}"
        "var age=performance.now()-lastOk;"
        "el.style.color=age<2000?'#22c55e':age<5000?'#eab308':'#dc2626';"
        "el.textContent='\\u25CF '+lastLat+'ms';}"

        "pollStatus();loadConfig();"
        "setInterval(pollStatus,1000);"
        "setInterval(updateConn,500);"
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

// ---------------------------------------------------------------------------
//  Captive portal DNS server — answers every A query with 192.168.4.1
// ---------------------------------------------------------------------------

static void dns_server_task(void *arg) {
    uint8_t rx[DNS_BUF_LEN];
    uint8_t tx[DNS_BUF_LEN];

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Captive DNS: failed to create socket (errno %d)", errno);
        vTaskDelete(NULL);
        return;
    }
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Captive DNS: failed to bind port 53 (errno %d)", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive portal DNS server listening on port 53");

    while (1) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&client, &clen);
        if (len < 12) {
            continue;  // smaller than a DNS header — ignore
        }

        // Walk past the first question's QNAME (length-prefixed labels until 0).
        int q = 12;
        while (q < len && rx[q] != 0) {
            q += rx[q] + 1;
        }
        q += 1;  // terminating zero label
        q += 4;  // QTYPE + QCLASS
        if (q > len || q + 16 > (int)sizeof(tx)) {
            continue;  // malformed or no room for the answer
        }

        // Response = original header + question, with an appended A record.
        memcpy(tx, rx, q);
        tx[2] = 0x81; tx[3] = 0x80;  // QR=1, RD copied, RA=1, RCODE=0
        tx[4] = 0x00; tx[5] = 0x01;  // QDCOUNT = 1
        tx[6] = 0x00; tx[7] = 0x01;  // ANCOUNT = 1
        tx[8] = 0x00; tx[9] = 0x00;  // NSCOUNT = 0
        tx[10] = 0x00; tx[11] = 0x00; // ARCOUNT = 0 (drop any EDNS OPT)

        int a = q;
        tx[a++] = 0xC0; tx[a++] = 0x0C;  // NAME: pointer to question at offset 12
        tx[a++] = 0x00; tx[a++] = 0x01;  // TYPE A
        tx[a++] = 0x00; tx[a++] = 0x01;  // CLASS IN
        tx[a++] = 0x00; tx[a++] = 0x00;
        tx[a++] = 0x00; tx[a++] = 0x3C;  // TTL = 60 s
        tx[a++] = 0x00; tx[a++] = 0x04;  // RDLENGTH = 4
        tx[a++] = CAPTIVE_IP[0]; tx[a++] = CAPTIVE_IP[1];
        tx[a++] = CAPTIVE_IP[2]; tx[a++] = CAPTIVE_IP[3];

        sendto(sock, tx, a, 0, (struct sockaddr *)&client, clen);
    }
}

// 302-redirect every unmatched request to the web UI so OS captive-portal
// probes (e.g. /generate_204, /hotspot-detect.html) trigger the sign-in popup.
static esp_err_t captive_redirect_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", CAPTIVE_REDIRECT_URL);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 13;

    if (server) return ESP_OK;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        {.uri = "/control",     .method = HTTP_POST, .handler = control_post_handler},
        {.uri = "/wifi",        .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/estop",       .method = HTTP_POST, .handler = estop_post_handler},
        {.uri = "/estop-reset", .method = HTTP_POST, .handler = estop_reset_post_handler},
        {.uri = "/arm",         .method = HTTP_POST, .handler = arm_post_handler},
        {.uri = "/status",      .method = HTTP_GET,  .handler = status_get_handler},
        {.uri = "/config",      .method = HTTP_GET,  .handler = config_get_handler},
        {.uri = "/config",      .method = HTTP_POST, .handler = config_post_handler},
        {.uri = "/reboot",      .method = HTTP_POST, .handler = reboot_post_handler},
        {.uri = "/",            .method = HTTP_GET,  .handler = index_get_handler},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    // Captive portal: redirect any unmatched path to the web UI.
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_redirect_handler);

    ESP_LOGI(TAG, "HTTP server started — 10 endpoints registered");
    ESP_LOGI(TAG, "Control UI: http://192.168.4.1/ (connect to TrackRobot-Setup AP first)");
    return ESP_OK;
}

esp_err_t controller_http_init(void) {
    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(start_webserver());

    // Start the captive-portal DNS responder (AP is always active).
    BaseType_t dns_ret = xTaskCreate(dns_server_task, "captive_dns", 3072, NULL, 4, NULL);
    if (dns_ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to start captive portal DNS task");
    }

    return ESP_OK;
}
