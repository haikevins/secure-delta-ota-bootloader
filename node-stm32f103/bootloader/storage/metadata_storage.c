#include "metadata_storage.h"

#include <stdint.h>

#include "firmware_version.h"
#include "memory_map.h"
#include "stm32f10x.h"
#include "stm32f10x_flash.h"

_Static_assert((sizeof(BootMetadata_t) % 2U) == 0U,
               "metadata record must be half-word programmable");
_Static_assert(sizeof(BootMetadata_t) <= BOOT_METADATA_SLOT_SIZE,
               "metadata record exceeds one internal Flash page");

static uint32_t MetadataStorage_SlotAddress(BootMetadataSlot_t slot)
{
    if (slot == BOOT_METADATA_SLOT_A)
    {
        return BOOT_METADATA_A_ADDRESS;
    }

    if (slot == BOOT_METADATA_SLOT_B)
    {
        return BOOT_METADATA_B_ADDRESS;
    }

    return 0UL;
}

static void MetadataStorage_CopyFromFlash(uint32_t address,
                                          BootMetadata_t *metadata)
{
    volatile const uint8_t *source = (volatile const uint8_t *)address;
    uint8_t *destination = (uint8_t *)metadata;
    uint32_t index;

    for (index = 0UL; index < sizeof(*metadata); ++index)
    {
        destination[index] = source[index];
    }
}

static uint8_t MetadataStorage_RecordsEqual(const BootMetadata_t *left,
                                            const BootMetadata_t *right)
{
    const uint8_t *left_bytes = (const uint8_t *)left;
    const uint8_t *right_bytes = (const uint8_t *)right;
    uint32_t index;

    for (index = 0UL; index < sizeof(*left); ++index)
    {
        if (left_bytes[index] != right_bytes[index])
        {
            return 0U;
        }
    }

    return 1U;
}

static uint32_t MetadataStorage_GetPrimask(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    return primask;
}

static void MetadataStorage_RestorePrimask(uint32_t primask)
{
    __asm volatile ("msr primask, %0" : : "r" (primask) : "memory");
}

BootMetadataValidationStatus_t MetadataStorage_ReadSlot(
    BootMetadataSlot_t slot,
    BootMetadata_t *metadata)
{
    const uint32_t address = MetadataStorage_SlotAddress(slot);

    if ((metadata == (BootMetadata_t *)0) || (address == 0UL))
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    MetadataStorage_CopyFromFlash(address, metadata);
    return BootMetadata_Validate(metadata);
}

MetadataStorageStatus_t MetadataStorage_Load(BootMetadata_t *metadata,
                                             BootMetadataSlot_t *active_slot)
{
    BootMetadata_t slot_a;
    BootMetadata_t slot_b;
    BootMetadataSlot_t newest;

    if (metadata == (BootMetadata_t *)0)
    {
        return METADATA_STORAGE_INVALID_ARGUMENT;
    }

    MetadataStorage_CopyFromFlash(BOOT_METADATA_A_ADDRESS, &slot_a);
    MetadataStorage_CopyFromFlash(BOOT_METADATA_B_ADDRESS, &slot_b);
    newest = BootMetadata_SelectNewestSlot(&slot_a, &slot_b);

    if (newest == BOOT_METADATA_SLOT_A)
    {
        *metadata = slot_a;
    }
    else if (newest == BOOT_METADATA_SLOT_B)
    {
        *metadata = slot_b;
    }
    else
    {
        BootMetadata_Init(metadata, APPLICATION_VERSION);
        if (active_slot != (BootMetadataSlot_t *)0)
        {
            *active_slot = BOOT_METADATA_SLOT_NONE;
        }
        return METADATA_STORAGE_DEFAULTS_USED;
    }

    if (active_slot != (BootMetadataSlot_t *)0)
    {
        *active_slot = newest;
    }

    return METADATA_STORAGE_OK;
}

