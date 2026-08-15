#ifndef TRIAL_CONFIRMATION_H
#define TRIAL_CONFIRMATION_H

#include <stdbool.h>
#include <stdint.h>

bool TrialConfirmation_Init(uint32_t now_ms);
void TrialConfirmation_Process(uint32_t now_ms);
bool TrialConfirmation_ConfirmNow(uint32_t *detail);
bool TrialConfirmation_IsActive(void);

#endif
