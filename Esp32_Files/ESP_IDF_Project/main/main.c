#include <stdio.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/semphr.h"
#include <stddef.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <math.h>
#include "Mini2.h"

#define TAG "MAIN"

#define SSID "BCOTI"
#define PASSWORD "wifipass1234"
#define PRESET_COUNT 5

#define UART_TX GPIO_NUM_1
#define UART_RX GPIO_NUM_2
#define POTI_PIN GPIO_NUM_4
#define MULTI_BTN GPIO_NUM_8

volatile int64_t button_debouce;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t debug_html_start[] asm("_binary_debug_html_start");
extern const uint8_t debug_html_end[]   asm("_binary_debug_html_end");

static esp_netif_t *sta_netif = NULL;

nvs_handle_t flash_handle;
static SemaphoreHandle_t config_mutex;
static TaskHandle_t control_task;
static bool nvs_writable;
static bool settings_saved;
static bool camera_tx_ok;
static bool camera_reply;
static bool flip_verified;

int8_t brightness_button_count = 0;

Mini2_t cam = {
    .uart_port = UART_NUM_1, // C3 only has num0 and num1, and num0 is used for debug / USB_CDC
    .uart_tx = UART_TX,
    .uart_rx = UART_RX,
    .variant = Mini2_256,
};

typedef struct stored_values_t{
    uint8_t active_preset;
    alignment_preset_t alignment;
    value_preset_t presets[PRESET_COUNT];
    bool first_boot;
} stored_values_t;

// Preserve the V2_Beta blob ABI for app-only upgrades and rollback.
_Static_assert(sizeof(stored_values_t) == 132, "NVS layout changed");
_Static_assert(offsetof(stored_values_t, alignment) == 4, "NVS alignment moved");
_Static_assert(offsetof(stored_values_t, presets) == 28, "NVS presets moved");

value_preset_t default_presets[] = {
    {
        .preset_en = true,
        .pseudo_color = WHOT,
        .scene_mode = Highlight,
        .contrast = 100,
        .edge_enhancment_gear = 1,
        .detail_enhancement_gear = 55,
        .burn_protection_en = true,
        .auto_shutter_en = true,
        .breathing = false
    },
    {
        .preset_en = true,
        .pseudo_color = WHOT,
        .scene_mode = HighContrast,
        .contrast = 100,
        .edge_enhancment_gear = 1,
        .detail_enhancement_gear = 55,
        .burn_protection_en = true,
        .auto_shutter_en = true,
        .breathing = false
    },
    {
        .preset_en = true,
        .pseudo_color = WHOT,
        .scene_mode = Outline,
        .contrast = 90,
        .edge_enhancment_gear = 1,
        .detail_enhancement_gear = 50,
        .burn_protection_en = true,
        .auto_shutter_en = true,
        .breathing = false
    }
};

value_preset_t base_preset = {
    .preset_en = false,
    .pseudo_color = WHOT,
    .scene_mode = Outline,
    .contrast = 50,
    .edge_enhancment_gear = 1,
    .detail_enhancement_gear = 50,
    .burn_protection_en = true,
    .auto_shutter_en = true,
    .breathing = false
};

stored_values_t stored = {
    .active_preset = 0,
    .alignment = {
        .zoom = 11,
        .zoom_x = 128,
        .zoom_y = 96,
        .av_format = PAL,
        .flip_mode = No_Flip,
        .fps = Hz50,
        .refresh_flip_mode = false,
    },
    .first_boot = true
};

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

// Match the original WebUI crop limits (including its rounding), without
// silently increasing zoom. At 1.1x: X=116..140, Y=87..105.
static void alignment_limits(uint8_t zoom, int *xmin, int *xmax, int *ymin, int *ymax) {
    int xr = (int)ceilf((256.0f - 2560.0f / zoom) / 2.0f);
    int yr = (int)ceilf((192.0f - 1920.0f / zoom) / 2.0f);
    *xmin = 128 - xr; *xmax = 128 + xr;
    *ymin = 96 - yr; *ymax = 96 + yr;
}

