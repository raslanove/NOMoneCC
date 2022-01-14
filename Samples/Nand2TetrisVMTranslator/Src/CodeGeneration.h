#pragma once

// Created by Omar El Sayyed on the 18th of November 2021.

#include <NTypes.h>
#include <NString.h>

struct OutputData {
    struct NString fileName;
    struct NString code;
    int32_t lastLabelIndex;
};

struct NCC;

typedef enum {
    VARIABLES=1u,
    STACK_POINTER=2u,
    SEGMENTS=4u,
    SYS_INIT=8u
} InitializationFlags;

void               emitCode(struct NCC* ncc, const char* format, ...);
void emitInitializationCode(struct NCC* ncc, InitializationFlags flags);
void    emitTerminationCode(struct NCC* ncc);
void          labelListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void           pushListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void            popListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void            addListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void            subListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void            andListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void             orListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void             eqListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void             ltListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void             gtListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void            negListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void            notListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void           jumpListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void    jumpNotZeroListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void       functionListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void         returnListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void           callListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);