#include <assert.h>
#include <stdio.h>

#include "ext_flash_layout.h"
#include "memory_map.h"

int main(void)
{
    assert(ExtFlash_IsRangeValid(4096UL, 0UL, 4096UL));
    assert(ExtFlash_IsRangeValid(4096UL, 4096UL, 0UL));
    assert(!ExtFlash_IsRangeValid(4096UL, 4095UL, 2UL));
    assert(!ExtFlash_IsRangeValid(EXT_FLASH_SIZE, 0xFFFFFFF0UL, 0x40UL));

    assert(ExtFlash_IsAbsoluteRangeValid(EXT_FLASH_SIZE - 1UL, 1UL));
    assert(!ExtFlash_IsAbsoluteRangeValid(EXT_FLASH_SIZE - 1UL, 2UL));

    assert(ExtFlash_IsSectorAligned(0x1000UL));
    assert(ExtFlash_IsSectorAligned(EXT_SELF_TEST_ADDRESS));
    assert(!ExtFlash_IsSectorAligned(0x1001UL));

    assert(ExtFlash_PageChunkLength(0x00000000UL, 300UL) == 256UL);
    assert(ExtFlash_PageChunkLength(0x000000F0UL, 40UL) == 16UL);
    assert(ExtFlash_PageChunkLength(0x00000100UL, 24UL) == 24UL);
    assert(ExtFlash_PageChunkLength(0x000001FFUL, 2UL) == 1UL);

    assert(EXT_SELF_TEST_ADDRESS == 0x3FF000UL);
    assert((EXT_SELF_TEST_ADDRESS + EXT_SELF_TEST_SIZE) == EXT_FLASH_SIZE);
    assert((EXT_LOG_ADDRESS + EXT_LOG_SIZE) <= EXT_SELF_TEST_ADDRESS);

    puts("Phase 4 external-Flash geometry tests: PASS");
    return 0;
}
