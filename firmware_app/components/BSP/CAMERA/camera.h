 #ifndef __CAMERA_H__
 #define __CAMERA_H__
 
 #include "esp_log.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "esp_camera.h"
 #include "esp_system.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "driver/gpio.h"
 #include "xl9555.h"
 #include "lcd.h"
 
 
 /* Waveshare ESP32-S3-CAM-OV5640 (ESP_EYE pin map).
 * The running firmware on this board reports the same SCCB pins
 * (SDA=IO8, SCL=IO7) and detects the OV5640 successfully. */
#define CAM_PIN_PWDN     GPIO_NUM_NC
#define CAM_PIN_RESET    GPIO_NUM_NC
#define CAM_PIN_XCLK     GPIO_NUM_38
#define CAM_PIN_SIOD     GPIO_NUM_8
#define CAM_PIN_SIOC     GPIO_NUM_7
#define CAM_PIN_D7       GPIO_NUM_21
#define CAM_PIN_D6       GPIO_NUM_39
#define CAM_PIN_D5       GPIO_NUM_40
#define CAM_PIN_D4       GPIO_NUM_42
#define CAM_PIN_D3       GPIO_NUM_46
#define CAM_PIN_D2       GPIO_NUM_48
#define CAM_PIN_D1       GPIO_NUM_47
#define CAM_PIN_D0       GPIO_NUM_45
#define CAM_PIN_VSYNC    GPIO_NUM_17
#define CAM_PIN_HREF     GPIO_NUM_18
#define CAM_PIN_PCLK     GPIO_NUM_41

/* Camera-only build: no XL9555, camera power is already enabled. */
#define CAM_LED(x)         do { (void)(x); } while (0)
 
 /* 函数声明 */
 uint8_t camera_init(void);                 /* 摄像头初始化 */
 void camera_show(uint16_t x, uint16_t y);  /* 摄像头显示 */
 #endif
 