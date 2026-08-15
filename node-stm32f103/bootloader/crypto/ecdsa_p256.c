#include "ecdsa_p256.h"

#include <stdint.h>

#define U256_LIMBS 8U
#define U256_BITS  256U

typedef struct
{
    uint32_t limb[U256_LIMBS];
} U256_t;

typedef struct
{
    U256_t x;
    U256_t y;
} AffinePoint_t;

typedef struct
{
    U256_t x;
    U256_t y;
    U256_t z;
} JacobianPoint_t;

/* NIST P-256 / secp256r1 field prime. */
static const U256_t g_p =
{
    {
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0x00000000UL,
        0x00000000UL, 0x00000000UL, 0x00000001UL, 0xFFFFFFFFUL
    }
};

/* NIST P-256 group order. */
static const U256_t g_n =
{
    {
        0xFC632551UL, 0xF3B9CAC2UL, 0xA7179E84UL, 0xBCE6FAADUL,
        0xFFFFFFFFUL, 0xFFFFFFFFUL, 0x00000000UL, 0xFFFFFFFFUL
    }
};

/* Curve coefficient b. Coefficient a is -3 mod p. */
static const U256_t g_b =
{
    {
        0x27D2604BUL, 0x3BCE3C3EUL, 0xCC53B0F6UL, 0x651D06B0UL,
        0x769886BCUL, 0xB3EBBD55UL, 0xAA3A93E7UL, 0x5AC635D8UL
    }
};

static const AffinePoint_t g_generator =
{
    {
        {
            0xD898C296UL, 0xF4A13945UL, 0x2DEB33A0UL, 0x77037D81UL,
            0x63A440F2UL, 0xF8BCE6E5UL, 0xE12C4247UL, 0x6B17D1F2UL
        }
    },
    {
        {
            0x37BF51F5UL, 0xCBB64068UL, 0x6B315ECEUL, 0x2BCE3357UL,
            0x7C0F9E16UL, 0x8EE7EB4AUL, 0xFE1A7F9BUL, 0x4FE342E2UL
        }
    }
};

static void U256_Clear(U256_t *value)
{
    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        value->limb[i] = 0UL;
    }
}

static void U256_Copy(U256_t *destination, const U256_t *source)
{
    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        destination->limb[i] = source->limb[i];
    }
}

static uint8_t U256_IsZero(const U256_t *value)
{
    uint32_t combined = 0UL;

    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        combined |= value->limb[i];
    }

    return (uint8_t)(combined == 0UL);
}

static int32_t U256_Compare(const U256_t *left, const U256_t *right)
{
    for (int32_t i = (int32_t)U256_LIMBS - 1; i >= 0; --i)
    {
        if (left->limb[(uint32_t)i] > right->limb[(uint32_t)i])
        {
            return 1;
        }
        if (left->limb[(uint32_t)i] < right->limb[(uint32_t)i])
        {
            return -1;
        }
    }

    return 0;
}

static void U256_FromBigEndian(U256_t *value, const uint8_t bytes[32])
{
    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        const uint32_t offset = 28UL - (i * 4UL);

        value->limb[i] =
            ((uint32_t)bytes[offset] << 24U) |
            ((uint32_t)bytes[offset + 1UL] << 16U) |
            ((uint32_t)bytes[offset + 2UL] << 8U) |
            (uint32_t)bytes[offset + 3UL];
    }
}

static uint8_t U256_GetBit(const U256_t *value, uint32_t bit)
{
    return (uint8_t)(
        (value->limb[bit >> 5U] >> (bit & 31U)) & 1UL);
}

/* destination = left - right. Caller guarantees left >= right. */
static void U256_SubRaw(U256_t *destination,
                        const U256_t *left,
                        const U256_t *right)
{
    uint32_t borrow = 0UL;

    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        const uint32_t a = left->limb[i];
        const uint32_t b = right->limb[i];
        const uint32_t first = a - b;
        const uint32_t borrow1 = (uint32_t)(a < b);
        const uint32_t second = first - borrow;
        const uint32_t borrow2 = (uint32_t)(first < borrow);

        destination->limb[i] = second;
        borrow = borrow1 | borrow2;
    }
}

/* destination = left + right. Caller guarantees mathematical sum < 2^256. */
static void U256_AddRaw(U256_t *destination,
                        const U256_t *left,
                        const U256_t *right)
{
    uint32_t carry = 0UL;

    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        const uint32_t a = left->limb[i];
        const uint32_t b = right->limb[i];
        const uint32_t first = a + b;
        const uint32_t carry1 = (uint32_t)(first < a);
        const uint32_t second = first + carry;
        const uint32_t carry2 = (uint32_t)(second < first);

        destination->limb[i] = second;
        carry = carry1 | carry2;
    }
}

