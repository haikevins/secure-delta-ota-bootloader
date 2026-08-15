#include "secure_container.h"

#include <stdint.h>

#include "crc32.h"
#include "download_checkpoint_storage.h"
#include "ecdsa_p256.h"
#include "external_flash_storage.h"
#include "firmware_container.h"
#include "full_image_validation.h"
#include "janpatch_port.h"
#include "memory_map.h"
#include "metadata_storage.h"
#include "phase14_trusted_key.h"
#include "sha256.h"

#define SECURE_IO_BUFFER_SIZE 256UL

static uint8_t BytesEqual(const uint8_t *left,
                          const uint8_t *right,
                          uint32_t length)
{
    uint8_t difference = 0U;

    for (uint32_t i = 0UL; i < length; ++i)
    {
        difference |= (uint8_t)(left[i] ^ right[i]);
    }

    return (uint8_t)(difference == 0U);
}

static MetadataStorageStatus_t CommitMetadata(BootMetadata_t *metadata)
{
    BootMetadata_t committed;
    MetadataStorageStatus_t status;

    status = MetadataStorage_Commit(
        metadata,
        &committed,
        (BootMetadataSlot_t *)0);

    if (status == METADATA_STORAGE_OK)
    {
        *metadata = committed;
    }

    return status;
}

static void CopyResult(const BootMetadata_t *metadata,
                       BootMetadata_t *result_metadata)
{
    if (result_metadata != (BootMetadata_t *)0)
    {
        *result_metadata = *metadata;
    }
}

static SecureContainerStatus_t Reject(
    BootMetadata_t *metadata,
    SecureContainerStatus_t reason,
    BootMetadata_t *result_metadata)
{
    metadata->state = (uint32_t)UPDATE_IDLE;
    metadata->pending_version = 0UL;
    metadata->active_update_id = 0UL;
    metadata->received_size = 0UL;
    metadata->expected_size = 0UL;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error =
        SECURE_CONTAINER_ERROR_BASE | (uint32_t)reason;

    (void)DownloadCheckpointStorage_Clear();

    if (CommitMetadata(metadata) != METADATA_STORAGE_OK)
    {
        return SECURE_CONTAINER_METADATA_COMMIT_FAILED;
    }

    CopyResult(metadata, result_metadata);
    return SECURE_CONTAINER_SOURCE_REJECTED;
}

static SecureContainerStatus_t Transition(
    BootMetadata_t *metadata,
    UpdateState_t state)
{
    metadata->state = (uint32_t)state;
    metadata->copy_offset = 0UL;
    metadata->boot_attempts = 0UL;
    metadata->last_error = 0UL;

    return (CommitMetadata(metadata) == METADATA_STORAGE_OK)
               ? SECURE_CONTAINER_OK
               : SECURE_CONTAINER_METADATA_COMMIT_FAILED;
}

static SecureContainerStatus_t ReadHeader(
    uint32_t artifact_size,
    FirmwareContainerInfo_t *info,
    uint8_t raw[FW_CONTAINER_HEADER_SIZE])
{
    FirmwareContainerStatus_t status;

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            raw,
            FW_CONTAINER_HEADER_SIZE))
    {
        return SECURE_CONTAINER_HEADER_READ_FAILED;
    }

    status = FirmwareContainer_Parse(
        raw,
        artifact_size,
        info);

    return (status == FW_CONTAINER_VALID)
               ? SECURE_CONTAINER_OK
               : SECURE_CONTAINER_HEADER_INVALID;
}

static SecureContainerStatus_t HashAndCrcPayload(
    const FirmwareContainerInfo_t *info,
    const uint8_t raw_header[FW_CONTAINER_HEADER_SIZE],
    uint8_t signed_digest[SHA256_DIGEST_SIZE],
    uint8_t payload_digest[SHA256_DIGEST_SIZE],
    uint32_t *payload_crc)
{
    Sha256Context_t signed_hash;
    Sha256Context_t payload_hash;
    uint8_t buffer[SECURE_IO_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running_crc = CRC32_IEEE_INITIAL_VALUE;

    Sha256_Init(&signed_hash);
    Sha256_Init(&payload_hash);
    Sha256_Update(
        &signed_hash,
        raw_header,
        FW_CONTAINER_HEADER_SIZE);

    while (offset < info->header.payload_size)
    {
        uint32_t chunk = info->header.payload_size - offset;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!ExternalFlashStorage_Read(
                EXTERNAL_FLASH_PARTITION_INCOMING,
                info->payload_offset + offset,
                buffer,
                chunk))
        {
            return SECURE_CONTAINER_PAYLOAD_READ_FAILED;
        }

        Sha256_Update(&signed_hash, buffer, chunk);
        Sha256_Update(&payload_hash, buffer, chunk);
        running_crc = Crc32_Update(
            running_crc,
            buffer,
            chunk);
        offset += chunk;
    }

    Sha256_Final(&signed_hash, signed_digest);
    Sha256_Final(&payload_hash, payload_digest);
    *payload_crc = running_crc ^ CRC32_IEEE_FINAL_XOR;
    return SECURE_CONTAINER_OK;
}

