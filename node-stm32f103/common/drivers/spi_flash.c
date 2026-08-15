#include "spi_flash.h"

#include "ext_flash_layout.h"
#include "memory_map.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"

#define SPI_FLASH_SPI                    SPI1
#define SPI_FLASH_CS_PORT                GPIOB
#define SPI_FLASH_CS_PIN                 GPIO_Pin_0

#define SPI_FLASH_CMD_WRITE_ENABLE       0x06U
#define SPI_FLASH_CMD_READ_STATUS1       0x05U
#define SPI_FLASH_CMD_READ_DATA          0x03U
#define SPI_FLASH_CMD_PAGE_PROGRAM       0x02U
#define SPI_FLASH_CMD_SECTOR_ERASE_4K    0x20U
#define SPI_FLASH_CMD_JEDEC_ID           0x9FU

#define SPI_FLASH_STATUS1_BUSY           0x01U
#define SPI_FLASH_STATUS1_WEL            0x02U

#define SPI_FLASH_TRANSFER_TIMEOUT_LOOPS 200000UL
#define SPI_FLASH_PROGRAM_TIMEOUT_LOOPS  3000000UL
#define SPI_FLASH_ERASE_TIMEOUT_LOOPS    30000000UL
#define SPI_FLASH_PREFLIGHT_BUFFER_SIZE  32UL

static uint8_t g_initialized;
static uint32_t g_jedec_id;
static SpiFlashStatus_t g_last_status = SPI_FLASH_STATUS_NOT_INITIALIZED;

static void SetStatus(SpiFlashStatus_t status) { g_last_status = status; }
static void Select(void) { GPIO_ResetBits(SPI_FLASH_CS_PORT, SPI_FLASH_CS_PIN); }

static void Deselect(void)
{
    uint32_t timeout = SPI_FLASH_TRANSFER_TIMEOUT_LOOPS;
    while ((SPI_I2S_GetFlagStatus(SPI_FLASH_SPI, SPI_I2S_FLAG_BSY) == SET) &&
           (timeout > 0UL))
    {
        --timeout;
    }
    GPIO_SetBits(SPI_FLASH_CS_PORT, SPI_FLASH_CS_PIN);
}

static bool Transfer(uint8_t tx, uint8_t *rx)
{
    uint32_t timeout = SPI_FLASH_TRANSFER_TIMEOUT_LOOPS;
    while ((SPI_I2S_GetFlagStatus(SPI_FLASH_SPI, SPI_I2S_FLAG_TXE) == RESET) &&
           (timeout > 0UL))
    {
        --timeout;
    }
    if (timeout == 0UL)
    {
        SetStatus(SPI_FLASH_STATUS_TRANSFER_TIMEOUT);
        return false;
    }

    SPI_I2S_SendData(SPI_FLASH_SPI, tx);

    timeout = SPI_FLASH_TRANSFER_TIMEOUT_LOOPS;
    while ((SPI_I2S_GetFlagStatus(SPI_FLASH_SPI, SPI_I2S_FLAG_RXNE) == RESET) &&
           (timeout > 0UL))
    {
        --timeout;
    }
    if (timeout == 0UL)
    {
        SetStatus(SPI_FLASH_STATUS_TRANSFER_TIMEOUT);
        return false;
    }

    {
        const uint8_t value = (uint8_t)SPI_I2S_ReceiveData(SPI_FLASH_SPI);
        if (rx != (uint8_t *)0) { *rx = value; }
    }
    return true;
}

static bool SendAddress24(uint32_t address)
{
    return Transfer((uint8_t)(address >> 16U), (uint8_t *)0) &&
           Transfer((uint8_t)(address >> 8U), (uint8_t *)0) &&
           Transfer((uint8_t)address, (uint8_t *)0);
}

