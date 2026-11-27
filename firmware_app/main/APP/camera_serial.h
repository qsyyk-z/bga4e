#pragma once

#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * USB Serial/JTAG image stream.
 *
 * Wire format (little endian):
 *   magic[4] = A5 5A 42 47
 *   uint32_t jpeg_len
 *   uint32_t seq
 *   jpeg bytes
 *
 * This is intentionally independent of the SoftAP/web module: the PC viewer
 * reads the same USB cable that is used for flashing and serves a local page.
 */
void camera_serial_start(void);
void camera_serial_publish(camera_fb_t *frame);

#ifdef __cplusplus
}
#endif
