#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "boot_decision.h"
#include "download_checkpoint.h"
#include "boot_metadata.h"
#include "install_progress.h"
#include "memory_map.h"

#define TEST_IMAGE_SIZE 3500UL

static BootMetadata_t InstallMetadata(uint32_t state, uint32_t copy_offset)
{
    BootMetadata_t metadata;

    BootMetadata_Init(&metadata, 1UL);
    metadata.generation = 7UL;
    metadata.state = state;
    metadata.pending_version = 2UL;
    metadata.active_update_id = 0x70070001UL;
    metadata.received_size = TEST_IMAGE_SIZE;
    metadata.expected_size = TEST_IMAGE_SIZE;
    metadata.copy_offset = copy_offset;
    BootMetadata_Finalize(&metadata);

    return metadata;
}


static DownloadCheckpointRecord_t Checkpoint(
    DownloadCheckpointState_t state,
    uint32_t generation,
    uint32_t next_offset)
{
    DownloadCheckpointRecord_t record;

    if (state == DOWNLOAD_CHECKPOINT_IDLE)
    {
        DownloadCheckpoint_InitIdle(&record);
    }
    else
    {
        DownloadCheckpoint_InitSession(&record,
                                       state,
                                       0x70070001UL,
                                       2UL,
                                       10000UL,
                                       0x12345678UL,
                                       next_offset);
    }

    record.generation = generation;
    DownloadCheckpoint_Finalize(&record);
    assert(DownloadCheckpoint_Validate(&record) ==
           DOWNLOAD_CHECKPOINT_VALID);
    return record;
}

static void TestDownloadCheckpointRules(void)
{
    DownloadCheckpointRecord_t a =
        Checkpoint(DOWNLOAD_CHECKPOINT_RECEIVING, 4UL, 4096UL);
    DownloadCheckpointRecord_t b =
        Checkpoint(DOWNLOAD_CHECKPOINT_RECEIVING, 5UL, 8192UL);

    assert(sizeof(DownloadCheckpointRecord_t) == 40U);
    assert(DownloadCheckpoint_SelectNewest(&a, &b) ==
           DOWNLOAD_CHECKPOINT_SLOT_B);
    assert(DownloadCheckpoint_SelectWriteSlot(&a, &b) ==
           DOWNLOAD_CHECKPOINT_SLOT_A);

    b.next_offset = 4608UL;
    DownloadCheckpoint_Finalize(&b);
    assert(DownloadCheckpoint_Validate(&b) ==
           DOWNLOAD_CHECKPOINT_INVALID_FIELDS);

    b = Checkpoint(DOWNLOAD_CHECKPOINT_ARTIFACT_READY, 5UL, 10000UL);
    assert(DownloadCheckpoint_Validate(&b) == DOWNLOAD_CHECKPOINT_VALID);

    b.next_offset = 8192UL;
    DownloadCheckpoint_Finalize(&b);
    assert(DownloadCheckpoint_Validate(&b) ==
           DOWNLOAD_CHECKPOINT_INVALID_FIELDS);

    a = Checkpoint(DOWNLOAD_CHECKPOINT_RECEIVING, 0xFFFFFFFFUL, 4096UL);
    b = Checkpoint(DOWNLOAD_CHECKPOINT_RECEIVING, 1UL, 8192UL);
    assert(DownloadCheckpoint_IsGenerationNewer(1UL, 0xFFFFFFFFUL) != 0U);
    assert(DownloadCheckpoint_SelectNewest(&a, &b) ==
           DOWNLOAD_CHECKPOINT_SLOT_B);
}

