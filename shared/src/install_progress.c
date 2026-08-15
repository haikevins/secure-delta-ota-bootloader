#include "install_progress.h"

#include "memory_map.h"

InstallProgressStatus_t InstallProgress_Validate(uint32_t image_size,
                                                 uint32_t copy_offset)
{
    if ((image_size < 8UL) || (image_size > APPLICATION_MAX_SIZE))
    {
        return INSTALL_PROGRESS_INVALID_IMAGE_SIZE;
    }

    if (copy_offset > image_size)
    {
        return INSTALL_PROGRESS_INVALID_OFFSET;
    }

    /*
     * Intermediate checkpoints are always internal-Flash page boundaries.
     * The final checkpoint is the exact image size and may be unaligned.
     */
    if ((copy_offset != image_size) &&
        ((copy_offset & (INTERNAL_FLASH_PAGE_SIZE - 1UL)) != 0UL))
    {
        return INSTALL_PROGRESS_INVALID_ALIGNMENT;
    }

    return INSTALL_PROGRESS_VALID;
}

uint32_t InstallProgress_PageLength(uint32_t image_size,
                                    uint32_t copy_offset)
{
    uint32_t remaining;

    if (InstallProgress_Validate(image_size, copy_offset) !=
        INSTALL_PROGRESS_VALID)
    {
        return 0UL;
    }

    if (copy_offset == image_size)
    {
        return 0UL;
    }

    remaining = image_size - copy_offset;
    return (remaining > INTERNAL_FLASH_PAGE_SIZE)
               ? INTERNAL_FLASH_PAGE_SIZE
               : remaining;
}

uint32_t InstallProgress_NextCheckpoint(uint32_t image_size,
                                        uint32_t copy_offset)
{
    const uint32_t page_length =
        InstallProgress_PageLength(image_size, copy_offset);

    if (page_length == 0UL)
    {
        return copy_offset;
    }

    return copy_offset + page_length;
}
