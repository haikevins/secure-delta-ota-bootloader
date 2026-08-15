#ifndef BACKUP_IMAGE_H
#define BACKUP_IMAGE_H

#include <stdint.h>

#define BACKUP_IMAGE_MAGIC            0x38504B42UL /* "BKP8" */
#define BACKUP_IMAGE_FORMAT_VERSION   1UL
#define BACKUP_IMAGE_DATA_OFFSET      0x1000UL

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t active_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t load_address;
    uint32_t crc32;
} BackupImageRecord_t;

typedef enum
{
    BACKUP_IMAGE_VALID = 0,
    BACKUP_IMAGE_INVALID_ARGUMENT,
    BACKUP_IMAGE_INVALID_MAGIC,
    BACKUP_IMAGE_INVALID_VERSION,
    BACKUP_IMAGE_INVALID_ACTIVE_VERSION,
    BACKUP_IMAGE_INVALID_SIZE,
    BACKUP_IMAGE_INVALID_LOAD_ADDRESS,
    BACKUP_IMAGE_INVALID_CRC
} BackupImageValidationStatus_t;

void BackupImage_Init(BackupImageRecord_t *record,
                      uint32_t active_version,
                      uint32_t image_crc32);
uint32_t BackupImage_CalculateCrc(const BackupImageRecord_t *record);
void BackupImage_Finalize(BackupImageRecord_t *record);
BackupImageValidationStatus_t BackupImage_Validate(
    const BackupImageRecord_t *record);

#endif
