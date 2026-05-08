/**
 * @file ps4.c
 * @brief PS4 DualShock 4 controller driver using ESP-IDF esp_hidh
 *
 * BT Classic HID input report layout (report ID 0x01, offsets after ID byte):
 *   [0]  Left stick X      (0-255, centre=128)
 *   [1]  Left stick Y      (0-255, centre=128, up=0)
 *   [2]  Right stick X
 *   [3]  Right stick Y
 *   [4]  Buttons 1: lo nibble=D-pad, bit4=Square, bit5=Cross, bit6=Circle, bit7=Triangle
 *   [5]  Buttons 2: bit0=L1, bit1=R1, bit2=L2, bit3=R2, bit4=Share, bit5=Options, bit6=L3, bit7=R3
 *   [6]  Buttons 3: bit0=PS, bit1=Touchpad click
 */

#include "ps4.h"
#include "esp_hidh.h"
#include "esp_hid_gap.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ps4";

#define PS4_SCAN_DURATION_S  10
#define PS4_REPORT_MIN_LEN   7   // minimum useful bytes after report ID

// Input report offsets (after optional 0x01 report-ID byte)
#define OFF_LX      0
#define OFF_LY      1
#define OFF_RX      2
#define OFF_RY      3
#define OFF_BTNS1   4
#define OFF_BTNS2   5
#define OFF_BTNS3   6

static ps4_callback_t s_callback = NULL;
static ps4_gamepad_t  s_gamepad  = {0};
static SemaphoreHandle_t s_mutex = NULL;

// Normalise raw axis byte to [-1.0, +1.0], clamped
static float axis_norm(uint8_t raw) {
    float v = ((float)raw - 128.0f) / 127.0f;
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

static void parse_report(const uint8_t *d, size_t len) {
    if (len < PS4_REPORT_MIN_LEN) return;

    ps4_gamepad_t g = {.connected = true};

    g.lx = axis_norm(d[OFF_LX]);
    g.ly = axis_norm(d[OFF_LY]);
    g.rx = axis_norm(d[OFF_RX]);
    g.ry = axis_norm(d[OFF_RY]);

    uint8_t b1 = d[OFF_BTNS1];
    g.square   = (b1 & BIT(4)) != 0;
    g.cross    = (b1 & BIT(5)) != 0;
    g.circle   = (b1 & BIT(6)) != 0;
    g.triangle = (b1 & BIT(7)) != 0;

    uint8_t b2 = d[OFF_BTNS2];
    g.l1      = (b2 & BIT(0)) != 0;
    g.r1      = (b2 & BIT(1)) != 0;
    g.l2      = (b2 & BIT(2)) != 0;
    g.r2      = (b2 & BIT(3)) != 0;
    g.options = (b2 & BIT(5)) != 0;

    g.ps = (d[OFF_BTNS3] & BIT(0)) != 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&s_gamepad, &g, sizeof(ps4_gamepad_t));
    xSemaphoreGive(s_mutex);

    if (s_callback) s_callback(&g);
}

static void hidh_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *p = (esp_hidh_event_data_t *)event_data;

    switch (event) {
        case ESP_HIDH_OPEN_EVENT:
            if (p->open.status == ESP_OK) {
                ESP_LOGI(TAG, "PS4 controller connected");
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_gamepad.connected = true;
                xSemaphoreGive(s_mutex);
            } else {
                ESP_LOGW(TAG, "PS4 open failed: %s", esp_err_to_name(p->open.status));
            }
            break;

        case ESP_HIDH_INPUT_EVENT: {
            const uint8_t *d   = p->input.data;
            size_t          len = p->input.length;
            // Strip leading report-ID byte if present
            if (len > 0 && d[0] == 0x01) {
                parse_report(d + 1, len - 1);
            } else {
                parse_report(d, len);
            }
            break;
        }

        case ESP_HIDH_CLOSE_EVENT:
            ESP_LOGW(TAG, "PS4 controller disconnected");
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            memset(&s_gamepad, 0, sizeof(s_gamepad));
            xSemaphoreGive(s_mutex);
            break;

        default:
            break;
    }
}

static void scan_connect_task(void *arg) {
    size_t count = 0;
    esp_hid_scan_result_t *results = NULL;

    ESP_LOGI(TAG, "Scanning for PS4 controller (%ds) — press PS+Share to pair",
             PS4_SCAN_DURATION_S);

    esp_hid_scan(PS4_SCAN_DURATION_S, &count, &results);
    ESP_LOGI(TAG, "BT scan done: %d HID device(s) found", (int)count);

    for (esp_hid_scan_result_t *r = results; r != NULL; r = r->next) {
        if (r->transport != ESP_HID_TRANSPORT_BT) continue;
        ESP_LOGI(TAG, "  BT HID: %s", r->name ? r->name : "(unnamed)");
        if (r->name && strstr(r->name, "Wireless Controller") != NULL) {
            ESP_LOGI(TAG, "PS4 found — connecting");
            esp_hidh_dev_open(r->bt.bda, ESP_HID_TRANSPORT_BT, 0);
            break;
        }
    }

    esp_hid_scan_results_free(results);
    vTaskDelete(NULL);
}

esp_err_t ps4_init(const uint8_t *host_mac, ps4_callback_t callback) {
    esp_err_t ret;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_callback = callback;

    if (host_mac) {
        ret = esp_base_mac_addr_set(host_mac);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_base_mac_addr_set: %s", esp_err_to_name(ret));
        }
    }

    // Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable: %s", esp_err_to_name(ret));
        return ret;
    }

    // Bluedroid host stack
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_bt_dev_set_device_name("TrackRobot");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // HID host
    esp_hidh_config_t hidh_cfg = {
        .callback        = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg    = NULL,
    };
    ret = esp_hidh_init(&hidh_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HIDH init: %s", esp_err_to_name(ret));
        return ret;
    }

    // HID GAP for BT Classic discovery
    ret = esp_hid_gap_init(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID GAP init: %s", esp_err_to_name(ret));
        return ret;
    }

    xTaskCreate(scan_connect_task, "ps4_scan", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "PS4 driver ready");
    return ESP_OK;
}

bool ps4_is_connected(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool c = s_gamepad.connected;
    xSemaphoreGive(s_mutex);
    return c;
}
