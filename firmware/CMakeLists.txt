# ESP-IDF Project CMakeLists
cmake_minimum_required(VERSION 3.16)

# Bluepad32 is used as the stable Bluetooth gamepad backend.
# It is fetched during CMake configure so CI and local builds stay reproducible
# without manually copying third-party source into this repository.
set(BLUEPAD32_ROOT "${CMAKE_BINARY_DIR}/_deps/bluepad32")
set(BLUEPAD32_REPO "https://github.com/ricardoquesada/bluepad32.git")
set(BLUEPAD32_BRANCH "main")

if(NOT EXISTS "${BLUEPAD32_ROOT}/src/components/bluepad32/CMakeLists.txt")
    message(STATUS "Fetching Bluepad32 from ${BLUEPAD32_REPO}")
    execute_process(
        COMMAND git clone --depth 1 --branch ${BLUEPAD32_BRANCH} --recurse-submodules --shallow-submodules ${BLUEPAD32_REPO} ${BLUEPAD32_ROOT}
        RESULT_VARIABLE BLUEPAD32_CLONE_RESULT
    )
    if(NOT BLUEPAD32_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to fetch Bluepad32")
    endif()
endif()

set(BTSTACK_ROOT "${BLUEPAD32_ROOT}/external/btstack")
set(BTSTACK_ESP32_PORT "${BTSTACK_ROOT}/port/esp32")
set(BTSTACK_COMPONENT_DIR "${BTSTACK_ESP32_PORT}/components/btstack")

# Bluepad32 requires BTstack to be generated/installed as an ESP-IDF component.
# Upstream recommends running external/btstack/port/esp32/integrate_btstack.py.
if(NOT EXISTS "${BTSTACK_COMPONENT_DIR}/CMakeLists.txt")
    message(STATUS "Integrating BTstack ESP-IDF component")
    execute_process(
        COMMAND python3 integrate_btstack.py
        WORKING_DIRECTORY ${BTSTACK_ESP32_PORT}
        RESULT_VARIABLE BTSTACK_INTEGRATE_RESULT
    )
    if(NOT BTSTACK_INTEGRATE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to integrate BTstack component")
    endif()
endif()

# Project components + Bluepad32 / BTstack components
set(EXTRA_COMPONENT_DIRS
    "${BLUEPAD32_ROOT}/src/components"
    "${BTSTACK_ESP32_PORT}/components"
    "components/ps4"
    "components/control"
    "components/motor"
    "components/motion"
    "components/safety"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(track-robot)
