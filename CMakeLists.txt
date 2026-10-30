cmake_minimum_required(VERSION 3.16)

# VSCode 直接调用 cmake 时不会像 idf.py 那样从 sdkconfig 推断目标，
# 这里显式指定 ESP32-S3，避免 IDF_TARGET 为空与 sdkconfig 中的 esp32s3 冲突。
set(IDF_TARGET "esp32s3" CACHE STRING "IDF target" FORCE)

# 仓库根目录也可以直接作为 ESP-IDF 工程构建。
# 实际源码在 firmware_app/ 下，这里把它的 main 和 components 注册到本工程。
#
# 同时把 firmware_app/sdkconfig 和分区表复制到根目录，
# 这样 VSCode 以仓库根目录为 -S、根目录 sdkconfig 为 -DSDKCONFIG 时也能直接 build。

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/sdkconfig"
        "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig"
        COPYONLY)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/partitions-16MiB.csv")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/partitions-16MiB.csv"
        "${CMAKE_CURRENT_SOURCE_DIR}/partitions-16MiB.csv"
        COPYONLY)
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/main"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/components/BSP"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/components/esp-dl"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/components/esp32-camera"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/components/esp-code-scanner"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/components/fb_gfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware_app/components/modules"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bga4e)
