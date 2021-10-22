
/////////////////////////////////////////////////////////
// Created by Omar El Sayyed on the 21st of October 2021.
/////////////////////////////////////////////////////////

#pragma once

#include <NTypes.h>

//   literal:         a
//   anyOf:           [...] or |
//   literals range:  a-z
//   repeat:          ^*  or ^1-49
//   item:            ${name} or {rule}
//   anything:        *   or  * followed by something

struct NCC_NodeType {
    int32_t ROOT, ACCEPT, LITERAL, ANY_OF, LITERALS_RANGE, REPEAT, ITEM, ANYTHING;
};

struct NCC_Node {
    int32_t type;
    void *data;
    int32_t (*match)(struct NCC_Node* node, const char* text); // Returns match length if matched, 0 if rejected.
    void (*addChildTree)(struct NCC_Node* node, struct NCC_Node* childTree);
    // delete method ...
};

struct NCC_Node* NCC_constructRuleTree(const char* rule);

extern const struct NCC_NodeType NCC_NodeType;