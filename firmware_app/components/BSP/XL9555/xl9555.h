#ifndef __XL9555_H
#define __XL9555_H

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iic.h"


/* 引脚与相关参数定义 */
#define XL9555_INT_IO               GPIO_NUM_40                     /* XL9555_INT引脚 */
#define XL9555_INT                  gpio_get_level(XL9555_INT_IO)   /* 读取XL9555_INT的电平 */

/* XL9555命令宏 */
#define XL9555_INPUT_PORT0_REG      0                               /* 输入寄存器0地址 */
#define XL9555_INPUT_PORT1_REG      1                               /* 输入寄存器1地址 */
#define XL9555_OUTPUT_PORT0_REG     2                               /* 输出寄存器0地址 */
#define XL9555_OUTPUT_PORT1_REG     3                               /* 输出寄存器1地址 */
#define XL9555_INVERSION_PORT0_REG  4                               /* 极性反转寄存器0地址 */
#define XL9555_INVERSION_PORT1_REG  5                               /* 极性反转寄存器1地址 */
#define XL9555_CONFIG_PORT0_REG     6                               /* 方向配置寄存器0地址 */
#define XL9555_CONFIG_PORT1_REG     7                               /* 方向配置寄存器1地址 */

#define XL9555_ADDR                 0X20                            /* XL9555地址(左移了一位) */

/* XL9555各个IO的功能 */

#define CAM_LED_IO                  0x0001
#define QMA_INT_IO                  0x0002
#define KEY1_IO                     0x0010
#define KEY2_IO                     0x0020
#define KEY3_IO                     0x0040
#define KEY4_IO                     0x0080
#define AP_INT_IO                   0x0100
#define TP_INT_IO                   0x0200
#define LCD_BL_IO                   0x4000
#define SPK_EN_IO                   0x8000

#define KEY1                        xl9555_pin_read(KEY1_IO)        /* 读取KEY1引脚 */
#define KEY2                        xl9555_pin_read(KEY2_IO)        /* 读取KEY2引脚 */
#define KEY3                        xl9555_pin_read(KEY3_IO)        /* 读取KEY3引脚 */
#define KEY4                        xl9555_pin_read(KEY4_IO)        /* 读取KEY4引脚 */

#define KEY1_PRES                   1                               /* KEY1按下 */
#define KEY2_PRES                   2                               /* KEY2按下 */
#define KEY3_PRES                   3                               /* KEY3按下 */
#define KEY4_PRES                   4                               /* KEY4按下 */

/* 函数声明 */
void xl9555_init(i2c_obj_t self);                                   /* 初始化XL9555 */
int xl9555_pin_read(uint16_t pin);                                  /* 获取某个IO状态 */
uint16_t xl9555_pin_write(uint16_t pin, int val);                   /* 控制某个IO的电平 */
esp_err_t xl9555_read_byte(uint8_t* data, size_t len);              /* 读取XL9555的16位IO值 */
uint8_t xl9555_key_scan(uint8_t mode);                              /* 扫描按键值 */

#endif