static bool valid_settings(const stored_values_t *s) {
    const alignment_preset_t *a = &s->alignment;
    if (s->active_preset >= PRESET_COUNT || a->zoom < 10 || a->zoom > 80 ||
        a->zoom_x > 256 || a->zoom_y > 192 ||
        a->flip_mode < No_Flip || a->flip_mode > XY_Flip ||
        (a->av_format != PAL && a->av_format != NTSC) ||
        (a->fps != Hz25 && a->fps != Hz50)) return false;
    for (int i = 0; i < PRESET_COUNT; ++i) {
        const value_preset_t *p = &s->presets[i];
        if ((p->pseudo_color != WHOT && p->pseudo_color != BHOT) ||
            !((p->scene_mode >= LowHighlight && p->scene_mode <= Highlight) || p->scene_mode == Outline) ||
            p->brightness > 100 || p->contrast > 100 ||
            p->edge_enhancment_gear > 2 || p->detail_enhancement_gear > 100) return false;
    }
    return true;
}

// All callers hold config_mutex; the ISR never changes configuration.
static esp_err_t save_settings(void) {
    settings_saved = false;
    if (!nvs_writable) {
        ESP_LOGE(TAG, "NVS unavailable/incompatible; existing flash preserved");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_blob(flash_handle, "stored_values", &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(flash_handle);
    if (err == ESP_OK) {
        stored_values_t check;
        size_t len = sizeof(check);
        err = nvs_get_blob(flash_handle, "stored_values", &check, &len);
        if (err == ESP_OK && (len != sizeof(check) || memcmp(&check, &stored, len) != 0))
            err = ESP_FAIL;
    }
    settings_saved = err == ESP_OK;
    if (settings_saved) {
        ESP_LOGI(TAG, "NVS committed/read back: preset=%u X=%u Y=%u zoom=%u flip=%d",
                 stored.active_preset, stored.alignment.zoom_x, stored.alignment.zoom_y,
                 stored.alignment.zoom, stored.alignment.flip_mode);
    } else {
        ESP_LOGE(TAG, "NVS write/commit/readback failed: %s; do not assume saved", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t apply_settings(const stored_values_t *s, bool alignment_only, bool startup) {
    esp_err_t err = ESP_FAIL;
    camera_tx_ok = false;
    flip_verified = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = alignment_only ? Mini2_apply_alignment(&cam, &s->alignment) :
              Mini2_apply_preset(&cam, &s->presets[s->active_preset], &s->alignment, !startup);
        if (err == ESP_OK) {
            enum FlipMode actual;
            esp_err_t read_err = Mini2_get_flip_mode(&cam, &actual);
            camera_reply = read_err == ESP_OK;
            if (camera_reply) {
                flip_verified = actual == s->alignment.flip_mode;
                if (!flip_verified) err = ESP_FAIL;
            } else {
                ESP_LOGW(TAG, "Flip readback unavailable (%s); TX only, image must be checked",
                         esp_err_to_name(read_err));
            }
        }
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "Camera apply attempt %d/3 failed: %s", attempt + 1, esp_err_to_name(err));
        if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(300));
    }
    camera_tx_ok = err == ESP_OK;
    ESP_LOGI(TAG, "Camera status: TX=%d reply=%d flip_verified=%d; crop requires optical check",
             camera_tx_ok, camera_reply, flip_verified);
    return err;
}

static void camera_wait_ready(void) {
    enum FlipMode mode;
    // app_main already waited 5 seconds for camera power-up. Bound further waits.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (Mini2_get_flip_mode(&cam, &mode) == ESP_OK) {
            camera_reply = true;
            ESP_LOGI(TAG, "Camera replied to readiness query");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGW(TAG, "No readiness reply; using timed startup and bounded alignment sends");
}

void next_preset() { // Preset0 is always available.
    stored.active_preset = (stored.active_preset + 1) % PRESET_COUNT;
    if (stored.active_preset != 0 && !stored.presets[stored.active_preset].preset_en) {
        next_preset();
    }
}

void IRAM_ATTR button_isr_handler(void* arg) {
    if (esp_timer_get_time() - button_debouce > 200000) { // 200ms debounce
        button_debouce = esp_timer_get_time();
        BaseType_t wake = pdFALSE;
        if (control_task) vTaskNotifyGiveFromISR(control_task, &wake);
        if (wake) portYIELD_FROM_ISR();
    };
}

static void loop_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000));

    int max_gain = 0;
    int64_t breathing_time_out = 0;
    #define breathing_pause_time_us (2 * 1000 * 1000)

    adc_unit_t unit;
    adc_channel_t channel;
    esp_err_t err = adc_oneshot_io_to_channel(POTI_PIN, &unit, &channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get ADC info about GPIO%d", (int)POTI_PIN);
    }

    int adc_raw;
    int last_adc_val = 0;
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel, &config));

    int64_t last_flip_check = esp_timer_get_time();
    #define FLIP_CHECK_INTERVAL (5 * 1000 * 1000)

    while (true) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
        uint32_t presses = ulTaskNotifyTake(pdTRUE, 0);
        if (presses) {
            stored_values_t previous = stored;
            // A burst is bounded; skip disabled presets exactly as before.
            for (uint32_t i = 0; i < presses && i < 20; ++i) next_preset();
            if (apply_settings(&stored, false, false) == ESP_OK) {
                save_settings();
            } else {
                stored = previous;
                apply_settings(&stored, false, false);
            }
        }
        if (adc_oneshot_read(adc_handle, channel, &adc_raw) != ESP_OK) adc_raw = last_adc_val;
        if (abs(adc_raw - last_adc_val) >= 100) {
            last_adc_val = adc_raw;
            float new_brightness = ((float)adc_raw / 4095.0) * 100.0;
            max_gain = (int)new_brightness;
            Mini2_set_brightness(&cam, max_gain);
            breathing_time_out = esp_timer_get_time();
        }
        if (stored.presets[stored.active_preset].breathing && (esp_timer_get_time() - breathing_time_out) >= breathing_pause_time_us) {
            int x_ms = (esp_timer_get_time() - breathing_time_out - breathing_pause_time_us) / 1000;
            float wave_value = (cos(((float)x_ms / 1000.0) * M_PI) + 1.0) / 2.0;
            Mini2_set_brightness(&cam, (int)(wave_value * (float)max_gain));
        }
        if (stored.alignment.refresh_flip_mode && stored.alignment.flip_mode != No_Flip &&
            esp_timer_get_time() - last_flip_check > FLIP_CHECK_INTERVAL) {
            last_flip_check = esp_timer_get_time();

            esp_err_t flip_err = Mini2_set_flip_mode(&cam, stored.alignment.flip_mode);
            if (flip_err != ESP_OK) ESP_LOGW(TAG, "Flip refresh failed: %s", esp_err_to_name(flip_err));
        }
        xSemaphoreGive(config_mutex);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static esp_err_t json_response(httpd_req_t *req, const char *status, const char *message) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char reply[256];
    snprintf(reply, sizeof(reply), "{\"saved\":%s,\"camera_tx_ok\":%s,\"message\":\"%s\"}",
             settings_saved ? "true" : "false", camera_tx_ok ? "true" : "false", message);
    return httpd_resp_sendstr(req, reply);
}

