#ifndef EXT_FLASH_LAYOUT_H
#define EXT_FLASH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

bool ExtFlash_IsRangeValid(uint32_t region_size, uint32_t offset, uint32_t length);
bool ExtFlash_IsAbsoluteRangeValid(uint32_t address, uint32_t length);
bool ExtFlash_IsSectorAligned(uint32_t address);
uint32_t ExtFlash_PageChunkLength(uint32_t address, uint32_t remaining_length);

#endif
