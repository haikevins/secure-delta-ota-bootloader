#include "phase4_flash_selftest.h"

#include "external_flash_storage.h"
#include "memory_map.h"
#include "spi_flash.h"

#define PHASE4_TEST_OFFSET 0xF0UL
#define PHASE4_TEST_LENGTH 40UL

volatile uint32_t g_phase4_flash_test_status = PHASE4_FLASH_TEST_NOT_RUN;
volatile uint32_t g_phase4_flash_jedec_id;
volatile uint32_t g_phase4_flash_driver_status;

static void Fail(uint32_t status)
{
    g_phase4_flash_driver_status = (uint32_t)SpiFlash_GetLastStatus();
    g_phase4_flash_test_status = status;
}

void Phase4FlashSelfTest_Run(void)
{
    uint8_t pattern[PHASE4_TEST_LENGTH];
    uint8_t readback[PHASE4_TEST_LENGTH];
    uint32_t index;

    g_phase4_flash_test_status = PHASE4_FLASH_TEST_RUNNING;
    g_phase4_flash_jedec_id = 0UL;
    g_phase4_flash_driver_status = 0UL;

    for (index = 0UL; index < PHASE4_TEST_LENGTH; ++index)
    {
        pattern[index] = (uint8_t)(0xA5U ^ (uint8_t)(index * 13UL));
        readback[index] = 0U;
    }

    if (!ExternalFlashStorage_Init())
    {
        g_phase4_flash_jedec_id = SpiFlash_GetJedecId();
        Fail(PHASE4_FLASH_TEST_FAIL_INIT);
        return;
    }

    g_phase4_flash_jedec_id = SpiFlash_GetJedecId();
    if (!SpiFlash_IsSupportedJedecId(g_phase4_flash_jedec_id))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_ID);
        return;
    }

    if (!ExternalFlashStorage_ErasePartition(EXTERNAL_FLASH_PARTITION_SELF_TEST))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_ERASE1);
        return;
    }

    if (!ExternalFlashStorage_IsErased(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                       0UL,
                                       EXT_SELF_TEST_SIZE))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_BLANK1);
        return;
    }

    if (!ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                    PHASE4_TEST_OFFSET,
                                    pattern,
                                    PHASE4_TEST_LENGTH))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_WRITE);
        return;
    }

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                   PHASE4_TEST_OFFSET,
                                   readback,
                                   PHASE4_TEST_LENGTH))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_READ);
        return;
    }

    for (index = 0UL; index < PHASE4_TEST_LENGTH; ++index)
    {
        if (readback[index] != pattern[index])
        {
            Fail(PHASE4_FLASH_TEST_FAIL_COMPARE);
            return;
        }
    }

    if (!ExternalFlashStorage_Verify(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                     PHASE4_TEST_OFFSET,
                                     pattern,
                                     PHASE4_TEST_LENGTH))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_VERIFY);
        return;
    }

    if (ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                  EXT_SELF_TEST_SIZE - 4UL,
                                  readback,
                                  8UL))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_BOUNDS);
        return;
    }

    if (!ExternalFlashStorage_ErasePartition(EXTERNAL_FLASH_PARTITION_SELF_TEST))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_ERASE2);
        return;
    }

    if (!ExternalFlashStorage_IsErased(EXTERNAL_FLASH_PARTITION_SELF_TEST,
                                       0UL,
                                       EXT_SELF_TEST_SIZE))
    {
        Fail(PHASE4_FLASH_TEST_FAIL_BLANK2);
        return;
    }

    g_phase4_flash_driver_status = (uint32_t)SpiFlash_GetLastStatus();
    g_phase4_flash_test_status = PHASE4_FLASH_TEST_PASS;
}
