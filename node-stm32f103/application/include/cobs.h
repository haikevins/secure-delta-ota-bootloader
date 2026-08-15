#ifndef COBS_H
#define COBS_H

#include <stdbool.h>
#include <stdint.h>

bool Cobs_Encode(const uint8_t *input, uint32_t input_length,
                 uint8_t *output, uint32_t output_capacity,
                 uint32_t *output_length);
bool Cobs_Decode(const uint8_t *input, uint32_t input_length,
                 uint8_t *output, uint32_t output_capacity,
                 uint32_t *output_length);

#endif
