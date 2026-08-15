TARGET := bootloader
ROOT_DIR := $(abspath ../..)
LINKER_SCRIPT := linker/bootloader.ld

LOCAL_C_SOURCES := \
  src/main.c \
  src/boot_manager.c \
  src/boot_decision.c \
  src/application_jump.c \
  src/image_installer.c \
  src/trial_boot.c \
  storage/metadata_storage.c \
  platform/system_clock.c \
  $(ROOT_DIR)/shared/src/crc32.c \
  $(ROOT_DIR)/shared/src/download_checkpoint.c \
  $(ROOT_DIR)/shared/src/boot_metadata.c \
  $(ROOT_DIR)/shared/src/backup_image.c \
  $(ROOT_DIR)/shared/src/backup_progress.c \
  $(ROOT_DIR)/shared/src/ext_flash_layout.c \
  $(ROOT_DIR)/shared/src/full_image_validation.c \
  $(ROOT_DIR)/shared/src/install_progress.c \
  $(ROOT_DIR)/shared/src/update_handoff.c \
  $(ROOT_DIR)/node-stm32f103/common/drivers/spi_flash.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/backup_image_storage.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/download_checkpoint_storage.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/external_flash_storage.c \
  $(ROOT_DIR)/node-stm32f103/common/storage/update_handoff_storage.c
LOCAL_ASM_SOURCES := \
  startup/startup_stm32f10x_md.s \
  startup/application_handoff.s
LOCAL_SPL_C_SOURCES := \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_flash.c \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_iwdg.c \
  $(ROOT_DIR)/node-stm32f103/spl/src/stm32f10x_spi.c

PROJECT_DEFINES := -DBUILD_BOOTLOADER=1 -DPROJECT_VECTOR_TABLE_OFFSET=0x00000000UL

include $(ROOT_DIR)/mk/stm32f103.mk
