#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "camera.h"
#include "esp_card_recognition.hpp"
#include "camera_serial.h"

extern QueueHandle_t xQueueAIFrameO;

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Camera-only build: no SoftAP and no LCD.  The PC viewer reads JPEG
     * frames from the same USB cable used for flashing. */
    printf("BGA4E camera: initializing OV5640...\n");
    while (camera_init())
    {
        printf("BGA4E camera: init failed, retrying...\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("BGA4E camera: OV5640 ready\n");

    while (esp_card_recognition_ai_start())
    {
        printf("BGA4E camera: create AI task/queue failed, retrying...\n");
        esp_card_recognition_ai_deinit();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    camera_serial_start();
    printf("BGA4E camera: USB image stream starting\n");

    /* No LCD on this camera-only test build. Drain the annotated-frame queue
     * so the AI task never blocks after publishing a frame. */
    while (1)
    {
        camera_fb_t *frame = NULL;
        if (xQueueAIFrameO &&
            xQueueReceive(xQueueAIFrameO, &frame, 0) == pdTRUE &&
            frame != NULL)
        {
            esp_camera_fb_return(frame);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