static bool ReadStatus1(uint8_t *status)
{
    uint8_t value = 0U;
    bool ok;
    if (status == (uint8_t *)0)
    {
        SetStatus(SPI_FLASH_STATUS_INVALID_ARGUMENT);
        return false;
    }

    Select();
    ok = Transfer(SPI_FLASH_CMD_READ_STATUS1, (uint8_t *)0) &&
         Transfer(0xFFU, &value);
    Deselect();

    if (!ok) { return false; }
    *status = value;
    return true;
}

static bool WaitReady(uint32_t timeout)
{
    while (timeout > 0UL)
    {
        uint8_t status;
        if (!ReadStatus1(&status)) { return false; }
        if ((status & SPI_FLASH_STATUS1_BUSY) == 0U) { return true; }
        --timeout;
    }
    SetStatus(SPI_FLASH_STATUS_BUSY_TIMEOUT);
    return false;
}

static bool WriteEnable(void)
{
    uint8_t status;
    Select();
    if (!Transfer(SPI_FLASH_CMD_WRITE_ENABLE, (uint8_t *)0))
    {
        Deselect();
        return false;
    }
    Deselect();

    if (!ReadStatus1(&status)) { return false; }
    if ((status & SPI_FLASH_STATUS1_WEL) == 0U)
    {
        SetStatus(SPI_FLASH_STATUS_WRITE_ENABLE_FAILED);
        return false;
    }
    return true;
}

static bool ReadJedecIdRaw(uint32_t *jedec_id)
{
    uint8_t manufacturer = 0U;
    uint8_t memory_type = 0U;
    uint8_t capacity = 0U;
    bool ok;

    if (jedec_id == (uint32_t *)0)
    {
        SetStatus(SPI_FLASH_STATUS_INVALID_ARGUMENT);
        return false;
    }

    Select();
    ok = Transfer(SPI_FLASH_CMD_JEDEC_ID, (uint8_t *)0) &&
         Transfer(0xFFU, &manufacturer) &&
         Transfer(0xFFU, &memory_type) &&
         Transfer(0xFFU, &capacity);
    Deselect();
    if (!ok) { return false; }

    *jedec_id = ((uint32_t)manufacturer << 16U) |
                ((uint32_t)memory_type << 8U) |
                (uint32_t)capacity;
    return true;
}

static bool PreflightProgrammable(uint32_t address,
                                  const uint8_t *data,
                                  uint32_t length)
{
    uint8_t existing[SPI_FLASH_PREFLIGHT_BUFFER_SIZE];
    uint32_t offset = 0UL;

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        uint32_t index;
        if (chunk > sizeof(existing)) { chunk = sizeof(existing); }

        if (!SpiFlash_Read(address + offset, existing, chunk)) { return false; }

        for (index = 0UL; index < chunk; ++index)
        {
            if ((existing[index] & data[offset + index]) != data[offset + index])
            {
                SetStatus(SPI_FLASH_STATUS_NEEDS_ERASE);
                return false;
            }
        }
        offset += chunk;
    }
    return true;
}

static bool ProgramPageChunk(uint32_t address,
                             const uint8_t *data,
                             uint32_t length)
{
    uint32_t index;

    if ((length == 0UL) ||
        (length > ExtFlash_PageChunkLength(address, length)))
    {
        SetStatus(SPI_FLASH_STATUS_INVALID_ARGUMENT);
        return false;
    }

    if (!WriteEnable()) { return false; }

    Select();
    if (!Transfer(SPI_FLASH_CMD_PAGE_PROGRAM, (uint8_t *)0) ||
        !SendAddress24(address))
    {
        Deselect();
        return false;
    }

    for (index = 0UL; index < length; ++index)
    {
        if (!Transfer(data[index], (uint8_t *)0))
        {
            Deselect();
            return false;
        }
    }
    Deselect();

    if (!WaitReady(SPI_FLASH_PROGRAM_TIMEOUT_LOOPS)) { return false; }
    return SpiFlash_Verify(address, data, length);
}

