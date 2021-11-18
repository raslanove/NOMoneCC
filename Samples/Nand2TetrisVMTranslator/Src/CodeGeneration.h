#pragma once

// Created by Omar El Sayyed on the 18th of November 2021.

#include <NTypes.h>
#include <NString.h>

struct OutputData {
    struct NString code;
    int32_t lastLabelIndex;
};

struct NCC;

void emitInitializationCode(struct NCC* ncc);
void emitTerminationCode(struct NCC* ncc);
void pushListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void  addListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void  subListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void  andListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void   orListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void   eqListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void   ltListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void   gtListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void  negListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);
void  notListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);