static bool json_integer(const cJSON *item, int min, int max) {
    return cJSON_IsNumber(item) && item->valuedouble >= min &&
           item->valuedouble <= max && item->valuedouble == item->valueint;
}

static esp_err_t repair_camera(void) {
    // Explicit legacy debug action only; never part of normal startup.
    esp_err_t err = Mini2_restore_factory_parameters(&cam);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(3000));
    err = Mini2_set_digital_video_format(&cam, true, UsbProgressive, cam.variant.high_fps);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(3000));
    err = Mini2_set_analog_video_format(&cam, stored.alignment.av_format);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(3000));
    err = Mini2_set_detector_fps(&cam, stored.alignment.fps);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(3000));
    err = Mini2_save_video(&cam);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(3000));
    return apply_settings(&stored, false, true);
}

static esp_err_t post_handler(httpd_req_t *req) {
    // One setting per request, as used by both existing pages. Read fragmented
    // bodies completely and reject invalid input before touching camera or NVS.
    if (req->content_len == 0 || req->content_len > 512)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body length");
    char buf[513];
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Incomplete request");
        received += n;
    }
    buf[received] = 0;
    cJSON *root = cJSON_ParseWithLengthOpts(buf, received + 1, NULL, true);
    if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 1) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected one setting");
    }

    xSemaphoreTake(config_mutex, portMAX_DELAY);
    stored_values_t candidate = stored;
    value_preset_t *preset = &candidate.presets[candidate.active_preset];
    cJSON *item = root->child;
    const char *key = item->string;
    bool valid = true, known = false, alignment_only = false, action = false;
    bool video_change = false;
    esp_err_t err = ESP_OK;

