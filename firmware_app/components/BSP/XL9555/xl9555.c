#include "xl9555.h"


i2c_obj_t xl9555_i2c_master;
static uint16_t xl9555_failed = 0;

/**
 * @brief       读取XL9555的16位IO值
 * @param       data：读取数据的存储区
 * @param       len：读取数据的大小
 * @retval      ESP_OK：读取成功；其他：读取失败
 */
esp_err_t xl9555_read_byte(uint8_t *data, size_t len)
{
    uint8_t memaddr_buf[1];
    memaddr_buf[0]  = XL9555_INPUT_PORT0_REG;

    i2c_buf_t bufs[2] = {
        {.len = 1, .buf = memaddr_buf},
        {.len = len, .buf = data},
    };

    return i2c_transfer(&xl9555_i2c_master, XL9555_ADDR, 2, bufs, I2C_FLAG_WRITE | I2C_FLAG_READ | I2C_FLAG_STOP);
}

/**
 * @brief       向XL9555写入16位IO值
 * @param       reg：寄存器地址
 * @param       data：要写入的数据
 * @param       len：要写入数据的大小
 * @retval      ESP_OK：读取成功；其他：读取失败
 */
esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_buf_t bufs[2] = {
        {.len = 1, .buf = &reg},
        {.len = len, .buf = data},
    };

    return i2c_transfer(&xl9555_i2c_master, XL9555_ADDR, 2, bufs, I2C_FLAG_STOP);
}

/**
 * @brief       控制某个IO的电平
 * @param       pin     : 控制的IO
 * @param       val     : 电平
 * @retval      返回所有IO状态
 */
uint16_t xl9555_pin_write(uint16_t pin, int val)
{
    uint8_t w_data[2];
    uint16_t temp = 0x0000;

    xl9555_read_byte(w_data, 2);

    if (pin <= KEY4_IO)
    {
        if (val)
        {
            w_data[0] |= (uint8_t)(0xFF & pin);
        }
        else
        {
            w_data[0] &= ~(uint8_t)(0xFF & pin);
        }
    }
    else
    {
        if (val)
        {
            w_data[1] |= (uint8_t)(0xFF & (pin >> 8));
        }
        else
        {
            w_data[1] &= ~(uint8_t)(0xFF & (pin >> 8));
        }
    }

    temp = ((uint16_t)w_data[1] << 8) | w_data[0]; 

    xl9555_write_byte(XL9555_OUTPUT_PORT0_REG, w_data, 2);

    return temp;
}

/**
 * @brief       获取某个IO状态
 * @param       pin     : 要获取状态的IO
 * @retval      此IO口的值(状态, 0/1)
 */
int xl9555_pin_read(uint16_t pin)
{
    uint16_t ret;
    uint8_t r_data[2];

    xl9555_read_byte(r_data, 2);

    ret = r_data[1] << 8 | r_data[0];

    return (ret & pin) ? 1 : 0;
}

/* 
 * XL9555 IO方向配置值（16位）：
 * - 0 = 输出，1 = 输入
 * - Port0 (低8位): 0xF0 = 1111 0000
 *   * P0.7-P0.4 (KEY4-KEY1) = 输入 (1)
 *   * P0.3-P0.0 = 输出 (0)
 * - Port1 (高8位): 0x00 = 0000 0000
 *   * P1.7-P1.0 = 全部输出 (0)
 * - 总计: 0x00F0
 */
#define XL9555_IO_CONFIG_VALUE     0x00F0

/**
 * @brief       XL9555的IO配置
 * @param       config_value：IO配置输入或者输出（1:输入,0:输出）
 * @retval      返回设置的数值
 */
uint16_t xl9555_ioconfig(uint16_t config_value)
{
    /* 从机地址 + CMD + data1(P0) + data2(P1) */
    /* P00、P01、P04、P05、P06、P07、P14、P15、P16、P17为输入，其他引脚为输出 -->1111 1111 0000 0011 注意：0为输出，1为输入*/
    uint8_t data[2];
    esp_err_t err;
    int retry = 3;

    data[0] = (uint8_t)(0xFF & config_value);
    data[1] = (uint8_t)(0xFF & (config_value >> 8));

    do
    {
        err = xl9555_write_byte(XL9555_CONFIG_PORT0_REG, data, 2);
        
        if (err != ESP_OK)
        {
            retry--;
            vTaskDelay(100); 
            ESP_LOGE("IIC", "%s configure %X failed, ret: %d", __func__, config_value, err);
            xl9555_failed = 1;
            
            if ((retry <= 0) && xl9555_failed)
            {
                vTaskDelay(5000); 
                esp_restart();
            }
        }
        else
        {
            xl9555_failed = 0;
            break;
        }
        
    } while (retry);
    
    return config_value;
}

