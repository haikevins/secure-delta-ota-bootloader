TARGET := bootloader
ROOT_DIR := $(abspath ../..)
LINKER_SCRIPT := linker/bootloader.ld

LOCAL_C_SOURCES := \
  src/main.c \
  src/boot_manager.c \
  src/boot_decision.c \
  src/application_jump.c \
  storage/metadata_storage.c \
  platform/system_clock.c \
  $(ROOT_DIR)/shared/src/crc32.c \
  $(ROOT_DIR)/shared/src/boot_metadata.c
LOCAL_ASM_SOURCES := \
  startup/startup_stm32f10x_md.s \
  startup/application_handoff.s
LOCAL_SPL_C_SOURCES := \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_flash.c

PROJECT_DEFINES := -DBUILD_BOOTLOADER=1 -DPROJECT_VECTOR_TABLE_OFFSET=0x00000000UL

include $(ROOT_DIR)/mk/stm32f103.mk
