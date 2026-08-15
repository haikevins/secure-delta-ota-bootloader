#include "full_image_validation.h"

#include "memory_map.h"

#define THUMB_STATE_BIT            0x00000001UL
#define ADDRESS_WITHOUT_THUMB_MASK 0xFFFFFFFEUL
#define REQUIRED_STACK_ALIGNMENT   8UL

FullImageValidationStatus_t FullImage_ValidateVector(
    uint32_t image_size,
    uint32_t initial_stack_pointer,
    uint32_t reset_handler_address)
{
    uint32_t reset_code;
    uint32_t image_end;

    if (image_size < 8UL) { return FULL_IMAGE_TOO_SMALL; }
    if (image_size > APPLICATION_MAX_SIZE) { return FULL_IMAGE_TOO_LARGE; }

    if ((initial_stack_pointer < INTERNAL_SRAM_BASE) ||
        (initial_stack_pointer > INTERNAL_SRAM_END))
    {
        return FULL_IMAGE_BAD_STACK_RANGE;
    }
    if ((initial_stack_pointer & (REQUIRED_STACK_ALIGNMENT - 1UL)) != 0UL)
    {
        return FULL_IMAGE_BAD_STACK_ALIGNMENT;
    }
    if ((reset_handler_address & THUMB_STATE_BIT) == 0UL)
    {
        return FULL_IMAGE_BAD_RESET_THUMB_BIT;
    }

    reset_code = reset_handler_address & ADDRESS_WITHOUT_THUMB_MASK;
    image_end = APPLICATION_START_ADDRESS + image_size;
    if ((reset_code < APPLICATION_START_ADDRESS) || (reset_code >= image_end))
    {
        return FULL_IMAGE_BAD_RESET_RANGE;
    }

    return FULL_IMAGE_VALID;
}
