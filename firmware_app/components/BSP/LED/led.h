#ifndef __LED_H_
#define __LED_H_

#include "driver/gpio.h"


/* 引脚定义 */
#define LED1_GPIO_PIN    GPIO_NUM_43  /* LED连接的GPIO端口 */
#define LED2_GPIO_PIN    GPIO_NUM_44  /* LED连接的GPIO端口 */

/* 引脚的输出的电平状态 */
enum GPIO_OUTPUT_STATE
{
    PIN_RESET,
    PIN_SET
};

/* LED端口定义 */
#define LED1(x)          do { x ?                                      \
                             gpio_set_level(LED1_GPIO_PIN, PIN_SET) :  \
                             gpio_set_level(LED1_GPIO_PIN, PIN_RESET); \
                        } while(0)  /* LED翻转 */

#define LED2(x)          do { x ?                                      \
                            gpio_set_level(LED2_GPIO_PIN, PIN_SET) :  \
                            gpio_set_level(LED2_GPIO_PIN, PIN_RESET); \
                        } while(0)  /* LED翻转 */

/* LED取反定义 */
#define LED1_TOGGLE()    do { gpio_set_level(LED1_GPIO_PIN, !gpio_get_level(LED1_GPIO_PIN)); } while(0)  /* LED翻转 */
#define LED2_TOGGLE()    do { gpio_set_level(LED2_GPIO_PIN, !gpio_get_level(LED2_GPIO_PIN)); } while(0)  /* LED翻转 */


/* 函数声明*/
void led_init(void);    /* 初始化LED */

#endif
