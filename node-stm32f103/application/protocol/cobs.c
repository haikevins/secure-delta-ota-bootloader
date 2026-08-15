#include "cobs.h"

bool Cobs_Encode(const uint8_t *input, uint32_t input_length,
                 uint8_t *output, uint32_t output_capacity,
                 uint32_t *output_length)
{
    uint32_t read_index = 0UL;
    uint32_t write_index = 1UL;
    uint32_t code_index = 0UL;
    uint8_t code = 1U;

    if ((output == (uint8_t *)0) || (output_length == (uint32_t *)0) ||
        ((input == (const uint8_t *)0) && (input_length != 0UL)) ||
        (output_capacity == 0UL))
    {
        return false;
    }

    while (read_index < input_length)
    {
        if (input[read_index] == 0U)
        {
            output[code_index] = code;
            code = 1U;
            code_index = write_index;
            if (write_index >= output_capacity) { return false; }
            ++write_index;
            ++read_index;
        }
        else
        {
            if (write_index >= output_capacity) { return false; }
            output[write_index++] = input[read_index++];
            ++code;

            if (code == 0xFFU)
            {
                output[code_index] = code;
                code = 1U;
                code_index = write_index;
                if (write_index >= output_capacity) { return false; }
                ++write_index;
            }
        }
    }

    output[code_index] = code;
    *output_length = write_index;
    return true;
}

bool Cobs_Decode(const uint8_t *input, uint32_t input_length,
                 uint8_t *output, uint32_t output_capacity,
                 uint32_t *output_length)
{
    uint32_t read_index = 0UL;
    uint32_t write_index = 0UL;

    if ((input == (const uint8_t *)0) || (output == (uint8_t *)0) ||
        (output_length == (uint32_t *)0) || (input_length == 0UL))
    {
        return false;
    }

    while (read_index < input_length)
    {
        const uint8_t code = input[read_index++];
        uint32_t count;
        uint32_t i;

        if (code == 0U) { return false; }
        count = (uint32_t)code - 1UL;
        if (count > (input_length - read_index)) { return false; }
        if (count > (output_capacity - write_index)) { return false; }

        for (i = 0UL; i < count; ++i)
        {
            output[write_index++] = input[read_index++];
        }

        if ((code != 0xFFU) && (read_index < input_length))
        {
            if (write_index >= output_capacity) { return false; }
            output[write_index++] = 0U;
        }
    }

    *output_length = write_index;
    return true;
}
