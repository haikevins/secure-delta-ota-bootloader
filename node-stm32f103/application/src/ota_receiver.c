#include "ota_receiver.h"

#include "boot_metadata.h"
#include "crc32.h"
#include "download_checkpoint.h"
#include "download_checkpoint_storage.h"
#include "delta_patch.h"
#include "external_flash_storage.h"
#include "firmware_container.h"
#include "firmware_version.h"
#include "full_image_validation.h"
#include "memory_map.h"
#include "metadata_storage.h"
#include "ota_status.h"
#include "spi_flash.h"
#include "trial_confirmation.h"
#include "update_handoff.h"
#include "update_handoff_storage.h"

typedef struct
{
    uint8_t state;
    uint8_t last_status;
    uint8_t storage_ready;
    uint8_t has_last_data;
    uint8_t reset_pending;
    uint32_t update_id;
    uint32_t target_version;
    uint32_t base_version;
    uint32_t expected_size;
    uint32_t next_offset;
    uint32_t persisted_offset;
    uint32_t artifact_crc32;
    uint32_t last_error_detail;
    uint32_t erased_sector_bitmap;
    uint32_t last_data_offset;
    uint16_t expected_sequence;
    uint16_t last_data_sequence;
    uint16_t last_data_length;
    uint8_t artifact_type;
} OtaReceiverSession_t;

static OtaReceiverSession_t g_session;

static uint16_t GetU16Le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}

static uint32_t GetU32Le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8U) |
           ((uint32_t)src[2] << 16U) |
           ((uint32_t)src[3] << 24U);
}

static void ResetTransfer(void)
{
    g_session.state = (uint8_t)UPDATE_IDLE;
    g_session.update_id = 0UL;
    g_session.target_version = 0UL;
    g_session.base_version = 0UL;
    g_session.expected_size = 0UL;
    g_session.next_offset = 0UL;
    g_session.persisted_offset = 0UL;
    g_session.artifact_crc32 = 0UL;
    g_session.erased_sector_bitmap = 0UL;
    g_session.expected_sequence = 0U;
    g_session.has_last_data = 0U;
    g_session.reset_pending = 0U;
    g_session.last_data_offset = 0UL;
    g_session.last_data_sequence = 0U;
    g_session.last_data_length = 0U;
    g_session.artifact_type = 0U;
}


static uint32_t CheckpointErrorDetail(DownloadCheckpointStorageStatus_t status)
{
    return 0x00071000UL | (uint32_t)status;
}

static DownloadCheckpointStorageStatus_t PersistDownloadCheckpoint(
    DownloadCheckpointState_t state,
    uint32_t next_offset)
{
    DownloadCheckpointRecord_t requested;
    DownloadCheckpointRecord_t committed;
    DownloadCheckpointStorageStatus_t status;

    DownloadCheckpoint_InitSession(&requested,
                                   state,
                                   g_session.update_id,
                                   g_session.target_version,
                                   g_session.expected_size,
                                   g_session.artifact_crc32,
                                   next_offset);

    status = DownloadCheckpointStorage_Commit(
        &requested,
        &committed,
        (DownloadCheckpointSlot_t *)0);

    if (status == DOWNLOAD_CHECKPOINT_STORAGE_OK)
    {
        g_session.persisted_offset = committed.next_offset;
    }

    return status;
}

static DownloadCheckpointStorageStatus_t ClearDownloadCheckpoint(void)
{
    const DownloadCheckpointStorageStatus_t status =
        DownloadCheckpointStorage_Clear();

    if (status == DOWNLOAD_CHECKPOINT_STORAGE_OK)
    {
        g_session.persisted_offset = 0UL;
    }

    return status;
}

static uint32_t CompletedSectorBitmap(uint32_t checkpoint_offset)
{
    uint32_t bitmap = 0UL;
    uint32_t sector;
    const uint32_t complete_sectors =
        checkpoint_offset / EXT_FLASH_SECTOR_SIZE;

    for (sector = 0UL; sector < complete_sectors; ++sector)
    {
        bitmap |= (1UL << sector);
    }

    return bitmap;
}


