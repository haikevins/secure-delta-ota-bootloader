#ifndef ECDSA_P256_H
#define ECDSA_P256_H

#include <stdint.h>

#define ECDSA_P256_PUBLIC_KEY_SIZE 64U
#define ECDSA_P256_SIGNATURE_SIZE  64U
#define ECDSA_P256_DIGEST_SIZE     32U

typedef enum
{
    ECDSA_P256_VALID = 0,
    ECDSA_P256_INVALID_ARGUMENT,
    ECDSA_P256_INVALID_PUBLIC_KEY,
    ECDSA_P256_INVALID_SIGNATURE,
    ECDSA_P256_MATH_ERROR
} EcdsaP256Status_t;

/*
 * Public key encoding: 32-byte big-endian X || 32-byte big-endian Y.
 * Signature encoding:  32-byte big-endian r || 32-byte big-endian s.
 */
EcdsaP256Status_t EcdsaP256_VerifyDigest(
    const uint8_t public_key[ECDSA_P256_PUBLIC_KEY_SIZE],
    const uint8_t digest[ECDSA_P256_DIGEST_SIZE],
    const uint8_t signature[ECDSA_P256_SIGNATURE_SIZE]);

#endif
