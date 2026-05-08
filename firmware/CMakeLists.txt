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

# Project components + Bluepad32 components
set(EXTRA_COMPONENT_DIRS
    "${BLUEPAD32_ROOT}/src/components"
    "components/ps4"
    "components/control"
    "components/motor"
    "components/motion"
    "components/safety"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(track-robot)
