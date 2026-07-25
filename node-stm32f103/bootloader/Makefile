TARGET := bootloader
ROOT_DIR := $(abspath ../..)
LINKER_SCRIPT := linker/bootloader.ld

LOCAL_C_SOURCES := \
  src/main.c \
  src/boot_manager.c \
  src/application_jump.c \
  platform/system_clock.c
LOCAL_ASM_SOURCES := \
  startup/startup_stm32f10x_md.s \
  startup/application_handoff.s

PROJECT_DEFINES := -DBUILD_BOOTLOADER=1 -DPROJECT_VECTOR_TABLE_OFFSET=0x00000000UL

include $(ROOT_DIR)/mk/stm32f103.mk