static void U256_SubSmall(U256_t *value, uint32_t small)
{
    uint32_t borrow = small;

    for (uint32_t i = 0UL; i < U256_LIMBS; ++i)
    {
        const uint32_t current = value->limb[i];
        const uint32_t next = current - borrow;

        value->limb[i] = next;

        if (current >= borrow)
        {
            break;
        }

        borrow = 1UL;
    }
}

/*
 * Modular addition without needing a 257-bit temporary:
 * if left >= modulus-right, result = left-(modulus-right), otherwise left+right.
 */
static void ModAdd(U256_t *result,
                   const U256_t *left,
                   const U256_t *right,
                   const U256_t *modulus)
{
    U256_t threshold;

    U256_SubRaw(&threshold, modulus, right);

    if (U256_Compare(left, &threshold) >= 0)
    {
        U256_SubRaw(result, left, &threshold);
    }
    else
    {
        U256_AddRaw(result, left, right);
    }
}

static void ModSub(U256_t *result,
                   const U256_t *left,
                   const U256_t *right,
                   const U256_t *modulus)
{
    if (U256_Compare(left, right) >= 0)
    {
        U256_SubRaw(result, left, right);
    }
    else
    {
        U256_t difference;

        U256_SubRaw(&difference, right, left);
        U256_SubRaw(result, modulus, &difference);
    }
}

static void ModMul(U256_t *result,
                   const U256_t *left,
                   const U256_t *right,
                   const U256_t *modulus)
{
    U256_t accumulator;
    U256_t doubled;

    U256_Clear(&accumulator);
    U256_Copy(&doubled, left);

    for (uint32_t bit = 0UL; bit < U256_BITS; ++bit)
    {
        if (U256_GetBit(right, bit) != 0U)
        {
            U256_t next;
            ModAdd(&next, &accumulator, &doubled, modulus);
            U256_Copy(&accumulator, &next);
        }

        if (bit + 1UL < U256_BITS)
        {
            U256_t next;
            ModAdd(&next, &doubled, &doubled, modulus);
            U256_Copy(&doubled, &next);
        }
    }

    U256_Copy(result, &accumulator);
}

static void ModSquare(U256_t *result,
                      const U256_t *value,
                      const U256_t *modulus)
{
    ModMul(result, value, value, modulus);
}

static void ModDouble(U256_t *result,
                      const U256_t *value,
                      const U256_t *modulus)
{
    ModAdd(result, value, value, modulus);
}

static void ModTriple(U256_t *result,
                      const U256_t *value,
                      const U256_t *modulus)
{
    U256_t doubled;
    ModDouble(&doubled, value, modulus);
    ModAdd(result, &doubled, value, modulus);
}

static void ModTimes4(U256_t *result,
                      const U256_t *value,
                      const U256_t *modulus)
{
    U256_t doubled;
    ModDouble(&doubled, value, modulus);
    ModDouble(result, &doubled, modulus);
}

static void ModTimes8(U256_t *result,
                      const U256_t *value,
                      const U256_t *modulus)
{
    U256_t times4;
    ModTimes4(&times4, value, modulus);
    ModDouble(result, &times4, modulus);
}

static uint8_t ModInverse(U256_t *result,
                          const U256_t *value,
                          const U256_t *modulus)
{
    U256_t exponent;
    U256_t accumulator;

    if (U256_IsZero(value) != 0U)
    {
        return 0U;
    }

    U256_Copy(&exponent, modulus);
    U256_SubSmall(&exponent, 2UL);

    U256_Clear(&accumulator);
    accumulator.limb[0] = 1UL;

    for (int32_t bit = 255; bit >= 0; --bit)
    {
        U256_t squared;

        ModSquare(&squared, &accumulator, modulus);
        U256_Copy(&accumulator, &squared);

        if (U256_GetBit(&exponent, (uint32_t)bit) != 0U)
        {
            U256_t multiplied;

            ModMul(&multiplied, &accumulator, value, modulus);
            U256_Copy(&accumulator, &multiplied);
        }
    }

    U256_Copy(result, &accumulator);
    return 1U;
}

static void PointSetInfinity(JacobianPoint_t *point)
{
    U256_Clear(&point->x);
    U256_Clear(&point->y);
    U256_Clear(&point->z);
}

static uint8_t PointIsInfinity(const JacobianPoint_t *point)
{
    return U256_IsZero(&point->z);
}

static void PointSetAffine(JacobianPoint_t *point,
                           const AffinePoint_t *affine)
{
    U256_Copy(&point->x, &affine->x);
    U256_Copy(&point->y, &affine->y);
    U256_Clear(&point->z);
    point->z.limb[0] = 1UL;
}

