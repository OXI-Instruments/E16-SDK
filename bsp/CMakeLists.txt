set(EXT ${CMAKE_CURRENT_SOURCE_DIR}/../external)
set(HAL ${EXT}/stm32f4xx_hal_driver)
set(DEVICE ${EXT}/cmsis_device_f4)
set(TUSB ${EXT}/tinyusb/src)
# For firmware that compiles TinyUSB class drivers of its own (usb.h).
set(E16_TINYUSB_DIR ${TUSB} CACHE INTERNAL "")

# platform/tinyusb-gd32.patch is made for this TinyUSB version. git apply also
# accepts newer versions (with offsets), but the firmware then hangs during
# USB start-up, so check the version itself.
set(TUSB_EXPECTED_VERSION 0.21.0)
file(STRINGS ${TUSB}/tusb_option.h tusb_version_lines REGEX "#define TUSB_VERSION_(MAJOR|MINOR|REVISION) ")
string(REGEX REPLACE ".*MAJOR +([0-9]+).*MINOR +([0-9]+).*REVISION +([0-9]+).*" "\\1.\\2.\\3"
    tusb_version "${tusb_version_lines}")
if(NOT tusb_version VERSION_EQUAL TUSB_EXPECTED_VERSION)
    message(FATAL_ERROR "external/tinyusb is version ${tusb_version}; the SDK needs ${TUSB_EXPECTED_VERSION}.\n"
        "Run: git -C external/tinyusb checkout ${TUSB_EXPECTED_VERSION}")
endif()

set(HAL_MODULES
    hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_exti hal_dma hal_dma_ex
    hal_pwr hal_pwr_ex hal_flash hal_flash_ex hal_uart hal_spi hal_tim hal_tim_ex
)
list(TRANSFORM HAL_MODULES PREPEND ${HAL}/Src/stm32f4xx_)
list(TRANSFORM HAL_MODULES APPEND .c)

# TinyUSB's DWC2 driver needs changes for the GD32F405's USB core, see
# platform/tinyusb-gd32.patch. Build it from patched copies, so the submodule
# stays untouched.
find_package(Git REQUIRED)
set(DWC2 ${TUSB}/portable/synopsys/dwc2)
set(DWC2_COPY ${CMAKE_CURRENT_BINARY_DIR}/tinyusb)
set(DWC2_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/platform/tinyusb-gd32.patch)
file(REMOVE_RECURSE ${DWC2_COPY})
file(COPY ${DWC2}/dcd_dwc2.c ${DWC2}/dwc2_common.c DESTINATION ${DWC2_COPY})
execute_process(
    COMMAND ${GIT_EXECUTABLE} apply ${DWC2_PATCH}
    WORKING_DIRECTORY ${DWC2_COPY}
    RESULT_VARIABLE patch_result
    ERROR_VARIABLE patch_error
)
if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "tinyusb-gd32.patch doesn't apply to this TinyUSB version:\n${patch_error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DWC2_PATCH} ${DWC2}/dcd_dwc2.c ${DWC2}/dwc2_common.c)
set(DWC2_PATCHED ${DWC2_COPY}/dcd_dwc2.c ${DWC2_COPY}/dwc2_common.c)
set_source_files_properties(${DWC2_PATCHED} PROPERTIES INCLUDE_DIRECTORIES ${DWC2})

add_library(e16_bsp STATIC
    src/system.c
    src/leds.c
    src/enc.c
    src/display.c
    src/midi.c
    src/storage.c
    src/ble_update.c
    src/usb.c
    src/usb_name.c
    src/sbrk.c
    ${HAL_MODULES}
    ${DEVICE}/Source/Templates/system_stm32f4xx.c
    ${DEVICE}/Source/Templates/gcc/startup_stm32f405xx.s
    ${TUSB}/tusb.c
    ${TUSB}/common/tusb_fifo.c
    ${TUSB}/device/usbd.c
    ${TUSB}/class/midi/midi_device.c
    ${DWC2_PATCHED}
)

target_include_directories(e16_bsp
    PUBLIC
        include
        platform
        ${HAL}/Inc
        ${DEVICE}/Include
        ${EXT}/cmsis_core/Include
        ${TUSB}
)

target_compile_definitions(e16_bsp PUBLIC STM32F405xx USE_HAL_DRIVER HSE_VALUE=8000000U)
target_compile_options(e16_bsp PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall>)

set(E16_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/platform/e16_app.ld CACHE INTERNAL "")
