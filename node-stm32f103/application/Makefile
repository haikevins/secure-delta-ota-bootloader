TARGET := application
ROOT_DIR := $(abspath ../..)
LINKER_SCRIPT := linker/application.ld

LOCAL_C_SOURCES := \
  src/main.c \
  src/ota_agent.c \
  src/ota_receiver.c \
  src/trial_confirmation.c \
  src/phase4_flash_selftest.c \
  drivers/uart.c \
  protocol/cobs.c \
  protocol/ota_packet.c \
  protocol/ota_parser.c \
  protocol/ota_response.c \
  platform/system_clock.c \
  $(ROOT_DIR)/shared/src/crc32.c \
  $(ROOT_DIR)/shared/src/download_checkpoint.c \
  $(ROOT_DIR)/shared/src/boot_metadata.c \
  $(ROOT_DIR)/shared/src/ext_flash_layout.c \
  $(ROOT_DIR)/shared/src/full_image_validation.c \
  $(ROOT_DIR)/shared/src/update_handoff.c \
  $(ROOT_DIR)/node-stm32f103/common/drivers/spi_flash.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/download_checkpoint_storage.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/external_flash_storage.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/update_handoff_storage.c \
  $(ROOT_DIR)/node-stm32f103/bootloader/storage/metadata_storage.c

LOCAL_ASM_SOURCES := startup/startup_stm32f10x_md.s
LOCAL_SPL_C_SOURCES := \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_flash.c \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_spi.c \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_usart.c

PROJECT_DEFINES := -DBUILD_APPLICATION=1 -DPROJECT_VECTOR_TABLE_OFFSET=0x00006000UL

include $(ROOT_DIR)/mk/stm32f103.mk