/* Jacobian doubling specialized for a = -3. */
static void PointDouble(JacobianPoint_t *point)
{
    U256_t delta;
    U256_t gamma;
    U256_t beta;
    U256_t alpha;
    U256_t t1;
    U256_t t2;
    U256_t x3;
    U256_t y3;
    U256_t z3;
    U256_t gamma2;

    if ((PointIsInfinity(point) != 0U) ||
        (U256_IsZero(&point->y) != 0U))
    {
        PointSetInfinity(point);
        return;
    }

    ModSquare(&delta, &point->z, &g_p);
    ModSquare(&gamma, &point->y, &g_p);
    ModMul(&beta, &point->x, &gamma, &g_p);

    ModSub(&t1, &point->x, &delta, &g_p);
    ModAdd(&t2, &point->x, &delta, &g_p);
    ModMul(&alpha, &t1, &t2, &g_p);
    ModTriple(&alpha, &alpha, &g_p);

    ModSquare(&x3, &alpha, &g_p);
    ModTimes8(&t1, &beta, &g_p);
    ModSub(&x3, &x3, &t1, &g_p);

    ModAdd(&t1, &point->y, &point->z, &g_p);
    ModSquare(&z3, &t1, &g_p);
    ModSub(&z3, &z3, &gamma, &g_p);
    ModSub(&z3, &z3, &delta, &g_p);

    ModTimes4(&t1, &beta, &g_p);
    ModSub(&t1, &t1, &x3, &g_p);
    ModMul(&y3, &alpha, &t1, &g_p);
    ModSquare(&gamma2, &gamma, &g_p);
    ModTimes8(&gamma2, &gamma2, &g_p);
    ModSub(&y3, &y3, &gamma2, &g_p);

    U256_Copy(&point->x, &x3);
    U256_Copy(&point->y, &y3);
    U256_Copy(&point->z, &z3);
}

/* Jacobian + affine mixed addition. */
static void PointAddAffine(JacobianPoint_t *point,
                           const AffinePoint_t *affine)
{
    U256_t z1z1;
    U256_t u2;
    U256_t s2;
    U256_t h;
    U256_t hh;
    U256_t i4;
    U256_t j;
    U256_t r;
    U256_t v;
    U256_t t1;
    U256_t t2;
    U256_t x3;
    U256_t y3;
    U256_t z3;

    if (PointIsInfinity(point) != 0U)
    {
        PointSetAffine(point, affine);
        return;
    }

    ModSquare(&z1z1, &point->z, &g_p);
    ModMul(&u2, &affine->x, &z1z1, &g_p);

    ModMul(&t1, &point->z, &z1z1, &g_p);
    ModMul(&s2, &affine->y, &t1, &g_p);

    ModSub(&h, &u2, &point->x, &g_p);
    ModSub(&r, &s2, &point->y, &g_p);

    if (U256_IsZero(&h) != 0U)
    {
        if (U256_IsZero(&r) != 0U)
        {
            PointDouble(point);
        }
        else
        {
            PointSetInfinity(point);
        }
        return;
    }

    ModSquare(&hh, &h, &g_p);
    ModTimes4(&i4, &hh, &g_p);
    ModMul(&j, &h, &i4, &g_p);
    ModDouble(&r, &r, &g_p);
    ModMul(&v, &point->x, &i4, &g_p);

    ModSquare(&x3, &r, &g_p);
    ModSub(&x3, &x3, &j, &g_p);
    ModDouble(&t1, &v, &g_p);
    ModSub(&x3, &x3, &t1, &g_p);

    ModSub(&t1, &v, &x3, &g_p);
    ModMul(&y3, &r, &t1, &g_p);
    ModMul(&t2, &point->y, &j, &g_p);
    ModDouble(&t2, &t2, &g_p);
    ModSub(&y3, &y3, &t2, &g_p);

    ModAdd(&t1, &point->z, &h, &g_p);
    ModSquare(&z3, &t1, &g_p);
    ModSub(&z3, &z3, &z1z1, &g_p);
    ModSub(&z3, &z3, &hh, &g_p);

    U256_Copy(&point->x, &x3);
    U256_Copy(&point->y, &y3);
    U256_Copy(&point->z, &z3);
}

