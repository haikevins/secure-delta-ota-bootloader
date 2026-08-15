#ifndef UPDATE_HANDOFF_STORAGE_H
#define UPDATE_HANDOFF_STORAGE_H

#include "update_handoff.h"

typedef enum
{
    UPDATE_HANDOFF_STORAGE_OK = 0,
    UPDATE_HANDOFF_STORAGE_NOT_FOUND,
    UPDATE_HANDOFF_STORAGE_INVALID_ARGUMENT,
    UPDATE_HANDOFF_STORAGE_READ_FAILED,
    UPDATE_HANDOFF_STORAGE_ERASE_FAILED,
    UPDATE_HANDOFF_STORAGE_WRITE_FAILED,
    UPDATE_HANDOFF_STORAGE_VERIFY_FAILED
} UpdateHandoffStorageStatus_t;

UpdateHandoffStorageStatus_t UpdateHandoffStorage_Load(
    UpdateHandoffRecord_t *record,
    UpdateHandoffSlot_t *slot);

UpdateHandoffStorageStatus_t UpdateHandoffStorage_Commit(
    const UpdateHandoffRecord_t *requested,
    UpdateHandoffRecord_t *committed,
    UpdateHandoffSlot_t *written_slot);

#endif
