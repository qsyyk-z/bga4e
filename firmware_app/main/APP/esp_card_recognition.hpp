#ifndef __ESP_CARD_RECOGNITION_H
#define __ESP_CARD_RECOGNITION_H

#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

extern QueueHandle_t xQueueAIFrameO;

/**
 * @brief 创建摄像头/AI/按键任务与队列。
 * @return 0 成功，1 失败
 */
uint8_t esp_card_recognition_ai_start(void);

/**
 * @brief 删除任务与队列。
 */
void esp_card_recognition_ai_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
