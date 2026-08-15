#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESC 0xA7
#define MOD 0xA6
#define INS 0xA5
#define DEL 0xA4
#define EQL 0xA3
#define BKT 0xA2

static int is_op(int value)
{
    return value >= BKT && value <= MOD;
}

static int read_length(FILE *patch, uint32_t *length)
{
    int prefix = fgetc(patch);

    if (prefix == EOF)
    {
        return -1;
    }

    if (prefix <= 251)
    {
        *length = (uint32_t)prefix + 1U;
        return 0;
    }

    if (prefix == 252)
    {
        int low = fgetc(patch);
        if (low == EOF)
        {
            return -1;
        }
        *length = 253U + (uint32_t)low;
        return 0;
    }

    if (prefix == 253)
    {
        int b0 = fgetc(patch);
        int b1 = fgetc(patch);
        if (b0 == EOF || b1 == EOF)
        {
            return -1;
        }
        *length = ((uint32_t)b0 << 8) | (uint32_t)b1;
        return 0;
    }

    if (prefix == 254)
    {
        uint32_t value = 0U;
        for (unsigned i = 0; i < 4U; ++i)
        {
            int byte = fgetc(patch);
            if (byte == EOF)
            {
                return -1;
            }
            value = (value << 8) | (uint32_t)byte;
        }
        *length = value;
        return 0;
    }

    return -1;
}

static int copy_equal(FILE *source, FILE *target, uint32_t length)
{
    while (length-- != 0U)
    {
        int byte = fgetc(source);
        if (byte == EOF || fputc(byte, target) == EOF)
        {
            return -1;
        }
    }
    return 0;
}

static int advance_source(FILE *source, long delta)
{
    return fseek(source, delta, SEEK_CUR);
}

static int copy_literal(FILE *source,
                        FILE *patch,
                        FILE *target,
                        int advance)
{
    for (;;)
    {
        long before = ftell(patch);
        int byte = fgetc(patch);

        if (byte == EOF)
        {
            return 0;
        }

        if (byte != ESC)
        {
            if (fputc(byte, target) == EOF)
            {
                return -1;
            }
            if (advance && advance_source(source, 1L) != 0)
            {
                return -1;
            }
            continue;
        }

        {
            int next = fgetc(patch);

            if (next == EOF)
            {
                return 0;
            }

            if (next == ESC)
            {
                if (fputc(ESC, target) == EOF)
                {
                    return -1;
                }
                if (advance && advance_source(source, 1L) != 0)
                {
                    return -1;
                }
                continue;
            }

            if (is_op(next))
            {
                if (fseek(patch, before, SEEK_SET) != 0)
                {
                    return -1;
                }
                return 0;
            }

            if (fputc(ESC, target) == EOF ||
                fputc(next, target) == EOF)
            {
                return -1;
            }

            if (advance && advance_source(source, 2L) != 0)
            {
                return -1;
            }
        }
    }
}

int main(int argc, char **argv)
{
    FILE *source = NULL;
    FILE *patch = NULL;
    FILE *target = NULL;
    int result = 1;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s source patch target\n", argv[0]);
        return 2;
    }

    source = fopen(argv[1], "rb");
    patch = fopen(argv[2], "rb");
    target = fopen(argv[3], "wb");

    if (source == NULL || patch == NULL || target == NULL)
    {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        goto cleanup;
    }

    for (;;)
    {
        int first = fgetc(patch);
        int opcode;

        if (first == EOF)
        {
            result = 0;
            break;
        }

        if (first == ESC)
        {
            opcode = fgetc(patch);
            if (opcode == EOF)
            {
                fprintf(stderr, "truncated opcode\n");
                goto cleanup;
            }
        }
        else
        {
            if (fseek(patch, -1L, SEEK_CUR) != 0)
            {
                goto cleanup;
            }
            opcode = MOD;
        }

        if (opcode == EQL || opcode == DEL || opcode == BKT)
        {
            uint32_t length;

            if (read_length(patch, &length) != 0)
            {
                fprintf(stderr, "invalid length\n");
                goto cleanup;
            }

            if (opcode == EQL)
            {
                if (copy_equal(source, target, length) != 0)
                {
                    goto cleanup;
                }
            }
            else if (opcode == DEL)
            {
                if (advance_source(source, (long)length) != 0)
                {
                    goto cleanup;
                }
            }
            else
            {
                if (advance_source(source, -(long)length) != 0)
                {
                    goto cleanup;
                }
            }
        }
        else if (opcode == MOD || opcode == INS)
        {
            if (copy_literal(
                    source,
                    patch,
                    target,
                    opcode == MOD) != 0)
            {
                goto cleanup;
            }
        }
        else
        {
            fprintf(stderr, "unsupported opcode 0x%02X\n", opcode);
            goto cleanup;
        }
    }

cleanup:
    if (source != NULL)
    {
        fclose(source);
    }
    if (patch != NULL)
    {
        fclose(patch);
    }
    if (target != NULL)
    {
        fclose(target);
    }

    return result;
}