bool SpiFlash_IsSupportedJedecId(uint32_t jedec_id)
{
    return (jedec_id == SPI_FLASH_JEDEC_ID_W25Q32) ||
           (jedec_id == SPI_FLASH_JEDEC_ID_W25Q64);
}

bool SpiFlash_Init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;
    uint32_t jedec_id = 0UL;

    g_initialized = 0U;
    g_jedec_id = 0UL;
    SetStatus(SPI_FLASH_STATUS_NOT_INITIALIZED);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_SPI1, ENABLE);

    GPIO_SetBits(SPI_FLASH_CS_PORT, SPI_FLASH_CS_PIN);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_6;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = SPI_FLASH_CS_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(SPI_FLASH_CS_PORT, &gpio);
    Deselect();

    SPI_I2S_DeInit(SPI_FLASH_SPI);
    SPI_StructInit(&spi);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;
    SPI_Init(SPI_FLASH_SPI, &spi);
    SPI_NSSInternalSoftwareConfig(SPI_FLASH_SPI, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI_FLASH_SPI, ENABLE);

    if (!ReadJedecIdRaw(&jedec_id)) { return false; }
    g_jedec_id = jedec_id;

    if (!SpiFlash_IsSupportedJedecId(jedec_id))
    {
        SetStatus(SPI_FLASH_STATUS_UNSUPPORTED_DEVICE);
        return false;
    }

    if (!WaitReady(SPI_FLASH_ERASE_TIMEOUT_LOOPS)) { return false; }

    g_initialized = 1U;
    SetStatus(SPI_FLASH_STATUS_OK);
    return true;
}

uint32_t SpiFlash_GetJedecId(void) { return g_jedec_id; }
SpiFlashStatus_t SpiFlash_GetLastStatus(void) { return g_last_status; }

bool SpiFlash_Read(uint32_t address, uint8_t *data, uint32_t length)
{
    uint32_t index;

    if (g_initialized == 0U)
    {
        SetStatus(SPI_FLASH_STATUS_NOT_INITIALIZED);
        return false;
    }
    if ((length != 0UL) && (data == (uint8_t *)0))
    {
        SetStatus(SPI_FLASH_STATUS_INVALID_ARGUMENT);
        return false;
    }
    if (!ExtFlash_IsAbsoluteRangeValid(address, length))
    {
        SetStatus(SPI_FLASH_STATUS_RANGE_ERROR);
        return false;
    }
    if (length == 0UL)
    {
        SetStatus(SPI_FLASH_STATUS_OK);
        return true;
    }

    Select();
    if (!Transfer(SPI_FLASH_CMD_READ_DATA, (uint8_t *)0) ||
        !SendAddress24(address))
    {
        Deselect();
        return false;
    }

    for (index = 0UL; index < length; ++index)
    {
        if (!Transfer(0xFFU, &data[index]))
        {
            Deselect();
            return false;
        }
    }
    Deselect();
    SetStatus(SPI_FLASH_STATUS_OK);
    return true;
}

bool SpiFlash_Verify(uint32_t address,
                     const uint8_t *data,
                     uint32_t length)
{
    uint32_t index;

    if (g_initialized == 0U)
    {
        SetStatus(SPI_FLASH_STATUS_NOT_INITIALIZED);
        return false;
    }
    if ((length != 0UL) && (data == (const uint8_t *)0))
    {
        SetStatus(SPI_FLASH_STATUS_INVALID_ARGUMENT);
        return false;
    }
    if (!ExtFlash_IsAbsoluteRangeValid(address, length))
    {
        SetStatus(SPI_FLASH_STATUS_RANGE_ERROR);
        return false;
    }
    if (length == 0UL)
    {
        SetStatus(SPI_FLASH_STATUS_OK);
        return true;
    }

    Select();
    if (!Transfer(SPI_FLASH_CMD_READ_DATA, (uint8_t *)0) ||
        !SendAddress24(address))
    {
        Deselect();
        return false;
    }

    for (index = 0UL; index < length; ++index)
    {
        uint8_t value;
        if (!Transfer(0xFFU, &value))
        {
            Deselect();
            return false;
        }
        if (value != data[index])
        {
            Deselect();
            SetStatus(SPI_FLASH_STATUS_VERIFY_FAILED);
            return false;
        }
    }
    Deselect();
    SetStatus(SPI_FLASH_STATUS_OK);
    return true;
}

