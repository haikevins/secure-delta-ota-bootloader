TARGET := application
ROOT_DIR := $(abspath ../..)
LINKER_SCRIPT := linker/application.ld

LOCAL_C_SOURCES := \
  src/main.c \
  src/phase4_flash_selftest.c \
  platform/system_clock.c \
  $(ROOT_DIR)/shared/src/ext_flash_layout.c \
  $(ROOT_DIR)/node-stm32f103/common/drivers/spi_flash.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/external_flash_storage.c
LOCAL_ASM_SOURCES := startup/startup_stm32f10x_md.s
LOCAL_SPL_C_SOURCES := \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_spi.c

PROJECT_DEFINES := -DBUILD_APPLICATION=1 -DPROJECT_VECTOR_TABLE_OFFSET=0x00006000UL

include $(ROOT_DIR)/mk/stm32f103.mk
