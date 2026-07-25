TARGET := application
ROOT_DIR := $(abspath ../..)
LINKER_SCRIPT := linker/application.ld

LOCAL_C_SOURCES := \
  src/main.c \
  platform/system_clock.c
LOCAL_ASM_SOURCES := startup/startup_stm32f10x_md.s

PROJECT_DEFINES := -DBUILD_APPLICATION=1 -DPROJECT_VECTOR_TABLE_OFFSET=0x00006000UL

include $(ROOT_DIR)/mk/stm32f103.mk
