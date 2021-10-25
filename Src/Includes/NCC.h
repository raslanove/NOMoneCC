
/////////////////////////////////////////////////////////
// Created by Omar El Sayyed on the 21st of October 2021.
/////////////////////////////////////////////////////////

#pragma once

#include <NTypes.h>

// Node types:
//   literal:         a
//   or:              |
//   literals range:  a-z
//   repeat:          ^*  or ^1-49
//   sub-rule:        ${name} or {rule}
//   anything:        *   or  * followed by something

struct NCC_NodeType {
    int32_t ROOT, ACCEPT, LITERAL, OR, LITERALS_RANGE, REPEAT, SUB_RULE, ANYTHING;
};

// Unimplemented nodes:
//   Or.
//   Repeat.
//   Sub-rule.
//   Anything.

struct NCC_Node {
    int32_t type;
    void *data;
    struct NCC_Node* previousNode;
    struct NCC_Node*     nextNode;
    int32_t (*match)(struct NCC_Node* node, const char* text); // Returns match length if matched, 0 if rejected.
    void (*setPreviousNode)(struct NCC_Node* node, struct NCC_Node* previousNode);
    void (*setNextNode    )(struct NCC_Node* node, struct NCC_Node*     nextNode);
    struct NCC_Node* (*getPreviousNode)(struct NCC_Node* node);
    struct NCC_Node* (*getNextNode    )(struct NCC_Node* node);
    void (*deleteTree)(struct NCC_Node* tree);
};

struct NCC_Node* NCC_constructRuleTree(const char* rule);
void             NCC_setPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode);
void             NCC_setNextNode    (struct NCC_Node* node, struct NCC_Node*     nextNode);
struct NCC_Node* NCC_getPreviousNode(struct NCC_Node* node);
struct NCC_Node* NCC_getNextNode    (struct NCC_Node* node);

extern const struct NCC_NodeType NCC_NodeType;