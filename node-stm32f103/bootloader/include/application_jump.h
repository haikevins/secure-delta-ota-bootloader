#ifndef APPLICATION_JUMP_H
#define APPLICATION_JUMP_H

#include <stdint.h>

typedef enum
{
    APPLICATION_VALIDATION_OK = 0,
    APPLICATION_VALIDATION_BAD_VECTOR_ALIGNMENT,
    APPLICATION_VALIDATION_BAD_VECTOR_RANGE,
    APPLICATION_VALIDATION_BAD_STACK_RANGE,
    APPLICATION_VALIDATION_BAD_STACK_ALIGNMENT,
    APPLICATION_VALIDATION_BAD_RESET_THUMB_BIT,
    APPLICATION_VALIDATION_BAD_RESET_RANGE
} ApplicationValidationStatus_t;

typedef struct
{
    uint32_t vector_table_address;
    uint32_t initial_stack_pointer;
    uint32_t reset_handler_address;
} ApplicationVector_t;

ApplicationValidationStatus_t ApplicationJump_Validate(
    uint32_t vector_table_address,
    ApplicationVector_t *application_vector);

void ApplicationJump_Execute(const ApplicationVector_t *application_vector)
    __attribute__((noreturn));

#endif
