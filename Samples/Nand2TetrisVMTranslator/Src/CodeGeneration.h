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
struct NCC_MatchingData;

typedef enum {
    VARIABLES=1u,
    STACK_POINTER=2u,
    SEGMENTS=4u,
    SYS_INIT=8u
} InitializationFlags;

void               emitCode(struct NCC* ncc, const char* format, ...);
void emitInitializationCode(struct NCC* ncc, InitializationFlags flags);
void    emitTerminationCode(struct NCC* ncc);
void          labelListener(struct NCC_MatchingData* matchingData);
void           pushListener(struct NCC_MatchingData* matchingData);
void            popListener(struct NCC_MatchingData* matchingData);
void            addListener(struct NCC_MatchingData* matchingData);
void            subListener(struct NCC_MatchingData* matchingData);
void            andListener(struct NCC_MatchingData* matchingData);
void             orListener(struct NCC_MatchingData* matchingData);
void             eqListener(struct NCC_MatchingData* matchingData);
void             ltListener(struct NCC_MatchingData* matchingData);
void             gtListener(struct NCC_MatchingData* matchingData);
void            negListener(struct NCC_MatchingData* matchingData);
void            notListener(struct NCC_MatchingData* matchingData);
void           jumpListener(struct NCC_MatchingData* matchingData);
void    jumpNotZeroListener(struct NCC_MatchingData* matchingData);
void       functionListener(struct NCC_MatchingData* matchingData);
void         returnListener(struct NCC_MatchingData* matchingData);
void           callListener(struct NCC_MatchingData* matchingData);