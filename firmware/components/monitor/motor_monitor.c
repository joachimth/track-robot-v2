/**
 * @file motor_monitor.c
 * @brief Motor current-sense + battery voltage monitoring implementation
 *
 * Uses the ESP-IDF oneshot ADC driver (esp_adc) on ADC unit 1 only. ADC2 is
 * NOT used because it is unavailable while Wi-Fi is active, and Wi-Fi is always
 * on in this firmware (HTTP controller AP). All current-sense pins (GPIO
 * 34/35/36/39) and any battery-divider pin must therefore live on ADC1.
 */

#include "motor_monitor.h"
#include "motor_bts7960.h"
#include "safety_failsafe.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "monitor";

// Sampling cadence
#define MONITOR_LOOP_MS        100   // current sampled at 10 Hz
#define MONITOR_BATTERY_EVERY  10    // battery sampled once per 10 loops (1 Hz)

// ADC full-scale (mV) used only as a fallback if hardware calibration fails.
#define ADC_FALLBACK_FS_MV     3100
#define ADC_FALLBACK_FS_RAW    4095

// Battery divider must be configured; provide safe fallbacks so the file always
// compiles even if the Kconfig section is regenerated without these symbols.
#ifndef CONFIG_ROBOT_BATTERY_ADC_PIN
#define CONFIG_ROBOT_BATTERY_ADC_PIN -1
#endif

typedef struct {
    int             gpio;     // configured GPIO, < 0 if unused
    adc_channel_t   channel;  // resolved ADC1 channel
    bool            valid;    // true if channel resolved on ADC1
} adc_input_t;

// Current-sense inputs: [0]=left R_IS, [1]=left L_IS, [2]=right R_IS, [3]=right L_IS
static adc_input_t is_inputs[4];
static adc_input_t batt_input;

static adc_oneshot_unit_handle_t adc1 = NULL;
static adc_cali_handle_t cali = NULL;
static bool cali_enabled = false;
static bool initialized = false;

static SemaphoreHandle_t status_mutex = NULL;
static monitor_status_t latest;  // protected by status_mutex

// ---------------------------------------------------------------------------

static int raw_to_mv(int raw) {
    if (cali_enabled) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }
    return raw * ADC_FALLBACK_FS_MV / ADC_FALLBACK_FS_RAW;
}

// Configure one ADC1 input from a GPIO. Leaves valid=false on any failure or if
// the pin resolves to a unit other than ADC1.
static void setup_adc_input(adc_input_t *in, int gpio, const char *what) {
    in->gpio = gpio;
    in->valid = false;
    if (gpio < 0) {
        return;
    }

    adc_unit_t unit;
    adc_channel_t channel;
    esp_err_t ret = adc_oneshot_io_to_channel(gpio, &unit, &channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s: GPIO %d is not an ADC pin (%s)", what, gpio, esp_err_to_name(ret));
        return;
    }
    if (unit != ADC_UNIT_1) {
        ESP_LOGW(TAG, "%s: GPIO %d is on ADC2 — unusable with Wi-Fi active, skipping",
                 what, gpio);
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(adc1, channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s: failed to configure GPIO %d: %s", what, gpio, esp_err_to_name(ret));
        return;
    }

    in->channel = channel;
    in->valid = true;
    ESP_LOGI(TAG, "%s: GPIO %d -> ADC1 channel %d", what, gpio, (int)channel);
}

// Read a configured input and return its voltage in mV (0 if not valid).
static int read_input_mv(const adc_input_t *in) {
    if (!in->valid) {
        return 0;
    }
    int raw = 0;
    if (adc_oneshot_read(adc1, in->channel, &raw) != ESP_OK) {
        return 0;
    }
    return raw_to_mv(raw);
}

static uint32_t mv_to_ma(int mv) {
    if (CONFIG_ROBOT_CURRENT_MV_PER_A <= 0) {
        return 0;
    }
    return (uint32_t)((int64_t)mv * 1000 / CONFIG_ROBOT_CURRENT_MV_PER_A);
}

// ---------------------------------------------------------------------------

static void monitor_task(void *arg) {
    uint32_t loop = 0;
    bool overcurrent_latched = false;

    while (1) {
        // --- Current sense (10 Hz) ---
        uint32_t left_r  = mv_to_ma(read_input_mv(&is_inputs[0]));
        uint32_t left_l  = mv_to_ma(read_input_mv(&is_inputs[1]));
        uint32_t right_r = mv_to_ma(read_input_mv(&is_inputs[2]));
        uint32_t right_l = mv_to_ma(read_input_mv(&is_inputs[3]));

        uint32_t left_ma  = (left_r  > left_l)  ? left_r  : left_l;
        uint32_t right_ma = (right_r > right_l) ? right_r : right_l;

        bool over = (left_ma > (uint32_t)CONFIG_ROBOT_OVERCURRENT_MA) ||
                    (right_ma > (uint32_t)CONFIG_ROBOT_OVERCURRENT_MA);

        if (over && !overcurrent_latched) {
            overcurrent_latched = true;
            ESP_LOGE(TAG, "!!! OVERCURRENT !!! left=%lu mA right=%lu mA (limit %d mA)",
                     (unsigned long)left_ma, (unsigned long)right_ma,
                     CONFIG_ROBOT_OVERCURRENT_MA);
            motor_emergency_stop();
            safety_emergency_stop();
        } else if (!over) {
            overcurrent_latched = false;
        }

        // --- Battery (1 Hz) ---
        uint32_t battery_mv = 0;
        bool battery_low = false;
        if (batt_input.valid && (loop % MONITOR_BATTERY_EVERY == 0)) {
            int adc_mv = read_input_mv(&batt_input);
            // Undo the resistor divider: Vbatt = Vadc * (R1 + R2) / R2
            battery_mv = (uint32_t)((int64_t)adc_mv *
                         (CONFIG_ROBOT_BATTERY_R1_OHMS + CONFIG_ROBOT_BATTERY_R2_OHMS) /
                         CONFIG_ROBOT_BATTERY_R2_OHMS);
            battery_low = battery_mv < (uint32_t)CONFIG_ROBOT_BATTERY_LOW_MV;
            if (battery_low) {
                ESP_LOGW(TAG, "Battery LOW: %lu mV (threshold %d mV)",
                         (unsigned long)battery_mv, CONFIG_ROBOT_BATTERY_LOW_MV);
            }
        }

        // --- Publish ---
        if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            latest.left_ma = left_ma;
            latest.right_ma = right_ma;
            latest.overcurrent = over;
            if (batt_input.valid && (loop % MONITOR_BATTERY_EVERY == 0)) {
                latest.battery_mv = battery_mv;
                latest.battery_low = battery_low;
            }
            xSemaphoreGive(status_mutex);
        }

        loop++;
        vTaskDelay(pdMS_TO_TICKS(MONITOR_LOOP_MS));
    }
}

