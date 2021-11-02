
/////////////////////////////////////////////////////////
// Created by Omar El Sayyed on the 21st of October 2021.
/////////////////////////////////////////////////////////

#pragma once

#include <NTypes.h>
#include <NVector.h>
#include <NString.h>

// Node types:
//   literal:         a
//   or:              |
//   literals range:  a-z
//   repeat:          ^*
//   sub-rule:        {rule}
//   substitute:      ${name}
//   anything:        *   or  * followed by something

struct NCC {
    struct NVector rules; // Pointers to rules, not rules, so that they don't get relocated when more rules are added.
    struct NVector variables;
    int32_t currentCallStackBeginning;
};

typedef void (*NCC_onMatchListener)(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);

struct NCC* NCC_initializeNCC(struct NCC* ncc);
struct NCC* NCC_createNCC();
void NCC_destroyNCC(struct NCC* ncc);
void NCC_destroyAndFreeNCC(struct NCC* ncc);
boolean NCC_addRule(struct NCC* ncc, const char* name, const char* ruleText, NCC_onMatchListener onMatchListener);
int32_t NCC_match(struct NCC* ncc, const char* text); // Returns match length if matched, 0 if rejected.
boolean NCC_popVariable(struct NCC* ncc, struct NString* outName, struct NString* outValue);