static void MirrorPersistentTrialState(void)
{
    BootMetadata_t metadata;
    const MetadataStorageStatus_t status =
        MetadataStorage_Load(&metadata, (BootMetadataSlot_t *)0);

    if ((status == METADATA_STORAGE_OK) &&
        ((metadata.state == (uint32_t)UPDATE_TRIAL_BOOT) ||
         (metadata.state == (uint32_t)UPDATE_CONFIRMED)))
    {
        g_session.state = (uint8_t)metadata.state;
        g_session.update_id = metadata.active_update_id;
        g_session.target_version = metadata.pending_version;
        g_session.expected_size = metadata.expected_size;
        g_session.next_offset = metadata.received_size;
        g_session.persisted_offset = 0UL;
        g_session.expected_sequence = 0U;
        g_session.has_last_data = 0U;
    }
}

static DownloadCheckpointStorageStatus_t RestoreDownloadCheckpoint(void)
{
    DownloadCheckpointRecord_t record;
    DownloadCheckpointStorageStatus_t status;

    status = DownloadCheckpointStorage_Load(
        &record,
        (DownloadCheckpointSlot_t *)0);

    if (status == DOWNLOAD_CHECKPOINT_STORAGE_NOT_FOUND)
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_OK;
    }

    if (status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
    {
        return status;
    }

    if (record.state == (uint32_t)DOWNLOAD_CHECKPOINT_IDLE)
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_OK;
    }

    g_session.state =
        (record.state == (uint32_t)DOWNLOAD_CHECKPOINT_ARTIFACT_READY)
            ? (uint8_t)UPDATE_ARTIFACT_READY
            : (uint8_t)UPDATE_RECEIVING;
    g_session.update_id = record.update_id;
    g_session.target_version = record.target_version;
    g_session.expected_size = record.image_size;
    g_session.next_offset = record.next_offset;
    g_session.persisted_offset = record.next_offset;
    g_session.artifact_crc32 = record.image_crc32;
    g_session.erased_sector_bitmap =
        CompletedSectorBitmap(record.next_offset);
    g_session.expected_sequence =
        (uint16_t)((record.next_offset / OTA_MAX_PAYLOAD_SIZE) & 0xFFFFUL);
    g_session.has_last_data = 0U;
    g_session.reset_pending = 0U;

    return DOWNLOAD_CHECKPOINT_STORAGE_OK;
}

static DownloadCheckpointStorageStatus_t PersistReceiveBoundaryIfNeeded(void)
{
    if ((g_session.next_offset == 0UL) ||
        ((g_session.next_offset & (EXT_FLASH_SECTOR_SIZE - 1UL)) != 0UL) ||
        (g_session.persisted_offset == g_session.next_offset))
    {
        return DOWNLOAD_CHECKPOINT_STORAGE_OK;
    }

    return PersistDownloadCheckpoint(DOWNLOAD_CHECKPOINT_RECEIVING,
                                     g_session.next_offset);
}

void OtaReceiver_GetResponseInfo(OtaResponseInfo_t *info)
{
    if (info == (OtaResponseInfo_t *)0) { return; }

    info->update_state = g_session.state;
    info->last_status = g_session.last_status;
    info->active_update_id = g_session.update_id;
    info->next_expected_offset = g_session.next_offset;
    info->received_size = g_session.next_offset;
    info->expected_size = g_session.expected_size;
    info->last_error_detail = g_session.last_error_detail;
    info->capability_flags = (g_session.storage_ready != 0U)
                                 ? (OTA_CAP_FULL_IMAGE |
                                    OTA_CAP_DELTA_IMAGE |
                                    OTA_CAP_RESUME |
                                    OTA_CAP_SIGNATURE_VERIFY |
                                    OTA_CAP_ROLLBACK)
                                 : 0UL;
}

static void SetStatus(OtaStatus_t status, uint32_t detail)
{
    g_session.last_status = (uint8_t)status;
    g_session.last_error_detail = detail;
}

static void Ack(const OtaPacket_t *request, OtaPacket_t *response)
{
    OtaResponseInfo_t info;
    SetStatus(OTA_STATUS_OK, 0UL);
    OtaReceiver_GetResponseInfo(&info);
    OtaResponse_BuildAck(request, &info, response);
}

static void Nack(const OtaPacket_t *request,
                 OtaStatus_t status,
                 uint32_t detail,
                 OtaPacket_t *response)
{
    OtaResponseInfo_t info;
    SetStatus(status, detail);
    OtaReceiver_GetResponseInfo(&info);
    OtaResponse_BuildNack(request, status, &info, response);
}

