#pragma once

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start SoftAP + HTTP MJPEG server. Safe to call once after NVS init. */
esp_err_t camera_web_start(void);

/* Publish one RGB565 frame (called from the AI task, after overlay drawing).
 * The frame is only JPEG-encoded when at least one browser is watching. */
void camera_web_publish(camera_fb_t *frame);

/* Latest recognition JSON shown on the web page. */
void camera_web_set_result(const char *json);

/* 0 while camera init is retrying, 1 after OV5640 is ready. */
void camera_web_set_camera_ready(int ready);

#ifdef __cplusplus
}
#endif