#define INT_SETTING(name, field, minimum, maximum) \
    if (strcmp(key, name) == 0) { \
        known = true; valid = json_integer(item, minimum, maximum); \
        if (valid) field = item->valueint; \
    }
#define BOOL_SETTING(name, field) \
    if (strcmp(key, name) == 0) { \
        known = true; valid = cJSON_IsBool(item) || json_integer(item, 0, 1); \
        if (valid) field = cJSON_IsTrue(item) || item->valueint == 1; \
    }
    INT_SETTING("active_profile", candidate.active_preset, 0, PRESET_COUNT - 1);
    INT_SETTING("pseudo_color", preset->pseudo_color, 0, 9);
    INT_SETTING("scene_mode", preset->scene_mode, 0, 9);
    INT_SETTING("contrast", preset->contrast, 0, 100);
    INT_SETTING("edge_enhancment_gear", preset->edge_enhancment_gear, 0, 2);
    INT_SETTING("detail_enhancement_gear", preset->detail_enhancement_gear, 0, 100);
    INT_SETTING("flip_mode", candidate.alignment.flip_mode, 0, 3);
    INT_SETTING("av_format", candidate.alignment.av_format, 0, 1);
    BOOL_SETTING("burn_protection_en", preset->burn_protection_en);
    BOOL_SETTING("auto_shutter_en", preset->auto_shutter_en);
    BOOL_SETTING("breathing", preset->breathing);
    BOOL_SETTING("preset_en", preset->preset_en);
    BOOL_SETTING("refresh_flip_mode", candidate.alignment.refresh_flip_mode);