uint8_t SecureContainer_IsIncoming(void)
{
    uint8_t magic[4];

    if (!ExternalFlashStorage_Init())
    {
        return 0U;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            magic,
            sizeof(magic)))
    {
        return 0U;
    }

    return (uint8_t)(
        FirmwareContainer_GetU32Le(magic) == FW_CONTAINER_MAGIC);
}

SecureContainerStatus_t SecureContainer_LoadVerifiedInfo(
    uint32_t artifact_size,
    FirmwareContainerInfo_t *info)
{
    uint8_t raw_header[FW_CONTAINER_HEADER_SIZE];
    uint8_t signed_digest[SHA256_DIGEST_SIZE];
    uint8_t payload_digest[SHA256_DIGEST_SIZE];
    uint8_t signature[FW_ECDSA_P256_RAW_SIGNATURE_SIZE];
    uint32_t payload_crc = 0UL;
    SecureContainerStatus_t status;

    if (info == (FirmwareContainerInfo_t *)0)
    {
        return SECURE_CONTAINER_HEADER_INVALID;
    }

    if (!ExternalFlashStorage_Init())
    {
        return SECURE_CONTAINER_FLASH_INIT_FAILED;
    }

#if PHASE14_TRUSTED_KEY_PROVISIONED == 0
    return SECURE_CONTAINER_UNPROVISIONED_KEY;
#endif

    status = ReadHeader(artifact_size, info, raw_header);
    if (status != SECURE_CONTAINER_OK)
    {
        return status;
    }

    if (info->extension.key_id != PHASE14_TRUSTED_KEY_ID)
    {
        return SECURE_CONTAINER_KEY_ID_MISMATCH;
    }

    status = HashAndCrcPayload(
        info,
        raw_header,
        signed_digest,
        payload_digest,
        &payload_crc);
    if (status != SECURE_CONTAINER_OK)
    {
        return status;
    }

    if (payload_crc != info->header.payload_crc32)
    {
        return SECURE_CONTAINER_PAYLOAD_CRC_MISMATCH;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            info->signature_offset,
            signature,
            sizeof(signature)))
    {
        return SECURE_CONTAINER_SIGNATURE_READ_FAILED;
    }

    if (EcdsaP256_VerifyDigest(
            g_phase14_trusted_public_key,
            signed_digest,
            signature) != ECDSA_P256_VALID)
    {
        return SECURE_CONTAINER_SIGNATURE_INVALID;
    }

    /*
     * For a full image the payload itself is the final target, so its signed
     * target hash can be checked immediately. Delta target hash is checked
     * only after reconstruction.
     */
    if ((info->header.image_type == (uint32_t)FW_IMAGE_FULL) &&
        (BytesEqual(
             payload_digest,
             info->header.target_image_sha256,
             FW_SHA256_SIZE) == 0U))
    {
        return SECURE_CONTAINER_TARGET_HASH_MISMATCH;
    }

    return SECURE_CONTAINER_OK;
}

static SecureContainerStatus_t ValidateMetadataPolicy(
    const FirmwareContainerInfo_t *info,
    const BootMetadata_t *metadata)
{
    if ((metadata->active_update_id == 0UL) ||
        (metadata->pending_version != info->header.target_version) ||
        (metadata->received_size != metadata->expected_size) ||
        (metadata->expected_size != info->total_size))
    {
        return SECURE_CONTAINER_METADATA_MISMATCH;
    }

    if (info->header.target_version <= metadata->active_version)
    {
        return SECURE_CONTAINER_VERSION_REJECTED;
    }

    if (info->header.image_type == (uint32_t)FW_IMAGE_FULL)
    {
        if (info->header.base_version != 0UL)
        {
            return SECURE_CONTAINER_METADATA_MISMATCH;
        }
    }
    else
    {
        if (info->header.base_version != metadata->active_version)
        {
            return SECURE_CONTAINER_BASE_VERSION_MISMATCH;
        }
    }

    return SECURE_CONTAINER_OK;
}

