#include "esp_card_recognition.hpp"

#include "card_classifier_mahjong.hpp"
#include "card_classifier_poker.hpp"
#include "card_labels.hpp"
#include "dl_define.hpp"
#include "dl_image.hpp"
#include "fb_gfx.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#define CARD_COLOR_GREEN  0x07E0
#define CARD_COLOR_RED    0xF800
#define CARD_COLOR_YELLOW 0xFFE0
#define CARD_COLOR_WHITE  0xFFFF

#define INPUT_SIZE 96
#define INPUT_EXPONENT (-7)

QueueHandle_t xQueueFrameO = NULL;
QueueHandle_t xQueueAIFrameO = NULL;

static TaskHandle_t s_camera_task = NULL;
static TaskHandle_t s_ai_task = NULL;
static TaskHandle_t s_key_task = NULL;

static volatile int s_mode = 0;      /* 0: mahjong, 1: poker */
static volatile int s_show_conf = 1; /* long press toggles */

static const char *TAG = "card";

/**
 * @brief 摄像头采集任务：把 frame 指针送入队列，不拷贝图像。
 */
static void card_camera_task(void *arg)
{
    (void)arg;
    camera_fb_t *frame = NULL;
    while (1)
    {
        frame = esp_camera_fb_get();
        if (frame)
        {
            xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
        }
    }
}

/**
 * @brief BOOT 键（GPIO0）：短按切换麻将/扑克，长按切换是否显示置信度。
 */