bool SpiFlash_Write(uint32_t address,
                    const uint8_t *data,
                    uint32_t length)
{
    uint32_t offset = 0UL;

    if (g_initialized == 0U)
    {
        SetStatus(SPI_FLASH_STATUS_NOT_INITIALIZED);
        return false;
    }
    if ((length != 0UL) && (data == (const uint8_t *)0))
    {
        SetStatus(SPI_FLASH_STATUS_INVALID_ARGUMENT);
        return false;
    }
    if (!ExtFlash_IsAbsoluteRangeValid(address, length))
    {
        SetStatus(SPI_FLASH_STATUS_RANGE_ERROR);
        return false;
    }
    if (length == 0UL)
    {
        SetStatus(SPI_FLASH_STATUS_OK);
        return true;
    }

    if (!PreflightProgrammable(address, data, length)) { return false; }

    while (offset < length)
    {
        const uint32_t chunk =
            ExtFlash_PageChunkLength(address + offset, length - offset);
        if (!ProgramPageChunk(address + offset, &data[offset], chunk))
        {
            return false;
        }
        offset += chunk;
    }

    SetStatus(SPI_FLASH_STATUS_OK);
    return true;
}

bool SpiFlash_EraseSector(uint32_t address)
{
    if (g_initialized == 0U)
    {
        SetStatus(SPI_FLASH_STATUS_NOT_INITIALIZED);
        return false;
    }
    if (!ExtFlash_IsSectorAligned(address))
    {
        SetStatus(SPI_FLASH_STATUS_ALIGNMENT_ERROR);
        return false;
    }
    if (!ExtFlash_IsAbsoluteRangeValid(address, EXT_FLASH_SECTOR_SIZE))
    {
        SetStatus(SPI_FLASH_STATUS_RANGE_ERROR);
        return false;
    }

    if (!WriteEnable()) { return false; }

    Select();
    if (!Transfer(SPI_FLASH_CMD_SECTOR_ERASE_4K, (uint8_t *)0) ||
        !SendAddress24(address))
    {
        Deselect();
        return false;
    }
    Deselect();

    if (!WaitReady(SPI_FLASH_ERASE_TIMEOUT_LOOPS)) { return false; }

    SetStatus(SPI_FLASH_STATUS_OK);
    return true;
}

bool SpiFlash_IsErased(uint32_t address, uint32_t length)
{
    uint32_t index;

    if (g_initialized == 0U)
    {
        SetStatus(SPI_FLASH_STATUS_NOT_INITIALIZED);
        return false;
    }
    if (!ExtFlash_IsAbsoluteRangeValid(address, length))
    {
        SetStatus(SPI_FLASH_STATUS_RANGE_ERROR);
        return false;
    }
    if (length == 0UL)
    {
        SetStatus(SPI_FLASH_STATUS_OK);
        return true;
    }

    Select();
    if (!Transfer(SPI_FLASH_CMD_READ_DATA, (uint8_t *)0) ||
        !SendAddress24(address))
    {
        Deselect();
        return false;
    }

    for (index = 0UL; index < length; ++index)
    {
        uint8_t value;
        if (!Transfer(0xFFU, &value))
        {
            Deselect();
            return false;
        }
        if (value != 0xFFU)
        {
            Deselect();
            SetStatus(SPI_FLASH_STATUS_VERIFY_FAILED);
            return false;
        }
    }
    Deselect();
    SetStatus(SPI_FLASH_STATUS_OK);
    return true;
}
