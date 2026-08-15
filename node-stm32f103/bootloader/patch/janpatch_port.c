#include "janpatch_port.h"

#include <stdint.h>

#include "external_flash_storage.h"
#include "memory_map.h"

#define JDIFF_ESC 0xA7U
#define JDIFF_MOD 0xA6U
#define JDIFF_INS 0xA5U
#define JDIFF_DEL 0xA4U
#define JDIFF_EQL 0xA3U
#define JDIFF_BKT 0xA2U

#define JANPATCH_IO_BUFFER_SIZE 128U

typedef struct
{
    const JanpatchPortStream_t *stream;
    uint32_t source_position;
    uint32_t patch_position;
    uint32_t target_position;
    JanpatchPortResult_t result;
} JanpatchContext_t;

#if defined(JANPATCH_PORT_HOST_TEST)
extern const uint8_t *g_janpatch_host_source;
#endif

static JanpatchPortStatus_t ReadSource(
    uint32_t offset,
    uint8_t *buffer,
    uint32_t length)
{
    if ((buffer == (uint8_t *)0) ||
        (offset > APPLICATION_MAX_SIZE) ||
        (length > (APPLICATION_MAX_SIZE - offset)))
    {
        return JANPATCH_PORT_SOURCE_RANGE;
    }

#if defined(JANPATCH_PORT_HOST_TEST)
    if (g_janpatch_host_source == (const uint8_t *)0)
    {
        return JANPATCH_PORT_SOURCE_RANGE;
    }

    for (uint32_t i = 0UL; i < length; ++i)
    {
        buffer[i] = g_janpatch_host_source[offset + i];
    }
#else
    {
        volatile const uint8_t *source =
            (volatile const uint8_t *)(
                APPLICATION_START_ADDRESS + offset);

        for (uint32_t i = 0UL; i < length; ++i)
        {
            buffer[i] = source[i];
        }
    }
#endif

    return JANPATCH_PORT_OK;
}

static uint8_t IsOpcode(uint8_t value)
{
    return (uint8_t)(
        (value == JDIFF_MOD) ||
        (value == JDIFF_INS) ||
        (value == JDIFF_DEL) ||
        (value == JDIFF_EQL) ||
        (value == JDIFF_BKT));
}

static JanpatchPortStatus_t PatchReadByte(
    JanpatchContext_t *ctx,
    uint8_t *value)
{
    if ((ctx == (JanpatchContext_t *)0) ||
        (value == (uint8_t *)0))
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    if (ctx->patch_position >= ctx->stream->patch_size)
    {
        return JANPATCH_PORT_TRUNCATED_STREAM;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            ctx->stream->patch_offset + ctx->patch_position,
            value,
            1UL))
    {
        return JANPATCH_PORT_PATCH_READ_FAILED;
    }

    ++ctx->patch_position;
    return JANPATCH_PORT_OK;
}

static JanpatchPortStatus_t PatchPeekByte(
    JanpatchContext_t *ctx,
    uint32_t relative_offset,
    uint8_t *value)
{
    uint32_t position;

    if ((ctx == (JanpatchContext_t *)0) ||
        (value == (uint8_t *)0))
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    if (relative_offset >
        (ctx->stream->patch_size - ctx->patch_position))
    {
        return JANPATCH_PORT_PATCH_RANGE;
    }

    position = ctx->patch_position + relative_offset;
    if (position >= ctx->stream->patch_size)
    {
        return JANPATCH_PORT_TRUNCATED_STREAM;
    }

    if (!ExternalFlashStorage_Read(
            EXTERNAL_FLASH_PARTITION_INCOMING,
            ctx->stream->patch_offset + position,
            value,
            1UL))
    {
        return JANPATCH_PORT_PATCH_READ_FAILED;
    }

    return JANPATCH_PORT_OK;
}

static JanpatchPortStatus_t ReadLength(
    JanpatchContext_t *ctx,
    uint32_t *length)
{
    uint8_t prefix;
    JanpatchPortStatus_t status;

    if (length == (uint32_t *)0)
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    status = PatchReadByte(ctx, &prefix);
    if (status != JANPATCH_PORT_OK)
    {
        return status;
    }

    if (prefix <= 251U)
    {
        *length = (uint32_t)prefix + 1UL;
        return JANPATCH_PORT_OK;
    }

    if (prefix == 252U)
    {
        uint8_t low;

        status = PatchReadByte(ctx, &low);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        *length = 253UL + (uint32_t)low;
        return JANPATCH_PORT_OK;
    }

    if (prefix == 253U)
    {
        uint8_t bytes[2];

        status = PatchReadByte(ctx, &bytes[0]);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }
        status = PatchReadByte(ctx, &bytes[1]);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        *length = ((uint32_t)bytes[0] << 8U) |
                  (uint32_t)bytes[1];

        return (*length != 0UL)
                   ? JANPATCH_PORT_OK
                   : JANPATCH_PORT_BAD_LENGTH;
    }

    if (prefix == 254U)
    {
        uint8_t bytes[4];

        for (uint32_t i = 0UL; i < 4UL; ++i)
        {
            status = PatchReadByte(ctx, &bytes[i]);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }
        }

        *length = ((uint32_t)bytes[0] << 24U) |
                  ((uint32_t)bytes[1] << 16U) |
                  ((uint32_t)bytes[2] << 8U) |
                  (uint32_t)bytes[3];

        return (*length != 0UL)
                   ? JANPATCH_PORT_OK
                   : JANPATCH_PORT_BAD_LENGTH;
    }

    return JANPATCH_PORT_BAD_LENGTH;
}

