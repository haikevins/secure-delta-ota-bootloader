#ifndef TRIAL_BOOT_H
#define TRIAL_BOOT_H

#include "boot_metadata.h"

typedef enum
{
    TRIAL_BOOT_STATUS_OK = 0,
    TRIAL_BOOT_STATUS_INVALID_STATE,
    TRIAL_BOOT_STATUS_ATTEMPT_LIMIT,
    TRIAL_BOOT_STATUS_METADATA_COMMIT_FAILED
} TrialBootStatus_t;

TrialBootStatus_t TrialBoot_PrepareAttempt(
    const BootMetadata_t *metadata,
    BootMetadata_t *committed_metadata);

TrialBootStatus_t TrialBoot_FinalizeConfirmation(
    const BootMetadata_t *metadata,
    BootMetadata_t *committed_metadata);

void TrialBoot_StartWatchdog(void);

#endif