static bool EnsureIncomingSectorsErased(uint32_t offset, uint32_t length)
{
    uint32_t first_sector;
    uint32_t last_sector;
    uint32_t sector;

    if (length == 0UL) { return false; }

    first_sector = offset / EXT_FLASH_SECTOR_SIZE;
    last_sector = (offset + length - 1UL) / EXT_FLASH_SECTOR_SIZE;

    for (sector = first_sector; sector <= last_sector; ++sector)
    {
        const uint32_t mask = 1UL << sector;
        if ((g_session.erased_sector_bitmap & mask) == 0UL)
        {
            if (!ExternalFlashStorage_EraseRange(
                    EXTERNAL_FLASH_PARTITION_INCOMING,
                    sector * EXT_FLASH_SECTOR_SIZE,
                    EXT_FLASH_SECTOR_SIZE))
            {
                return false;
            }
            g_session.erased_sector_bitmap |= mask;
        }
    }
    return true;
}

static bool StoredDataMatches(const OtaPacket_t *request)
{
    uint8_t readback[OTA_MAX_PAYLOAD_SIZE];
    uint32_t i;

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                   request->offset,
                                   readback,
                                   request->payload_length))
    {
        return false;
    }

    for (i = 0UL; i < (uint32_t)request->payload_length; ++i)
    {
        if (readback[i] != request->payload[i]) { return false; }
    }
    return true;
}

static bool CalculateIncomingCrc(uint32_t length, uint32_t *crc_out)
{
    uint8_t buffer[256];
    uint32_t offset = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    if (crc_out == (uint32_t *)0) { return false; }

    while (offset < length)
    {
        uint32_t chunk = length - offset;
        if (chunk > sizeof(buffer)) { chunk = sizeof(buffer); }

        if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                       offset, buffer, chunk))
        {
            return false;
        }
        running = Crc32_Update(running, buffer, chunk);
        offset += chunk;
    }

    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return true;
}


#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
static bool CalculateIncomingCrcRange(uint32_t offset,
                                      uint32_t length,
                                      uint32_t *crc_out)
{
    uint8_t buffer[256];
    uint32_t processed = 0UL;
    uint32_t running = CRC32_IEEE_INITIAL_VALUE;

    if ((crc_out == (uint32_t *)0) ||
        (offset > g_session.expected_size) ||
        (length > (g_session.expected_size - offset)))
    {
        return false;
    }

    while (processed < length)
    {
        uint32_t chunk = length - processed;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if (!ExternalFlashStorage_Read(
                EXTERNAL_FLASH_PARTITION_INCOMING,
                offset + processed,
                buffer,
                chunk))
        {
            return false;
        }

        running = Crc32_Update(running, buffer, chunk);
        processed += chunk;
    }

    *crc_out = running ^ CRC32_IEEE_FINAL_XOR;
    return true;
}

static bool ReadDeltaHeader(DeltaPatchHeader_t *header,
                            DeltaPatchHeaderStatus_t *validation)
{
    uint8_t raw[DELTA_PATCH_HEADER_SIZE];
    DeltaPatchHeaderStatus_t status;

    if (header == (DeltaPatchHeader_t *)0)
    {
        return false;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            raw,
            sizeof(raw)))
    {
        return false;
    }

    status = DeltaPatch_ParseHeader(
        raw,
        g_session.expected_size,
        header);

    if (validation != (DeltaPatchHeaderStatus_t *)0)
    {
        *validation = status;
    }

    return status == DELTA_PATCH_HEADER_VALID;
}
#endif

static uint8_t IncomingLooksLikeSecureContainer(void)
{
    uint8_t magic[4];

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            magic,
            sizeof(magic)))
    {
        return 0U;
    }

    return (uint8_t)(
        GetU32Le(magic) == FW_CONTAINER_MAGIC);
}

#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
static uint8_t IncomingLooksLikeDelta(void)
{
    uint8_t magic[4];

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            0UL,
            magic,
            sizeof(magic)))
    {
        return 0U;
    }

    return (uint8_t)(
        GetU32Le(magic) == DELTA_PATCH_MAGIC);
}
#endif

