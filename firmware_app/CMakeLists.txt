# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# 显式指定目标，避免 VSCode 直接调用 cmake 时 IDF_TARGET 为空
set(IDF_TARGET "esp32s3" CACHE STRING "IDF target" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bga4e)
