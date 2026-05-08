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
 *
 * Discovery uses the raw BT GAP API (esp_gap_bt_api.h) rather than the
 * private esp_hid_gap.h header, which is not exported as a public IDF API.
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ps4";

#define PS4_DEVICE_NAME  "Wireless Controller"
// BT inquiry length in 1.28 s units (8 = ~10 s)
#define PS4_INQ_LEN      8
#define PS4_REPORT_MIN_LEN   7

// Input report offsets (after the optional 0x01 report-ID byte)
#define OFF_LX      0
#define OFF_LY      1
#define OFF_RX      2
#define OFF_RY      3
#define OFF_BTNS1   4
#define OFF_BTNS2   5
#define OFF_BTNS3   6

static ps4_callback_t    s_callback = NULL;
static ps4_gamepad_t     s_gamepad  = {0};
static SemaphoreHandle_t s_mutex    = NULL;
static bool              s_connecting = false;

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

// ---- HIDH event handler ------------------------------------------------

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

        case ESP_HIDH_INPUT_EVENT: {
            const uint8_t *d  = p->input.data;
            size_t         len = p->input.length;
            if (len > 0 && d[0] == 0x01) {
                parse_report(d + 1, len - 1);
            } else {
                parse_report(d, len);
            }
            break;
        }

        case ESP_HIDH_CLOSE_EVENT:
            ESP_LOGW(TAG, "PS4 controller disconnected, restarting scan");
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            memset(&s_gamepad, 0, sizeof(s_gamepad));
            xSemaphoreGive(s_mutex);
            // Restart discovery so the controller can reconnect
            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                       PS4_INQ_LEN, 0);
            break;

        default:
            break;
    }
}

// ---- BT GAP event handler (discovery) ----------------------------------

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_DISC_RES_EVT) {
        if (s_connecting) return;   // already found one

        const char *found_name = NULL;
        esp_bd_addr_t found_bda;

        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];

            if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME && prop->len > 0) {
                found_name = (const char *)prop->val;
            } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR) {
                // Try to extract the complete or short local name from EIR
                uint8_t name_len = 0;
                uint8_t *eir_name = esp_bt_gap_resolve_eir_data(
                    (uint8_t *)prop->val,
                    ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &name_len);
                if (!eir_name) {
                    eir_name = esp_bt_gap_resolve_eir_data(
                        (uint8_t *)prop->val,
                        ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &name_len);
                }
                if (eir_name && name_len > 0) {
                    // eir_name is not null-terminated; compare by length
                    if (name_len >= strlen(PS4_DEVICE_NAME) &&
                        memcmp(eir_name, PS4_DEVICE_NAME,
                               strlen(PS4_DEVICE_NAME)) == 0) {
                        memcpy(found_bda, param->disc_res.bda, sizeof(esp_bd_addr_t));
                        s_connecting = true;
                        ESP_LOGI(TAG, "PS4 found via EIR — connecting");
                        esp_bt_gap_cancel_discovery();
                        esp_hidh_dev_open(found_bda, ESP_HID_TRANSPORT_BT, 0);
                        return;
                    }
                }
            }
        }

        if (found_name && strstr(found_name, PS4_DEVICE_NAME)) {
            memcpy(found_bda, param->disc_res.bda, sizeof(esp_bd_addr_t));
            s_connecting = true;
            ESP_LOGI(TAG, "PS4 found: %s — connecting", found_name);
            esp_bt_gap_cancel_discovery();
            esp_hidh_dev_open(found_bda, ESP_HID_TRANSPORT_BT, 0);
        }

    } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            if (!s_connecting) {
                ESP_LOGW(TAG, "PS4 not found — press PS+Share and waiting will retry");
            }
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(TAG, "BT discovery started — press PS+Share on PS4");
        }
    }
}

// ---- Public API --------------------------------------------------------

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

    // BT controller
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

    // Register GAP callback for device discovery
    ret = esp_bt_gap_register_callback(bt_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize HID host
    esp_hidh_config_t hidh_cfg = {
        .callback         = hidh_event_handler,
        .event_stack_size = 4096,
        .callback_arg     = NULL,
    };
    ret = esp_hidh_init(&hidh_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HIDH init: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start BT Classic inquiry — PS4 will be found when it's in pairing mode
    ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                     PS4_INQ_LEN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT discovery start: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "PS4 driver ready — press PS+Share to pair (~10 s window)");
    return ESP_OK;
}

bool ps4_is_connected(void) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool c = s_gamepad.connected;
    xSemaphoreGive(s_mutex);
    return c;
}