static void ProcessStart(const OtaPacket_t *request, OtaPacket_t *response)
{
    uint8_t artifact_type;
    uint16_t container_version;
    uint32_t base_version;
    uint32_t target_version;
    uint32_t artifact_size;
    uint32_t artifact_crc32;
    uint32_t container_header_size;
    uint8_t header_allowed = 0U;

    if (request->payload_length != OTA_START_PAYLOAD_SIZE)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }
    if ((g_session.state != (uint8_t)UPDATE_IDLE) &&
        (g_session.state != (uint8_t)UPDATE_FAILED) &&
        (g_session.state != (uint8_t)UPDATE_RECEIVING))
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (g_session.storage_ready == 0U)
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }
    if (request->update_id == 0UL)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    artifact_type = request->payload[0];
    container_version = GetU16Le(&request->payload[2]);
    base_version = GetU32Le(&request->payload[4]);
    target_version = GetU32Le(&request->payload[8]);
    artifact_size = GetU32Le(&request->payload[12]);
    artifact_crc32 = GetU32Le(&request->payload[16]);
    container_header_size = GetU32Le(&request->payload[20]);

    if ((artifact_type != (uint8_t)FW_IMAGE_FULL) &&
        (artifact_type != (uint8_t)FW_IMAGE_DELTA))
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, artifact_type, response);
        return;
    }

    if (container_version != FW_CONTAINER_FORMAT_VERSION)
    {
        Nack(request, OTA_STATUS_CONTAINER_ERROR,
             (uint32_t)container_version, response);
        return;
    }

    if ((artifact_size <=
         (FW_CONTAINER_HEADER_SIZE +
          FW_ECDSA_P256_RAW_SIGNATURE_SIZE)) ||
        (artifact_size > EXT_INCOMING_SIZE))
    {
#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
        if (artifact_size == 0UL)
        {
            Nack(request, OTA_STATUS_IMAGE_TOO_LARGE, artifact_size, response);
            return;
        }
#else
        Nack(request, OTA_STATUS_IMAGE_TOO_LARGE, artifact_size, response);
        return;
#endif
    }

    if ((target_version == 0UL) ||
        (target_version <= (uint32_t)APPLICATION_VERSION))
    {
        Nack(request, OTA_STATUS_VERSION_REJECTED,
             (uint32_t)APPLICATION_VERSION, response);
        return;
    }

    if (container_header_size == FW_CONTAINER_HEADER_SIZE)
    {
        header_allowed = 1U;
    }

#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
    if ((artifact_type == (uint8_t)FW_IMAGE_DELTA) &&
        (container_header_size == DELTA_PATCH_HEADER_SIZE))
    {
        header_allowed = 1U;
    }
    if ((artifact_type == (uint8_t)FW_IMAGE_FULL) &&
        (container_header_size == 0UL))
    {
        header_allowed = 1U;
    }
#endif

    if (header_allowed == 0U)
    {
        Nack(request, OTA_STATUS_SIGNATURE_ERROR,
             container_header_size, response);
        return;
    }

    if (artifact_type == (uint8_t)FW_IMAGE_DELTA)
    {
        if ((base_version == 0UL) ||
            (base_version != (uint32_t)APPLICATION_VERSION))
        {
            Nack(request, OTA_STATUS_BASE_MISMATCH,
                 (uint32_t)APPLICATION_VERSION, response);
            return;
        }
    }
    else if (base_version != 0UL)
    {
        Nack(request, OTA_STATUS_CONTAINER_ERROR,
             base_version, response);
        return;
    }

    g_session.state = (uint8_t)UPDATE_RECEIVING;
    g_session.update_id = request->update_id;
    g_session.target_version = target_version;
    g_session.base_version = base_version;
    g_session.artifact_type = artifact_type;
    g_session.expected_size = artifact_size;
    g_session.next_offset = 0UL;
    g_session.artifact_crc32 = artifact_crc32;
    g_session.erased_sector_bitmap = 0UL;
    g_session.expected_sequence = 0U;
    g_session.has_last_data = 0U;
    g_session.reset_pending = 0U;

    {
        const DownloadCheckpointStorageStatus_t checkpoint_status =
            PersistDownloadCheckpoint(DOWNLOAD_CHECKPOINT_RECEIVING, 0UL);
        if (checkpoint_status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
        {
            ResetTransfer();
            Nack(request, OTA_STATUS_STORAGE_ERROR,
                 CheckpointErrorDetail(checkpoint_status), response);
            return;
        }
    }

    Ack(request, response);
}

