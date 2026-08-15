#include "sha256.h"

#include <stdint.h>

static const uint32_t g_sha256_k[64] =
{
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
};

static uint32_t RotateRight(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32U - count));
}

static uint32_t LoadBe32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static void StoreBe32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void Transform(Sha256Context_t *context, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;

    for (i = 0U; i < 16U; ++i)
    {
        w[i] = LoadBe32(&block[i * 4U]);
    }

    for (i = 16U; i < 64U; ++i)
    {
        const uint32_t s0 =
            RotateRight(w[i - 15U], 7U) ^
            RotateRight(w[i - 15U], 18U) ^
            (w[i - 15U] >> 3U);
        const uint32_t s1 =
            RotateRight(w[i - 2U], 17U) ^
            RotateRight(w[i - 2U], 19U) ^
            (w[i - 2U] >> 10U);

        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (i = 0U; i < 64U; ++i)
    {
        const uint32_t s1 =
            RotateRight(e, 6U) ^
            RotateRight(e, 11U) ^
            RotateRight(e, 25U);
        const uint32_t choice = (e & f) ^ ((~e) & g);
        const uint32_t temp1 =
            h + s1 + choice + g_sha256_k[i] + w[i];
        const uint32_t s0 =
            RotateRight(a, 2U) ^
            RotateRight(a, 13U) ^
            RotateRight(a, 22U);
        const uint32_t majority =
            (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void Sha256_Init(Sha256Context_t *context)
{
    if (context == (Sha256Context_t *)0)
    {
        return;
    }

    context->state[0] = 0x6A09E667UL;
    context->state[1] = 0xBB67AE85UL;
    context->state[2] = 0x3C6EF372UL;
    context->state[3] = 0xA54FF53AUL;
    context->state[4] = 0x510E527FUL;
    context->state[5] = 0x9B05688CUL;
    context->state[6] = 0x1F83D9ABUL;
    context->state[7] = 0x5BE0CD19UL;
    context->total_length = 0ULL;
    context->buffer_length = 0UL;
}

void Sha256_Update(Sha256Context_t *context,
                   const void *data,
                   uint32_t length)
{
    const uint8_t *input = (const uint8_t *)data;

    if ((context == (Sha256Context_t *)0) ||
        ((input == (const uint8_t *)0) && (length != 0UL)))
    {
        return;
    }

    context->total_length += (uint64_t)length;

    while (length != 0UL)
    {
        uint32_t chunk =
            SHA256_BLOCK_SIZE - context->buffer_length;

        if (chunk > length)
        {
            chunk = length;
        }

        for (uint32_t i = 0UL; i < chunk; ++i)
        {
            context->buffer[context->buffer_length + i] = input[i];
        }

        context->buffer_length += chunk;
        input += chunk;
        length -= chunk;

        if (context->buffer_length == SHA256_BLOCK_SIZE)
        {
            Transform(context, context->buffer);
            context->buffer_length = 0UL;
        }
    }
}

void Sha256_Final(Sha256Context_t *context,
                  uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint64_t bit_length;
    uint32_t i;

    if ((context == (Sha256Context_t *)0) ||
        (digest == (uint8_t *)0))
    {
        return;
    }

    bit_length = context->total_length * 8ULL;

    context->buffer[context->buffer_length++] = 0x80U;

    if (context->buffer_length > 56UL)
    {
        while (context->buffer_length < SHA256_BLOCK_SIZE)
        {
            context->buffer[context->buffer_length++] = 0U;
        }

        Transform(context, context->buffer);
        context->buffer_length = 0UL;
    }

    while (context->buffer_length < 56UL)
    {
        context->buffer[context->buffer_length++] = 0U;
    }

    for (i = 0U; i < 8U; ++i)
    {
        context->buffer[63U - i] =
            (uint8_t)(bit_length >> (i * 8U));
    }

    Transform(context, context->buffer);

    for (i = 0U; i < 8U; ++i)
    {
        StoreBe32(&digest[i * 4U], context->state[i]);
    }

    context->buffer_length = 0UL;
}

void Sha256_Calculate(const void *data,
                      uint32_t length,
                      uint8_t digest[SHA256_DIGEST_SIZE])
{
    Sha256Context_t context;

    Sha256_Init(&context);
    Sha256_Update(&context, data, length);
    Sha256_Final(&context, digest);
}
