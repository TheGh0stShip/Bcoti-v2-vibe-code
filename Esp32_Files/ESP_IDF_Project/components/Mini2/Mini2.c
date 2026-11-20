#include "Mini2.h"
#include "freertos/task.h"

uint16_t crc16_xmodem(uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (int i=0; i<len; i++) {
        crc ^= (data[i] << 8);
        for (int j=0; j<8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void Mini2_init(Mini2_t* cam) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(cam->uart_port, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(cam->uart_port, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(cam->uart_port, cam->uart_tx, cam->uart_rx, -1, -1));
}

esp_err_t Mini2_write_command(Mini2_t* cam, uint8_t* cmd, size_t len) {
    uint8_t full_cmd[] = {0x55, 0x43, 0x49, 0x12, 0x00, 0x00 /*command class*/, 0x00 /*command index*/, 0x00 /*sub command*/, 0x00 /*reserved*/, 0x00, 0x00, 0x00, 0x00 /*4byte Parameter1*/, 0x00, 0x00, 0x00, 0x00 /*4byte Parameter2*/, 0x00, 0x00 /*Read length, 0 when writing*/, 0x00, 0x00 /*2btyes Reserved*/, 0x00, 0x00 /*2byte crc16 xmodem*/};

    if (len > 16) {
        ESP_LOGE(Mini2_TAG, "command length may not be longer than 16bytes");
        return ESP_FAIL;
    }

    memcpy(full_cmd + 5, cmd, len);
    uint16_t checksum = crc16_xmodem(full_cmd + 5, 16);
    memcpy(full_cmd + 21, &checksum, 2);

    ESP_LOG_BUFFER_HEXDUMP(Mini2_TAG, full_cmd, sizeof(full_cmd), ESP_LOG_DEBUG);

    for (int i=0; i < sizeof(full_cmd); i++) {
        int bytes_written = uart_write_bytes(cam->uart_port, full_cmd + i, 1);
        if (bytes_written != 1) {
            ESP_LOGE(Mini2_TAG, "Failed to write byte%d command to UART", i);
            return ESP_FAIL;
        }
    }

    esp_err_t err = uart_wait_tx_done(cam->uart_port, pdMS_TO_TICKS(200));
    // Pace commands; TX completion alone does not mean the camera applied them.
    vTaskDelay(pdMS_TO_TICKS(20));
    return err;
}

esp_err_t Mini2_read_command(Mini2_t* cam, uint8_t* cmd, size_t len, uint8_t* out_buf, size_t* out_len) {
    const size_t expected = *out_len;
    *out_len = 0;
    if (expected < 9 || expected > 32) return ESP_ERR_INVALID_ARG;
    esp_err_t err = uart_flush(cam->uart_port);
    if (err != ESP_OK) return err;
    err = Mini2_write_command(cam, cmd, len);
    if (err != ESP_OK) return err;

    // Captured Mini2 replies: BE AA, payload length LE16, status/data,
    // CRC16-XMODEM over header+payload (LE16), EB AA.
    // Writes can acknowledge late, even after the input flush. Skip those
    // 9-byte ACK frames instead of mistaking ACK+first reply byte for a query.
    uint8_t frame[32];
    size_t used = 0;
    TickType_t start = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(500)) {
        int n = uart_read_bytes(cam->uart_port, frame + used, 1, pdMS_TO_TICKS(20));
        if (n < 0) return ESP_FAIL;
        if (n == 0) continue;
        used++;
        while (used >= 2) {
            if (frame[0] != 0xBE || frame[1] != 0xAA) {
                memmove(frame, frame + 1, --used);
                continue;
            }
            if (used < 4) break;
            size_t payload = frame[2] | ((size_t)frame[3] << 8);
            size_t total = payload + 8;
            if (payload < 1 || total > sizeof(frame)) {
                memmove(frame, frame + 1, --used);
                continue;
            }
            if (used < total) break;
            uint16_t crc = frame[total - 4] | ((uint16_t)frame[total - 3] << 8);
            if (frame[total - 2] != 0xEB || frame[total - 1] != 0xAA ||
                crc16_xmodem(frame, total - 4) != crc) {
                memmove(frame, frame + 1, --used);
                continue;
            }
            if (total == expected) {
                if (frame[4] != 0) {
                    ESP_LOGW(Mini2_TAG, "Camera query returned status 0x%02x", frame[4]);
                    return ESP_FAIL;
                }
                memcpy(out_buf, frame, total);
                *out_len = total;
                ESP_LOG_BUFFER_HEXDUMP("UART_READ", out_buf, total, ESP_LOG_INFO);
                return ESP_OK;
            }
            // Complete, valid frame of another length (normally a write ACK).
            used -= total;
            memmove(frame, frame + total, used);
        }
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t Mini2_get_flip_mode(Mini2_t* cam, enum FlipMode* flip_mode_out) {
    uint8_t rx_buffer[10];
    size_t len = sizeof(rx_buffer);

    uint8_t cmd[] = {0x10, 0x10, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    esp_err_t err_code = Mini2_read_command(cam, cmd, sizeof(cmd), rx_buffer, &len);
    if (err_code != ESP_OK) {
        return err_code;
    }

    if (len != sizeof(rx_buffer) || !(rx_buffer[0] == 0xBE && rx_buffer[1] == 0xAA && rx_buffer[8] == 0xEB && rx_buffer[9] == 0xAA) || rx_buffer[5] > XY_Flip) {
        return ESP_FAIL;
    }

    *flip_mode_out = (enum FlipMode)rx_buffer[5];
    return ESP_OK;
}

esp_err_t Mini2_set_color_pallet(Mini2_t* cam, enum PseudoColor pseudo_color) {
    uint8_t cmd[] = {0x10, 0x03, 0x45, 0x00, 0x00, pseudo_color};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_scene_mode(Mini2_t* cam, enum SceneMode scene_mode) {
    uint8_t cmd[] = {0x10, 0x04, 0x42, 0x00, scene_mode};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_flip_mode(Mini2_t* cam, enum FlipMode flip_mode) {
    uint8_t cmd[] = {0x10, 0x10, 0x43, 0x00, flip_mode};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_analog_video_format(Mini2_t* cam, enum AnalogVideoFormat av_format) {
    uint8_t cmd[] = {0x10, 0x10, 0x4A, 0x00, 0x01, av_format};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_get_analog_video_format(Mini2_t* cam, enum AnalogVideoFormat* av_format_out) {
    uint8_t rx_buffer[11];
    size_t len = sizeof(rx_buffer);

    uint8_t cmd[] = {0x10, 0x10, 0x8a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    esp_err_t err_code = Mini2_read_command(cam, cmd, sizeof(cmd), rx_buffer, &len);
    if (err_code != ESP_OK) {
        return err_code;
    }

    if (len != sizeof(rx_buffer) || !(rx_buffer[0] == 0xBE && rx_buffer[1] == 0xAA && rx_buffer[9] == 0xEB && rx_buffer[10] == 0xAA) || rx_buffer[6] > PAL) {
        return ESP_FAIL;
    }

    *av_format_out = (enum AnalogVideoFormat)rx_buffer[6];
    return ESP_OK;
}

esp_err_t Mini2_set_digital_video_format(Mini2_t* cam, bool enabled, enum DigitalVideoFormat video_format, enum DetectorRefreshRate fps) {
    uint8_t cmd[] = {0x10, 0x10, 0x46, 0x00, (uint8_t)enabled, (uint8_t)video_format, (uint8_t)fps};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_save_video(Mini2_t* cam) {
    uint8_t cmd[] = {0x10, 0x10, 0x49};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_brightness(Mini2_t* cam, uint8_t brightness) {
    if (brightness > 100) {
        ESP_LOGE(Mini2_TAG, "Brightness must be <= 100");
        return ESP_FAIL;
    }
    uint8_t cmd[] = {0x10, 0x04, 0x47, 0x00, brightness};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_contrast(Mini2_t* cam, uint8_t contrast) {
    if (contrast > 100) {
        ESP_LOGE(Mini2_TAG, "Contrast must be <= 100");
        return ESP_FAIL;
    }
    uint8_t cmd[] = {0x10, 0x04, 0x4A, 0x00, contrast};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_edge_enhancment(Mini2_t* cam, uint8_t gear) {
    if (gear > 2) {
        ESP_LOGE(Mini2_TAG, "Edge enhancment gear must be <= 2");
        return ESP_FAIL;
    }
    uint8_t cmd[] = {0x10, 0x04, 0x4E, 0x00, gear};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_detail_enhancement(Mini2_t* cam, uint8_t gear) {
    if (gear > 100) {
        ESP_LOGE(Mini2_TAG, "Detail enhancement gear must be <= 100");
        return ESP_FAIL;
    }
    uint8_t cmd[] = {0x10, 0x04, 0x45, 0x00, gear};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_burn_protection(Mini2_t* cam, bool enabled) {
    uint8_t cmd[] = {0x10, 0x03, 0x4B, 0x00, (uint8_t)enabled};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_shutter_position(Mini2_t* cam, bool open) {
    uint8_t cmd[] = {0x01, 0x0F, 0x45, 0x00, (uint8_t)open};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_auto_shutter(Mini2_t* cam, bool enabled) {
    uint8_t cmd[] = {0x10, 0x02, 0x41, 0x00, (uint8_t)enabled};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_centre_zoom(Mini2_t* cam, uint8_t zoom) {
    if (zoom < 10 || zoom > 80 ) {
        ESP_LOGE(Mini2_TAG, "Zoom must be between 10 and 80 for 1x to 8x");
        return ESP_FAIL;
    }

    uint8_t cmd[] = {0x01, 0x31, 0x42, 0x00, 0x00, zoom};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_point_zoom(Mini2_t* cam, uint16_t x, uint16_t y, uint8_t zoom) {
    if (zoom < 10 || zoom > 80 ) {
        ESP_LOGE(Mini2_TAG, "Zoom must be between 10 and 80 for 1x to 8x");
        return ESP_FAIL;
    }
    if (x > cam->variant.sensor_width) {
        ESP_LOGE(Mini2_TAG, "X must be between 0 and sensor width");
        return ESP_FAIL;
    }
    if (y > cam->variant.sensor_height) {
        ESP_LOGE(Mini2_TAG, "Y must be between 0 and sensor height");
        return ESP_FAIL;
    }

    ESP_LOGI(Mini2_TAG, "Setting %.1fzoom at %dx, %dy", ((float)zoom / 10.0), (int)x, (int)y);

    uint8_t cmd[] = {0x01, 0x31, 0x51, 0x00, 0x00, zoom, 0x00, 0x00, *((uint8_t*)&x), *(((uint8_t*)&x) + 1)/*X*/, *((uint8_t*)&y), *(((uint8_t*)&y) + 1) /*Y*/};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_detector_fps(Mini2_t* cam, enum DetectorRefreshRate fps) {
    if (!(fps == cam->variant.low_fps || fps == cam->variant.high_fps)) {
        ESP_LOGE(Mini2_TAG, "Selected variant doesn't support this detector fps.");
        return ESP_FAIL;
    }
    ESP_LOGI(Mini2_TAG, "Setting FPS to %d", (int)fps);
    uint8_t cmd[] = {0x10, 0x10, 0x44, 0x00, fps};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_sleep(Mini2_t* cam, bool sleep) {
    uint8_t cmd[] = {0x10, 0x10, 0x48, 0x00, (uint8_t)sleep};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_set_crosshair(Mini2_t* cam, bool enable) {
    uint8_t cmd[] = {0x10, 0x11, 0x57, 0x00, (uint8_t)enable};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_parameters_save(Mini2_t* cam) {
    uint8_t cmd[] = {0x10, 0x10, 0x51};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_restore_factory_parameters(Mini2_t* cam) {
    uint8_t cmd[] = {0x10, 0x10, 0x52};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_NUC(Mini2_t* cam) {
    uint8_t cmd[] = {0x10, 0x2, 0x43};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

esp_err_t Mini2_Background_Correction(Mini2_t* cam) {
    uint8_t cmd[] = {0x1, 0x10, 0x52};
    return Mini2_write_command(cam, cmd, sizeof(cmd));
}

// Keep the existing three-send workaround: some modules accept a command
// without visibly applying it. There is no documented crop readback here.
#define MINI2_TRY(command) do { \
    esp_err_t result = (command); \
    if (result != ESP_OK) { \
        ESP_LOGE(Mini2_TAG, "%s failed: %s", #command, esp_err_to_name(result)); \
        return result; \
    } \
} while (0)

esp_err_t Mini2_apply_alignment(Mini2_t* cam, const alignment_preset_t* alignment) {
    // Disable zoom BEFORE enabling camera flip. Clear camera flip BEFORE zoom.
    MINI2_TRY(Mini2_set_centre_zoom(cam, 10));
    MINI2_TRY(Mini2_set_flip_mode(cam, alignment->flip_mode));
    vTaskDelay(pdMS_TO_TICKS(250));
    for (int i = 0; i < 3; i++) {
        if (alignment->flip_mode == No_Flip) {
            MINI2_TRY(Mini2_set_point_zoom(cam, alignment->zoom_x, alignment->zoom_y, alignment->zoom));
        } else {
            MINI2_TRY(Mini2_set_flip_mode(cam, alignment->flip_mode));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(Mini2_TAG, "Alignment TX complete: X=%u Y=%u zoom=%u flip=%d; crop NOT read back",
             alignment->zoom_x, alignment->zoom_y, alignment->zoom, alignment->flip_mode);
    return ESP_OK;
}

esp_err_t Mini2_apply_preset(Mini2_t* cam, const value_preset_t* preset,
                             const alignment_preset_t* alignment, bool seem_less) {
    // Preserve a working video output. Only change it at boot if readback
    // positively establishes a mismatch. FPS setup remains in explicit repair.
    if (!seem_less) {
        enum AnalogVideoFormat actual;
        esp_err_t read_err = Mini2_get_analog_video_format(cam, &actual);
        if (read_err == ESP_OK && actual != alignment->av_format) {
            MINI2_TRY(Mini2_set_analog_video_format(cam, alignment->av_format));
            vTaskDelay(pdMS_TO_TICKS(3000));
        } else if (read_err != ESP_OK) {
            ESP_LOGW(Mini2_TAG, "Video format readback unavailable; keeping existing output");
        }
    }
    MINI2_TRY(Mini2_set_scene_mode(cam, preset->scene_mode));
    MINI2_TRY(Mini2_set_contrast(cam, preset->contrast));
    MINI2_TRY(Mini2_set_edge_enhancment(cam, preset->edge_enhancment_gear));
    MINI2_TRY(Mini2_set_detail_enhancement(cam, preset->detail_enhancement_gear));
    MINI2_TRY(Mini2_set_burn_protection(cam, preset->burn_protection_en));
    MINI2_TRY(Mini2_set_auto_shutter(cam, preset->auto_shutter_en));
    MINI2_TRY(Mini2_set_color_pallet(cam, preset->pseudo_color));
    // Scene/palette commands may disturb alignment, so restore it LAST.
    return Mini2_apply_alignment(cam, alignment);
}
