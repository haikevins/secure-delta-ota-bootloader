#ifndef FULL_IMAGE_VALIDATION_H
#define FULL_IMAGE_VALIDATION_H

#include <stdint.h>

typedef enum
{
    FULL_IMAGE_VALID = 0,
    FULL_IMAGE_TOO_SMALL,
    FULL_IMAGE_TOO_LARGE,
    FULL_IMAGE_BAD_STACK_RANGE,
    FULL_IMAGE_BAD_STACK_ALIGNMENT,
    FULL_IMAGE_BAD_RESET_THUMB_BIT,
    FULL_IMAGE_BAD_RESET_RANGE
} FullImageValidationStatus_t;

FullImageValidationStatus_t FullImage_ValidateVector(
    uint32_t image_size,
    uint32_t initial_stack_pointer,
    uint32_t reset_handler_address);

#endif
