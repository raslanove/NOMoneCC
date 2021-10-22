#include <NCC.h>
#include <NSystemUtils.h>
#include <NError.h>

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

static int32_t rootNodeMatch(struct NCC_Node* node, const char* text) {
    struct RootNodeData* nodeData = node->data;
    int32_t matchLength = nodeData->nextNode->match(nodeData->nextNode, text);
    return matchLength > 0 ? matchLength-1 : 0; // Delete the one added by the accept node.
}

static struct NCC_Node* createRootNode() {
    struct NCC_Node* rootNode = NSystemUtils.malloc(sizeof(struct NCC_Node));
    rootNode->type = NCC_NodeType.ROOT;
    rootNode->data = NSystemUtils.malloc(sizeof(struct RootNodeData));
    rootNode->addChildTree = rootNodeAddChildTree;
    rootNode->match = rootNodeMatch;
    return rootNode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accept node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void acceptNodeAddChildTree(struct NCC_Node* node, struct NCC_Node* childTree) {
    NERROR("NCC.c", "addChildTree() shouldn't be called on an accept node");
}

static int32_t acceptNodeMatch(struct NCC_Node* node, const char* text) {
    // Reaching accept node means that the strings matches the rule, even if the string is not over
    // yet,
    return 1;
}

static struct NCC_Node* createAcceptNode() {
    struct NCC_Node* acceptNode = NSystemUtils.malloc(sizeof(struct NCC_Node));
    acceptNode->type = NCC_NodeType.ACCEPT;
    acceptNode->data = 0;
    acceptNode->addChildTree = acceptNodeAddChildTree;
    acceptNode->match = acceptNodeMatch;
    return acceptNode;
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

static int32_t literalNodeMatch(struct NCC_Node* node, const char* text) {
    struct LiteralNodeData* nodeData = node->data;
    if (*text != nodeData->literal) return 0;
    int32_t matchLength = nodeData->nextNode->match(nodeData->nextNode, &text[1]);
    return matchLength > 0 ? matchLength+1 : 0;
}

static struct NCC_Node* createLiteralNode(const char* rule, int32_t* in_out_index) {

    struct NCC_Node* newNode = NSystemUtils.malloc(sizeof(struct NCC_Node));
    struct LiteralNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralNodeData));

    char literal = rule[(*in_out_index)++];
    if (literal == '\\') literal = rule[(*in_out_index)++];

    newNode->type = NCC_NodeType.LITERAL;
    newNode->addChildTree = literalNodeAddChildTree;
    newNode->match = literalNodeMatch;
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
            return createAcceptNode();
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

    struct NCC_Node* rootNode = createRootNode();

    struct NCC_Node* lastNode = rootNode;
    struct NCC_Node* currentNode;
    int32_t index=0;
    while ((currentNode = getNextNode(rule, &index))) {
        lastNode->addChildTree(lastNode, currentNode);
        lastNode = currentNode;
        if (currentNode->type == NCC_NodeType.ACCEPT) break;
    }

    return rootNode;
}

const struct NCC_NodeType NCC_NodeType = {
    .ROOT = 0,
    .ACCEPT = 1,
    .LITERAL = 2,
    .ANY_OF = 3,
    .LITERALS_RANGE = 4,
    .REPEAT = 5,
    .ITEM = 6,
    .ANYTHING = 7
};