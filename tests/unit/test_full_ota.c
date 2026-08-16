#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "boot_decision.h"
#include "boot_metadata.h"
#include "full_image_validation.h"
#include "memory_map.h"
#include "update_handoff.h"

static BootMetadata_t ValidInstallMetadata(uint32_t state)
{
    BootMetadata_t metadata;
    BootMetadata_Init(&metadata, 1UL);
    metadata.generation = 4UL;
    metadata.state = state;
    metadata.pending_version = 2UL;
    metadata.active_update_id = 0x60060001UL;
    metadata.received_size = 10000UL;
    metadata.expected_size = 10000UL;
    if (state == (uint32_t)UPDATE_VERIFYING_INSTALL)
    {
        metadata.copy_offset = 10000UL;
    }
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    return metadata;
}

static void TestVectorValidation(void)
{
    const uint32_t msp = INTERNAL_SRAM_END;
    const uint32_t reset = APPLICATION_START_ADDRESS + 0x101UL;

    assert(FullImage_ValidateVector(4096UL, msp, reset) == FULL_IMAGE_VALID);
    assert(FullImage_ValidateVector(7UL, msp, reset) == FULL_IMAGE_TOO_SMALL);
    assert(FullImage_ValidateVector(APPLICATION_MAX_SIZE + 1UL, msp, reset) ==
           FULL_IMAGE_TOO_LARGE);
    assert(FullImage_ValidateVector(4096UL, INTERNAL_SRAM_BASE - 8UL, reset) ==
           FULL_IMAGE_BAD_STACK_RANGE);
    assert(FullImage_ValidateVector(4096UL, msp - 4UL, reset) ==
           FULL_IMAGE_BAD_STACK_ALIGNMENT);
    assert(FullImage_ValidateVector(4096UL, msp, reset & ~1UL) ==
           FULL_IMAGE_BAD_RESET_THUMB_BIT);
    assert(FullImage_ValidateVector(128UL, msp, reset) ==
           FULL_IMAGE_BAD_RESET_RANGE);
}

static UpdateHandoffRecord_t ValidRecord(uint32_t generation,
                                         uint32_t target_version)
{
    UpdateHandoffRecord_t record;
    UpdateHandoff_Init(&record, 0x60060001UL, target_version,
                       10000UL, 0x12345678UL);
    record.generation = generation;
    UpdateHandoff_Finalize(&record);
    assert(UpdateHandoff_Validate(&record) == UPDATE_HANDOFF_VALID);
    return record;
}

static void TestHandoffRecord(void)
{
    UpdateHandoffRecord_t a = ValidRecord(10UL, 2UL);
    UpdateHandoffRecord_t b = ValidRecord(11UL, 3UL);

    assert(sizeof(UpdateHandoffRecord_t) == 36U);
    assert(UpdateHandoff_SelectNewest(&a, &b) == UPDATE_HANDOFF_SLOT_B);
    assert(UpdateHandoff_SelectWriteSlot(&a, &b) == UPDATE_HANDOFF_SLOT_A);

    b.crc32 ^= 1UL;
    assert(UpdateHandoff_Validate(&b) == UPDATE_HANDOFF_INVALID_CRC);
    assert(UpdateHandoff_SelectNewest(&a, &b) == UPDATE_HANDOFF_SLOT_A);

    a = ValidRecord(0xFFFFFFFFUL, 2UL);
    b = ValidRecord(1UL, 3UL);
    assert(UpdateHandoff_IsGenerationNewer(1UL, 0xFFFFFFFFUL) != 0U);
    assert(UpdateHandoff_SelectNewest(&a, &b) == UPDATE_HANDOFF_SLOT_B);
}

static void TestBootDecisionInstallStates(void)
{
    BootMetadata_t metadata = ValidInstallMetadata(UPDATE_ARTIFACT_READY);
    BootDecision_t decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_PROCESS_ARTIFACT);

    metadata = ValidInstallMetadata(UPDATE_INSTALLING);
    decision = BootDecision_Evaluate(&metadata, 0U);
    assert(decision.action == BOOT_ACTION_RESUME_INSTALL);

    metadata = ValidInstallMetadata(UPDATE_VERIFYING_INSTALL);
    decision = BootDecision_Evaluate(&metadata, 0U);
    assert(decision.action == BOOT_ACTION_VERIFY_INSTALL);
}

static void TestBootMetadataGenerationPortableSerialArithmetic(void)
{
    assert(BootMetadata_IsGenerationNewer(1UL, 0xFFFFFFFFUL) != 0U);
    assert(BootMetadata_IsGenerationNewer(0xFFFFFFFFUL, 1UL) == 0U);
    assert(BootMetadata_IsGenerationNewer(7UL, 7UL) == 0U);
}

int main(void)
{
    TestVectorValidation();
    TestHandoffRecord();
    TestBootDecisionInstallStates();
    TestBootMetadataGenerationPortableSerialArithmetic();
    puts("full-image OTA full OTA host tests: PASS");
    return 0;
}
