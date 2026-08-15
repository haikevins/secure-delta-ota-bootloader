
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ecdsa_p256.h"
#include "sha256.h"

static int read_exact(const char *path, uint8_t *out, size_t n)
{
    FILE *f = fopen(path, "rb");
    size_t got;
    int extra;
    if (f == NULL) return 0;
    got = fread(out, 1U, n, f);
    extra = fgetc(f);
    fclose(f);
    return (got == n) && (extra == EOF);
}

int main(int argc, char **argv)
{
    uint8_t pub[64], sig[64], msg[4096], digest[32];
    FILE *f;
    long n;
    EcdsaP256Status_t status;

    if (argc != 4) return 2;
    if (!read_exact(argv[1], pub, sizeof(pub)) ||
        !read_exact(argv[3], sig, sizeof(sig))) return 3;

    f = fopen(argv[2], "rb");
    if (f == NULL) return 4;
    if (fseek(f, 0L, SEEK_END) != 0) return 5;
    n = ftell(f);
    if ((n < 0L) || (n > (long)sizeof(msg))) return 5;
    rewind(f);
    if (fread(msg, 1U, (size_t)n, f) != (size_t)n)
    {
        fclose(f);
        return 6;
    }
    fclose(f);

    Sha256_Calculate(msg, (uint32_t)n, digest);
    status = EcdsaP256_VerifyDigest(pub, digest, sig);
    printf("status=%d\n", (int)status);
    return status == ECDSA_P256_VALID ? 0 : 1;
}
