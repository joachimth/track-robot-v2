/**
 * @file controller_http.c
 * @brief HTTP (WiFi) controller implementation
 * 
 * REST API:
 * - POST /control  {"throttle": 0.5, "steering": -0.2}
 * - POST /estop
 * - POST /arm
 * - GET /status
 * - GET /  (web UI)
 */

#include "controller_http.h"
#include "control_manager.h"
#include "control_frame.h"
#include "safety_failsafe.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "ctrl_http";

static httpd_handle_t server = NULL;

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "Station "MACSTR" joined", MAC2STR(event->mac));
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "Station "MACSTR" left", MAC2STR(event->mac));
        } else if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "Disconnected from AP, retrying...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief Initialize WiFi
 */
static esp_err_t init_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
#ifdef CONFIG_ROBOT_WIFI_MODE_AP
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_ROBOT_WIFI_SSID,
            .ssid_len = strlen(CONFIG_ROBOT_WIFI_SSID),
            .channel = CONFIG_ROBOT_WIFI_CHANNEL,
            .password = CONFIG_ROBOT_WIFI_PASSWORD,
            .max_connection = CONFIG_ROBOT_WIFI_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    if (strlen(CONFIG_ROBOT_WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi AP started");
    ESP_LOGI(TAG, "  SSID: %s", CONFIG_ROBOT_WIFI_SSID);
    ESP_LOGI(TAG, "  Password: %s", CONFIG_ROBOT_WIFI_PASSWORD);
    ESP_LOGI(TAG, "  IP: 192.168.4.1");
    
#else // STA mode
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ROBOT_WIFI_SSID,
            .password = CONFIG_ROBOT_WIFI_PASSWORD,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi STA started, connecting to %s...", CONFIG_ROBOT_WIFI_SSID);
#endif
    
    return ESP_OK;
}

/**
 * @brief POST /control handler
 */
static esp_err_t control_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse JSON
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
    
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/**
 * @brief POST /estop handler
 */
static esp_err_t estop_post_handler(httpd_req_t *req) {
    control_frame_t frame = {0};
    frame.estop = true;
    frame.timestamp = xTaskGetTickCount();
    control_manager_submit(CONTROL_SOURCE_HTTP, &frame);
    
    httpd_resp_sendstr(req, "{\"status\":\"estop\"}");
    return ESP_OK;
}

/**
 * @brief POST /arm handler
 */
static esp_err_t arm_post_handler(httpd_req_t *req) {
    control_frame_t frame = {0};
    frame.arm = true;
    frame.timestamp = xTaskGetTickCount();
    control_manager_submit(CONTROL_SOURCE_HTTP, &frame);
    
    httpd_resp_sendstr(req, "{\"status\":\"armed\"}");
    return ESP_OK;
}

/**
 * @brief GET /status handler
 */
static esp_err_t status_get_handler(httpd_req_t *req) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"armed\":%s,\"source\":%d}",
             safety_is_armed() ? "true" : "false",
             control_manager_get_active_source());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

/**
 * @brief GET / handler (web UI)
 */
static esp_err_t index_get_handler(httpd_req_t *req) {
    const char *html = 
        "<!DOCTYPE html><html><head><title>Tracked Robot</title>"
        "<style>body{font-family:Arial;text-align:center;padding:20px;}"
        "button{padding:20px;margin:10px;font-size:18px;}</style></head><body>"
        "<h1>Tracked Robot Control</h1>"
        "<button onclick=\"fetch('/arm',{method:'POST'})\">ARM</button>"
        "<button onclick=\"fetch('/estop',{method:'POST'})\">E-STOP</button>"
        "<h2>Manual Control</h2>"
        "<p>Throttle: <input id='t' type='range' min='-100' max='100' value='0'></p>"
        "<p>Steering: <input id='s' type='range' min='-100' max='100' value='0'></p>"
        "<button onclick=\"send()\">Send</button>"
        "<script>function send(){fetch('/control',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({throttle:parseInt(document.getElementById('t').value)/100,"
        "steering:parseInt(document.getElementById('s').value)/100})})}</script>"
        "</body></html>";
    
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

/**
 * @brief Start HTTP server
 */
static esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    
    // Register handlers
    httpd_uri_t uri_control = {
        .uri = "/control", .method = HTTP_POST, .handler = control_post_handler
    };
    httpd_register_uri_handler(server, &uri_control);
    
    httpd_uri_t uri_estop = {
        .uri = "/estop", .method = HTTP_POST, .handler = estop_post_handler
    };
    httpd_register_uri_handler(server, &uri_estop);
    
    httpd_uri_t uri_arm = {
        .uri = "/arm", .method = HTTP_POST, .handler = arm_post_handler
    };
    httpd_register_uri_handler(server, &uri_arm);
    
    httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = status_get_handler
    };
    httpd_register_uri_handler(server, &uri_status);
    
    httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = index_get_handler
    };
    httpd_register_uri_handler(server, &uri_index);
    
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

esp_err_t controller_http_init(void) {
    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(start_webserver());
    return ESP_OK;
}
