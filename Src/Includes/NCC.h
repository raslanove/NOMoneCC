
/////////////////////////////////////////////////////////
// NOMone Compiler Compiler
// Created by Omar El Sayyed on the 21st of October 2021.
/////////////////////////////////////////////////////////

#pragma once

#include <NTypes.h>
#include <NVector.h>
#include <NString.h>

//
// Node types:
//   literals:        abc
//   or:              |
//   literal range:   a-z
//   repeat:          ^*
//   sub-rule:        ${name} or {rule}
//   anything:        *   or  * followed by something
//
// Example rules:
//   Literals     :         for             = for
//   Literal range:         smallLetter     = a-z
//   Or           :         letter          = a-z|A-Z
//   Repeat       :         name            = A-Za-z^*
//   Sub-rule     :         namesList       = {A-Za-z^*}|{{A-Za-z^*}{,A-Za-z^*}^*}
//   Substitute   :         integer         = 1-90-9^*
//                          integerPair     = ${integer},${integer}
//   Anything     :         sentence        = *.
//
// Reserved characters (must be escaped):
//   \ | - ^ * { } $
//   Whitespaces must be escaped, otherwise they only serve to resolve ambiguities. For example:
//     identifier = {a-z|A-Z}{a-z|A-Z|0-9}^0-49
//   Does this mean a letter or a digit repeated from 0 to 49 times? Or is it from 0 to 4 followed
//   by 9? It's the former. If we need that latter, we use:
//     variableName = {a-z|A-Z}{a-z|A-Z|0-9}^0-4 9
//   The space is ignored. It only serves to clearly indicate that the 9 is separate from the 4.
//
//   Note: now that we've removed the support for limited repeat, this ambiguity doesn't exist at all,
//         still, spaces must still be escaped, for they can be used to make rules look a lot cleaner.
//

// Limitations:
// ============
// Left recursion:
// ---------------
// Left recursion is not allowed. This is a top-down recursive parser that fails miserably with
// left-recursion. Luckily, we support repeats.
//
// Wildcard nodes:
// ---------------
// Anything nodes (*) will match anything until the remaining part of the sub-rule is encountered.
// For instance:
//   *xyz
// Will consume all the text until an "xyz" is found. This works well as long as the termination
// sequence is within the same sub-rule. If the termination sequence is part of a parent rule, it
// won't ever be reached. For example:
//   {*}xyz
// will never match anything.
//
// The same could be said for the repeat nodes (^*), depending on the contents of the repeated
// expression. For example:
//   {xyz}^*xyz
// will find an immediate match in the text "xyzxyzxyz", because the termination sequence is right
// there at the beginning. However,
//   {{xyz}^*}xyz
// will consume the entire text within the sub-rule, as it can't see the parent's termination
// sequence.
//
// Or nodes:
// ---------
// Or nodes will consider whatever comes after the "|" a separate sub-rule. Or nodes work by
// creating a tree for the node just before the "|" (lhs) and another tree for the node following
// it (rhs). During matching, whichever gets the longest match is considered the correct match.
// However, this effectively puts the next node within braces ({rhs}), hence exposing it to the
// wildcard limitations.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NCC
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Variable;
struct NByteVector;
struct NCC_RuleData;

struct NCC {
    void* extraData;
    struct NVector rules; // Pointers to rules, not rules, so that they don't get relocated when more rules are added.
    struct NVector variables;
    struct NByteVector *matchRoute, *tempRoute1, *tempRoute2, *tempRoute3, *tempRoute4; // Pointers to nodes. TODO: maybe turn them into an array?
    uint32_t currentCallStackBeginning;
    uint32_t ruleIndexSizeBytes;
};

// Returned from the unconfirmed match listener,
struct NCC_MatchingResult {
    int32_t matchLength;
    boolean terminate;
    boolean pushVariable;
    boolean couldNeedRollBack;
};

// Sent to the listeners,
struct NCC_MatchingData {
    struct NCC_RuleData* ruleData;
    char* matchedText;
    int32_t matchLength;
    int32_t variablesCount;

    struct NCC_MatchingResult outResult; // Setting this is useful only inside the unconfirmed match listener.
};

typedef boolean (*NCC_onUnconfirmedMatchListener)(struct NCC_MatchingData* matchingData);
typedef void    (*   NCC_onRollBackMatchListener)(struct NCC_MatchingData* matchingData);
typedef void    (*  NCC_onConfirmedMatchListener)(struct NCC_MatchingData* matchingData);

struct NCC_RuleData {
    struct NCC* ncc;
    struct NString ruleName;
    struct NString ruleText;
    NCC_onUnconfirmedMatchListener onUnconfirmedMatchListener;
    NCC_onRollBackMatchListener       onRollBackMatchListener;
    NCC_onConfirmedMatchListener     onConfirmedMatchListener;
    boolean rootRule;              // True: can be matched alone. False: must be part of some other rule.
    boolean pushVariable;          // False: matches, but the value is ignored.
    boolean popsChildrenVariables; // False: keeps the variables of nested rules.

    struct NCC_RuleData* (*set)(struct NCC_RuleData* ruleData, const char* ruleName, const char* ruleText);
    struct NCC_RuleData* (*setListeners)(struct NCC_RuleData* ruleData, NCC_onUnconfirmedMatchListener onUnconfirmedMatchListener, NCC_onRollBackMatchListener onRollBackMatchListener, NCC_onConfirmedMatchListener onConfirmedMatchListener);
    struct NCC_RuleData* (*setFlags)(struct NCC_RuleData* ruleData, boolean rootRule, boolean pushVariable, boolean popsChildrenVariables);
};

// TODO: populate and return NCC_MatchingResult from matching functions...
// TODO: NCC_onUnconfirmedMatchListener should return True if this rule was accepted, False otherwise to look for other alternatives.
// TODO: replace child function pointers with look-up tables based on type_id...

struct NCC* NCC_initializeNCC(struct NCC* ncc);
struct NCC* NCC_createNCC();
void NCC_destroyNCC(struct NCC* ncc);
void NCC_destroyAndFreeNCC(struct NCC* ncc);

struct NCC_RuleData* NCC_initializeRuleData(struct NCC_RuleData* ruleData, struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_onUnconfirmedMatchListener onUnconfirmedMatchListener, NCC_onRollBackMatchListener onRollBackMatchListener, NCC_onConfirmedMatchListener onConfirmedMatchListener, boolean rootRule, boolean pushVariable, boolean popsChildrenVariables);
void NCC_destroyRuleData(struct NCC_RuleData* ruleData);

boolean NCC_addRule(struct NCC_RuleData* ruleData);
boolean NCC_updateRule(struct NCC_RuleData* ruleData);

int32_t NCC_match(struct NCC* ncc, const char* text); // Returns match length if matched, -1 if rejected.
boolean NCC_popRuleVariable(struct NCC* ncc, struct NCC_Variable* outVariable); // Pops variables of the currently active rule.
boolean NCC_getRuleVariable(struct NCC* ncc, uint32_t index, struct NCC_Variable* outVariable); // Gets variables of the currently active rule.
void NCC_discardRuleVariables(struct NCC* ncc); // Discards variables of the currently active rule.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variable
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Variable {
    const char* name;
    struct NString value;
};

struct NCC_Variable* NCC_initializeVariable(struct NCC_Variable* variable, const char* name, const char* value);
void NCC_destroyVariable(struct NCC_Variable* variable);
