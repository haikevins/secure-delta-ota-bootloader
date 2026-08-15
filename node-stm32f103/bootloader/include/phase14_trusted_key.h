#ifndef PHASE14_TRUSTED_KEY_H
#define PHASE14_TRUSTED_KEY_H

#include <stdint.h>

/*
 * Production builds must replace this file using tools/phase14_keytool.py.
 * The private signing key must never be placed in MCU firmware or committed.
 */
#define PHASE14_TRUSTED_KEY_PROVISIONED 0U
#define PHASE14_TRUSTED_KEY_ID          0UL

static const uint8_t g_phase14_trusted_public_key[64] =
{
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
};

#endif