static JanpatchPortStatus_t TargetWrite(
    JanpatchContext_t *ctx,
    const uint8_t *data,
    uint32_t length)
{
    if ((ctx == (JanpatchContext_t *)0) ||
        ((data == (const uint8_t *)0) && (length != 0UL)))
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    if ((ctx->target_position > ctx->stream->target_image_size) ||
        (length >
         (ctx->stream->target_image_size - ctx->target_position)))
    {
        return JANPATCH_PORT_TARGET_RANGE;
    }

    if ((length != 0UL) &&
        !ExternalFlashStorage_Write(
            EXTERNAL_FLASH_PARTITION_RECONSTRUCTED,
            ctx->target_position,
            data,
            length))
    {
        return JANPATCH_PORT_TARGET_WRITE_FAILED;
    }

    ctx->target_position += length;
    return JANPATCH_PORT_OK;
}

static JanpatchPortStatus_t CopyEqual(
    JanpatchContext_t *ctx,
    uint32_t length)
{
    uint8_t buffer[JANPATCH_IO_BUFFER_SIZE];

    if ((ctx->source_position > ctx->stream->base_image_size) ||
        (length >
         (ctx->stream->base_image_size - ctx->source_position)))
    {
        return JANPATCH_PORT_SOURCE_RANGE;
    }

    while (length != 0UL)
    {
        uint32_t chunk = length;
        JanpatchPortStatus_t status;

        if (chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        status = ReadSource(
            ctx->source_position,
            buffer,
            chunk);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        status = TargetWrite(ctx, buffer, chunk);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        ctx->source_position += chunk;
        ctx->result.equal_bytes += chunk;
        length -= chunk;
    }

    return JANPATCH_PORT_OK;
}

static JanpatchPortStatus_t ConsumeLiteral(
    JanpatchContext_t *ctx,
    uint8_t advance_source,
    uint32_t *written_out)
{
    uint8_t buffer[JANPATCH_IO_BUFFER_SIZE];
    uint32_t used = 0UL;
    uint32_t total = 0UL;

    if (written_out == (uint32_t *)0)
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    for (;;)
    {
        uint8_t value;
        JanpatchPortStatus_t status;

        if (ctx->patch_position >= ctx->stream->patch_size)
        {
            break;
        }

        status = PatchPeekByte(ctx, 0UL, &value);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        if (value != JDIFF_ESC)
        {
            status = PatchReadByte(ctx, &value);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }
            buffer[used++] = value;
        }
        else
        {
            uint8_t next;

            if ((ctx->patch_position + 1UL) >= ctx->stream->patch_size)
            {
                /*
                 * Match JANPatch/JojoDiff compatibility behavior: ESC at EOF
                 * terminates the current literal run.
                 */
                ++ctx->patch_position;
                break;
            }

            status = PatchPeekByte(ctx, 1UL, &next);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }

            if (next == JDIFF_ESC)
            {
                ctx->patch_position += 2UL;
                buffer[used++] = JDIFF_ESC;
            }
            else if (IsOpcode(next) != 0U)
            {
                break;
            }
            else
            {
                /*
                 * Unknown escaped pairs are literal data for compatibility.
                 */
                ctx->patch_position += 2UL;
                buffer[used++] = JDIFF_ESC;

                if (used == sizeof(buffer))
                {
                    status = TargetWrite(ctx, buffer, used);
                    if (status != JANPATCH_PORT_OK)
                    {
                        return status;
                    }

                    if (advance_source != 0U)
                    {
                        if ((ctx->source_position >
                             ctx->stream->base_image_size) ||
                            (used >
                             (ctx->stream->base_image_size -
                              ctx->source_position)))
                        {
                            return JANPATCH_PORT_SOURCE_RANGE;
                        }
                        ctx->source_position += used;
                    }

                    total += used;
                    used = 0UL;
                }

                buffer[used++] = next;
            }
        }

        if (used == sizeof(buffer))
        {
            status = TargetWrite(ctx, buffer, used);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }

            if (advance_source != 0U)
            {
                if ((ctx->source_position >
                     ctx->stream->base_image_size) ||
                    (used >
                     (ctx->stream->base_image_size -
                      ctx->source_position)))
                {
                    return JANPATCH_PORT_SOURCE_RANGE;
                }
                ctx->source_position += used;
            }

            total += used;
            used = 0UL;
        }
    }

    if (used != 0UL)
    {
        JanpatchPortStatus_t status =
            TargetWrite(ctx, buffer, used);

        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        if (advance_source != 0U)
        {
            if ((ctx->source_position >
                 ctx->stream->base_image_size) ||
                (used >
                 (ctx->stream->base_image_size -
                  ctx->source_position)))
            {
                return JANPATCH_PORT_SOURCE_RANGE;
            }
            ctx->source_position += used;
        }

        total += used;
    }

    *written_out = total;
    return JANPATCH_PORT_OK;
}

