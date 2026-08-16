#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "backup_image.h"
#include "backup_progress.h"
#include "boot_decision.h"
#include "boot_metadata.h"
#include "memory_map.h"

static BootMetadata_t TrialMetadata(uint32_t state, uint32_t attempts)
{
    BootMetadata_t metadata;

    BootMetadata_Init(&metadata, 1UL);
    metadata.generation = 10UL;
    metadata.state = state;
    metadata.pending_version = 2UL;
    metadata.active_update_id = 0x80080001UL;
    metadata.received_size = 10000UL;
    metadata.expected_size = 10000UL;
    metadata.copy_offset = 0UL;
    metadata.boot_attempts = attempts;
    BootMetadata_Finalize(&metadata);
    return metadata;
}

static void TestBackupRecord(void)
{
    BackupImageRecord_t record;

    BackupImage_Init(&record, 2UL, 0x12345678UL);
    BackupImage_Finalize(&record);

    assert(sizeof(BackupImageRecord_t) == 28U);
    assert(record.image_size == APPLICATION_MAX_SIZE);
    assert(record.load_address == APPLICATION_START_ADDRESS);
    assert(BackupImage_Validate(&record) == BACKUP_IMAGE_VALID);

    record.image_crc32 ^= 1UL;
    assert(BackupImage_Validate(&record) == BACKUP_IMAGE_INVALID_CRC);
}

static void TestBackupProgress(void)
{
    assert(BackupProgress_Validate(0UL) == BACKUP_PROGRESS_VALID);
    assert(BackupProgress_Validate(4096UL) == BACKUP_PROGRESS_VALID);
    assert(BackupProgress_Validate(36UL * 1024UL) == BACKUP_PROGRESS_VALID);
    assert(BackupProgress_Validate(APPLICATION_MAX_SIZE) ==
           BACKUP_PROGRESS_VALID);

    assert(BackupProgress_Validate(1024UL) ==
           BACKUP_PROGRESS_INVALID_ALIGNMENT);
    assert(BackupProgress_Validate(APPLICATION_MAX_SIZE + 1UL) ==
           BACKUP_PROGRESS_INVALID_OFFSET);

    assert(BackupProgress_ChunkLength(0UL) == 4096UL);
    assert(BackupProgress_ChunkLength(36UL * 1024UL) == 2048UL);
    assert(BackupProgress_NextCheckpoint(36UL * 1024UL) ==
           APPLICATION_MAX_SIZE);
}

static void TestTrialMetadataAndDecision(void)
{
    BootMetadata_t metadata;
    BootDecision_t decision;

    metadata = TrialMetadata(UPDATE_TRIAL_BOOT, 0UL);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_BOOT_TRIAL);

    metadata = TrialMetadata(UPDATE_TRIAL_BOOT, 2UL);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_BOOT_TRIAL);

    metadata = TrialMetadata(UPDATE_TRIAL_BOOT,
                             BOOT_METADATA_MAX_BOOT_ATTEMPTS);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_RESUME_ROLLBACK);
    assert(decision.reason == BOOT_DECISION_REASON_TRIAL_LIMIT_REACHED);

    metadata = TrialMetadata(UPDATE_TRIAL_BOOT, 1UL);
    decision = BootDecision_Evaluate(&metadata, 0U);
    assert(decision.action == BOOT_ACTION_RESUME_ROLLBACK);
    assert(decision.reason == BOOT_DECISION_REASON_APPLICATION_INVALID);
}

static void TestConfirmedDecision(void)
{
    BootMetadata_t metadata = TrialMetadata(UPDATE_CONFIRMED, 1UL);
    BootDecision_t decision;

    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_FINALIZE_CONFIRMATION);
}

static void TestRollbackMetadata(void)
{
    BootMetadata_t metadata = TrialMetadata(UPDATE_ROLLBACK, 3UL);
    BootDecision_t decision;

    metadata.copy_offset = 1024UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    decision = BootDecision_Evaluate(&metadata, 0U);
    assert(decision.action == BOOT_ACTION_RESUME_ROLLBACK);

    metadata.copy_offset = 512UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_STATE_FIELDS);
}

static void TestBackupMetadataState(void)
{
    BootMetadata_t metadata = TrialMetadata(UPDATE_BACKING_UP, 0UL);

    metadata.copy_offset = 4096UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    metadata.copy_offset = 1024UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_STATE_FIELDS);

    metadata.copy_offset = APPLICATION_MAX_SIZE;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
}

int main(void)
{
    TestBackupRecord();
    TestBackupProgress();
    TestTrialMetadataAndDecision();
    TestConfirmedDecision();
    TestRollbackMetadata();
    TestBackupMetadataState();

    puts("trial boot and rollback trial/rollback host C tests: PASS");
    return 0;
}
