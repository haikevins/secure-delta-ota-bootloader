#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

#define CRC32_IEEE_INITIAL_VALUE 0xFFFFFFFFUL
#define CRC32_IEEE_FINAL_XOR     0xFFFFFFFFUL

uint32_t Crc32_Update(uint32_t running_crc,
                      const void *data,
                      uint32_t length);

uint32_t Crc32_Calculate(const void *data, uint32_t length);

#endif