#undef INT_SETTING
#undef BOOL_SETTING

    alignment_only = strcmp(key, "flip_mode") == 0;
    video_change = strcmp(key, "av_format") == 0;
    if (strcmp(key, "restore") == 0) {
        // Portable v1 backup: alignment + image presets only. Never import video
        // timing, repair commands, internal camera saves or raw C structure bytes.
        known = true;
        valid = cJSON_IsArray(item) && cJSON_GetArraySize(item) == 6 + PRESET_COUNT * 10;
        int values[6 + PRESET_COUNT * 10] = {0};
        for (int i = 0; valid && i < 6 + PRESET_COUNT * 10; ++i) {
            cJSON *v = cJSON_GetArrayItem(item, i);
            valid = json_integer(v, 0, 256);
            if (valid) values[i] = v->valueint;
        }
        valid = valid && values[0] == 1 && values[1] < PRESET_COUNT && values[2] <= 3 &&
                values[3] >= 10 && values[3] <= 80;
        if (valid) {
            int xmin, xmax, ymin, ymax;
            alignment_limits(values[3], &xmin, &xmax, &ymin, &ymax);
            valid = values[4] >= xmin && values[4] <= xmax &&
                    values[5] >= ymin && values[5] <= ymax;
        }
        if (valid) {
            candidate.active_preset = values[1];
            candidate.alignment.flip_mode = values[2];
            candidate.alignment.zoom = values[3];
            candidate.alignment.zoom_x = values[4];
            candidate.alignment.zoom_y = values[5];
            for (int i = 0; i < PRESET_COUNT; ++i) {
                int *v = &values[6 + i * 10];
                if (v[0] > 1 || v[3] > 100 || v[4] > 100 || v[5] > 2 ||
                    v[6] > 100 || v[7] > 1 || v[8] > 1 || v[9] > 1) valid = false;
                candidate.presets[i] = (value_preset_t){
                    .preset_en = v[0] == 1, .pseudo_color = v[1], .scene_mode = v[2],
                    .brightness = v[3], .contrast = v[4], .edge_enhancment_gear = v[5],
                    .detail_enhancement_gear = v[6], .burn_protection_en = v[7] == 1,
                    .auto_shutter_en = v[8] == 1, .breathing = v[9] == 1};
            }
        }
    }
    if (strcmp(key, "detail_preset") == 0) {
        known = true;
        valid = json_integer(item, 2, PRESET_COUNT - 1);
        if (valid) valid = !candidate.presets[item->valueint].preset_en;
        if (valid) {
            candidate.active_preset = item->valueint;
            candidate.presets[item->valueint] = (value_preset_t){
                .preset_en = false, .pseudo_color = WHOT, .scene_mode = GeneralMode,
                .brightness = 0, .contrast = 50, .edge_enhancment_gear = 0,
                .detail_enhancement_gear = 55, .burn_protection_en = true,
                .auto_shutter_en = true, .breathing = false};
        }
    }
    if (strcmp(key, "reapply") == 0) {
        known = true;
        valid = cJSON_IsTrue(item) || json_integer(item, 1, 1);
        video_change = true; // Also repeat the boot-time video-format check.
    }
    if (strcmp(key, "zoom") == 0) {
        known = true;
        alignment_only = true;
        cJSON *x = cJSON_GetObjectItemCaseSensitive(item, "x");
        cJSON *y = cJSON_GetObjectItemCaseSensitive(item, "y");
        cJSON *zoom = cJSON_GetObjectItemCaseSensitive(item, "zoom");
        valid = cJSON_IsObject(item) && cJSON_GetArraySize(item) == 3 &&
                json_integer(zoom, 10, 80) && json_integer(x, 0, 256) && json_integer(y, 0, 192);
        if (valid) {
            int xmin, xmax, ymin, ymax;
            alignment_limits(zoom->valueint, &xmin, &xmax, &ymin, &ymax);
            valid = x->valueint >= xmin && x->valueint <= xmax &&
                    y->valueint >= ymin && y->valueint <= ymax;
            if (valid) {
                candidate.alignment.zoom = zoom->valueint;
                candidate.alignment.zoom_x = x->valueint;
                candidate.alignment.zoom_y = y->valueint;
                candidate.alignment.flip_mode = No_Flip;
            }
        }
    }
    if (strcmp(key, "NUC") == 0 || strcmp(key, "BG") == 0 ||
        strcmp(key, "parameters_save") == 0 || strcmp(key, "resend") == 0) {
        known = action = true;
        valid = cJSON_IsTrue(item) || (json_integer(item, 1, 1));
        if (valid) {
            if (strcmp(key, "NUC") == 0) err = Mini2_NUC(&cam);
            if (strcmp(key, "BG") == 0) err = Mini2_Background_Correction(&cam);
            if (strcmp(key, "parameters_save") == 0) err = Mini2_parameters_save(&cam);
            if (strcmp(key, "resend") == 0) err = repair_camera();
        }
    }
    candidate.presets[0].preset_en = true;
    const char *status = "200 OK";
    const char *message = "Saved; commands transmitted. Check overlay visually.";
    if (!known || !valid || !valid_settings(&candidate)) {
        status = "400 Bad Request";
        message = "Invalid setting or alignment limit reached; no change saved.";
    } else if (action) {
        status = err == ESP_OK ? "200 OK" : "502 Bad Gateway";
        message = err == ESP_OK ? "Camera command transmitted." : "Camera command failed.";
    } else {
        if (strcmp(key, "av_format") == 0) {
            // An explicit video-format edit is allowed even without readback.
            err = Mini2_set_analog_video_format(&cam, candidate.alignment.av_format);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(3000));
        }
        // These fields are consumed only by the ESP32 button/brightness loop.
        // Do not disturb camera crop for a metadata-only change when already healthy.
        bool metadata_only = strcmp(key, "preset_en") == 0 || strcmp(key, "breathing") == 0;
        bool unchanged = memcmp(&candidate, &stored, sizeof(stored)) == 0;
        if (err == ESP_OK && !(camera_tx_ok && (metadata_only ||
            (unchanged && strcmp(key, "reapply") != 0 && !video_change))))
            err = apply_settings(&candidate, alignment_only, video_change);
        if (err != ESP_OK) {
            // Recover the last requested configuration after a partial command sequence.
            apply_settings(&stored, false, video_change);
            status = "502 Bad Gateway";
            message = "Camera update failed; previous settings retained. Check image.";
        } else {
            candidate.first_boot = false;
            bool changed = memcmp(&candidate, &stored, sizeof(stored)) != 0;
            stored = candidate;
            if ((changed || !settings_saved) && save_settings() != ESP_OK) {
                status = "500 Internal Server Error";
                message = "Camera commands sent but SAVE FAILED. Keep power on and retry.";
            }
        }
    }
    cJSON_Delete(root);
    esp_err_t result = json_response(req, status, message);
    xSemaphoreGive(config_mutex);
    return result;
}

