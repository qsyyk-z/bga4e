

#ifndef __SPI_H
#define __SPI_H

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"


/* 引脚定义 */
#define SPI_MOSI_GPIO_PIN   GPIO_NUM_47         /* SPI2_MOSI */
#define SPI_CLK_GPIO_PIN    GPIO_NUM_21         /* SPI2_CLK */
#define SPI_MISO_GPIO_PIN   GPIO_NUM_14         /* SPI2_MISO */

/* 函数声明 */
void spi2_init(void);                                                               /* 初始化SPI2 */
void spi2_write_cmd(spi_device_handle_t handle, uint8_t cmd);                       /* SPI发送命令 */
void spi2_write_data(spi_device_handle_t handle, const uint8_t *data, int len);     /* SPI发送数据 */

#endif