JanpatchPortStatus_t JanpatchPort_ApplyStream(
    const JanpatchPortStream_t *stream,
    JanpatchPortResult_t *result)
{
    JanpatchContext_t ctx;

    if ((stream == (const JanpatchPortStream_t *)0) ||
        (stream->base_image_size == 0UL) ||
        (stream->patch_size == 0UL) ||
        (stream->target_image_size == 0UL))
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    ctx.stream = stream;
    ctx.source_position = 0UL;
    ctx.patch_position = 0UL;
    ctx.target_position = 0UL;
    ctx.result.source_position = 0UL;
    ctx.result.patch_position = 0UL;
    ctx.result.target_position = 0UL;
    ctx.result.operation_count = 0UL;
    ctx.result.equal_bytes = 0UL;
    ctx.result.modified_bytes = 0UL;
    ctx.result.inserted_bytes = 0UL;
    ctx.result.deleted_bytes = 0UL;
    ctx.result.backward_bytes = 0UL;

    while (ctx.patch_position < stream->patch_size)
    {
        uint8_t first;
        uint8_t opcode = JDIFF_MOD;
        JanpatchPortStatus_t status;

        status = PatchPeekByte(&ctx, 0UL, &first);
        if (status != JANPATCH_PORT_OK)
        {
            return status;
        }

        if (first == JDIFF_ESC)
        {
            uint8_t next;

            if ((ctx.patch_position + 1UL) >= stream->patch_size)
            {
                return JANPATCH_PORT_TRUNCATED_STREAM;
            }

            status = PatchPeekByte(&ctx, 1UL, &next);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }

            if (IsOpcode(next) != 0U)
            {
                ctx.patch_position += 2UL;
                opcode = next;
            }
            else
            {
                opcode = JDIFF_MOD;
            }
        }

        if ((opcode == JDIFF_EQL) ||
            (opcode == JDIFF_DEL) ||
            (opcode == JDIFF_BKT))
        {
            uint32_t length;

            status = ReadLength(&ctx, &length);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }

            if (opcode == JDIFF_EQL)
            {
                status = CopyEqual(&ctx, length);
                if (status != JANPATCH_PORT_OK)
                {
                    return status;
                }
            }
            else if (opcode == JDIFF_DEL)
            {
                if ((ctx.source_position >
                     stream->base_image_size) ||
                    (length >
                     (stream->base_image_size -
                      ctx.source_position)))
                {
                    return JANPATCH_PORT_SOURCE_RANGE;
                }

                ctx.source_position += length;
                ctx.result.deleted_bytes += length;
            }
            else
            {
                if (length > ctx.source_position)
                {
                    return JANPATCH_PORT_SOURCE_RANGE;
                }

                ctx.source_position -= length;
                ctx.result.backward_bytes += length;
            }
        }
        else if ((opcode == JDIFF_MOD) ||
                 (opcode == JDIFF_INS))
        {
            uint32_t written = 0UL;

            status = ConsumeLiteral(
                &ctx,
                (uint8_t)(opcode == JDIFF_MOD),
                &written);
            if (status != JANPATCH_PORT_OK)
            {
                return status;
            }

            if (opcode == JDIFF_MOD)
            {
                ctx.result.modified_bytes += written;
            }
            else
            {
                ctx.result.inserted_bytes += written;
            }
        }
        else
        {
            return JANPATCH_PORT_BAD_OPCODE;
        }

        ++ctx.result.operation_count;
    }

    if (ctx.target_position != stream->target_image_size)
    {
        return JANPATCH_PORT_TARGET_SIZE_MISMATCH;
    }

    ctx.result.source_position = ctx.source_position;
    ctx.result.patch_position = ctx.patch_position;
    ctx.result.target_position = ctx.target_position;

    if (result != (JanpatchPortResult_t *)0)
    {
        *result = ctx.result;
    }

    return JANPATCH_PORT_OK;
}


JanpatchPortStatus_t JanpatchPort_Apply(
    const DeltaPatchHeader_t *header,
    JanpatchPortResult_t *result)
{
    JanpatchPortStream_t stream;

    if (header == (const DeltaPatchHeader_t *)0)
    {
        return JANPATCH_PORT_INVALID_ARGUMENT;
    }

    stream.base_image_size = header->base_image_size;
    stream.patch_offset = DELTA_PATCH_HEADER_SIZE;
    stream.patch_size = header->patch_size;
    stream.target_image_size = header->target_image_size;

    return JanpatchPort_ApplyStream(&stream, result);
}