static void ProcessData(const OtaPacket_t *request, OtaPacket_t *response)
{
    if (g_session.state != (uint8_t)UPDATE_RECEIVING)
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (request->update_id != g_session.update_id)
    {
        Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
        return;
    }
    if ((request->payload_length == 0U) ||
        (request->payload_length > OTA_MAX_PAYLOAD_SIZE))
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    if ((g_session.has_last_data != 0U) &&
        (request->sequence == g_session.last_data_sequence) &&
        (request->offset == g_session.last_data_offset) &&
        (request->payload_length == g_session.last_data_length))
    {
        if (StoredDataMatches(request))
        {
            const DownloadCheckpointStorageStatus_t checkpoint_status =
                PersistReceiveBoundaryIfNeeded();

            if (checkpoint_status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
            {
                Nack(request, OTA_STATUS_STORAGE_ERROR,
                     CheckpointErrorDetail(checkpoint_status), response);
            }
            else
            {
                Ack(request, response);
            }
        }
        else
        {
            Nack(request, OTA_STATUS_WRONG_OFFSET, request->offset, response);
        }
        return;
    }

    if (request->sequence != g_session.expected_sequence)
    {
        Nack(request, OTA_STATUS_WRONG_SEQUENCE,
             (uint32_t)g_session.expected_sequence, response);
        return;
    }
    if (request->offset != g_session.next_offset)
    {
        Nack(request, OTA_STATUS_WRONG_OFFSET,
             g_session.next_offset, response);
        return;
    }
    if ((uint32_t)request->payload_length >
        (g_session.expected_size - g_session.next_offset))
    {
        Nack(request, OTA_STATUS_WRONG_OFFSET,
             g_session.expected_size, response);
        return;
    }

    if (!EnsureIncomingSectorsErased(request->offset, request->payload_length))
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }

    if (!ExternalFlashStorage_Write(EXTERNAL_FLASH_PARTITION_INCOMING,
                                    request->offset,
                                    request->payload,
                                    request->payload_length) ||
        !ExternalFlashStorage_Verify(EXTERNAL_FLASH_PARTITION_INCOMING,
                                     request->offset,
                                     request->payload,
                                     request->payload_length))
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }

    g_session.has_last_data = 1U;
    g_session.last_data_offset = request->offset;
    g_session.last_data_sequence = request->sequence;
    g_session.last_data_length = request->payload_length;
    g_session.next_offset += (uint32_t)request->payload_length;
    g_session.expected_sequence = (uint16_t)(g_session.expected_sequence + 1U);

    {
        const DownloadCheckpointStorageStatus_t checkpoint_status =
            PersistReceiveBoundaryIfNeeded();

        if (checkpoint_status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
        {
            Nack(request, OTA_STATUS_STORAGE_ERROR,
                 CheckpointErrorDetail(checkpoint_status), response);
            return;
        }
    }

    Ack(request, response);
}

static void ProcessFinish(const OtaPacket_t *request, OtaPacket_t *response)
{
    uint32_t artifact_crc = 0UL;

    if (request->payload_length != 0U)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    if ((g_session.state == (uint8_t)UPDATE_ARTIFACT_READY) &&
        (request->update_id == g_session.update_id) &&
        (g_session.next_offset == g_session.expected_size))
    {
        Ack(request, response);
        return;
    }

    if (g_session.state != (uint8_t)UPDATE_RECEIVING)
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (request->update_id != g_session.update_id)
    {
        Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
        return;
    }
    if (g_session.next_offset != g_session.expected_size)
    {
        Nack(request, OTA_STATUS_WRONG_OFFSET,
             g_session.next_offset, response);
        return;
    }

    if (!CalculateIncomingCrc(g_session.expected_size, &artifact_crc))
    {
        Nack(request, OTA_STATUS_STORAGE_ERROR,
             (uint32_t)SpiFlash_GetLastStatus(), response);
        return;
    }

    if (artifact_crc != g_session.artifact_crc32)
    {
        g_session.state = (uint8_t)UPDATE_FAILED;
        Nack(request, OTA_STATUS_CONTAINER_ERROR, artifact_crc, response);
        return;
    }

    {
        const DownloadCheckpointStorageStatus_t checkpoint_status =
            PersistDownloadCheckpoint(DOWNLOAD_CHECKPOINT_ARTIFACT_READY,
                                      g_session.expected_size);

        if (checkpoint_status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
        {
            Nack(request, OTA_STATUS_STORAGE_ERROR,
                 CheckpointErrorDetail(checkpoint_status), response);
            return;
        }
    }

    g_session.state = (uint8_t)UPDATE_ARTIFACT_READY;
    Ack(request, response);
}

#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
static bool ValidateIncomingApplication(uint32_t *detail)
{
    uint8_t vector[8];
    FullImageValidationStatus_t status;

    if (!ExternalFlashStorage_Read(EXTERNAL_FLASH_PARTITION_INCOMING,
                                   0UL, vector, sizeof(vector)))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = (uint32_t)SpiFlash_GetLastStatus();
        }
        return false;
    }

    status = FullImage_ValidateVector(g_session.expected_size,
                                     GetU32Le(&vector[0]),
                                     GetU32Le(&vector[4]));
    if (detail != (uint32_t *)0) { *detail = (uint32_t)status; }
    return status == FULL_IMAGE_VALID;
}

