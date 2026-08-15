#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ecdsa_p256.h"
#include "sha256.h"

static int ReadExact(const char *path, uint8_t *data, size_t length)
{
    FILE *stream = fopen(path, "rb");
    size_t count;
    int extra;

    if (stream == NULL)
    {
        return 0;
    }

    count = fread(data, 1U, length, stream);
    extra = fgetc(stream);
    fclose(stream);

    return (count == length) && (extra == EOF);
}

static int ShaKnownVector(void)
{
    static const uint8_t expected[32] =
    {
        0xBAU, 0x78U, 0x16U, 0xBFU, 0x8FU, 0x01U, 0xCFU, 0xEAU,
        0x41U, 0x41U, 0x40U, 0xDEU, 0x5DU, 0xAEU, 0x22U, 0x23U,
        0xB0U, 0x03U, 0x61U, 0xA3U, 0x96U, 0x17U, 0x7AU, 0x9CU,
        0xB4U, 0x10U, 0xFFU, 0x61U, 0xF2U, 0x00U, 0x15U, 0xADU
    };
    static const uint8_t message[] = {'a', 'b', 'c'};
    uint8_t digest[32];

    Sha256_Calculate(message, sizeof(message), digest);
    return memcmp(digest, expected, sizeof(expected)) == 0;
}

int main(int argc, char **argv)
{
    uint8_t public_key[64];
    uint8_t digest[32];
    uint8_t signature[64];
    uint8_t bad_signature[64];
    EcdsaP256Status_t status;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s public.raw digest.bin signature.raw\n",
                argv[0]);
        return 2;
    }

    if (!ShaKnownVector())
    {
        fprintf(stderr, "SHA-256 known vector failed\n");
        return 3;
    }

    if (!ReadExact(argv[1], public_key, sizeof(public_key)) ||
        !ReadExact(argv[2], digest, sizeof(digest)) ||
        !ReadExact(argv[3], signature, sizeof(signature)))
    {
        fprintf(stderr, "failed to read crypto vectors\n");
        return 4;
    }

    status = EcdsaP256_VerifyDigest(
        public_key,
        digest,
        signature);
    if (status != ECDSA_P256_VALID)
    {
        fprintf(stderr, "valid ECDSA signature rejected: %d\n", (int)status);
        return 5;
    }

    memcpy(bad_signature, signature, sizeof(bad_signature));
    bad_signature[17] ^= 0x01U;

    status = EcdsaP256_VerifyDigest(
        public_key,
        digest,
        bad_signature);
    if (status == ECDSA_P256_VALID)
    {
        fprintf(stderr, "tampered ECDSA signature accepted\n");
        return 6;
    }

    digest[0] ^= 0x80U;
    status = EcdsaP256_VerifyDigest(
        public_key,
        digest,
        signature);
    if (status == ECDSA_P256_VALID)
    {
        fprintf(stderr, "tampered digest accepted\n");
        return 7;
    }

    printf("Phase 14 SHA-256 + ECDSA-P256 host crypto vectors: PASS\n");
    return 0;
}
