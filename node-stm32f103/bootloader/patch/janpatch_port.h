#ifndef JANPATCH_PORT_H
#define JANPATCH_PORT_H

#include <stdint.h>

#include "delta_patch.h"

typedef enum
{
    JANPATCH_PORT_OK = 0,
    JANPATCH_PORT_INVALID_ARGUMENT,
    JANPATCH_PORT_SOURCE_RANGE,
    JANPATCH_PORT_PATCH_RANGE,
    JANPATCH_PORT_TARGET_RANGE,
    JANPATCH_PORT_PATCH_READ_FAILED,
    JANPATCH_PORT_TARGET_WRITE_FAILED,
    JANPATCH_PORT_BAD_OPCODE,
    JANPATCH_PORT_BAD_LENGTH,
    JANPATCH_PORT_TRUNCATED_STREAM,
    JANPATCH_PORT_TARGET_SIZE_MISMATCH
} JanpatchPortStatus_t;

typedef struct
{
    uint32_t base_image_size;
    uint32_t patch_offset;
    uint32_t patch_size;
    uint32_t target_image_size;
} JanpatchPortStream_t;

typedef struct
{
    uint32_t source_position;
    uint32_t patch_position;
    uint32_t target_position;
    uint32_t operation_count;
    uint32_t equal_bytes;
    uint32_t modified_bytes;
    uint32_t inserted_bytes;
    uint32_t deleted_bytes;
    uint32_t backward_bytes;
} JanpatchPortResult_t;

JanpatchPortStatus_t JanpatchPort_ApplyStream(
    const JanpatchPortStream_t *stream,
    JanpatchPortResult_t *result);

/* Phase-13 compatibility wrapper around the generic stream API. */
JanpatchPortStatus_t JanpatchPort_Apply(
    const DeltaPatchHeader_t *header,
    JanpatchPortResult_t *result);

#endif
