#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SPI_FLASH_STATUS_OK = 0,
    SPI_FLASH_STATUS_NOT_INITIALIZED,
    SPI_FLASH_STATUS_INVALID_ARGUMENT,
    SPI_FLASH_STATUS_RANGE_ERROR,
    SPI_FLASH_STATUS_ALIGNMENT_ERROR,
    SPI_FLASH_STATUS_TRANSFER_TIMEOUT,
    SPI_FLASH_STATUS_BUSY_TIMEOUT,
    SPI_FLASH_STATUS_WRITE_ENABLE_FAILED,
    SPI_FLASH_STATUS_UNSUPPORTED_DEVICE,
    SPI_FLASH_STATUS_NEEDS_ERASE,
    SPI_FLASH_STATUS_VERIFY_FAILED
} SpiFlashStatus_t;

#define SPI_FLASH_JEDEC_ID_W25Q32 0x00EF4016UL
#define SPI_FLASH_JEDEC_ID_W25Q64 0x00EF4017UL

/*
 * The OTA partition map intentionally remains limited to the first 4 MiB.
 * W25Q64 is accepted for development hardware because the basic SPI commands
 * used by external-flash integration are compatible; its upper 4 MiB is left unused.
 */
bool SpiFlash_IsSupportedJedecId(uint32_t jedec_id);
bool SpiFlash_Init(void);
uint32_t SpiFlash_GetJedecId(void);
SpiFlashStatus_t SpiFlash_GetLastStatus(void);
bool SpiFlash_Read(uint32_t address, uint8_t *data, uint32_t length);
bool SpiFlash_Write(uint32_t address, const uint8_t *data, uint32_t length);
bool SpiFlash_EraseSector(uint32_t address);
bool SpiFlash_Verify(uint32_t address, const uint8_t *data, uint32_t length);
bool SpiFlash_IsErased(uint32_t address, uint32_t length);

#endif