static void card_key_task(void *arg)
{
    (void)arg;
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << GPIO_NUM_0;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    int last = 1;
    int64_t press_us = 0;
    while (1)
    {
        int level = gpio_get_level(GPIO_NUM_0);
        if (last == 1 && level == 0)
        {
            press_us = esp_timer_get_time();
        }
        else if (last == 0 && level == 1)
        {
            int64_t dt = esp_timer_get_time() - press_us;
            if (dt > 500000)
            {
                s_show_conf = !s_show_conf;
            }
            else
            {
                s_mode ^= 1;
            }
            ESP_LOGI(TAG, "mode=%s show_conf=%d", s_mode ? "POKER" : "MAHJONG", s_show_conf);
        }
        last = level;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

/**
 * @brief 把摄像头 RGB565 中央区域裁剪并缩放到 96x96，归一化后量化成 int8。
 *
 * 训练时使用 PIL RGB + (x/255-0.5)/0.5；ESP-DL 的
 * convert_pixel_rgb565_to_rgb888 输出顺序是 B,G,R，所以这里重新排成 R,G,B。
 */
static void card_prepare_input(const uint16_t *pixels, int width, int height, int8_t *out)
{
    int side = (width < height ? width : height) * 4 / 5;
    int x0 = (width - side) / 2;
    int y0 = (height - side) / 2;

    for (int y = 0; y < INPUT_SIZE; y++)
    {
        int sy = y0 + (y * side) / INPUT_SIZE;
        for (int x = 0; x < INPUT_SIZE; x++)
        {
            int sx = x0 + (x * side) / INPUT_SIZE;
            uint16_t p = pixels[sy * width + sx];
            uint8_t bgr[3];
            dl::image::convert_pixel_rgb565_to_rgb888(p, bgr);

            float r = (bgr[2] / 255.0f - 0.5f) * 2.0f;
            float g = (bgr[1] / 255.0f - 0.5f) * 2.0f;
            float b = (bgr[0] / 255.0f - 0.5f) * 2.0f;

            int idx = (y * INPUT_SIZE + x) * 3;
            out[idx + 0] = (int8_t)DL_CLIP(r * 128.0f, -128, 127);
            out[idx + 1] = (int8_t)DL_CLIP(g * 128.0f, -128, 127);
            out[idx + 2] = (int8_t)DL_CLIP(b * 128.0f, -128, 127);
        }
    }
}

/**
 * @brief AI 任务：中央裁剪 -> 分类 -> 画框/文字 -> 送 LCD 队列。
 */
static void card_ai_task(void *arg)
{
    (void)arg;

    static CardClassifierMahjong s_mj;
    static CardClassifierPoker s_pk;
    static int8_t s_input_buf[INPUT_SIZE * INPUT_SIZE * 3];

    dl::Tensor<int8_t> input;
    input.set_element(s_input_buf)
        .set_exponent(INPUT_EXPONENT)
        .set_shape({INPUT_SIZE, INPUT_SIZE, 3})
        .set_auto_free(false);

    s_mj.build(input);
    s_pk.build(input);
    ESP_LOGI(TAG, "card models built");

    camera_fb_t *frame = NULL;
    while (1)
    {
        if (!xQueueReceive(xQueueFrameO, &frame, portMAX_DELAY) || frame == NULL)
        {
            continue;
        }

        int width = frame->width;
        int height = frame->height;
        card_prepare_input((const uint16_t *)frame->buf, width, height, s_input_buf);

        int8_t *score = NULL;
        int class_num = 0;
        const char **names = NULL;

        if (s_mode == 0)
        {
            s_mj.call(input);
            score = s_mj.get_output();
            class_num = 42;
            names = MJ_NAMES;
        }
        else
        {
            s_pk.call(input);
            score = s_pk.get_output();
            class_num = 52;
            names = PK_NAMES;
        }

        int best = 0;
        int max_score = score[0];
        for (int i = 1; i < class_num; i++)
        {
            if (score[i] > max_score)
            {
                max_score = score[i];
                best = i;
            }
        }

        /* 输出指数 -4：logit_real = score * 2^-4 */
        float sum = 0.0f;
        for (int i = 0; i < class_num; i++)
        {
            sum += expf((score[i] - max_score) * 0.0625f);
        }
        float conf = 1.0f / sum;

        int side = (width < height ? width : height) * 4 / 5;
        int x0 = (width - side) / 2;
        int y0 = (height - side) / 2;
        dl::image::draw_hollow_rectangle((uint16_t *)frame->buf, height, width,
                                         x0, y0, x0 + side, y0 + side, CARD_COLOR_GREEN);

        char label[64];
        if (conf >= 0.45f)
        {
            snprintf(label, sizeof(label), "%s %.2f", names[best], conf);
        }
        else
        {
            snprintf(label, sizeof(label), "?");
        }
        fb_gfx_print(frame, 4, 4, CARD_COLOR_YELLOW, label);
        fb_gfx_printf(frame, 4, 20, CARD_COLOR_GREEN, "MODE:%s", s_mode ? "POKER" : "MAHJONG");
        if (s_show_conf)
        {
            fb_gfx_printf(frame, 4, 36, CARD_COLOR_GREEN, "CONF:%.2f", conf);
        }

        if (xQueueAIFrameO)
        {
            xQueueSend(xQueueAIFrameO, &frame, portMAX_DELAY);
        }
        else
        {
            esp_camera_fb_return(frame);
        }
    }
}

uint8_t esp_card_recognition_ai_start(void)
{
    xQueueFrameO = xQueueCreate(5, sizeof(camera_fb_t *));
    xQueueAIFrameO = xQueueCreate(5, sizeof(camera_fb_t *));

    xTaskCreatePinnedToCore(card_camera_task, "card_camera", 4 * 1024, NULL, 5, &s_camera_task, 1);
    xTaskCreatePinnedToCore(card_ai_task, "card_ai", 16 * 1024, NULL, 5, &s_ai_task, 1);
    xTaskCreatePinnedToCore(card_key_task, "card_key", 3 * 1024, NULL, 5, &s_key_task, 0);

    if (xQueueFrameO == NULL || xQueueAIFrameO == NULL || s_camera_task == NULL || s_ai_task == NULL)
    {
        return 1;
    }
    return 0;
}

void esp_card_recognition_ai_deinit(void)
{
    if (xQueueFrameO)
    {
        vQueueDelete(xQueueFrameO);
        xQueueFrameO = NULL;
    }
    if (xQueueAIFrameO)
    {
        vQueueDelete(xQueueAIFrameO);
        xQueueAIFrameO = NULL;
    }
    if (s_camera_task)
    {
        vTaskDelete(s_camera_task);
        s_camera_task = NULL;
    }
    if (s_ai_task)
    {
        vTaskDelete(s_ai_task);
        s_ai_task = NULL;
    }
    if (s_key_task)
    {
        vTaskDelete(s_key_task);
        s_key_task = NULL;
    }
}
