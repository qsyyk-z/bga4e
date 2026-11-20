#include "camera_web.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"

static const char *TAG = "camera_web";
static const char *AP_SSID = "BGA4E-CAM";
static const char *AP_PASS = "bga4e1234";

static EXT_RAM_BSS_ATTR httpd_handle_t s_server;
static EXT_RAM_BSS_ATTR SemaphoreHandle_t s_lock;
static EXT_RAM_BSS_ATTR uint8_t *s_jpeg;
static EXT_RAM_BSS_ATTR size_t s_jpeg_len;
static EXT_RAM_BSS_ATTR uint32_t s_seq;
static EXT_RAM_BSS_ATTR volatile int s_clients;
static EXT_RAM_BSS_ATTR volatile int s_camera_ready;
static EXT_RAM_BSS_ATTR char s_result[768];
static EXT_RAM_BSS_ATTR uint32_t s_frame_count;
static EXT_RAM_BSS_ATTR uint32_t s_fps;
static EXT_RAM_BSS_ATTR int64_t s_last_pub_us;

static esp_err_t index_handler(httpd_req_t *req)
{
    static const char page[] = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BGA4E 摄像头</title>
<style>
body{margin:0;background:#111;color:#eee;font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif}
main{max-width:900px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 6px}
.hint{color:#9fb3c8;font-size:13px;line-height:1.6;margin:6px 0 14px}
.hint b{color:#ffd166}
.wrap{background:#000;border:1px solid #2a3642;border-radius:10px;overflow:hidden}
img{display:block;width:100%;height:auto;min-height:180px;object-fit:contain}
.bar{display:flex;gap:12px;flex-wrap:wrap;font-size:13px;color:#9fb3c8;padding:10px 0}
pre{background:#0b1117;border:1px solid #22303c;border-radius:8px;padding:12px;color:#7ee787;white-space:pre-wrap;word-break:break-all;font-size:13px;min-height:44px}
.err{color:#ff7b72}
</style>
</head>
<body><main>
<h1>BGA4E 摄像头实时画面</h1>
<div class="hint">
  连接 WiFi <b>BGA4E-CAM</b>（密码 <b>bga4e1234</b>），然后打开 <b>http://192.168.4.1</b>。<br>
  画面为摄像头原始 RGB565 帧转成的 MJPEG；识别框和文字会叠加在画面上。
</div>
<div class="wrap"><img id="stream" src="/stream" alt="camera stream"></div>
<div class="bar">
  <span id="status">连接中…</span>
  <span id="fps"></span>
  <span id="camera"></span>
</div>
<pre id="latest">等待识别结果…</pre>
<script>
async function refresh() {
  try {
    const r = await fetch('/api/status?t=' + Date.now());
    const j = await r.json();
    document.getElementById('status').className = 'bar';
    document.getElementById('status').textContent = '浏览器客户端: ' + j.clients + ' / 已发布帧: ' + j.frames;
    document.getElementById('fps').textContent = '编码约 ' + j.fps + ' fps';
    document.getElementById('camera').textContent = j.camera ? '摄像头已就绪' : '摄像头初始化中/失败';
    const l = await fetch('/api/latest?t=' + Date.now());
    document.getElementById('latest').textContent = await l.text();
  } catch (e) {
    const s = document.getElementById('status');
    s.className = 'bar err';
    s.textContent = '连接中断，请确认已连接 BGA4E-CAM 后刷新页面';
  }
}
setInterval(refresh, 1000);
refresh();
</script>
</main></body></html>)HTML";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char json[320];
    int n = snprintf(json, sizeof(json),
                     "{\"wifi\":\"%s\",\"ip\":\"192.168.4.1\",\"camera\":%d,"
                     "\"clients\":%d,\"frames\":%u,\"fps\":%u}",
                     AP_SSID, s_camera_ready ? 1 : 0, s_clients,
                     (unsigned)s_frame_count, (unsigned)s_fps);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, n);
}

static esp_err_t latest_handler(httpd_req_t *req)
{
    char copy[sizeof(s_result)];
    copy[0] = '\0';
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        snprintf(copy, sizeof(copy), "%s", s_result);
        xSemaphoreGive(s_lock);
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, copy, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    if (s_clients >= 2)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "stream busy");
    }

    s_clients++;
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    uint32_t last_seq = 0;
    while (true)
    {
        uint8_t *copy = NULL;
        size_t copy_len = 0;
        uint32_t seq = 0;

        if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            if (s_seq != last_seq && s_jpeg != NULL && s_jpeg_len > 0)
            {
                copy_len = s_jpeg_len;
                copy = (uint8_t *)malloc(copy_len);
                if (copy != NULL)
                {
                    memcpy(copy, s_jpeg, copy_len);
                    seq = s_seq;
                }
            }
            xSemaphoreGive(s_lock);
        }

        if (copy == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        last_seq = seq;
        char header[128];
        int hlen = snprintf(header, sizeof(header),
                            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                            (unsigned)copy_len);
        esp_err_t err = httpd_resp_send_chunk(req, header, hlen);
        if (err == ESP_OK)
        {
            err = httpd_resp_send_chunk(req, (const char *)copy, copy_len);
        }
        if (err == ESP_OK)
        {
            err = httpd_resp_send_chunk(req, "\r\n", 2);
        }
        free(copy);
        if (err != ESP_OK)
        {
            break;
        }
    }

    s_clients--;
    return ESP_OK;
}

static esp_err_t wifi_init_softap(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_create_default_wifi_ap() == NULL)
    {
        ESP_LOGE(TAG, "create default wifi ap failed");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "%s", AP_SSID);
    wc.ap.ssid_len = strlen(AP_SSID);
    snprintf((char *)wc.ap.password, sizeof(wc.ap.password), "%s", AP_PASS);
    wc.ap.channel = 6;
    wc.ap.max_connection = 2;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK)
    {
        err = esp_wifi_set_config(WIFI_IF_AP, &wc);
    }
    if (err == ESP_OK)
    {
        err = esp_wifi_start();
    }
    if (err == ESP_OK)
    {
        esp_wifi_set_ps(WIFI_PS_NONE);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "softap start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SoftAP ready: SSID=%s PASS=%s URL=http://192.168.4.1/",
             AP_SSID, AP_PASS);
    return ESP_OK;
}

esp_err_t camera_web_start(void)
{
    if (s_server != NULL)
    {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    snprintf(s_result, sizeof(s_result), "{\"note\":\"waiting for recognition\"}");

    esp_err_t err = wifi_init_softap();
    if (err != ESP_OK)
    {
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;
    config.stack_size = 10240;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    httpd_uri_t uri_index = {"/", HTTP_GET, index_handler, NULL};
    httpd_uri_t uri_status = {"/api/status", HTTP_GET, status_handler, NULL};
    httpd_uri_t uri_latest = {"/api/latest", HTTP_GET, latest_handler, NULL};
    httpd_uri_t uri_stream = {"/stream", HTTP_GET, stream_handler, NULL};

    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_latest);
    httpd_register_uri_handler(s_server, &uri_stream);

    ESP_LOGI(TAG, "web server started on http://192.168.4.1/");
    return ESP_OK;
}

void camera_web_set_result(const char *json)
{
    if (json == NULL || s_lock == NULL)
    {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        snprintf(s_result, sizeof(s_result), "%s", json);
        xSemaphoreGive(s_lock);
    }
}

void camera_web_set_camera_ready(int ready)
{
    s_camera_ready = ready ? 1 : 0;
}

void camera_web_publish(camera_fb_t *frame)
{
    if (frame == NULL || s_clients <= 0 || s_lock == NULL)
    {
        return;
    }

    uint8_t *jpg = NULL;
    size_t jpg_len = 0;
    if (!frame2jpg(frame, 60, &jpg, &jpg_len) || jpg == NULL || jpg_len == 0)
    {
        free(jpg);
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        free(jpg);
        return;
    }

    free(s_jpeg);
    s_jpeg = jpg;
    s_jpeg_len = jpg_len;
    s_seq++;
    s_frame_count++;

    int64_t now = esp_timer_get_time();
    if (s_last_pub_us > 0 && now > s_last_pub_us)
    {
        uint32_t inst = (uint32_t)(1000000LL / (now - s_last_pub_us));
        s_fps = (s_fps == 0) ? inst : (s_fps * 3 + inst) / 4;
    }
    s_last_pub_us = now;
    xSemaphoreGive(s_lock);
}
