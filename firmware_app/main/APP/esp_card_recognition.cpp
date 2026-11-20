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

static const char *card_mahjong_name(const char *code)
{
    static const char *ranks[] = {"一", "二", "三", "四", "五", "六", "七", "八", "九"};
    if (code == NULL) return "未知";
    if (code[0] >= '1' && code[0] <= '9' && code[1] != '\0')
    {
        int r = code[0] - '1';
        if (code[1] == 'W') return ranks[r]; /* caller appends 万 */
        if (code[1] == 'B') return ranks[r];
        if (code[1] == 'P') return ranks[r];
    }
    if (strcmp(code, "EAST") == 0) return "东";
    if (strcmp(code, "SOUTH") == 0) return "南";
    if (strcmp(code, "WEST") == 0) return "西";
    if (strcmp(code, "NORTH") == 0) return "北";
    if (strcmp(code, "RED") == 0) return "中";
    if (strcmp(code, "GREEN") == 0) return "发";
    if (strcmp(code, "WHITE") == 0) return "白";
    if (strcmp(code, "PLUM") == 0) return "梅";
    if (strcmp(code, "ORCHID") == 0) return "兰";
    if (strcmp(code, "BAMBOO") == 0) return "竹";
    if (strcmp(code, "CHRYS") == 0) return "菊";
    if (strcmp(code, "SPRING") == 0) return "春";
    if (strcmp(code, "SUMMER") == 0) return "夏";
    if (strcmp(code, "AUTUMN") == 0) return "秋";
    if (strcmp(code, "WINTER") == 0) return "冬";
    return "未知";
}

/* 1W -> 一万, 5B -> 五条, 9P -> 九筒; 字牌直接返回中文。 */
static void card_mahjong_full_name(const char *code, char *out, size_t out_size)
{
    const char *base = card_mahjong_name(code);
    snprintf(out, out_size, "%s", base);
    if (code != NULL && code[0] >= '1' && code[0] <= '9' && code[1] != '\0')
    {
        const char *suffix = code[1] == 'W' ? "万" : (code[1] == 'B' ? "条" : "筒");
        snprintf(out, out_size, "%s%s", base, suffix);
    }
}

static void card_poker_rank_suit(const char *code, char *rank, size_t rank_size, char *suit)
{
    size_t len = code ? strlen(code) : 0;
    if (len < 2)
    {
        snprintf(rank, rank_size, "?");
        *suit = '?';
        return;
    }
    *suit = code[len - 1];
    size_t n = len - 1;
    if (n >= rank_size) n = rank_size - 1;
    memcpy(rank, code, n);
    rank[n] = '\0';
}

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

        /* 串口协议：每帧一行 JSON，PC 端 tools/serial_bridge.py 可直接写入
           <game>/cards.jsonl。麻将字段 name，扑克字段 rank/suit，box 为裁剪框。 */
        if (s_mode)
        {
            char rank[8];
            char suit = '?';
            card_poker_rank_suit(names[best], rank, sizeof(rank), &suit);
            ESP_LOGI("CARD_RESULT",
                     "{\"mode\":\"poker\",\"rank\":\"%s\",\"suit\":\"%c\",\"conf\":%.3f,\"box\":[%d,%d,%d,%d]}",
                     rank, suit, conf, x0, y0, side, side);
        }
        else
        {
            char mj_name[24];
            card_mahjong_full_name(names[best], mj_name, sizeof(mj_name));
            ESP_LOGI("CARD_RESULT",
                     "{\"mode\":\"mahjong\",\"name\":\"%s\",\"conf\":%.3f,\"box\":[%d,%d,%d,%d]}",
                     mj_name, conf, x0, y0, side, side);
        }
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
