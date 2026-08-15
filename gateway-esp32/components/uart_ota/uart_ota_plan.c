#include "uart_ota_plan.h"

UartOtaPlan_t UartOta_SelectPlan(const UartOtaHelloInfo_t *target,
                                 uint32_t artifact_update_id,
                                 uint32_t artifact_size,
                                 uint32_t target_version)
{
    if ((target == (const UartOtaHelloInfo_t *)0) ||
        (artifact_update_id == 0UL) ||
        (artifact_size == 0UL) ||
        (target_version == 0UL))
    {
        return UART_OTA_PLAN_ERROR;
    }

    if ((target->application_version == target_version) &&
        (target->update_state == UART_OTA_UPDATE_IDLE))
    {
        return UART_OTA_PLAN_ALREADY_TARGET;
    }

    switch ((UartOtaUpdateState_t)target->update_state)
    {
        case UART_OTA_UPDATE_IDLE:
            return UART_OTA_PLAN_START_NEW;

        case UART_OTA_UPDATE_RECEIVING:
            if ((target->active_update_id == artifact_update_id) &&
                (target->expected_artifact_size == artifact_size) &&
                (target->next_expected_offset <= artifact_size) &&
                ((target->next_expected_offset %
                  UART_OTA_MAX_PAYLOAD) == 0UL))
            {
                return UART_OTA_PLAN_RESUME;
            }
            return UART_OTA_PLAN_ABORT_FOREIGN;

        case UART_OTA_UPDATE_ARTIFACT_READY:
            if ((target->active_update_id == artifact_update_id) &&
                (target->expected_artifact_size == artifact_size))
            {
                return UART_OTA_PLAN_INSTALL_READY;
            }
            return UART_OTA_PLAN_ABORT_FOREIGN;

        case UART_OTA_UPDATE_BACKING_UP:
        case UART_OTA_UPDATE_INSTALLING:
        case UART_OTA_UPDATE_VERIFYING_INSTALL:
        case UART_OTA_UPDATE_TRIAL_BOOT:
        case UART_OTA_UPDATE_CONFIRMED:
        case UART_OTA_UPDATE_ROLLBACK:
            return UART_OTA_PLAN_WAIT_TARGET;

        case UART_OTA_UPDATE_FAILED:
            return UART_OTA_PLAN_ABORT_FOREIGN;

        default:
            return UART_OTA_PLAN_ERROR;
    }
}
