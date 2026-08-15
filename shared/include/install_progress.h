#ifndef INSTALL_PROGRESS_H
#define INSTALL_PROGRESS_H

#include <stdint.h>

typedef enum
{
    INSTALL_PROGRESS_VALID = 0,
    INSTALL_PROGRESS_INVALID_IMAGE_SIZE,
    INSTALL_PROGRESS_INVALID_OFFSET,
    INSTALL_PROGRESS_INVALID_ALIGNMENT
} InstallProgressStatus_t;

InstallProgressStatus_t InstallProgress_Validate(uint32_t image_size,
                                                 uint32_t copy_offset);
uint32_t InstallProgress_PageLength(uint32_t image_size,
                                    uint32_t copy_offset);
uint32_t InstallProgress_NextCheckpoint(uint32_t image_size,
                                        uint32_t copy_offset);

#endif