static SecureContainerStatus_t ValidateBase(
    const FirmwareContainerInfo_t *info)
{
    uint8_t digest[SHA256_DIGEST_SIZE];

    if (info->header.image_type != (uint32_t)FW_IMAGE_DELTA)
    {
        return SECURE_CONTAINER_OK;
    }

    Sha256_Calculate(
        (const void *)APPLICATION_START_ADDRESS,
        info->extension.base_image_size,
        digest);

    return (BytesEqual(
                digest,
                info->header.base_image_sha256,
                FW_SHA256_SIZE) != 0U)
               ? SECURE_CONTAINER_OK
               : SECURE_CONTAINER_BASE_HASH_MISMATCH;
}

static SecureContainerStatus_t EraseReconstructed(uint32_t target_size)
{
    uint32_t erase_size =
        (target_size + EXT_FLASH_SECTOR_SIZE - 1UL) &
        ~(EXT_FLASH_SECTOR_SIZE - 1UL);

    if ((erase_size == 0UL) ||
        (erase_size > EXT_RECONSTRUCTED_SIZE))
    {
        return SECURE_CONTAINER_TARGET_ERASE_FAILED;
    }

    return ExternalFlashStorage_EraseRange(
               EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
               0UL,
               erase_size)
               ? SECURE_CONTAINER_OK
               : SECURE_CONTAINER_TARGET_ERASE_FAILED;
}

static SecureContainerStatus_t CopyFullPayload(
    const FirmwareContainerInfo_t *info)
{
    uint8_t buffer[SECURE_IO_BUFFER_SIZE];
    uint32_t offset = 0UL;

    while (offset < info->header.payload_size)
    {
        uint32_t chunk = info->header.payload_size - offset;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!ExternalFlashStorage_Read(
                EXTERNAL_FLASH_PARTITION_INCOMING,
                info->payload_offset + offset,
                buffer,
                chunk))
        {
            return SECURE_CONTAINER_PAYLOAD_READ_FAILED;
        }

        if (!ExternalFlashStorage_Write(
                EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
                offset,
                buffer,
                chunk))
        {
            return SECURE_CONTAINER_TARGET_WRITE_FAILED;
        }

        offset += chunk;
    }

    return SECURE_CONTAINER_OK;
}

static SecureContainerStatus_t ApplyDeltaPayload(
    const FirmwareContainerInfo_t *info)
{
    JanpatchPortStream_t stream;
    JanpatchPortResult_t result;

    stream.base_image_size = info->extension.base_image_size;
    stream.patch_offset = info->payload_offset;
    stream.patch_size = info->header.payload_size;
    stream.target_image_size = info->header.target_image_size;

    if (JanpatchPort_ApplyStream(&stream, &result) != JANPATCH_PORT_OK)
    {
        return SECURE_CONTAINER_PATCH_FAILED;
    }

    if ((result.patch_position != stream.patch_size) ||
        (result.target_position != stream.target_image_size))
    {
        return SECURE_CONTAINER_PATCH_FAILED;
    }

    return SECURE_CONTAINER_OK;
}

static SecureContainerStatus_t HashReconstructed(
    uint32_t length,
    uint8_t digest[SHA256_DIGEST_SIZE],
    uint32_t *crc_out)
{
    Sha256Context_t context;
    uint8_t buffer[SECURE_IO_BUFFER_SIZE];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    Sha256_Init(&context);

    while (offset < length)
    {
        uint32_t chunk = length - offset;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!ExternalFlashStorage_Read(
                EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
                offset,
                buffer,
                chunk))
        {
            return SECURE_CONTAINER_TARGET_READ_FAILED;
        }

        Sha256_Update(&context, buffer, chunk);
        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    Sha256_Final(&context, digest);
    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return SECURE_CONTAINER_OK;
}

