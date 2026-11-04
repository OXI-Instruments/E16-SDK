cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(e16_sdk C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json in the build folder gives editors (clangd, VS Code) the include paths.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/external/stm32f4xx_hal_driver/Inc OR NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/external/tinyusb/src)
    message(FATAL_ERROR "Vendor libraries missing. Run: git submodule update --init")
endif()

add_subdirectory(bsp)
include(cmake/e16.cmake)

option(E16_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
if(E16_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
