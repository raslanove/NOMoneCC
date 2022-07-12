
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
//   and whitespaces. If whitespaces are not escaped, they are ignored. You can use them to make rules look a lot cleaner.
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
// Or nodes will turn the node that comes after the "|" into a separate sub-rule. Or nodes work by
// creating a tree for the node just before the "|" (lhs) and another tree for the node following
// it (rhs). This effectively puts the next node within braces ({rhs}), hence exposing it to the
// wildcard limitations.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NCC
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_RuleData;
struct NCC_Rule;

#define NCC_AST_NODE_STACKS_COUNT 5
struct NCC {
    void* extraData;
    struct NVector rules; // Pointers to rules, not rules. This way, even if the vector expands, they still point to the original rules.
    struct NCC_Rule* matchRule;
    struct NVector* astNodeStacks[NCC_AST_NODE_STACKS_COUNT]; // NCC_ASTNode_Data.
};

struct NCC_ASTNode_Data {
    void* node;
    struct NCC_RuleData* rule;
};

struct NCC_MatchingResult {
    int32_t matchLength;
    boolean terminate;
};

struct NCC_MatchingData {
    struct NCC_ASTNode_Data node;
    const char* matchedText;
    int32_t matchLength;
    boolean terminate;
};

typedef void*   (*NCC_createNodeListener)(struct NCC_RuleData* ruleData, struct NCC_ASTNode_Data* parentNode);
typedef void    (*NCC_deleteNodeListener)(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* parentNode);
typedef boolean (*NCC_matchListener)(struct NCC_MatchingData* matchingData);  // Returns true if node accepted. Also, may set the match length and the terminate fields.

struct NCC_RuleData {
    struct NCC* ncc;
    struct NString ruleName;
    struct NString ruleText;
    NCC_createNodeListener createNodeListener;
    NCC_deleteNodeListener deleteNodeListener;
    NCC_matchListener matchListener;
    struct NCC_RuleData* (*set)(struct NCC_RuleData* ruleData, const char* ruleName, const char* ruleText);
    struct NCC_RuleData* (*setListeners)(struct NCC_RuleData* ruleData, NCC_createNodeListener createNodeListener, NCC_deleteNodeListener deleteNodeListener, NCC_matchListener matchListener);
};

struct NCC* NCC_initializeNCC(struct NCC* ncc);
struct NCC* NCC_createNCC();
void NCC_destroyNCC(struct NCC* ncc);
void NCC_destroyAndFreeNCC(struct NCC* ncc);

struct NCC_RuleData* NCC_initializeRuleData(struct NCC_RuleData* ruleData, struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_createNodeListener createNodeListener, NCC_deleteNodeListener deleteNodeListener, NCC_matchListener matchListener);
void NCC_destroyRuleData(struct NCC_RuleData* ruleData);

boolean NCC_addRule(struct NCC_RuleData* ruleData);
boolean NCC_updateRule(struct NCC_RuleData* ruleData);
boolean NCC_setRootRule(struct NCC* ncc, const char* ruleName);
boolean NCC_match(struct NCC* ncc, const char* text, struct NCC_MatchingResult* outResult, struct NCC_ASTNode_Data* outNode); // Returns True if matched. Sets outResult and outNode.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic AST construction methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_ASTNode {
    struct NString name, value;
    struct NVector childNodes;
    void* extraData;
};

void*   NCC_createASTNode(struct NCC_RuleData* ruleData, struct NCC_ASTNode_Data* parentNode);
void    NCC_deleteASTNode(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* parentNode);
boolean NCC_matchASTNode (struct NCC_MatchingData* matchingData);

void NCC_ASTTreeToString(struct NCC_ASTNode* tree, struct NString* prefix, struct NString* outString);