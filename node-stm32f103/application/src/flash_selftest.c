#include "flash_selftest.h"

#include "external_flash_storage.h"
#include "memory_map.h"
#include "spi_flash.h"

#define FLASH_SELFTEST_OFFSET 0xF0UL
#define FLASH_SELFTEST_LENGTH 40UL

volatile uint32_t g_flash_selftest_status = FLASH_SELFTEST_NOT_RUN;
volatile uint32_t g_flash_selftest_jedec_id;
volatile uint32_t g_flash_selftest_driver_status;

static void Fail(uint32_t status)
{
    g_flash_selftest_driver_status = (uint32_t)SpiFlash_GetLastStatus();
    g_flash_selftest_status = status;
}

void FlashSelfTest_Run(void)
{
    uint8_t pattern[FLASH_SELFTEST_LENGTH];
    uint8_t readback[FLASH_SELFTEST_LENGTH];
    uint32_t index;

    g_flash_selftest_status = FLASH_SELFTEST_RUNNING;
    g_flash_selftest_jedec_id = 0UL;
    g_flash_selftest_driver_status = 0UL;

    for (index = 0UL; index < FLASH_SELFTEST_LENGTH; ++index)
    {
        pattern[index] = (uint8_t)(0xA5U ^ (uint8_t)(index * 13UL));
        readback[index] = 0U;
    }

    if (!ExternalFlashStorage_Init())
    {
        g_flash_selftest_jedec_id = SpiFlash_GetJedecId();
        Fail(FLASH_SELFTEST_FAIL_INIT);
        return;
    }

    g_flash_selftest_jedec_id = SpiFlash_GetJedecId();
    if (!SpiFlash_IsSupportedJedecId(g_flash_selftest_jedec_id))
    {
        Fail(FLASH_SELFTEST_FAIL_ID);
        return;
    }

    if (!ExternalFlashStorage_ErasePartition(EXTERNAL_FLASH_PARTITION_SELF_TEST))
    {
        Fail(FLASH_SELFTEST_FAIL_ERASE1);
        return;
    }

    if (!ExternalFlashStorage_IsErased(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                       0UL,
                                       EXT_SELF_TEST_SIZE))
    {
        Fail(FLASH_SELFTEST_FAIL_BLANK1);
        return;
    }

    if (!ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                    FLASH_SELFTEST_OFFSET,
                                    pattern,
                                    FLASH_SELFTEST_LENGTH))
    {
        Fail(FLASH_SELFTEST_FAIL_WRITE);
        return;
    }

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                   FLASH_SELFTEST_OFFSET,
                                   readback,
                                   FLASH_SELFTEST_LENGTH))
    {
        Fail(FLASH_SELFTEST_FAIL_READ);
        return;
    }

    for (index = 0UL; index < FLASH_SELFTEST_LENGTH; ++index)
    {
        if (readback[index] != pattern[index])
        {
            Fail(FLASH_SELFTEST_FAIL_COMPARE);
            return;
        }
    }

    if (!ExternalFlashStorage_Verify(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                     FLASH_SELFTEST_OFFSET,
                                     pattern,
                                     FLASH_SELFTEST_LENGTH))
    {
        Fail(FLASH_SELFTEST_FAIL_VERIFY);
        return;
    }

    if (ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                  EXT_SELF_TEST_SIZE - 4UL,
                                  readback,
                                  8UL))
    {
        Fail(FLASH_SELFTEST_FAIL_BOUNDS);
        return;
    }

    if (!ExternalFlashStorage_ErasePartition(EXTERNAL_FLASH_PARTITION_SELF_TEST))
    {
        Fail(FLASH_SELFTEST_FAIL_ERASE2);
        return;
    }

    if (!ExternalFlashStorage_IsErased(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                       0UL,
                                       EXT_SELF_TEST_SIZE))
    {
        Fail(FLASH_SELFTEST_FAIL_BLANK2);
        return;
    }

    g_flash_selftest_driver_status = (uint32_t)SpiFlash_GetLastStatus();
    g_flash_selftest_status = FLASH_SELFTEST_PASS;
}