static bool PersistFullInstallRequest(uint32_t *detail)
{
    UpdateHandoffRecord_t record;
    UpdateHandoffRecord_t committed_record;
    BootMetadata_t metadata;
    BootMetadata_t committed_metadata;
    MetadataStorageStatus_t metadata_status;
    UpdateHandoffStorageStatus_t handoff_status;

    UpdateHandoff_Init(&record,
                       g_session.update_id,
                       g_session.target_version,
                       g_session.expected_size,
                       g_session.artifact_crc32);

    handoff_status = UpdateHandoffStorage_Commit(
        &record,
        &committed_record,
        (UpdateHandoffSlot_t *)0);
    if (handoff_status != UPDATE_HANDOFF_STORAGE_OK)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = (uint32_t)handoff_status;
        }
        return false;
    }

    metadata_status = MetadataStorage_Load(
        &metadata,
        (BootMetadataSlot_t *)0);
    if ((metadata_status != METADATA_STORAGE_OK) &&
        (metadata_status != METADATA_STORAGE_DEFAULTS_USED))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x100UL | (uint32_t)metadata_status;
        }
        return false;
    }

    metadata.state = (uint32_t)UPDATE_ARTIFACT_READY;
    metadata.pending_version = committed_record.target_version;
    metadata.active_update_id = committed_record.update_id;
    metadata.received_size = committed_record.image_size;
    metadata.expected_size = committed_record.image_size;
    metadata.copy_offset = 0UL;
    metadata.boot_attempts = 0UL;
    metadata.last_error = 0UL;

    metadata_status = MetadataStorage_Commit(
        &metadata,
        &committed_metadata,
        (BootMetadataSlot_t *)0);
    if (metadata_status != METADATA_STORAGE_OK)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x200UL | (uint32_t)metadata_status;
        }
        return false;
    }

    return true;
}

static bool ValidateDeltaInstallRequest(
    DeltaPatchHeader_t *header,
    uint32_t *detail)
{
    DeltaPatchHeaderStatus_t header_status =
        DELTA_PATCH_HEADER_INVALID_ARGUMENT;
    uint32_t patch_crc = 0UL;
    uint32_t base_crc;

    if (!ReadDeltaHeader(header, &header_status))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D1000UL | (uint32_t)header_status;
        }
        return false;
    }

    if ((header->base_version != (uint32_t)APPLICATION_VERSION) ||
        (header->target_version != g_session.target_version))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D2000UL |
                      (header->base_version & 0xFFFUL);
        }
        return false;
    }

    if (!CalculateIncomingCrcRange(
            DELTA_PATCH_HEADER_SIZE,
            header->patch_size,
            &patch_crc))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D3001UL;
        }
        return false;
    }

    if (patch_crc != header->patch_crc32)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D3002UL;
        }
        return false;
    }

    base_crc = Crc32_Calculate(
        (const void *)APPLICATION_START_ADDRESS,
        header->base_image_size);

    if (base_crc != header->base_image_crc32)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D4001UL;
        }
        return false;
    }

    return true;
}

static bool PersistDeltaInstallRequest(
    const DeltaPatchHeader_t *header,
    uint32_t *detail)
{
    BootMetadata_t metadata;
    BootMetadata_t committed_metadata;
    MetadataStorageStatus_t status;

    status = MetadataStorage_Load(
        &metadata,
        (BootMetadataSlot_t *)0);
    if ((status != METADATA_STORAGE_OK) &&
        (status != METADATA_STORAGE_DEFAULTS_USED))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D5000UL | (uint32_t)status;
        }
        return false;
    }

    if (metadata.active_version != header->base_version)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D5008UL;
        }
        return false;
    }

    metadata.state = (uint32_t)UPDATE_ARTIFACT_READY;
    metadata.pending_version = header->target_version;
    metadata.active_update_id = g_session.update_id;
    metadata.received_size = g_session.expected_size;
    metadata.expected_size = g_session.expected_size;
    metadata.copy_offset = 0UL;
    metadata.boot_attempts = 0UL;
    metadata.last_error = 0UL;

    status = MetadataStorage_Commit(
        &metadata,
        &committed_metadata,
        (BootMetadataSlot_t *)0);
    if (status != METADATA_STORAGE_OK)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x000D5100UL | (uint32_t)status;
        }
        return false;
    }

    return true;
}
#endif

