# ESP-IDF Project CMakeLists
cmake_minimum_required(VERSION 3.16)

# Set the components directory
set(EXTRA_COMPONENT_DIRS
    "components/ps3"
    "components/control"
    "components/motor"
    "components/motion"
    "components/safety"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(track-robot)
