#include "crc32.h"

#include <stdint.h>

#define CRC32_IEEE_REVERSED_POLYNOMIAL 0xEDB88320UL

uint32_t Crc32_Update(uint32_t running_crc,
                      const void *data,
                      uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = running_crc;
    uint32_t index;

    if ((bytes == (const uint8_t *)0) && (length != 0UL))
    {
        return running_crc;
    }

    for (index = 0UL; index < length; ++index)
    {
        uint32_t bit;

        crc ^= (uint32_t)bytes[index];
        for (bit = 0UL; bit < 8UL; ++bit)
        {
            const uint32_t mask = 0UL - (crc & 1UL);
            crc = (crc >> 1U) ^ (CRC32_IEEE_REVERSED_POLYNOMIAL & mask);
        }
    }

    return crc;
}

uint32_t Crc32_Calculate(const void *data, uint32_t length)
{
    const uint32_t running = Crc32_Update(CRC32_IEEE_INITIAL_VALUE,
                                          data,
                                          length);
    return running ^ CRC32_IEEE_FINAL_XOR;
}