/**
 * @brief       初始化XL9555
 * @param       无
 * @retval      无
 */
void xl9555_init(i2c_obj_t self)
{
    uint8_t r_data[2];

    if (self.init_flag == ESP_FAIL)
    {
        iic_init(I2C_NUM_0);        /* 初始化IIC */
    }

    xl9555_i2c_master = self;
    gpio_config_t gpio_init_struct = {0};
    
    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
    gpio_init_struct.mode = GPIO_MODE_INPUT;
    gpio_init_struct.pin_bit_mask = (1ull << XL9555_INT_IO);
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_init_struct);     /* 配置XL_INT引脚 */

    /* 上电先读取一次清除中断标志 */
    xl9555_read_byte(r_data, 2);
    
    /* 
     * IO方向配置（16位配置值：0x00F0）：
     * Port0 (低8位: 0xF0 = 1111 0000):
     *  - P0.0 (CAM_LED_IO, bit 0)  -> 输出 (0)
     *  - P0.1 (QMA_INT_IO, bit 1)  -> 输出 (0)
     *  - P0.2 (bit 2)              -> 输出 (0)
     *  - P0.3 (bit 3)              -> 输出 (0)
     *  - P0.4 (KEY1_IO, bit 4)     -> 输入 (1)
     *  - P0.5 (KEY2_IO, bit 5)     -> 输入 (1)
     *  - P0.6 (KEY3_IO, bit 6)     -> 输入 (1)
     *  - P0.7 (KEY4_IO, bit 7)     -> 输入 (1)
     * Port1 (高8位: 0x00 = 0000 0000):
     *  - P1.0 (AP_INT_IO, bit 8)   -> 输出 (0)
     *  - P1.1 (TP_INT_IO, bit 9)   -> 输出 (0)
     *  - P1.2-P1.5 (bit 10-13)     -> 输出 (0)
     *  - P1.6 (LCD_BL_IO, bit 14)  -> 输出 (0)
     *  - P1.7 (SPK_EN_IO, bit 15)  -> 输出 (0)
     */
    xl9555_ioconfig(XL9555_IO_CONFIG_VALUE);

    xl9555_pin_write(SPK_EN_IO, 1);
    xl9555_pin_write(CAM_LED_IO, 0);
}

/**
 * @brief       按键扫描函数
 * @param       mode:0->不连续;1->连续
 * @retval      键值, 定义如下:
 *              KEY1_PRES, 1, KEY1按下
 *              KEY2_PRES, 2, KEY2按下
 *              KEY3_PRES, 3, KEY3按下
 *              KEY4_PRES, 4, KEY4按下
 */
uint8_t xl9555_key_scan(uint8_t mode)
{
    uint8_t keyval = 0;
    static uint8_t key_up = 1;                                          /* 按键按松开标志 */

    if (mode)
    {
        key_up = 1;                                                     /* 支持连按 */
    }
    
    if (key_up && (KEY1 == 0 || KEY2 == 0 || KEY3 == 0  || KEY4 == 0 )) /* 按键松开标志为1, 且有任意一个按键按下了 */
    {
        vTaskDelay(10);                                                 /* 去抖动 */
        key_up = 0;

        if (KEY1 == 0)
        {
            keyval = KEY1_PRES;
        }

        if (KEY2 == 0)
        {
            keyval = KEY2_PRES;
        }

        if (KEY3 == 0)
        {
            keyval = KEY3_PRES;
        }

        if (KEY4 == 0)
        {
            keyval = KEY4_PRES;
        }
    }
    else if (KEY1 == 1 && KEY2 == 1 && KEY3 == 1 && KEY4 == 1)          /* 没有任何按键按下, 标记按键松开 */
    {
        key_up = 1;
    }

    return keyval;                                                      /* 返回键值 */
}