static bool PersistSecureInstallRequest(uint32_t *detail)
{
    BootMetadata_t metadata;
    BootMetadata_t committed_metadata;
    MetadataStorageStatus_t status;

    status = MetadataStorage_Load(
        &metadata,
        (BootMetadataSlot_t *)0);
    if ((status != METADATA_STORAGE_OK) &&
        (status != METADATA_STORAGE_DEFAULTS_USED))
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x00140100UL | (uint32_t)status;
        }
        return false;
    }

    metadata.state = (uint32_t)UPDATE_ARTIFACT_READY;
    metadata.pending_version = g_session.target_version;
    metadata.active_update_id = g_session.update_id;
    metadata.received_size = g_session.expected_size;
    metadata.expected_size = g_session.expected_size;
    metadata.copy_offset = 0UL;
    metadata.boot_attempts = 0UL;
    metadata.last_error = 0UL;

    status = MetadataStorage_Commit(
        &metadata,
        &committed_metadata,
        (BootMetadataSlot_t *)0);
    if (status != METADATA_STORAGE_OK)
    {
        if (detail != (uint32_t *)0)
        {
            *detail = 0x00140200UL | (uint32_t)status;
        }
        return false;
    }

    return true;
}

static void ProcessInstall(const OtaPacket_t *request, OtaPacket_t *response)
{
    uint32_t detail = 0UL;

    if (request->payload_length != 0U)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }
    if (g_session.state != (uint8_t)UPDATE_ARTIFACT_READY)
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }
    if (request->update_id != g_session.update_id)
    {
        Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
        return;
    }
    if (g_session.target_version == 0UL)
    {
        Nack(request, OTA_STATUS_VERSION_REJECTED, 0UL, response);
        return;
    }

    if (IncomingLooksLikeSecureContainer() != 0U)
    {
        /*
         * The application intentionally does not authenticate the container.
         * It only persists the transfer handoff. The bootloader is the trust
         * boundary and verifies SHA-256 + ECDSA before touching the active app.
         */
        if (!PersistSecureInstallRequest(&detail))
        {
            Nack(request, OTA_STATUS_INTERNAL_ERROR, detail, response);
            return;
        }
    }
#if SECURE_CONTAINER_ALLOW_UNSIGNED_LEGACY != 0
    else if (IncomingLooksLikeDelta() != 0U)
    {
        DeltaPatchHeader_t header;

        if (!ValidateDeltaInstallRequest(&header, &detail))
        {
            Nack(request, OTA_STATUS_BASE_MISMATCH, detail, response);
            return;
        }

        if (!PersistDeltaInstallRequest(&header, &detail))
        {
            Nack(request, OTA_STATUS_INTERNAL_ERROR, detail, response);
            return;
        }
    }
    else
    {
        if (g_session.expected_size > APPLICATION_MAX_SIZE)
        {
            Nack(request, OTA_STATUS_IMAGE_TOO_LARGE,
                 g_session.expected_size, response);
            return;
        }

        if (!ValidateIncomingApplication(&detail))
        {
            Nack(request, OTA_STATUS_CONTAINER_ERROR, detail, response);
            return;
        }

        if (!PersistFullInstallRequest(&detail))
        {
            Nack(request, OTA_STATUS_INTERNAL_ERROR, detail, response);
            return;
        }
    }
#else
    else
    {
        Nack(request, OTA_STATUS_SIGNATURE_ERROR,
             0x0014FFFFUL, response);
        return;
    }
#endif

    Ack(request, response);
    g_session.reset_pending = 1U;
}


