#include "camera_serial.h"

#include <cstdlib>
#include <cstring>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "img_converters.h"

namespace
{
constexpr uint8_t kMagic[4] = {0xA5, 0x5A, 0x42, 0x47};
constexpr int64_t kMinIntervalUs = 180000;  // ~5 fps ceiling
static SemaphoreHandle_t s_lock = nullptr;
static uint32_t s_seq = 0;
static int64_t s_last_us = 0;
static bool s_started = false;

void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
}  // namespace

void camera_serial_start(void)
{
    if (s_started)
    {
        return;
    }

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 8192;
    cfg.rx_buffer_size = 256;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE("camera_serial", "usb driver install failed: %s", esp_err_to_name(err));
        return;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == nullptr)
    {
        ESP_LOGE("camera_serial", "mutex allocation failed");
        return;
    }

    // Keep the USB link clean; the PC parser resynchronizes on the magic
    // header if an error log slips between frames.
    esp_log_level_set("*", ESP_LOG_ERROR);
    s_started = true;
}

void camera_serial_publish(camera_fb_t *frame)
{
    if (!s_started || frame == nullptr || s_lock == nullptr)
    {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (s_last_us != 0 && now - s_last_us < kMinIntervalUs)
    {
        return;
    }

    uint8_t *jpg = nullptr;
    size_t jpg_len = 0;
    if (!frame2jpg(frame, 60, &jpg, &jpg_len) || jpg == nullptr || jpg_len == 0)
    {
        free(jpg);
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE)
    {
        free(jpg);
        return;
    }

    const size_t total = 12 + jpg_len;
    uint8_t *packet = (uint8_t *)malloc(total);
    if (packet == nullptr)
    {
        xSemaphoreGive(s_lock);
        free(jpg);
        return;
    }

    memcpy(packet, kMagic, sizeof(kMagic));
    put_u32(packet + 4, (uint32_t)jpg_len);
    put_u32(packet + 8, ++s_seq);
    memcpy(packet + 12, jpg, jpg_len);

    size_t sent = 0;
    while (sent < total)
    {
        size_t chunk = total - sent;
        if (chunk > 4096)
        {
            chunk = 4096;
        }
        int n = usb_serial_jtag_write_bytes(packet + sent, chunk,
                                            pdMS_TO_TICKS(1000));
        if (n <= 0)
        {
            break;
        }
        sent += (size_t)n;
    }

    free(packet);
    free(jpg);
    xSemaphoreGive(s_lock);
    s_last_us = now;
}
