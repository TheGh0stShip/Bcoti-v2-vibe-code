"""Compile the actual C persistence/request/driver functions with host I/O mocks.

Run under WSL: python3 tests/test_core.py
Requires gcc and the workspace's ESP-IDF checkout. No hardware claims are made.
"""
from pathlib import Path
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "Esp32_Files/ESP_IDF_Project"
IDF = Path(os.environ.get('IDF_PATH', ''))
if not (IDF / 'tools/idf.py').is_file():
    raise SystemExit('Activate ESP-IDF v5.3.1 first (IDF_PATH required).')


def section(text, start, end):
    return text[text.index(start):text.index(end)]


main = (PROJECT / "main/main.c").read_text()
header = (PROJECT / "components/Mini2/Mini2.h").read_text()
driver = (PROJECT / "components/Mini2/Mini2.c").read_text()
types = section(header, "enum DetectorRefreshRate", "uint16_t crc16_xmodem")
types += section(main, "typedef struct stored_values_t", "static void wifi_event_handler")
common = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include "cJSON.h"
typedef int esp_err_t;
typedef int uart_port_t;
typedef int gpio_num_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 4
#define PRESET_COUNT 5
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOG_BUFFER_HEXDUMP(...) ((void)0)
#define ESP_LOG_DEBUG 0
#define ESP_LOG_INFO 0
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0
static void vTaskDelay(int ticks) {}
static const char *esp_err_to_name(int err) { return "mock"; }
typedef uint32_t TickType_t;
static TickType_t mock_ticks;
static TickType_t xTaskGetTickCount(void) { return mock_ticks; }
uint16_t crc16_xmodem(uint8_t*, size_t);
"""
io = r"""
static Mini2_t cam = {.variant = Mini2_256};
static int config_mutex, flash_handle;
static bool nvs_writable, settings_saved, camera_tx_ok, camera_reply, flip_verified;
static int set_calls, commit_calls, set_error, commit_error, read_error, corrupt_read;
static stored_values_t durable, pending;
static int nvs_set_blob(int h, const char *key, const void *s, size_t len) {
    set_calls++; if (set_error) return ESP_FAIL;
    memcpy(&pending, s, len); return ESP_OK;
}
static int nvs_commit(int h) {
    commit_calls++; if (commit_error) return ESP_FAIL;
    durable = pending; return ESP_OK;
}
static int nvs_get_blob(int h, const char *key, void *out, size_t *len) {
    if (read_error) return ESP_FAIL;
    memcpy(out, &durable, sizeof(durable)); *len = sizeof(durable);
    if (corrupt_read) ((stored_values_t *)out)->alignment.zoom_x++;
    return ESP_OK;
}
static int tx_calls, tx_fail_at, rx_length = 10, tx_count = 1, wait_error;
static int actual_flip;
static int forced_flip = -1;
static int rx_mode, rx_pos, rx_size;
static uint8_t rx_stream[64];
static unsigned char frame[23], frames[256][23];
static size_t frame_at;
static int uart_write_bytes(int port, const void *buf, size_t len) {
    if (tx_count != 1) return tx_count;
    frame[frame_at++] = *(const uint8_t *)buf;
    if (frame_at == sizeof(frame)) {
        frame_at = 0;
        memcpy(frames[tx_calls++], frame, sizeof(frame));
        if (frame[5] == 0x10 && frame[6] == 0x10 && frame[7] == 0x43)
            actual_flip = frame[9];
        if (tx_fail_at && tx_calls == tx_fail_at) return -1;
    }
    return 1;
}
static int uart_wait_tx_done(int port, int ticks) { return wait_error; }
static int uart_flush(int port) { rx_pos=0; rx_size=-1; return 0; }
static int uart_read_bytes(int port, void *out, size_t len, int ticks) {
    if (rx_size < 0) {
        // Exactly reproduce the observed late ACK before the actual query reply.
        const uint8_t ack[] = {0xBE,0xAA,1,0,0,0x7B,0x54,0xEB,0xAA};
        const uint8_t noise[] = {0x00,0xBE,0xBE,0xAA,0xFF,0xFF};
        uint8_t response[11] = {0xBE,0xAA,2,0,0,forced_flip < 0 ? actual_flip : forced_flip,0,0,0xEB,0xAA,0};
        int total=10;
        if(rx_length==11) { total=11; response[2]=3; response[5]=1; response[6]=PAL; response[9]=0xEB; response[10]=0xAA; }
        if(rx_mode==4) response[4]=1;
        uint16_t crc=crc16_xmodem(response,total-4);
        response[total-4]=crc & 255; response[total-3]=crc >> 8;
        if(rx_mode==2) response[total-4]^=1;
        rx_size=0;
        if(rx_mode==3) { memcpy(rx_stream,noise,sizeof(noise)); rx_size+=sizeof(noise); }
        if(rx_mode==1 || rx_mode==3) { memcpy(rx_stream+rx_size,ack,sizeof(ack)); rx_size+=sizeof(ack); }
        if(rx_length>0) { memcpy(rx_stream+rx_size,response,rx_length); rx_size+=rx_length; }
    }
    if(rx_pos>=rx_size) { mock_ticks+=ticks; return 0; }
    mock_ticks++;
    *(uint8_t*)out=rx_stream[rx_pos++];
    return 1;
}
static int xSemaphoreTake(int sem, int timeout) { return 1; }
static int nvs_open(const char *name, int mode, int *h) { return 0; }
#define NVS_READWRITE 0
#define ESP_ERR_NVS_NOT_FOUND 3
static int xSemaphoreGive(int sem) { return 1; }
typedef struct { size_t content_len; const char *body; size_t pos; } httpd_req_t;
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_408_REQ_TIMEOUT 408
static char response_status[40], response_body[2048];
static int httpd_resp_send(httpd_req_t *r, const char *s, int len) {
    assert(len < sizeof(response_body)); memcpy(response_body,s,len); response_body[len]=0; return 0;
}
static int httpd_resp_set_status(httpd_req_t *r, const char *s) { strcpy(response_status, s); return 0; }
static int httpd_resp_set_type(httpd_req_t *r, const char *s) { return 0; }
static int httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v) { return 0; }
static int httpd_resp_sendstr(httpd_req_t *r, const char *s) { strcpy(response_body, s); return 0; }
static int httpd_resp_send_err(httpd_req_t *r, int err, const char *s) {
    snprintf(response_status, sizeof(response_status), "%d", err); return 0;
}
static int httpd_req_recv(httpd_req_t *r, char *out, size_t len) {
    // Every valid request is deliberately fragmented into 3-byte chunks.
    if (r->pos >= strlen(r->body)) return -1;
    size_t n = len < 3 ? len : 3;
    if (n > strlen(r->body) - r->pos) n = strlen(r->body) - r->pos;
    memcpy(out, r->body + r->pos, n); r->pos += n; return n;
}
"""
tests = r"""
static void reset(void) {
    memset(&stored, 0, sizeof(stored));
    stored.alignment = (alignment_preset_t){.zoom=11, .zoom_x=128, .zoom_y=96, .fps=Hz50, .av_format=PAL};
    for (int i=0; i<5; i++) stored.presets[i] = base_preset;
    stored.presets[0].preset_en = true;
    stored.first_boot = true;
    tx_calls = tx_fail_at = frame_at = wait_error = 0;
    rx_length = 10; tx_count = 1; actual_flip = 0; forced_flip = -1;
    rx_mode=rx_pos=mock_ticks=0; rx_size=-1;
    set_calls = commit_calls = set_error = commit_error = read_error = corrupt_read = 0;
    nvs_writable = true; settings_saved = false;
    camera_tx_ok = camera_reply = flip_verified = false;
}
static void post(const char *body, const char *expected_status) {
    httpd_req_t r = {.content_len=strlen(body), .body=body};
    post_handler(&r);
    if(strncmp(response_status,expected_status,3)!=0)
        fprintf(stderr,"POST %s expected %s got %s\n",body,expected_status,response_status);
    assert(strncmp(response_status, expected_status, 3) == 0);
}
int main(void) {
    reset();
    assert(sizeof(stored) == 132);
    assert(valid_settings(&stored));
    int a,b,c,d;
    alignment_limits(11, &a,&b,&c,&d);
    assert(a==116 && b==140 && c==87 && d==105);
    assert(save_settings() == ESP_OK && settings_saved && commit_calls==1);
    assert(memcmp(&durable, &stored, sizeof(stored))==0);
    set_error=1;
    assert(save_settings()!=ESP_OK && !settings_saved && commit_calls==1);
    set_error=0; commit_error=1;
    assert(save_settings()!=ESP_OK && !settings_saved);
    commit_error=0; corrupt_read=1;
    assert(save_settings()!=ESP_OK && !settings_saved);
    corrupt_read=0; nvs_writable=false;
    int calls=set_calls;
    assert(save_settings()!=ESP_OK && set_calls==calls);
    puts("PASS NVS commit, failure propagation, readback mismatch, unavailable storage");

    reset();
    enum FlipMode flip;
    rx_length=0; assert(Mini2_get_flip_mode(&cam,&flip)==ESP_ERR_TIMEOUT);
    rx_length=3; assert(Mini2_get_flip_mode(&cam,&flip)!=ESP_OK);
    rx_length=10; assert(Mini2_get_flip_mode(&cam,&flip)==ESP_OK);
    rx_mode=1; assert(Mini2_get_flip_mode(&cam,&flip)==ESP_OK && flip==No_Flip);
    rx_mode=3; assert(Mini2_get_flip_mode(&cam,&flip)==ESP_OK && flip==No_Flip);
    rx_mode=2; assert(Mini2_get_flip_mode(&cam,&flip)==ESP_ERR_TIMEOUT);
    rx_mode=4; assert(Mini2_get_flip_mode(&cam,&flip)==ESP_FAIL);
    rx_mode=1; rx_length=0;
    TickType_t before_timeout=mock_ticks;
    assert(Mini2_get_flip_mode(&cam,&flip)==ESP_ERR_TIMEOUT);
    assert(mock_ticks-before_timeout<=520);
    rx_length=10; mock_ticks=UINT32_MAX-5;
    assert(Mini2_get_flip_mode(&cam,&flip)==ESP_OK);
    rx_mode=0; rx_length=11;
    enum AnalogVideoFormat format;
    assert(Mini2_get_analog_video_format(&cam,&format)==ESP_OK && format==PAL);
    const uint8_t captured[] = {0xBE,0xAA,2,0,0,0};
    assert(crc16_xmodem((uint8_t*)captured,sizeof(captured))==0xFAAD);
    tx_count=0; assert(Mini2_set_flip_mode(&cam,No_Flip)!=ESP_OK);
    tx_count=1; wait_error=ESP_FAIL;
    assert(Mini2_set_flip_mode(&cam,No_Flip)!=ESP_OK);
    puts("PASS fragmented replies, captured late ACK, noise, oversized frame, CRC/status rejection, bounded timeout/tick wrap and UART failures");

    reset();
    stored.presets[0].pseudo_color=BHOT;
    stored.presets[0].auto_shutter_en=false;
    stored.presets[0].burn_protection_en=false;
    stored.alignment.zoom_x=133;
    assert(apply_settings(&stored,false,true)==ESP_OK);
    assert(camera_tx_ok && camera_reply && flip_verified);
    assert(frames[0][7]==0x8A); // Read video format, never blindly reset output/FPS.
    bool palette=false, shutter=false, burn=false;
    int point_count=0;
    for(int i=0;i<tx_calls;i++) {
        assert(!(frames[i][6]==0x10 && frames[i][7]==0x44));
        if(frames[i][6]==3 && frames[i][7]==0x45) palette=frames[i][10]==BHOT;
        if(frames[i][6]==2 && frames[i][7]==0x41) shutter=frames[i][9]==0;
        if(frames[i][6]==3 && frames[i][7]==0x4B) burn=frames[i][9]==0;
        if(frames[i][6]==0x31 && frames[i][7]==0x51) {
            point_count++; assert(frames[i][10]==11 && frames[i][13]==133 && frames[i][15]==96);
        }
    }
    assert(palette && shutter && burn && point_count==3);
    assert(set_calls==0); // Startup application must not rewrite NVS.
    puts("PASS saved presets, preserved startup video, alignment last, three bounded crop sends");

    reset(); wait_error=ESP_FAIL;
    assert(apply_settings(&stored,false,false)!=ESP_OK && !camera_tx_ok);
    assert(tx_calls==3); // One failed first command on each of exactly 3 attempts.
    reset(); rx_length=0;
    assert(apply_settings(&stored,true,false)==ESP_OK && camera_tx_ok && !camera_reply && !flip_verified);
    reset(); forced_flip=2;
    assert(apply_settings(&stored,true,false)!=ESP_OK && !flip_verified);
    assert(tx_calls==18); // 5 writes plus a read query, on each of 3 attempts.
    puts("PASS bounded retries and explicit TX-only fallback");

    reset();
    post("{\"zoom\":{\"x\":133,\"y\":98,\"zoom\":11}}","200");
    assert(stored.alignment.zoom_x==133 && durable.alignment.zoom_y==98 && settings_saved);
    assert(!stored.first_boot);
    post("{\"flip_mode\":2}","200");
    assert(actual_flip==2 && stored.alignment.zoom_x==133 && stored.alignment.zoom==11);
    post("{\"flip_mode\":0}","200");
    assert(actual_flip==0 && stored.alignment.zoom_x==133);
    post("{\"active_profile\":2}","200");
    assert(durable.active_preset==2 && durable.alignment.zoom_x==133);
    post("{\"breathing\":1}","200");
    assert(durable.presets[2].breathing);
    post("{\"auto_shutter_en\":false}","200");
    assert(!durable.presets[2].auto_shutter_en);
    stored_values_t saved=stored;
    memset(&stored, 0, sizeof(stored)); // Simulated loss of all RAM at cold boot.
    boot_restore();
    assert(settings_saved && memcmp(&saved,&stored,sizeof(stored))==0);
    stored_values_t before=stored;
    calls=set_calls;
    post("{\"zoom\":{\"x\":141,\"y\":98,\"zoom\":11}}","400");
    post("{\"zoom\":{\"x\":-1,\"y\":98,\"zoom\":11}}","400");
    post("{\"zoom\":{\"x\":128.5,\"y\":98,\"zoom\":11}}","400");
    post("{\"active_profile\":5}","400");
    post("{\"scene_mode\":8}","400");
    post("{\"contrast\":101}","400");
    post("{\"flip_mode\":2,\"contrast\":50}","400");
    post("{\"contrast\":50} garbage","400");
    post("{\"bogus\":0}","400");
    assert(memcmp(&before,&stored,sizeof(stored))==0 && calls==set_calls);
    puts("PASS fragmented HTTP input, crop limits, invalid values, mode/preset retention");

    reset(); commit_error=1;
    post("{\"zoom\":{\"x\":130,\"y\":96,\"zoom\":11}}","500");
    assert(!settings_saved && strstr(response_body,"SAVE FAILED"));
    commit_error=0;
    post("{\"reapply\":1}","200");
    assert(settings_saved && durable.alignment.zoom_x==130);
    reset(); wait_error=ESP_FAIL;
    post("{\"zoom\":{\"x\":130,\"y\":96,\"zoom\":11}}","502");
    assert(stored.alignment.zoom_x==128 && set_calls==0 && tx_calls==6);
    puts("PASS save failure/retry and failed camera update leaves saved settings untouched");
    reset();
    assert(apply_settings(&stored,false,false)==ESP_OK);
    int commands=tx_calls;
    post("{\"breathing\":1}","200");
    assert(tx_calls==commands && durable.presets[0].breathing);
    post("{\"breathing\":1}","200");
    assert(tx_calls==commands);
    post("{\"reapply\":1}","200");
    assert(tx_calls>commands);
    reset();
    httpd_req_t get_req={0};
    assert(retireve_values(&get_req)==ESP_OK);
    cJSON *snapshot=cJSON_Parse(response_body);
    assert(snapshot);
    cJSON *backup=cJSON_GetObjectItemCaseSensitive(snapshot,"backup");
    assert(cJSON_GetArraySize(backup)==56);
    cJSON *restore=cJSON_CreateObject();
    cJSON_AddItemToObject(restore,"restore",cJSON_Duplicate(backup,true));
    char *restore_text=cJSON_PrintUnformatted(restore);
    stored_values_t original=stored;
    post("{\"detail_preset\":4}","200");
    assert(stored.active_preset==4 && !stored.presets[4].preset_en);
    assert(stored.presets[4].scene_mode==GeneralMode);
    assert(memcmp(&stored.alignment,&original.alignment,sizeof(stored.alignment))==0);
    assert(memcmp(stored.presets,original.presets,4*sizeof(value_preset_t))==0);
    post(restore_text,"200");
    assert(stored.alignment.av_format==original.alignment.av_format &&
           stored.alignment.fps==original.alignment.fps);
    assert(retireve_values(&get_req)==ESP_OK);
    cJSON *roundtrip=cJSON_Parse(response_body);
    assert(cJSON_Compare(backup,cJSON_GetObjectItemCaseSensitive(roundtrip,"backup"),true));
    cJSON_Delete(roundtrip); // Compare fields, not unspecified C structure padding.
    assert(stored.active_preset==original.active_preset);
    before=stored; calls=set_calls; commands=tx_calls;
    post("{\"restore\":[1,0]}","400");
    cJSON *restore_array=cJSON_GetObjectItemCaseSensitive(restore,"restore");
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,6),256);
    char *bad=cJSON_PrintUnformatted(restore);
    post(bad,"400"); free(bad);
    assert(memcmp(&before,&stored,sizeof(stored))==0 && set_calls==calls && tx_calls==commands);
    post("{\"detail_preset\":0}","400");
    stored.presets[2].preset_en=true;
    post("{\"detail_preset\":2}","400"); // Enabled slot must not be replaced.
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,6),1);
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,4),141);
    bad=cJSON_PrintUnformatted(restore); post(bad,"400"); free(bad);
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,4),128);
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,0),2);
    bad=cJSON_PrintUnformatted(restore); post(bad,"400"); free(bad);
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,0),1);
    cJSON_SetNumberValue(cJSON_GetArrayItem(restore_array,7),0.5);
    bad=cJSON_PrintUnformatted(restore); post(bad,"400"); free(bad);
    commit_error=1;
    post(restore_text,"500");
    assert(!settings_saved);
    commit_error=0;
    post("{\"reapply\":1}","200");
    assert(settings_saved);
    free(restore_text); cJSON_Delete(snapshot); cJSON_Delete(restore);
    puts("PASS portable backup roundtrip, rejected imports, safe Detail slot and metadata/no-op command elimination");
    return 0;
}
"""
# Compile real source bodies, replacing only platform I/O. Main task scheduling
# and physical boot timing are covered by device tests, not this harness.
driver_code = driver.replace('#include "Mini2.h"', "").replace('#include "freertos/task.h"', "")
driver_code = driver_code[:driver_code.index("void Mini2_init")] + driver_code[driver_code.index("esp_err_t Mini2_write_command"):]
helpers = section(main, "static void alignment_limits", "void next_preset")
handlers = section(main, "static esp_err_t json_response", "static const httpd_uri_t retireve_values_route")
boot = "static void boot_restore(void) { int nvs_err=ESP_OK;\n" + section(
    main, "    for (int i=0; i<PRESET_COUNT; i++)", "    bool wifi_en") + "\n}\n"
program = common + types + io + driver_code + helpers + handlers + boot + tests
with tempfile.TemporaryDirectory(prefix="bcoti-tests-") as tmp:
    binary = str(Path(tmp) / ("test-core.exe" if os.name == 'nt' else "test-core"))
    if os.name == 'nt':
        generated = Path(tmp) / 'test-core.c'
        generated.write_text(program)
        subprocess.run(['cl.exe','/nologo','/std:c11','/Zi','/fsanitize=address',
                        '/D_CRT_SECURE_NO_WARNINGS','/I' + str(IDF / 'components/json/cJSON'),
                        str(generated),str(IDF / 'components/json/cJSON/cJSON.c'),
                        '/Fe:' + binary], cwd=tmp, check=True)
    else:
        subprocess.run(["gcc", "-x", "c", "-std=c11", "-Wall", "-Wextra",
                    "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
                    "-fsanitize=address,undefined", "-I", str(IDF / "components/json/cJSON"),
                    "-", str(IDF / "components/json/cJSON/cJSON.c"), "-lm", "-o", binary],
                       input=program, text=True, check=True)
    subprocess.run([binary], check=True)
