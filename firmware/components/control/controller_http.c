/**
 * @file controller_http.c
 * @brief HTTP (WiFi) controller implementation with AP+STA fallback setup portal
 *
 * REST API:
 * - POST /control  {"throttle": 0.5, "steering": -0.2}
 * - POST /estop
 * - POST /arm
 * - GET /status
 * - GET /  (web UI + WiFi setup)
 * - POST /wifi {"ssid":"...", "password":"..."}
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    if (!ap_started) {
        ESP_LOGI(TAG, "Starting fallback setup AP...");
    }

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

static esp_err_t index_get_handler(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Tracked Robot</title>"
        "<style>body{font-family:Arial,sans-serif;max-width:760px;margin:auto;padding:20px;background:#111;color:#eee;}"
        "button,input{padding:12px;margin:6px;font-size:16px;border-radius:8px;border:1px solid #555;}"
        "button{cursor:pointer}.card{background:#1d1d1d;padding:16px;margin:14px 0;border-radius:12px;}"
        ".danger{background:#8b0000;color:white}.ok{background:#145214;color:white}"
        "input{width:calc(100% - 30px);background:#222;color:#eee}.range{width:100%;}</style></head><body>"
        "<h1>Tracked Robot</h1>"
        "<div class='card'><h2>Status</h2><pre id='status'>Loading...</pre>"
        "<button onclick='refreshStatus()'>Refresh</button></div>"
        "<div class='card'><h2>WiFi setup</h2>"
        "<p>Fallback AP: <b>TrackRobot-Setup</b> / <b>trackrobot</b><br>Setup IP: <b>192.168.4.1</b></p>"
        "<input id='ssid' placeholder='Existing WiFi SSID'>"
        "<input id='password' placeholder='WiFi password' type='password'>"
        "<button class='ok' onclick='saveWifi()'>Save WiFi and connect</button><pre id='wifiResult'></pre></div>"
        "<div class='card'><h2>Manual control</h2>"
        "<button class='ok' onclick=\"fetch('/arm',{method:'POST'}).then(refreshStatus)\">ARM</button>"
        "<button class='danger' onclick=\"fetch('/estop',{method:'POST'}).then(refreshStatus)\">E-STOP</button>"
        "<p>Throttle</p><input class='range' id='t' type='range' min='-100' max='100' value='0'>"
        "<p>Steering</p><input class='range' id='s' type='range' min='-100' max='100' value='0'>"
        "<button onclick='sendControl()'>Send once</button>"
        "<button onclick='stopControl()'>Stop</button></div>"
        "<script>"
        "async function refreshStatus(){let r=await fetch('/status');document.getElementById('status').textContent=JSON.stringify(await r.json(),null,2);}"
        "async function saveWifi(){let body={ssid:ssid.value,password:password.value};let r=await fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});document.getElementById('wifiResult').textContent=await r.text();setTimeout(refreshStatus,1000);}"
        "async function sendControl(){await fetch('/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({throttle:parseInt(t.value)/100,steering:parseInt(s.value)/100})});refreshStatus();}"
        "function stopControl(){t.value=0;s.value=0;sendControl();}"
        "refreshStatus();setInterval(refreshStatus,5000);"
        "</script></body></html>";

    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

static esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (server) return ESP_OK;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t uri_control = {.uri = "/control", .method = HTTP_POST, .handler = control_post_handler};
    httpd_register_uri_handler(server, &uri_control);

    httpd_uri_t uri_wifi = {.uri = "/wifi", .method = HTTP_POST, .handler = wifi_post_handler};
    httpd_register_uri_handler(server, &uri_wifi);

    httpd_uri_t uri_estop = {.uri = "/estop", .method = HTTP_POST, .handler = estop_post_handler};
    httpd_register_uri_handler(server, &uri_estop);

    httpd_uri_t uri_arm = {.uri = "/arm", .method = HTTP_POST, .handler = arm_post_handler};
    httpd_register_uri_handler(server, &uri_arm);

    httpd_uri_t uri_status = {.uri = "/status", .method = HTTP_GET, .handler = status_get_handler};
    httpd_register_uri_handler(server, &uri_status);

    httpd_uri_t uri_index = {.uri = "/", .method = HTTP_GET, .handler = index_get_handler};
    httpd_register_uri_handler(server, &uri_index);

    ESP_LOGI(TAG, "HTTP server started");
    ESP_LOGI(TAG, "Open setup/control UI at http://192.168.4.1/ when connected to fallback AP");
    return ESP_OK;
}

esp_err_t controller_http_init(void) {
    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(start_webserver());
    return ESP_OK;
}
