#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>

#define SHA256_DIGEST_SIZE 32U
#define SHA256_BLOCK_SIZE  64U

typedef struct
{
    uint32_t state[8];
    uint64_t total_length;
    uint8_t buffer[SHA256_BLOCK_SIZE];
    uint32_t buffer_length;
} Sha256Context_t;

void Sha256_Init(Sha256Context_t *context);
void Sha256_Update(Sha256Context_t *context,
                   const void *data,
                   uint32_t length);
void Sha256_Final(Sha256Context_t *context,
                  uint8_t digest[SHA256_DIGEST_SIZE]);
void Sha256_Calculate(const void *data,
                      uint32_t length,
                      uint8_t digest[SHA256_DIGEST_SIZE]);

#endif
