#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "camera.h"
#include "led.h"
#include "lcd.h"
#include "esp_card_recognition.hpp"
#include "driver/i2c.h"

static void scan_i2c_bus(i2c_port_t port, const char *name)
{
    printf("I2C scan %s (SDA=%d SCL=%d):", name,
           port == I2C_NUM_0 ? IIC0_SDA_GPIO_PIN : IIC1_SDA_GPIO_PIN,
           port == I2C_NUM_0 ? IIC0_SCL_GPIO_PIN : IIC1_SCL_GPIO_PIN);
    for (uint8_t addr = 1; addr < 127; ++addr)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(port, cmd, 30 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK)
        {
            printf(" 0x%02X", addr);
        }
    }
    printf("\n");
}

i2c_obj_t i2c0_master;
i2c_obj_t i2c1_master;
extern QueueHandle_t xQueueAIFrameO;
static camera_fb_t *card_ai_frame = NULL;

/**
 * @brief 从 AI 队列取一帧（已画好框/文字），送到 LCD 显示。
 */
static void lcd_card_camera(uint16_t x, uint16_t y)
{
    if (xQueueReceive(xQueueAIFrameO, &card_ai_frame, portMAX_DELAY) == pdTRUE)
    {
        if (card_ai_frame == NULL)
        {
            return;
        }

        int width = card_ai_frame->width;
        int height = card_ai_frame->height;
        if (x + width > lcd_self.width || y + height > lcd_self.height)
        {
            printf("image exceeds LCD area\r\n");
            esp_camera_fb_return(card_ai_frame);
            card_ai_frame = NULL;
            return;
        }

        size_t total = (size_t)width * height * 2;
        if (total > LCD_TOTAL_BUF_SIZE)
        {
            total = LCD_TOTAL_BUF_SIZE;
        }
        memcpy(lcd_buf, card_ai_frame->buf, total);

        lcd_set_window(x, y, x + width - 1, y + height - 1);
        for (size_t off = 0; off < total; off += LCD_BUF_SIZE)
        {
            size_t n = total - off;
            if (n > LCD_BUF_SIZE)
            {
                n = LCD_BUF_SIZE;
            }
            lcd_write_data(&lcd_buf[off], (int)n);
        }

        esp_camera_fb_return(card_ai_frame);
        card_ai_frame = NULL;
    }
}

void app_main(void)
{
    uint8_t counter = 0;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_init();
    i2c0_master = iic_init(I2C_NUM_0);
    i2c1_master = iic_init(I2C_NUM_1);
    scan_i2c_bus(I2C_NUM_0, "CAMERA/SCCB");
    scan_i2c_bus(I2C_NUM_1, "XL9555");
    spi2_init();
    xl9555_init(i2c1_master);
    lcd_init();

    lcd_show_string(20, 50, 280, 16, 16, "ESP32-S3", RED);
    lcd_show_string(20, 70, 280, 16, 16, "CARD RECOGNITION", RED);
    lcd_show_string(20, 90, 280, 16, 16, "MAHJONG / POKER", BLUE);

    while (camera_init())
    {
        lcd_show_string(20, 110, 280, 16, 16, "CAMERA Fail!", BLUE);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    lcd_clear(BLACK);

    while (esp_card_recognition_ai_start())
    {
        lcd_show_string(20, 110, 280, 16, 16, "Create Task/Queue Fail!", BLUE);
        esp_card_recognition_ai_deinit();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (1)
    {
        lcd_card_camera(0, 0);
        counter++;
        if (counter % 20 == 0)
        {
            LED1_TOGGLE();
        }
        vTaskDelay(1);
    }
}
