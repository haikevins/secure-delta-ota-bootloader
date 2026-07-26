#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

#define INTERNAL_FLASH_BASE              0x08000000UL
#define INTERNAL_SRAM_BASE               0x20000000UL
#define INTERNAL_SRAM_SIZE               (20UL * 1024UL)
#define INTERNAL_SRAM_END                (INTERNAL_SRAM_BASE + INTERNAL_SRAM_SIZE)
#define INTERNAL_FLASH_END               0x0800FFFFUL
#define INTERNAL_FLASH_PAGE_SIZE         (1UL * 1024UL)

#define BOOTLOADER_START_ADDRESS         0x08000000UL
#define BOOTLOADER_MAX_SIZE              (24UL * 1024UL)

#define APPLICATION_START_ADDRESS        0x08006000UL
#define APPLICATION_MAX_SIZE             (38UL * 1024UL)
#define APPLICATION_END_ADDRESS          (APPLICATION_START_ADDRESS + APPLICATION_MAX_SIZE)

/* Two separate internal-Flash erase pages provide power-loss-safe A/B metadata.
 * The older/invalid page is always erased and programmed first; the currently
 * selected valid page is never erased until the new copy has been verified. */
#define BOOT_METADATA_A_ADDRESS          0x0800F800UL
#define BOOT_METADATA_B_ADDRESS          0x0800FC00UL
#define BOOT_METADATA_SLOT_SIZE          INTERNAL_FLASH_PAGE_SIZE
#define BOOT_METADATA_ADDRESS            BOOT_METADATA_A_ADDRESS
#define BOOT_METADATA_SIZE               (2UL * INTERNAL_FLASH_PAGE_SIZE)
#define BOOT_METADATA_END_ADDRESS        0x08010000UL

#define EXT_FLASH_SIZE                   0x400000UL
#define EXT_FLASH_SECTOR_SIZE            0x1000UL
#define EXT_FLASH_PAGE_SIZE              0x0100UL
#define EXT_METADATA_A_ADDRESS           0x000000UL
#define EXT_METADATA_B_ADDRESS           0x001000UL
#define EXT_INCOMING_ADDRESS             0x002000UL
#define EXT_INCOMING_SIZE                0x020000UL
#define EXT_RECONSTRUCTED_ADDRESS        0x022000UL
#define EXT_RECONSTRUCTED_SIZE           0x020000UL
#define EXT_BACKUP_ADDRESS               0x042000UL
#define EXT_BACKUP_SIZE                  0x020000UL
#define EXT_LOG_ADDRESS                  0x062000UL
#define EXT_LOG_SIZE                     0x010000UL

#endif