static SecureContainerStatus_t VerifyReconstructed(
    const FirmwareContainerInfo_t *info)
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint8_t vector[8];
    uint32_t crc = 0UL;
    SecureContainerStatus_t status;

    status = HashReconstructed(
        info->header.target_image_size,
        digest,
        &crc);
    if (status != SECURE_CONTAINER_OK)
    {
        return status;
    }

    if (BytesEqual(
            digest,
            info->header.target_image_sha256,
            FW_SHA256_SIZE) == 0U)
    {
        return SECURE_CONTAINER_TARGET_HASH_MISMATCH;
    }

    if (crc != info->extension.target_image_crc32)
    {
        return SECURE_CONTAINER_TARGET_CRC_MISMATCH;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
            0UL,
            vector,
            sizeof(vector)))
    {
        return SECURE_CONTAINER_TARGET_READ_FAILED;
    }

    if (FullImage_ValidateVector(
            info->header.target_image_size,
            FirmwareContainer_GetU32Le(&vector[0]),
            FirmwareContainer_GetU32Le(&vector[4])) != FULL_IMAGE_VALID)
    {
        return SECURE_CONTAINER_TARGET_VECTOR_INVALID;
    }

    return SECURE_CONTAINER_OK;
}

SecureContainerStatus_t SecureContainer_Process(
    const BootMetadata_t *metadata,
    BootMetadata_t *result_metadata)
{
    BootMetadata_t working;
    FirmwareContainerInfo_t info;
    SecureContainerStatus_t status;

    if (metadata == (const BootMetadata_t *)0)
    {
        return SECURE_CONTAINER_METADATA_MISMATCH;
    }

    working = *metadata;

    if (!ExternalFlashStorage_Init())
    {
        return SECURE_CONTAINER_FLASH_INIT_FAILED;
    }

    if (SecureContainer_IsIncoming() == 0U)
    {
        return Reject(
            &working,
            SECURE_CONTAINER_NOT_PRESENT,
            result_metadata);
    }

    status = SecureContainer_LoadVerifiedInfo(
        working.expected_size,
        &info);
    if (status != SECURE_CONTAINER_OK)
    {
        return Reject(&working, status, result_metadata);
    }

    status = ValidateMetadataPolicy(&info, &working);
    if (status != SECURE_CONTAINER_OK)
    {
        return Reject(&working, status, result_metadata);
    }

    if (working.state == (uint32_t)UPDATE_ARTIFACT_READY)
    {
        status = Transition(
            &working,
            UPDATE_VERIFYING_CONTAINER);
        if (status != SECURE_CONTAINER_OK)
        {
            return status;
        }
    }

    if (info.header.image_type == (uint32_t)FW_IMAGE_DELTA)
    {
        if (working.state == (uint32_t)UPDATE_VERIFYING_CONTAINER)
        {
            status = Transition(
                &working,
                UPDATE_VERIFYING_BASE);
            if (status != SECURE_CONTAINER_OK)
            {
                return status;
            }
        }

        status = ValidateBase(&info);
        if (status != SECURE_CONTAINER_OK)
        {
            return Reject(&working, status, result_metadata);
        }

        if (working.state == (uint32_t)UPDATE_VERIFYING_BASE)
        {
            status = Transition(
                &working,
                UPDATE_PATCHING);
            if (status != SECURE_CONTAINER_OK)
            {
                return status;
            }
        }
    }
    else if (working.state == (uint32_t)UPDATE_VERIFYING_CONTAINER)
    {
        status = Transition(
            &working,
            UPDATE_PATCHING);
        if (status != SECURE_CONTAINER_OK)
        {
            return status;
        }
    }

    if (working.state != (uint32_t)UPDATE_PATCHING)
    {
        return Reject(
            &working,
            SECURE_CONTAINER_METADATA_MISMATCH,
            result_metadata);
    }

    status = EraseReconstructed(info.header.target_image_size);
    if (status != SECURE_CONTAINER_OK)
    {
        return Reject(&working, status, result_metadata);
    }

    if (info.header.image_type == (uint32_t)FW_IMAGE_DELTA)
    {
        status = ApplyDeltaPayload(&info);
    }
    else
    {
        status = CopyFullPayload(&info);
    }

    if (status != SECURE_CONTAINER_OK)
    {
        return Reject(&working, status, result_metadata);
    }

    status = VerifyReconstructed(&info);
    if (status != SECURE_CONTAINER_OK)
    {
        return Reject(&working, status, result_metadata);
    }

    working.state = (uint32_t)UPDATE_IMAGE_READY;
    working.received_size = info.header.target_image_size;
    working.expected_size = info.header.target_image_size;
    working.copy_offset = 0UL;
    working.boot_attempts = 0UL;
    working.last_error = 0UL;

    if (CommitMetadata(&working) != METADATA_STORAGE_OK)
    {
        return SECURE_CONTAINER_METADATA_COMMIT_FAILED;
    }

    CopyResult(&working, result_metadata);
    return SECURE_CONTAINER_OK;
}
