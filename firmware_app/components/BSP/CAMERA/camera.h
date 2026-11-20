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
 
 
 /* DNESP32S3 (ALIENTEK) camera connector pinout.
  * PWDN/RESET are driven by XL9555 P0.4/P0.5, not ESP32 GPIOs.
  * SCCB uses I2C0: SDA=IO39, SCL=IO38 (see iic.h). */
 #define CAM_PIN_PWDN     GPIO_NUM_NC
 #define CAM_PIN_RESET    GPIO_NUM_NC
 #define CAM_PIN_XCLK     GPIO_NUM_NC  /* OV5640 module has its own clock */
 #define CAM_PIN_SIOD     GPIO_NUM_39  /* IIC0_SDA */
 #define CAM_PIN_SIOC     GPIO_NUM_38  /* IIC0_SCL */
 #define CAM_PIN_D7       GPIO_NUM_18
 #define CAM_PIN_D6       GPIO_NUM_17
 #define CAM_PIN_D5       GPIO_NUM_16
 #define CAM_PIN_D4       GPIO_NUM_15
 #define CAM_PIN_D3       GPIO_NUM_7
 #define CAM_PIN_D2       GPIO_NUM_6
 #define CAM_PIN_D1       GPIO_NUM_5
 #define CAM_PIN_D0       GPIO_NUM_4
 #define CAM_PIN_VSYNC    GPIO_NUM_47
 #define CAM_PIN_HREF     GPIO_NUM_48
 #define CAM_PIN_PCLK     GPIO_NUM_45
 
 #define CAM_LED(x)         do{ x ? \
                                 (xl9555_pin_write(CAM_LED_IO, 1)):       \
                                 (xl9555_pin_write(CAM_LED_IO, 0));       \
                             }while(0)
 
 /* 函数声明 */
 uint8_t camera_init(void);                 /* 摄像头初始化 */
 void camera_show(uint16_t x, uint16_t y);  /* 摄像头显示 */
 #endif
 