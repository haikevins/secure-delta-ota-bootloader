#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "boot_decision.h"
#include "boot_metadata.h"
#include "crc32.h"

static BootMetadata_t ValidIdle(uint32_t generation)
{
    BootMetadata_t metadata;
    BootMetadata_Init(&metadata, 1UL);
    metadata.generation = generation;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    return metadata;
}

static BootMetadata_t ValidReceiving(uint32_t generation)
{
    BootMetadata_t metadata;
    BootMetadata_Init(&metadata, 1UL);
    metadata.generation = generation;
    metadata.state = (uint32_t)UPDATE_RECEIVING;
    metadata.pending_version = 2UL;
    metadata.active_update_id = 0x12345678UL;
    metadata.received_size = 1024UL;
    metadata.expected_size = 4096UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    return metadata;
}

static void TestCrcVector(void)
{
    static const char vector[] = "123456789";
    assert(Crc32_Calculate(vector, 9UL) == 0xCBF43926UL);
}

static void TestValidationAndCorruption(void)
{
    BootMetadata_t metadata = ValidIdle(1UL);
    metadata.active_version ^= 1UL;
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_INVALID_CRC);

    metadata = ValidIdle(1UL);
    metadata.state = 99UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_INVALID_STATE);

    metadata = ValidReceiving(2UL);
    metadata.received_size = metadata.expected_size + 1UL;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) ==
           BOOT_METADATA_INVALID_PROGRESS);
}

static void TestRedundantSelection(void)
{
    BootMetadata_t slot_a = ValidIdle(10UL);
    BootMetadata_t slot_b = ValidIdle(11UL);

    assert(BootMetadata_SelectNewestSlot(&slot_a, &slot_b) ==
           BOOT_METADATA_SLOT_B);
    assert(BootMetadata_SelectWriteSlot(&slot_a, &slot_b) ==
           BOOT_METADATA_SLOT_A);

    slot_b.crc32 ^= 1UL;
    assert(BootMetadata_SelectNewestSlot(&slot_a, &slot_b) ==
           BOOT_METADATA_SLOT_A);
    assert(BootMetadata_SelectWriteSlot(&slot_a, &slot_b) ==
           BOOT_METADATA_SLOT_B);

    slot_a.crc32 ^= 1UL;
    assert(BootMetadata_SelectNewestSlot(&slot_a, &slot_b) ==
           BOOT_METADATA_SLOT_NONE);
    assert(BootMetadata_SelectWriteSlot(&slot_a, &slot_b) ==
           BOOT_METADATA_SLOT_A);
}

static void TestGenerationWrap(void)
{
    BootMetadata_t old_slot = ValidIdle(0xFFFFFFFFUL);
    BootMetadata_t new_slot = ValidIdle(1UL);

    assert(BootMetadata_NextGeneration(0xFFFFFFFFUL) == 1UL);
    assert(BootMetadata_IsGenerationNewer(1UL, 0xFFFFFFFFUL) != 0U);
    assert(BootMetadata_SelectNewestSlot(&old_slot, &new_slot) ==
           BOOT_METADATA_SLOT_B);
}


static void TestPartialWriteKeepsPreviousGeneration(void)
{
    union
    {
        BootMetadata_t metadata;
        uint8_t bytes[sizeof(BootMetadata_t)];
    } partial;
    BootMetadata_t active = ValidIdle(20UL);
    BootMetadata_t candidate = ValidIdle(21UL);
    const uint8_t *candidate_bytes = (const uint8_t *)&candidate;
    uint32_t programmed;

    for (programmed = 0UL; programmed < sizeof(BootMetadata_t); ++programmed)
    {
        uint32_t index;
        for (index = 0UL; index < sizeof(BootMetadata_t); ++index)
        {
            partial.bytes[index] = 0xFFU;
        }
        for (index = 0UL; index < programmed; ++index)
        {
            partial.bytes[index] = candidate_bytes[index];
        }

        assert(BootMetadata_SelectNewestSlot(&active, &partial.metadata) ==
               BOOT_METADATA_SLOT_A);
    }

    partial.metadata = candidate;
    assert(BootMetadata_SelectNewestSlot(&active, &partial.metadata) ==
           BOOT_METADATA_SLOT_B);
}

static void TestBootDecision(void)
{
    BootMetadata_t metadata = ValidIdle(1UL);
    BootDecision_t decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_JUMP_ACTIVE);

    decision = BootDecision_Evaluate(&metadata, 0U);
    assert(decision.action == BOOT_ACTION_STAY_RECOVERY);
    assert(decision.reason == BOOT_DECISION_REASON_APPLICATION_INVALID);

    metadata = ValidReceiving(2UL);
    decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_RESUME_DOWNLOAD);

    metadata.state = (uint32_t)UPDATE_TRIAL_BOOT;
    metadata.received_size = 0UL;
    metadata.expected_size = 0UL;
    metadata.boot_attempts = BOOT_METADATA_MAX_BOOT_ATTEMPTS;
    BootMetadata_Finalize(&metadata);
    assert(BootMetadata_Validate(&metadata) == BOOT_METADATA_VALID);
    decision = BootDecision_Evaluate(&metadata, 1U);
    assert(decision.action == BOOT_ACTION_RESUME_ROLLBACK);
    assert(decision.reason == BOOT_DECISION_REASON_TRIAL_LIMIT_REACHED);
}

int main(void)
{
    TestCrcVector();
    TestValidationAndCorruption();
    TestRedundantSelection();
    TestGenerationWrap();
    TestPartialWriteKeepsPreviousGeneration();
    TestBootDecision();
    puts("Phase 3 metadata host tests: PASS");
    return 0;
}
