#ifndef SECURE_CONTAINER_H
#define SECURE_CONTAINER_H

#include <stdint.h>

#include "boot_metadata.h"
#include "firmware_container.h"

#define SECURE_CONTAINER_ERROR_BASE 0x00140000UL

typedef enum
{
    SECURE_CONTAINER_OK = 0,
    SECURE_CONTAINER_NOT_PRESENT,
    SECURE_CONTAINER_UNPROVISIONED_KEY,
    SECURE_CONTAINER_FLASH_INIT_FAILED,
    SECURE_CONTAINER_HEADER_READ_FAILED,
    SECURE_CONTAINER_HEADER_INVALID,
    SECURE_CONTAINER_KEY_ID_MISMATCH,
    SECURE_CONTAINER_METADATA_MISMATCH,
    SECURE_CONTAINER_VERSION_REJECTED,
    SECURE_CONTAINER_PAYLOAD_READ_FAILED,
    SECURE_CONTAINER_PAYLOAD_CRC_MISMATCH,
    SECURE_CONTAINER_SIGNATURE_READ_FAILED,
    SECURE_CONTAINER_SIGNATURE_INVALID,
    SECURE_CONTAINER_BASE_VERSION_MISMATCH,
    SECURE_CONTAINER_BASE_HASH_MISMATCH,
    SECURE_CONTAINER_TARGET_ERASE_FAILED,
    SECURE_CONTAINER_TARGET_WRITE_FAILED,
    SECURE_CONTAINER_PATCH_FAILED,
    SECURE_CONTAINER_TARGET_READ_FAILED,
    SECURE_CONTAINER_TARGET_HASH_MISMATCH,
    SECURE_CONTAINER_TARGET_CRC_MISMATCH,
    SECURE_CONTAINER_TARGET_VECTOR_INVALID,
    SECURE_CONTAINER_METADATA_COMMIT_FAILED,
    SECURE_CONTAINER_SOURCE_REJECTED
} SecureContainerStatus_t;

uint8_t SecureContainer_IsIncoming(void);

/*
 * Parse and authenticate the signed header + payload currently stored in
 * Incoming Artifact. artifact_size=0 accepts the canonical size from header.
 * This function does not inspect the currently installed base image.
 */
SecureContainerStatus_t SecureContainer_LoadVerifiedInfo(
    uint32_t artifact_size,
    FirmwareContainerInfo_t *info);

/*
 * Owns signed secure container states from ARTIFACT_READY through IMAGE_READY.
 * It authenticates the container, enforces version/base policy, reconstructs
 * into external Flash, verifies the target and commits IMAGE_READY.
 */
SecureContainerStatus_t SecureContainer_Process(
    const BootMetadata_t *metadata,
    BootMetadata_t *result_metadata);

#endif