static void ProcessConfirm(const OtaPacket_t *request, OtaPacket_t *response)
{
    uint32_t detail = 0UL;

    if (request->payload_length != 0U)
    {
        Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
        return;
    }

    if (g_session.state != (uint8_t)UPDATE_TRIAL_BOOT)
    {
        Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
        return;
    }

    if ((request->update_id != 0UL) &&
        (request->update_id != g_session.update_id))
    {
        Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
        return;
    }

    if (!TrialConfirmation_ConfirmNow(&detail))
    {
        Nack(request, OTA_STATUS_INTERNAL_ERROR, detail, response);
        return;
    }

    g_session.state = (uint8_t)UPDATE_CONFIRMED;
    Ack(request, response);
    g_session.reset_pending = 1U;
}

bool OtaReceiver_Init(void)
{
    DownloadCheckpointStorageStatus_t checkpoint_status =
        DOWNLOAD_CHECKPOINT_STORAGE_OK;

    g_session.storage_ready = ExternalFlashStorage_Init() ? 1U : 0U;
    g_session.last_status = (g_session.storage_ready != 0U)
                                ? (uint8_t)OTA_STATUS_OK
                                : (uint8_t)OTA_STATUS_STORAGE_ERROR;
    g_session.last_error_detail = (g_session.storage_ready != 0U)
                                      ? 0UL
                                      : (uint32_t)SpiFlash_GetLastStatus();

    ResetTransfer();

    if (g_session.storage_ready != 0U)
    {
        checkpoint_status = RestoreDownloadCheckpoint();
        if (checkpoint_status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
        {
            g_session.storage_ready = 0U;
            g_session.last_status = (uint8_t)OTA_STATUS_STORAGE_ERROR;
            g_session.last_error_detail =
                CheckpointErrorDetail(checkpoint_status);
            ResetTransfer();
        }
        else if (g_session.state == (uint8_t)UPDATE_IDLE)
        {
            MirrorPersistentTrialState();
        }
    }

    return g_session.storage_ready != 0U;
}

bool OtaReceiver_ShouldReset(void)
{
    return g_session.reset_pending != 0U;
}

void OtaReceiver_ProcessPacket(const OtaPacket_t *request,
                               OtaPacket_t *response)
{
    OtaResponseInfo_t info;

    if ((request == (const OtaPacket_t *)0) ||
        (response == (OtaPacket_t *)0))
    {
        return;
    }

    switch ((OtaCommand_t)request->command)
    {
        case OTA_CMD_HELLO:
        case OTA_CMD_QUERY:
        case OTA_CMD_STATUS:
            if (request->payload_length != 0U)
            {
                Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
                return;
            }
            SetStatus(OTA_STATUS_OK, 0UL);
            OtaReceiver_GetResponseInfo(&info);
            OtaResponse_BuildHelloQuery(request, &info, response);
            return;

        case OTA_CMD_START:
            ProcessStart(request, response);
            return;

        case OTA_CMD_DATA:
            ProcessData(request, response);
            return;

        case OTA_CMD_FINISH:
            ProcessFinish(request, response);
            return;

        case OTA_CMD_ABORT:
            if (request->payload_length != 0U)
            {
                Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
                return;
            }
            {
                const DownloadCheckpointStorageStatus_t checkpoint_status =
                    ClearDownloadCheckpoint();

                if (checkpoint_status != DOWNLOAD_CHECKPOINT_STORAGE_OK)
                {
                    Nack(request, OTA_STATUS_STORAGE_ERROR,
                         CheckpointErrorDetail(checkpoint_status), response);
                    return;
                }
            }
            ResetTransfer();
            Ack(request, response);
            return;

        case OTA_CMD_RESUME:
            if (request->payload_length != 0U)
            {
                Nack(request, OTA_STATUS_INVALID_PACKET, 0UL, response);
                return;
            }
            if (g_session.state != (uint8_t)UPDATE_RECEIVING)
            {
                Nack(request, OTA_STATUS_INVALID_STATE, 0UL, response);
                return;
            }
            if (request->update_id != g_session.update_id)
            {
                Nack(request, OTA_STATUS_UPDATE_ID_MISMATCH, 0UL, response);
                return;
            }
            Ack(request, response);
            return;

        case OTA_CMD_INSTALL:
            ProcessInstall(request, response);
            return;

        case OTA_CMD_CONFIRM:
            ProcessConfirm(request, response);
            return;

        case OTA_CMD_ACK:
        case OTA_CMD_NACK:
        default:
            Nack(request, OTA_STATUS_INVALID_PACKET,
                 request->command, response);
            return;
    }
}