static esp_err_t retireve_values(httpd_req_t *req) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    static char out_format[] = "{ \
    \"preset_count\": %u, \
    \"active_profile\": %u, \
    \"preset_en\": %u, \
    \"pseudo_color\": %u, \
    \"scene_mode\": %u, \
    \"contrast\": %u, \
    \"edge_enhancment_gear\": %u, \
    \"detail_enhancement_gear\": %u, \
    \"burn_protection_en\": %u, \
    \"auto_shutter_en\": %u, \
    \"breathing\": %u, \
    \"flip_mode\": %u, \
    \"zoom\": %u, \
    \"zoom_x\": %u, \
    \"zoom_y\": %u, \
    \"av_format\": %u, \
    \"sensor_width\": %u, \
    \"sensor_height\": %u, \
    \"refresh_flip_mode\": %u, \
    \"settings_saved\": %u, \
    \"camera_tx_ok\": %u, \
    \"camera_reply\": %u, \
    \"flip_verified\": %u, \
    \"nvs_writable\": %u \
    }";

    char out_json[1536];

    int res = snprintf(out_json, sizeof(out_json), out_format,
        PRESET_COUNT,
        stored.active_preset,
        (uint8_t)stored.presets[stored.active_preset].preset_en,
        (uint8_t)stored.presets[stored.active_preset].pseudo_color,
        (uint8_t)stored.presets[stored.active_preset].scene_mode,
        stored.presets[stored.active_preset].contrast,
        stored.presets[stored.active_preset].edge_enhancment_gear,
        stored.presets[stored.active_preset].detail_enhancement_gear,
        (uint8_t)stored.presets[stored.active_preset].burn_protection_en,
        (uint8_t)stored.presets[stored.active_preset].auto_shutter_en,
        (uint8_t)stored.presets[stored.active_preset].breathing,
        (uint8_t)stored.alignment.flip_mode,
        stored.alignment.zoom,
        stored.alignment.zoom_x,
        stored.alignment.zoom_y,
        stored.alignment.av_format,
        cam.variant.sensor_width,
        cam.variant.sensor_height,
        stored.alignment.refresh_flip_mode,
        settings_saved, camera_tx_ok, camera_reply, flip_verified, nvs_writable
    );

    // Append a compact, versioned backup while holding the same snapshot lock.
    if (res > 0 && res < sizeof(out_json)) {
        out_json[--res] = 0; // Replace the closing object brace.
        res += snprintf(out_json + res, sizeof(out_json) - res,
            ",\"backup\":[1,%u,%u,%u,%u,%u", stored.active_preset,
            stored.alignment.flip_mode, stored.alignment.zoom,
            stored.alignment.zoom_x, stored.alignment.zoom_y);
        for (int i = 0; i < PRESET_COUNT && res < sizeof(out_json); ++i) {
            const value_preset_t *p = &stored.presets[i];
            res += snprintf(out_json + res, sizeof(out_json) - res,
                ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", p->preset_en,
                p->pseudo_color, p->scene_mode, p->brightness, p->contrast,
                p->edge_enhancment_gear, p->detail_enhancement_gear,
                p->burn_protection_en, p->auto_shutter_en, p->breathing);
        }
        if (res < sizeof(out_json))
            res += snprintf(out_json + res, sizeof(out_json) - res, "]}");
    }
    xSemaphoreGive(config_mutex);
    if (res <= 0 || res >= sizeof(out_json)) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, out_json, res);
}

static const httpd_uri_t retireve_values_route = {
    .uri       = "/get",
    .method    = HTTP_GET,
    .handler   = retireve_values,
    .user_ctx  = NULL
};

static const httpd_uri_t echo = {
    .uri       = "/set",
    .method    = HTTP_POST,
    .handler   = post_handler,
    .user_ctx  = NULL
};

