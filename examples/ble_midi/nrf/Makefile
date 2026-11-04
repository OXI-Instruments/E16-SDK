# Bluetooth MIDI firmware for the E16's nRF52832, built with Nordic's nRF5 SDK 16.0.0.
#
#   make SDK_ROOT=/path/to/nRF5_SDK_16.0.0
#
# Produces $(OUTPUT_DIRECTORY)/ble_midi_nrf.hex. The examples' CMake build runs
# this when NRF5_SDK_ROOT is set and wraps the result in an updater firmware.

.DEFAULT_GOAL    := default
PROJECT_NAME     := ble_midi_nrf
TARGETS          := ble_midi_nrf
OUTPUT_DIRECTORY ?= _build
SDK_ROOT         ?=
PROJ_DIR         := .

ifeq ($(wildcard $(SDK_ROOT)/components),)
$(error nRF5 SDK 16.0.0 not found; pass SDK_ROOT=..., see ../README.md)
endif

# The Arm toolchain on PATH, unless given.
GNU_INSTALL_ROOT ?= $(dir $(shell command -v arm-none-eabi-gcc))
GNU_VERSION      ?= $(shell arm-none-eabi-gcc -dumpversion)
GNU_PREFIX       ?= arm-none-eabi

$(OUTPUT_DIRECTORY)/ble_midi_nrf.out: LINKER_SCRIPT := ble_midi_nrf.ld
# Relink when the linker script changes.
$(OUTPUT_DIRECTORY)/ble_midi_nrf/main.c.o: ble_midi_nrf.ld

SRC_FILES += \
  $(PROJ_DIR)/main.c \
  $(SDK_ROOT)/modules/nrfx/mdk/gcc_startup_nrf52.S \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_handler_gcc.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/libraries/fifo/app_fifo.c \
  $(SDK_ROOT)/components/libraries/uart/app_uart_fifo.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer2.c \
  $(SDK_ROOT)/components/libraries/timer/drv_rtc.c \
  $(SDK_ROOT)/components/libraries/atomic_fifo/nrf_atfifo.c \
  $(SDK_ROOT)/components/libraries/atomic_flags/nrf_atflags.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/pwr_mgmt/nrf_pwr_mgmt.c \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/sortlist/nrf_sortlist.c \
  $(SDK_ROOT)/components/libraries/strerror/nrf_strerror.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_uart.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/prs/nrfx_prs.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_uart.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_uarte.c \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/ble_advertising/ble_advertising.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_params.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_state.c \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt/nrf_ble_gatt.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \

INC_FOLDERS += \
  $(PROJ_DIR) \
  $(SDK_ROOT)/components \
  $(SDK_ROOT)/components/ble/ble_advertising \
  $(SDK_ROOT)/components/ble/common \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt \
  $(SDK_ROOT)/components/libraries/atomic \
  $(SDK_ROOT)/components/libraries/atomic_fifo \
  $(SDK_ROOT)/components/libraries/atomic_flags \
  $(SDK_ROOT)/components/libraries/delay \
  $(SDK_ROOT)/components/libraries/experimental_section_vars \
  $(SDK_ROOT)/components/libraries/fifo \
  $(SDK_ROOT)/components/libraries/log \
  $(SDK_ROOT)/components/libraries/log/src \
  $(SDK_ROOT)/components/libraries/mutex \
  $(SDK_ROOT)/components/libraries/pwr_mgmt \
  $(SDK_ROOT)/components/libraries/sortlist \
  $(SDK_ROOT)/components/libraries/strerror \
  $(SDK_ROOT)/components/libraries/timer \
  $(SDK_ROOT)/components/libraries/uart \
  $(SDK_ROOT)/components/libraries/util \
  $(SDK_ROOT)/components/softdevice/common \
  $(SDK_ROOT)/components/softdevice/s132/headers \
  $(SDK_ROOT)/components/softdevice/s132/headers/nrf52 \
  $(SDK_ROOT)/components/toolchain/cmsis/include \
  $(SDK_ROOT)/integration/nrfx \
  $(SDK_ROOT)/integration/nrfx/legacy \
  $(SDK_ROOT)/modules/nrfx \
  $(SDK_ROOT)/modules/nrfx/drivers/include \
  $(SDK_ROOT)/modules/nrfx/hal \
  $(SDK_ROOT)/modules/nrfx/mdk \

LIB_FILES += -lc -lnosys -lm

DEFINES := -DAPP_TIMER_V2 -DAPP_TIMER_V2_RTC1_ENABLED -DCONFIG_GPIO_AS_PINRESET -DFLOAT_ABI_HARD \
  -DNRF52 -DNRF52832_XXAA -DNRF52_PAN_74 -DNRF_SD_BLE_API_VERSION=7 -DS132 -DSOFTDEVICE_PRESENT
CPU := -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=hard -mfpu=fpv4-sp-d16

OPT = -Os -g3
CFLAGS += $(OPT) $(DEFINES) $(CPU) -Wall -Werror
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing -fno-builtin -fshort-enums
CFLAGS += -D__HEAP_SIZE=0 -D__STACK_SIZE=4096
ASMFLAGS += -g3 $(DEFINES) $(CPU) -D__HEAP_SIZE=0 -D__STACK_SIZE=4096
LDFLAGS += $(OPT) $(CPU) -L$(SDK_ROOT)/modules/nrfx/mdk -T$(LINKER_SCRIPT)
LDFLAGS += -Wl,--gc-sections --specs=nano.specs

.PHONY: default
default: ble_midi_nrf

TEMPLATE_PATH := $(SDK_ROOT)/components/toolchain/gcc
include $(TEMPLATE_PATH)/Makefile.common
$(foreach target, $(TARGETS), $(call define_target, $(target)))