// ---------------------------------------------------------------------------

esp_err_t motor_monitor_init(void) {
    if (initialized) {
        return ESP_OK;
    }

    status_mutex = xSemaphoreCreateMutex();
    if (status_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create status mutex");
        return ESP_ERR_NO_MEM;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &adc1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC1 unit: %s", esp_err_to_name(ret));
        return ret;
    }

    // Try to set up hardware calibration (line fitting on ESP32).
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali) == ESP_OK) {
        cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration enabled (line fitting)");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable — using approximate scaling");
    }

    setup_adc_input(&is_inputs[0], CONFIG_ROBOT_MOTOR_LEFT_RIS,  "left R_IS");
    setup_adc_input(&is_inputs[1], CONFIG_ROBOT_MOTOR_LEFT_LIS,  "left L_IS");
    setup_adc_input(&is_inputs[2], CONFIG_ROBOT_MOTOR_RIGHT_RIS, "right R_IS");
    setup_adc_input(&is_inputs[3], CONFIG_ROBOT_MOTOR_RIGHT_LIS, "right L_IS");
    setup_adc_input(&batt_input,   CONFIG_ROBOT_BATTERY_ADC_PIN, "battery");

    bool any_current = is_inputs[0].valid || is_inputs[1].valid ||
                       is_inputs[2].valid || is_inputs[3].valid;
    if (!any_current && !batt_input.valid) {
        ESP_LOGW(TAG, "No usable ADC inputs configured — monitor inactive");
        return ESP_ERR_NOT_FOUND;
    }

    BaseType_t task_ret = xTaskCreate(monitor_task, "monitor", 3072, NULL, 4, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return ESP_FAIL;
    }

    initialized = true;
    ESP_LOGI(TAG, "Monitor initialized: overcurrent=%d mA, battery=%s (low=%d mV)",
             CONFIG_ROBOT_OVERCURRENT_MA,
             batt_input.valid ? "enabled" : "disabled",
             CONFIG_ROBOT_BATTERY_LOW_MV);
    return ESP_OK;
}

void motor_monitor_get_status(monitor_status_t *out) {
    if (out == NULL) {
        return;
    }
    if (!initialized || status_mutex == NULL) {
        monitor_status_t zero = {0};
        zero.battery_enabled = false;
        *out = zero;
        return;
    }
    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = latest;
        out->battery_enabled = batt_input.valid;
        xSemaphoreGive(status_mutex);
    } else {
        monitor_status_t zero = {0};
        *out = zero;
    }
}