esp_err_t debug_get_handler(httpd_req_t *req) {
    const uint32_t html_size = debug_html_end - debug_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)debug_html_start, html_size);
    return ESP_OK;
}

httpd_uri_t debug_uri = {
    .uri      = "/debug",
    .method   = HTTP_GET,
    .handler  = debug_get_handler,
    .user_ctx = NULL
};

esp_err_t index_get_handler(httpd_req_t *req) {
    const uint32_t html_size = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, html_size);
    return ESP_OK;
}

httpd_uri_t index_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = index_get_handler,
    .user_ctx = NULL
};

void app_main(void) {
    ESP_LOGI(TAG, "BCOTI personal alignment build 3; legacy NVS layout=%u", (unsigned)sizeof(stored_values_t));
    config_mutex = xSemaphoreCreateMutex();
    configASSERT(config_mutex);
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) ESP_LOGE(TAG, "NVS init: %s; NOT erasing flash", esp_err_to_name(nvs_err));

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << MULTI_BTN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    for (int i=0; i<PRESET_COUNT; i++) {
        if (i < (sizeof(default_presets) / sizeof(value_preset_t))) {
            stored.presets[i] = default_presets[i];
        } else {
            stored.presets[i] = base_preset;
        }
    }

    if (nvs_err == ESP_OK) nvs_err = nvs_open("storage", NVS_READWRITE, &flash_handle);
    nvs_writable = nvs_err == ESP_OK;
    if (nvs_writable) {
        stored_values_t loaded;
        size_t len = sizeof(loaded);
        esp_err_t err = nvs_get_blob(flash_handle, "stored_values", &loaded, &len);
        if (err == ESP_OK && len == sizeof(loaded) && valid_settings(&loaded)) {
            stored = loaded;
            settings_saved = true;
            ESP_LOGI(TAG, "NVS restored: preset=%u X=%u Y=%u zoom=%u flip=%d",
                     stored.active_preset, stored.alignment.zoom_x, stored.alignment.zoom_y,
                     stored.alignment.zoom, stored.alignment.flip_mode);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "No saved configuration; defaults in RAM until first successful edit");
        } else {
            nvs_writable = false;
            ESP_LOGE(TAG, "NVS blob invalid/incompatible: %s size=%u; preserving original, defaults in RAM",
                     esp_err_to_name(err), (unsigned)len);
        }
    } else {
        ESP_LOGE(TAG, "NVS unavailable: %s; persistence disabled", esp_err_to_name(nvs_err));
    }

    bool wifi_en = (gpio_get_level(MULTI_BTN) == 0) || (stored.first_boot);
    ESP_LOGI(TAG, "Wifi: %d, stored: %d", (int)wifi_en, stored.first_boot);

    if (wifi_en) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        sta_netif = esp_netif_create_default_wifi_ap();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        cfg.nvs_enable = false; // AP configuration is supplied below on every Wi-Fi boot.
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
        wifi_config_t wifi_config = {
            .ap = {
                .ssid = SSID,
                .ssid_len = strlen(SSID),
                .password = PASSWORD,
                .channel = 1,
                .max_connection = 4,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .pmf_cfg = {
                    .required = true,
                },
            }
        };
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config) );
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    vTaskDelay(pdMS_TO_TICKS(5000));

    Mini2_init(&cam);
    camera_wait_ready();
    if (apply_settings(&stored, false, true) != ESP_OK)
        ESP_LOGE(TAG, "Startup camera apply failed; saved NVS unchanged; retry via WebUI");

    BaseType_t task_ok = xTaskCreate(loop_task, "loop task", 16384, NULL, 5, &control_task);
    configASSERT(task_ok == pdPASS);

    if (wifi_en) {
        httpd_handle_t server = NULL;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.stack_size = 8192;

        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server, &config) == ESP_OK) {
            ESP_LOGI(TAG, "Registering URI handlers");
            httpd_register_uri_handler(server, &echo);
            httpd_register_uri_handler(server, &index_uri);
            httpd_register_uri_handler(server, &debug_uri);
            httpd_register_uri_handler(server, &retireve_values_route);
        }
    }

    button_debouce = esp_timer_get_time();
    gpio_install_isr_service(0);

    gpio_isr_handler_add(MULTI_BTN, button_isr_handler, NULL);
}