static void TestProgressGeometry(void)
{
    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, 0UL) ==
           INSTALL_PROGRESS_VALID);
    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, 1024UL) ==
           INSTALL_PROGRESS_VALID);
    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, 2048UL) ==
           INSTALL_PROGRESS_VALID);
    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, 3072UL) ==
           INSTALL_PROGRESS_VALID);
    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, TEST_IMAGE_SIZE) ==
           INSTALL_PROGRESS_VALID);

    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, 512UL) ==
           INSTALL_PROGRESS_INVALID_ALIGNMENT);
    assert(InstallProgress_Validate(TEST_IMAGE_SIZE, TEST_IMAGE_SIZE + 1UL) ==
           INSTALL_PROGRESS_INVALID_OFFSET);
    assert(InstallProgress_Validate(7UL, 0UL) ==
           INSTALL_PROGRESS_INVALID_IMAGE_SIZE);
    assert(InstallProgress_Validate(APPLICATION_MAX_SIZE + 1UL, 0UL) ==
           INSTALL_PROGRESS_INVALID_IMAGE_SIZE);

    assert(InstallProgress_PageLength(TEST_IMAGE_SIZE, 0UL) == 1024UL);
    assert(InstallProgress_PageLength(TEST_IMAGE_SIZE, 3072UL) ==
           (TEST_IMAGE_SIZE - 3072UL));
    assert(InstallProgress_PageLength(TEST_IMAGE_SIZE, TEST_IMAGE_SIZE) == 0UL);

    assert(InstallProgress_NextCheckpoint(TEST_IMAGE_SIZE, 0UL) == 1024UL);
    assert(InstallProgress_NextCheckpoint(TEST_IMAGE_SIZE, 3072UL) ==
           TEST_IMAGE_SIZE);
}

static void TestMetadataCheckpointRules(void)
{
    BootMetadata_t metadata;

    metadata = InstallMetadata(UPDATE_ARTIFACT_READY, 0UL);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    metadata = InstallMetadata(UPDATE_ARTIFACT_READY, 1024UL);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_STATE_FIELDS);

    metadata = InstallMetadata(UPDATE_INSTALLING, 0UL);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    metadata = InstallMetadata(UPDATE_INSTALLING, 1024UL);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    metadata = InstallMetadata(UPDATE_INSTALLING, 512UL);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_STATE_FIELDS);

    metadata = InstallMetadata(UPDATE_INSTALLING, TEST_IMAGE_SIZE);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    metadata = InstallMetadata(UPDATE_INSTALLING, TEST_IMAGE_SIZE + 1UL);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_STATE_FIELDS);

    metadata = InstallMetadata(UPDATE_VERIFYING_INSTALL, TEST_IMAGE_SIZE);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);

    metadata = InstallMetadata(UPDATE_VERIFYING_INSTALL, 3072UL);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_STATE_FIELDS);
}

static void TestBootDecisionResumesWithBrokenApplication(void)
{
    BootMetadata_t metadata = InstallMetadata(UPDATE_INSTALLING, 1024UL);
    BootDecision_t decision = BootDecision_Evaluate(&metadata, 0U);

    /*
     * A partial internal image is expected after power loss. The bootloader
     * must resume install instead of rejecting the state because app vectors
     * are temporarily invalid.
     */
    assert(decision.action == BOOT_ACTION_RESUME_INSTALL);

    metadata = InstallMetadata(UPDATE_VERIFYING_INSTALL, TEST_IMAGE_SIZE);
    decision = BootDecision_Evaluate(&metadata, 0U);
    assert(decision.action == BOOT_ACTION_VERIFY_INSTALL);
}

static void TestCheckpointGenerationWrapStillPortable(void)
{
    assert(BootMetadata_IsGenerationNewer(1UL, 0xFFFFFFFFUL) != 0U);
    assert(BootMetadata_IsGenerationNewer(0xFFFFFFFFUL, 1UL) == 0U);
}

int main(void)
{
    TestDownloadCheckpointRules();
    TestProgressGeometry();
    TestMetadataCheckpointRules();
    TestBootDecisionResumesWithBrokenApplication();
    TestCheckpointGenerationWrapStillPortable();

    puts("power-loss recovery recovery host C tests: PASS");
    return 0;
}
