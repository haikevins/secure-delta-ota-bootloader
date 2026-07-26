#ifndef BOOT_DECISION_H
#define BOOT_DECISION_H

#include <stdint.h>

#include "boot_metadata.h"

typedef enum
{
    BOOT_ACTION_JUMP_ACTIVE = 0,
    BOOT_ACTION_RESUME_DOWNLOAD,
    BOOT_ACTION_PROCESS_ARTIFACT,
    BOOT_ACTION_RESTART_VALIDATION,
    BOOT_ACTION_RESTART_PATCH,
    BOOT_ACTION_CONTINUE_BACKUP,
    BOOT_ACTION_RESUME_INSTALL,
    BOOT_ACTION_VERIFY_INSTALL,
    BOOT_ACTION_BOOT_TRIAL,
    BOOT_ACTION_FINALIZE_CONFIRMATION,
    BOOT_ACTION_RESUME_ROLLBACK,
    BOOT_ACTION_STAY_RECOVERY
} BootAction_t;

typedef enum
{
    BOOT_DECISION_REASON_NONE = 0,
    BOOT_DECISION_REASON_APPLICATION_INVALID,
    BOOT_DECISION_REASON_METADATA_INVALID,
    BOOT_DECISION_REASON_TRIAL_LIMIT_REACHED,
    BOOT_DECISION_REASON_FAILED_STATE
} BootDecisionReason_t;

typedef struct
{
    BootAction_t action;
    BootDecisionReason_t reason;
} BootDecision_t;

BootDecision_t BootDecision_Evaluate(const BootMetadata_t *metadata,
                                     uint8_t application_valid);

#endif
