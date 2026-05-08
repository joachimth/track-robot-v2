/**
 * @file ps4.c
 * @brief PS4 DualShock 4 controller driver using ESP-IDF esp_hidh
 */

#include "ps4.h"
#include "esp_hidh.h"
#include "esp_hid_common.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ps4";

#define PS4_DEVICE_NAME  "Wireless Controller"
#define PS4_INQ_LEN      8
#define PS4_RESCAN_DELAY_MS  3000

static ps4_callback_t    s_callback = NULL;
static ps4_gamepad_t     s_gamepad  = {0};
static SemaphoreHandle_t s_mutex    = NULL;
static bool              s_connecting = false;
static bool              s_discovering = false;

static esp_err_t ensure_event_loop(void) {
    esp_err_t ret = esp_event_loop_create_default();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Created default ESP event loop");
        return ESP_OK;
    }

    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "Default ESP event loop already exists");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(ret));
    return ret;
}

bool ps4_is_connected(void) {
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool c = s_gamepad.connected;
    xSemaphoreGive(s_mutex);
    return c;
}

static esp_err_t ps4_start_discovery(void) {
    if (s_connecting || s_discovering || ps4_is_connected()) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting BT discovery — press PS+Share until fast blink");

    esp_err_t ret = esp_bt_gap_start_discovery(
        ESP_BT_INQ_MODE_GENERAL_INQUIRY,
        PS4_INQ_LEN,
        0
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT discovery start failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void ps4_scan_task(void *arg) {
    while (!ps4_is_connected()) {
        ps4_start_discovery();
        vTaskDelay(pdMS_TO_TICKS((PS4_INQ_LEN * 1280) + PS4_RESCAN_DELAY_MS));
    }

    ESP_LOGI(TAG, "PS4 scan task exiting — controller connected");
    vTaskDelete(NULL);
}

static void hidh_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *p = (esp_hidh_event_data_t *)event_data;

    switch (event) {
        case ESP_HIDH_OPEN_EVENT:
            s_connecting = false;

            if (p->open.status == ESP_OK) {
                ESP_LOGI(TAG, "PS4 controller connected");

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_gamepad.connected = true;
                xSemaphoreGive(s_mutex);
            } else {
                ESP_LOGW(TAG, "PS4 open failed: %s", esp_err_to_name(p->open.status));
            }
            break;

        case ESP_HIDH_CLOSE_EVENT:
            ESP_LOGW(TAG, "PS4 disconnected");

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            memset(&s_gamepad, 0, sizeof(s_gamepad));
            xSemaphoreGive(s_mutex);

            s_connecting = false;
            xTaskCreate(ps4_scan_task, "ps4_scan", 4096, NULL, 3, NULL);
            break;

        default:
            break;
    }
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event,
                      esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {

        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            s_discovering = true;
            ESP_LOGI(TAG, "BT discovery started");

        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            s_discovering = false;
            ESP_LOGI(TAG, "BT discovery stopped");
        }

    } else if (event == ESP_BT_GAP_DISC_RES_EVT) {

        if (s_connecting) return;

        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];

            if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME && prop->len > 0) {
                const char *name = (const char *)prop->val;

                ESP_LOGI(TAG, "BT device found: %s", name);

                if (strstr(name, PS4_DEVICE_NAME)) {
                    s_connecting = true;

                    ESP_LOGI(TAG, "PS4 controller found — opening HID connection");

                    esp_bt_gap_cancel_discovery();

                    esp_hidh_dev_open(
                        param->disc_res.bda,
                        ESP_HID_TRANSPORT_BT,
                        0
                    );

                    return;
                }
            }
        }
    }
}

esp_err_t ps4_init(const uint8_t *host_mac, ps4_callback_t callback) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Creating PS4 mutex");

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_callback = callback;

    ESP_LOGI(TAG, "Ensuring ESP event loop exists");
    ret = ensure_event_loop();
    if (ret != ESP_OK) {
        return ret;
    }

    if (host_mac) {
        ESP_LOGI(TAG, "Setting custom BT MAC");
        esp_base_mac_addr_set(host_mac);
    }

    ESP_LOGI(TAG, "Initializing BT controller");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Enabling BTDM mode");

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "BT enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initializing Bluedroid");

    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Enabling Bluedroid");

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Registering GAP callback");

    ret = esp_bt_gap_register_callback(bt_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initializing HID host");

    esp_hidh_config_t hidh_cfg = {
        .callback = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };

    ret = esp_hidh_init(&hidh_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID host init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "PS4 driver initialized successfully");

    BaseType_t task_ret = xTaskCreate(
        ps4_scan_task,
        "ps4_scan",
        4096,
        NULL,
        3,
        NULL
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scan task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