static MetadataStorageStatus_t MetadataStorage_ProgramSlot(
    BootMetadataSlot_t slot,
    const BootMetadata_t *metadata)
{
    const uint32_t address = MetadataStorage_SlotAddress(slot);
    const uint8_t *bytes = (const uint8_t *)metadata;
    uint32_t offset;
    FLASH_Status flash_status;
    uint32_t primask;

    if ((address == 0UL) ||
        ((address != BOOT_METADATA_A_ADDRESS) &&
         (address != BOOT_METADATA_B_ADDRESS)))
    {
        return METADATA_STORAGE_INVALID_ARGUMENT;
    }

    primask = MetadataStorage_GetPrimask();
    __disable_irq();

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);

    flash_status = FLASH_ErasePage(address);
    if (flash_status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        MetadataStorage_RestorePrimask(primask);
        return METADATA_STORAGE_ERASE_FAILED;
    }

    /* The CRC field is the last word, so a reset during programming leaves an
     * invalid record. The previously selected slot remains untouched. */
    for (offset = 0UL; offset < sizeof(*metadata); offset += 2UL)
    {
        const uint16_t value = (uint16_t)((uint16_t)bytes[offset] |
                                          ((uint16_t)bytes[offset + 1UL] << 8U));

        flash_status = FLASH_ProgramHalfWord(address + offset, value);
        if (flash_status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            if (primask == 0UL)
            {
                __enable_irq();
            }
            return METADATA_STORAGE_PROGRAM_FAILED;
        }
    }

    FLASH_Lock();
    MetadataStorage_RestorePrimask(primask);

    return METADATA_STORAGE_OK;
}

MetadataStorageStatus_t MetadataStorage_Commit(
    const BootMetadata_t *requested,
    BootMetadata_t *committed,
    BootMetadataSlot_t *written_slot)
{
    BootMetadata_t slot_a;
    BootMetadata_t slot_b;
    BootMetadata_t candidate;
    BootMetadata_t verification;
    BootMetadataSlot_t current_slot;
    BootMetadataSlot_t target_slot;
    uint32_t current_generation = 0UL;
    MetadataStorageStatus_t status;

    if (requested == (const BootMetadata_t *)0)
    {
        return METADATA_STORAGE_INVALID_ARGUMENT;
    }

    MetadataStorage_CopyFromFlash(BOOT_METADATA_A_ADDRESS, &slot_a);
    MetadataStorage_CopyFromFlash(BOOT_METADATA_B_ADDRESS, &slot_b);
    current_slot = BootMetadata_SelectNewestSlot(&slot_a, &slot_b);
    target_slot = BootMetadata_SelectWriteSlot(&slot_a, &slot_b);

    if (current_slot == BOOT_METADATA_SLOT_A)
    {
        current_generation = slot_a.generation;
    }
    else if (current_slot == BOOT_METADATA_SLOT_B)
    {
        current_generation = slot_b.generation;
    }

    candidate = *requested;
    candidate.generation = BootMetadata_NextGeneration(current_generation);
    BootMetadata_Finalize(&candidate);

    if (BootMetadata_Validate(&candidate) != BOOT_METADATA_VALID)
    {
        return METADATA_STORAGE_INVALID_ARGUMENT;
    }

    status = MetadataStorage_ProgramSlot(target_slot, &candidate);
    if (status != METADATA_STORAGE_OK)
    {
        return status;
    }

    MetadataStorage_CopyFromFlash(MetadataStorage_SlotAddress(target_slot),
                                  &verification);
    if ((BootMetadata_Validate(&verification) != BOOT_METADATA_VALID) ||
        (MetadataStorage_RecordsEqual(&candidate, &verification) == 0U))
    {
        return METADATA_STORAGE_VERIFY_FAILED;
    }

    if (committed != (BootMetadata_t *)0)
    {
        *committed = verification;
    }

    if (written_slot != (BootMetadataSlot_t *)0)
    {
        *written_slot = target_slot;
    }

    return METADATA_STORAGE_OK;
}