static uint8_t PointIsOnCurve(const AffinePoint_t *point)
{
    U256_t y2;
    U256_t x2;
    U256_t x3;
    U256_t three_x;
    U256_t rhs;

    if ((U256_Compare(&point->x, &g_p) >= 0) ||
        (U256_Compare(&point->y, &g_p) >= 0))
    {
        return 0U;
    }

    if ((U256_IsZero(&point->x) != 0U) &&
        (U256_IsZero(&point->y) != 0U))
    {
        return 0U;
    }

    ModSquare(&y2, &point->y, &g_p);
    ModSquare(&x2, &point->x, &g_p);
    ModMul(&x3, &x2, &point->x, &g_p);
    ModTriple(&three_x, &point->x, &g_p);
    ModSub(&rhs, &x3, &three_x, &g_p);
    ModAdd(&rhs, &rhs, &g_b, &g_p);

    return (uint8_t)(U256_Compare(&y2, &rhs) == 0);
}

static uint8_t PointAffineX(U256_t *x, const JacobianPoint_t *point)
{
    U256_t z_inverse;
    U256_t z_inverse_squared;

    if ((PointIsInfinity(point) != 0U) ||
        (ModInverse(&z_inverse, &point->z, &g_p) == 0U))
    {
        return 0U;
    }

    ModSquare(&z_inverse_squared, &z_inverse, &g_p);
    ModMul(x, &point->x, &z_inverse_squared, &g_p);
    return 1U;
}

/*
 * Simultaneous double-and-add computes scalar_g*G + scalar_q*Q.
 * Scalars and all input points are public during signature verification.
 */
static void PointLinearCombination(JacobianPoint_t *result,
                                   const U256_t *scalar_g,
                                   const U256_t *scalar_q,
                                   const AffinePoint_t *q)
{
    PointSetInfinity(result);

    for (int32_t bit = 255; bit >= 0; --bit)
    {
        if (PointIsInfinity(result) == 0U)
        {
            PointDouble(result);
        }

        if (U256_GetBit(scalar_g, (uint32_t)bit) != 0U)
        {
            PointAddAffine(result, &g_generator);
        }

        if (U256_GetBit(scalar_q, (uint32_t)bit) != 0U)
        {
            PointAddAffine(result, q);
        }
    }
}

EcdsaP256Status_t EcdsaP256_VerifyDigest(
    const uint8_t public_key[ECDSA_P256_PUBLIC_KEY_SIZE],
    const uint8_t digest[ECDSA_P256_DIGEST_SIZE],
    const uint8_t signature[ECDSA_P256_SIGNATURE_SIZE])
{
    AffinePoint_t q;
    U256_t r;
    U256_t s;
    U256_t z;
    U256_t w;
    U256_t u1;
    U256_t u2;
    JacobianPoint_t point;
    U256_t x;

    if ((public_key == (const uint8_t *)0) ||
        (digest == (const uint8_t *)0) ||
        (signature == (const uint8_t *)0))
    {
        return ECDSA_P256_INVALID_ARGUMENT;
    }

    U256_FromBigEndian(&q.x, &public_key[0]);
    U256_FromBigEndian(&q.y, &public_key[32]);
    U256_FromBigEndian(&r, &signature[0]);
    U256_FromBigEndian(&s, &signature[32]);
    U256_FromBigEndian(&z, digest);

    if (PointIsOnCurve(&q) == 0U)
    {
        return ECDSA_P256_INVALID_PUBLIC_KEY;
    }

    if ((U256_IsZero(&r) != 0U) ||
        (U256_IsZero(&s) != 0U) ||
        (U256_Compare(&r, &g_n) >= 0) ||
        (U256_Compare(&s, &g_n) >= 0))
    {
        return ECDSA_P256_INVALID_SIGNATURE;
    }

    /*
     * SHA-256 is exactly the P-256 order bit length, so the ECDSA message
     * representative is the 256-bit digest interpreted big-endian. Because
     * p/n are both 256-bit, reduce z modulo n once when needed.
     */
    if (U256_Compare(&z, &g_n) >= 0)
    {
        U256_t reduced;
        U256_SubRaw(&reduced, &z, &g_n);
        U256_Copy(&z, &reduced);
    }

    if (ModInverse(&w, &s, &g_n) == 0U)
    {
        return ECDSA_P256_MATH_ERROR;
    }

    ModMul(&u1, &z, &w, &g_n);
    ModMul(&u2, &r, &w, &g_n);

    PointLinearCombination(&point, &u1, &u2, &q);

    if (PointAffineX(&x, &point) == 0U)
    {
        return ECDSA_P256_INVALID_SIGNATURE;
    }

    /* P-256 field prime is smaller than 2*n; a single subtraction suffices. */
    if (U256_Compare(&x, &g_n) >= 0)
    {
        U256_t reduced;
        U256_SubRaw(&reduced, &x, &g_n);
        U256_Copy(&x, &reduced);
    }

    return (U256_Compare(&x, &r) == 0)
               ? ECDSA_P256_VALID
               : ECDSA_P256_INVALID_SIGNATURE;
}
