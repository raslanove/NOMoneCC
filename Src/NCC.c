#include <NCC.h>
#include <NSystemUtils.h>

//
// Rules:
//   Exact match  :         for             = for
//   Any of       :         numpadKey       = [0123456789.-+]
//   Range        :         letter          = [a-zA-Z]
//   Repeat       :         name            = A-Za-z^*
//   Repeat range :         variableName    = [a-zA-Z][a-zA-Z0-9]^0-49
//   Item         :         coordinates     = {(0-9^1-*,0-9^1-*)}
//   Repeated item:         coordinatesList = {(0-9^1-*,0-9^1-*)}^*
//   Substitute   :         integer         = 1-90-9^*
//                          integerPair     = ${integer},${integer}
//   Or           :         integer         = 1-90-9^*
//                          decimal         = 0-9^1-*.0-9^1-*
//                          number          = ${integer}|${decimal}
//   Anything     :         sentence        = *.
//
// Reserved characters (must be escaped):
//   \ | - [ ] ^ * { } $
//   Whitespaces must be escaped, otherwise they only serve to resolve ambiguities. For example:
//     variableName = [a-zA-Z][a-zA-Z0-9]^0-49
//   Does this mean a letter or a digit repeated from 0 to 49 times? Or is it from 0 to 4 followed
//   by 9? It's the former. If we need that latter, we use:
//     variableName = [a-zA-Z][a-zA-Z0-9]^0-4 9
//   The space is ignored. It only serves to clearly indicate that the 9 is separate from the 4.
//
// Node types:
//   literal:         a
//   anyOf:           [...] or |
//   literals range:  a-z
//   repeat:          ^*  or ^1-49
//   item:            ${name} or {rule}
//   anything:        *   or  * followed by something

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Root node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RootNodeData {
    struct NCC_Node* nextNode;
};

static void rootNodeAddChildTree(struct NCC_Node* node, struct NCC_Node* childTree) {
    struct RootNodeData* nodeData = node->data;
    nodeData->nextNode = childTree;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literal node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralNodeData {
    char literal;
    struct NCC_Node* nextNode;
};

static void literalNodeAddChildTree(struct NCC_Node* node, struct NCC_Node* childTree) {
    struct LiteralNodeData* nodeData = node->data;
    nodeData->nextNode = childTree;
}

static struct NCC_Node* createLiteralNode(const char* rule, int32_t* in_out_index) {

    struct NCC_Node* newNode = NSystemUtils.malloc(sizeof(struct NCC_Node));
    struct LiteralNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralNodeData));

    char literal = rule[(*in_out_index)++];
    if (literal == '\\') literal = rule[(*in_out_index)++];

    newNode->type = NCC_NodeType.LITERAL;
    newNode->addChildTree = literalNodeAddChildTree;
    newNode->data = nodeData;

    nodeData->literal = literal;
    nodeData->nextNode = 0;

    NLOGI("NCC", "Created literal node: %c", literal);

    return newNode;
}

static struct NCC_Node* getNextNode(const char* rule, int32_t* in_out_index) {

    char currentChar = rule[*in_out_index];

    switch (currentChar) {
        case 0:
            return 0;
        case '$':
            break;
        case '[':
            break;
        case '*':
            break;
        case '{':
            break;
        case '^':
            break;
        case '|':
            break;
        case '-':
            break;
        default:
            return createLiteralNode(rule, in_out_index);
    }
}

struct NCC_Node* NCC_constructRuleTree(const char* rule) {

    struct NCC_Node* rootNode = NSystemUtils.malloc(sizeof(struct NCC_Node));
    rootNode->data = NSystemUtils.malloc(sizeof(struct RootNodeData));
    rootNode->addChildTree = rootNodeAddChildTree;

    struct NCC_Node* lastNode = rootNode;
    struct NCC_Node* currentNode;
    int32_t index=0;
    while ((currentNode = getNextNode(rule, &index))) {
        lastNode->addChildTree(lastNode, currentNode);
    }

    return rootNode;
}

const struct NCC_NodeType NCC_NodeType = {
    .ROOT_NODE = 0,
    .LITERAL = 1,
    .ANY_OF = 2,
    .LITERALS_RANGE = 3,
    .REPEAT = 4,
    .ITEM = 5,
    .ANYTHING = 6
};