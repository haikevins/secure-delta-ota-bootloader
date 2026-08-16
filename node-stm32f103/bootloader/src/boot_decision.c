#include "boot_decision.h"

BootDecision_t BootDecision_Evaluate(const BootMetadata_t *metadata,
                                     uint8_t application_valid)
{
    BootDecision_t decision;

    decision.action = BOOT_ACTION_STAY_RECOVERY;
    decision.reason = BOOT_DECISION_REASON_NONE;

    if (BootMetadata_Validate(metadata) != BOOT_METADATA_VALID)
    {
        decision.reason = BOOT_DECISION_REASON_METADATA_INVALID;
        return decision;
    }

    switch ((UpdateState_t)metadata->state)
    {
        case UPDATE_IDLE:
            decision.action = BOOT_ACTION_JUMP_ACTIVE;
            break;

        case UPDATE_RECEIVING:
            decision.action = BOOT_ACTION_RESUME_DOWNLOAD;
            break;

        case UPDATE_ARTIFACT_READY:
            decision.action = BOOT_ACTION_PROCESS_ARTIFACT;
            break;

        case UPDATE_VERIFYING_CONTAINER:
        case UPDATE_VERIFYING_BASE:
            decision.action = BOOT_ACTION_RESTART_VALIDATION;
            break;

        case UPDATE_PATCHING:
            decision.action = BOOT_ACTION_RESTART_PATCH;
            break;

        case UPDATE_IMAGE_READY:
        case UPDATE_BACKING_UP:
            decision.action = BOOT_ACTION_CONTINUE_BACKUP;
            break;

        case UPDATE_INSTALLING:
            decision.action = BOOT_ACTION_RESUME_INSTALL;
            break;

        case UPDATE_VERIFYING_INSTALL:
            decision.action = BOOT_ACTION_VERIFY_INSTALL;
            break;

        case UPDATE_TRIAL_BOOT:
            if (metadata->boot_attempts >= BOOT_METADATA_MAX_BOOT_ATTEMPTS)
            {
                decision.action = BOOT_ACTION_RESUME_ROLLBACK;
                decision.reason = BOOT_DECISION_REASON_TRIAL_LIMIT_REACHED;
            }
            else
            {
                decision.action = BOOT_ACTION_BOOT_TRIAL;
            }
            break;

        case UPDATE_CONFIRMED:
            decision.action = BOOT_ACTION_FINALIZE_CONFIRMATION;
            break;

        case UPDATE_ROLLBACK:
            decision.action = BOOT_ACTION_RESUME_ROLLBACK;
            break;

        case UPDATE_FAILED:
        default:
            decision.action = BOOT_ACTION_STAY_RECOVERY;
            decision.reason = BOOT_DECISION_REASON_FAILED_STATE;
            break;
    }

    if ((application_valid == 0U) &&
        (decision.action == BOOT_ACTION_BOOT_TRIAL))
    {
        /*
         * A trial image with invalid vectors is never allowed to strand the
         * product. trial boot and rollback restores the validated external backup instead.
         */
        decision.action = BOOT_ACTION_RESUME_ROLLBACK;
        decision.reason = BOOT_DECISION_REASON_APPLICATION_INVALID;
    }
    else if ((application_valid == 0U) &&
             ((decision.action == BOOT_ACTION_JUMP_ACTIVE) ||
              (decision.action == BOOT_ACTION_RESUME_DOWNLOAD)))
    {
        decision.action = BOOT_ACTION_STAY_RECOVERY;
        decision.reason = BOOT_DECISION_REASON_APPLICATION_INVALID;
    }

    return decision;
}
