/**
 * @file ps4.c
 * @brief Stable gamepad backend using Bluepad32
 */

#include "ps4.h"

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "uni.h"
#include "bt/uni_bt.h"
#include "controller/uni_gamepad.h"
#include "platform/uni_platform.h"

#include <string.h>
#include <math.h>

static const char *TAG = "gamepad";

static ps4_callback_t s_callback = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static ps4_gamepad_t s_state = {0};
static bool s_started = false;

static float normalize_axis(int32_t v) {
    float f = (float)v / 512.0f;

    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;

    if (fabsf(f) < 0.05f) {
        f = 0.0f;
    }

    return f;
}

static void update_gamepad_state(const uni_gamepad_t* gp) {
    ps4_gamepad_t snapshot;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_state.connected = true;

    s_state.lx = normalize_axis(gp->axis_x);
    s_state.ly = -normalize_axis(gp->axis_y);
    s_state.rx = normalize_axis(gp->axis_rx);
    s_state.ry = -normalize_axis(gp->axis_ry);

    s_state.cross = (gp->buttons & BUTTON_A) != 0;
    s_state.circle = (gp->buttons & BUTTON_B) != 0;
    s_state.square = (gp->buttons & BUTTON_X) != 0;
    s_state.triangle = (gp->buttons & BUTTON_Y) != 0;

    s_state.l1 = (gp->buttons & BUTTON_SHOULDER_L) != 0;
    s_state.r1 = (gp->buttons & BUTTON_SHOULDER_R) != 0;
    s_state.l2 = (gp->buttons & BUTTON_TRIGGER_L) != 0;
    s_state.r2 = (gp->buttons & BUTTON_TRIGGER_R) != 0;

    s_state.share = (gp->misc_buttons & MISC_BUTTON_SELECT) != 0;
    s_state.options = (gp->misc_buttons & MISC_BUTTON_START) != 0;
    s_state.ps = (gp->misc_buttons & MISC_BUTTON_SYSTEM) != 0;

    s_state.dpad_up = (gp->dpad & DPAD_UP) != 0;
    s_state.dpad_down = (gp->dpad & DPAD_DOWN) != 0;
    s_state.dpad_left = (gp->dpad & DPAD_LEFT) != 0;
    s_state.dpad_right = (gp->dpad & DPAD_RIGHT) != 0;

    snapshot = s_state;
    xSemaphoreGive(s_mutex);

    if (s_callback) {
        s_callback(&snapshot);
    }
}

static void platform_init(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    ESP_LOGI(TAG, "Bluepad32 platform init");
}

static void platform_on_init_complete(void) {
    ESP_LOGI(TAG, "Bluepad32 ready");
    ESP_LOGI(TAG, "Starting controller scan / autoconnect");
    ESP_LOGI(TAG, "PS4: Hold SHARE + PS until rapid blinking");

    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    (void)addr;
    (void)cod;
    ESP_LOGI(TAG, "Discovered device: %s RSSI=%d", name ? name : "(unknown)", rssi);
    return UNI_ERROR_SUCCESS;
}

static void platform_on_device_connected(uni_hid_device_t* d) {
    (void)d;
    ESP_LOGI(TAG, "Controller connected");

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.connected = true;
    xSemaphoreGive(s_mutex);
}

static void platform_on_device_disconnected(uni_hid_device_t* d) {
    (void)d;
    ESP_LOGW(TAG, "Controller disconnected");

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(&s_state, 0, sizeof(s_state));
    xSemaphoreGive(s_mutex);
}

static uni_error_t platform_on_device_ready(uni_hid_device_t* d) {
    (void)d;
    ESP_LOGI(TAG, "Controller ready");
    return UNI_ERROR_SUCCESS;
}

static void platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    (void)d;
    if (!ctl) return;

    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }

    update_gamepad_state(&ctl->gamepad);
}

static const uni_property_t* platform_get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    (void)data;
    ESP_LOGI(TAG, "Bluepad32 OOB event: %d", event);
}

static struct uni_platform s_platform = {
    .name = "track-robot",
    .init = platform_init,
    .on_init_complete = platform_on_init_complete,
    .on_device_discovered = platform_on_device_discovered,
    .on_device_connected = platform_on_device_connected,
    .on_device_disconnected = platform_on_device_disconnected,
    .on_device_ready = platform_on_device_ready,
    .on_controller_data = platform_on_controller_data,
    .get_property = platform_get_property,
    .on_oob_event = platform_on_oob_event,
};

static void bluepad_task(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "Starting Bluepad32 / BTstack backend");

    // Do NOT initialize BTstack stdio/UART console.
    // ESP-IDF already owns the UART driver.

    btstack_init();
    uni_platform_set_custom(&s_platform);
    uni_init(0, NULL);

    ESP_LOGI(TAG, "Entering BTstack run loop");
    btstack_run_loop_execute();

    ESP_LOGE(TAG, "BTstack run loop returned unexpectedly");
    vTaskDelete(NULL);
}

esp_err_t ps4_init(const uint8_t *host_mac, ps4_callback_t callback) {
    (void)host_mac;

    ESP_LOGI(TAG, "Initializing Bluepad32 gamepad backend");

    s_callback = callback;

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_started) {
        ESP_LOGW(TAG, "Bluepad32 backend already started");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        bluepad_task,
        "bluepad32",
        12288,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Bluepad32 task");
        return ESP_FAIL;
    }

    s_started = true;
    ESP_LOGI(TAG, "Bluepad32 backend task started");

    return ESP_OK;
}

bool ps4_is_connected(void) {
    if (!s_mutex) {
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool connected = s_state.connected;
    xSemaphoreGive(s_mutex);

    return connected;
}
