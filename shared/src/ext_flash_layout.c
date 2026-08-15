#include "ext_flash_layout.h"
#include "memory_map.h"

bool ExtFlash_IsRangeValid(uint32_t region_size, uint32_t offset, uint32_t length)
{
    if (offset > region_size) { return false; }
    return length <= (region_size - offset);
}

bool ExtFlash_IsAbsoluteRangeValid(uint32_t address, uint32_t length)
{
    return ExtFlash_IsRangeValid(EXT_FLASH_SIZE, address, length);
}

bool ExtFlash_IsSectorAligned(uint32_t address)
{
    return (address & (EXT_FLASH_SECTOR_SIZE - 1UL)) == 0UL;
}

uint32_t ExtFlash_PageChunkLength(uint32_t address, uint32_t remaining_length)
{
    const uint32_t page_offset = address & (EXT_FLASH_PAGE_SIZE - 1UL);
    const uint32_t page_remaining = EXT_FLASH_PAGE_SIZE - page_offset;
    return (remaining_length < page_remaining) ? remaining_length : page_remaining;
}
