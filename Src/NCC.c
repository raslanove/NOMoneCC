#include <NCC.h>
#include <NSystemUtils.h>
#include <NError.h>

//
// Rules:
//   Exact match   :         for             = for
//   Literals range:         smallLetter     = a-z
//   Or            :         letter          = a-z|A-Z
//   Repeat        :         name            = A-Za-z^*
//   Sub-rule      :         namesList       = {A-Za-z^*}|{{A-Za-z^*}{,A-Za-z^*}^*}
//   Limited repeat:         identifier      = {a-z|A-Z}{a-z|A-Z|0-9}^0-49
//   Substitute    :         integer         = 1-90-9^*
//                           integerPair     = ${integer},${integer}
//   Anything      :         sentence        = *.
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
// Node types:
//   literal:         a
//   or:              |
//   literals range:  a-z
//   repeat:          ^*  or ^1-49
//   sub-rule:        ${name} or {rule}
//   anything:        *   or  * followed by something

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void             NCC_setPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode) { node->setPreviousNode(node, previousNode); }
void             NCC_setNextNode    (struct NCC_Node* node, struct NCC_Node*     nextNode) { node->setNextNode    (node,     nextNode); }
struct NCC_Node* NCC_getPreviousNode(struct NCC_Node* node) { return node->getPreviousNode(node); }
struct NCC_Node* NCC_getNextNode    (struct NCC_Node* node) { return node->getNextNode    (node); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void             genericSetPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode) { node->previousNode = previousNode; }
static void             genericSetNextNode    (struct NCC_Node* node, struct NCC_Node*     nextNode) { node->nextNode =         nextNode; }
static struct NCC_Node* genericGetPreviousNode(struct NCC_Node* node) { return node->previousNode; }
static struct NCC_Node* genericGetNextNode    (struct NCC_Node* node) { return node->    nextNode; }

static void genericDeleteTreeNoData(struct NCC_Node* tree) {
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NSystemUtils.free(tree);
}

static void genericDeleteTreeWithData(struct NCC_Node* tree) {
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

static struct NCC_Node* genericCreateNode(int32_t type, void* data, int32_t (*matchMethod)(struct NCC_Node* node, const char* text)) {
    struct NCC_Node* node = NSystemUtils.malloc(sizeof(struct NCC_Node));
    node->type = type;
    node->data = data;
    node->match = matchMethod;

    node->previousNode = 0;
    node->nextNode = 0;
    node->setPreviousNode = genericSetPreviousNode;
    node->setNextNode     = genericSetNextNode;
    node->getPreviousNode = genericGetPreviousNode;
    node->getNextNode     = genericGetNextNode;
    node->deleteTree      = data ? genericDeleteTreeWithData : genericDeleteTreeNoData;

    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Root node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void rootNodeSetPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode) {
    NERROR("NCC.c", "%ssetPreviousNode()%s shouldn't be called on a %sroot%s node", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
}

static int32_t rootNodeMatch(struct NCC_Node* node, const char* text) {
    int32_t matchLength = node->nextNode->match(node->nextNode, text);
    return matchLength > 0 ? matchLength-1 : 0; // Delete the one added by the accept node.
}

static struct NCC_Node* createRootNode() {
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ROOT, 0, rootNodeMatch);
    node->setPreviousNode = rootNodeSetPreviousNode;
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accept node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void acceptNodeSetNextNode(struct NCC_Node* node, struct NCC_Node* nextNode) {
    NERROR("NCC.c", "%ssetNextNode()%s shouldn't be called on an %saccept%s node", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
}

static int32_t acceptNodeMatch(struct NCC_Node* node, const char* text) {
    // Reaching accept node means that the strings matches the rule, even if the string is not over
    // yet,
    return 1;
}

static struct NCC_Node* createAcceptNode() {
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ACCEPT, 0, acceptNodeMatch);
    node->setNextNode = acceptNodeSetNextNode;
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literal node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralNodeData {
    char literal;
};

static int32_t literalNodeMatch(struct NCC_Node* node, const char* text) {
    struct LiteralNodeData* nodeData = node->data;
    if (*text != nodeData->literal) return 0;
    int32_t matchLength = node->nextNode->match(node->nextNode, &text[1]);
    return matchLength > 0 ? matchLength+1 : 0;
}

static struct NCC_Node* createLiteralNode(const char literal) {
    struct LiteralNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralNodeData));
    nodeData->literal = literal;
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL, nodeData, literalNodeMatch);
    // TODO: remove...
    NLOGI("NCC", "Created literal node: %c", literal);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literals range node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralsRangeNodeData {
    char rangeStart, rangeEnd;
};

static int32_t literalsRangeNodeMatch(struct NCC_Node* node, const char* text) {
    struct LiteralsRangeNodeData* nodeData = node->data;
    char literal = *text;
    if ((literal < nodeData->rangeStart) || (literal > nodeData->rangeEnd)) return 0;
    int32_t matchLength = node->nextNode->match(node->nextNode, &text[1]);
    return matchLength > 0 ? matchLength+1 : 0;
}

static struct NCC_Node* createLiteralsRangeNode(char rangeStart, char rangeEnd) {

    struct LiteralsRangeNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralsRangeNodeData));
    if (rangeStart > rangeEnd) {
        char temp = rangeStart;
        rangeStart = rangeEnd;
        rangeEnd = temp;
    }
    nodeData->rangeStart = rangeStart;
    nodeData->rangeEnd = rangeEnd;

    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS_RANGE, nodeData, literalsRangeNodeMatch);

    // TODO: remove...
    NLOGI("NCC", "Created range literal node: %c-%c", rangeStart, rangeEnd);
    return node;
}

static boolean isReserved(const char literal) {
    switch (literal) {
        case 0:
        case ' ':
        case '$':
        case '*':
        case '{':
        case '^':
        case '|':
        case '-':
            return True;
        default:
            return False;
    }
}

static char unescapeLiteral(const char** in_out_rule) {

    char literal = ((*in_out_rule)++)[0];
    if (literal == '\\') {
        literal = ((*in_out_rule)++)[0];
        if (literal == 0) {
            NERROR("NCC", "getEscapedLiteral(): escape character %s\\%s not followed by anything", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        }
    }

    // New line handling,
    if (literal == 'n') return '\n';

    // Just return as is,
    return literal;
}

static struct NCC_Node* handleLiteral(const char** in_out_rule) {

    char literal = unescapeLiteral(in_out_rule);
    if (!literal) return 0;

    // Check if this was a literals range,
    char followingLiteral = **in_out_rule;
    if (followingLiteral == '-') {
        (*in_out_rule)++;
        if (isReserved(**in_out_rule)) {
            NERROR("NCC", "handleLiteral(): A '%s-%s' can't be followed by an unescaped '%s%c%s'", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), **in_out_rule, NTCOLOR(STREAM_DEFAULT));
            return 0;
        }
        if (!(followingLiteral = unescapeLiteral(in_out_rule))) return 0;
        return createLiteralsRangeNode(literal, followingLiteral);
    } else {
        return createLiteralNode(literal);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Or node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO:////







static struct NCC_Node* getNextNode(const char** in_out_rule) {

    char currentChar;
    while ((currentChar = **in_out_rule) == ' ') (*in_out_rule)++;

    switch (currentChar) {
        case 0:
            return createAcceptNode();
        case '$':
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
            NERROR("NCC", "getNextNode(): a '%s-%s' must always be preceded by a literal", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        default: {
            return handleLiteral(in_out_rule);
        }
    }
}

struct NCC_Node* NCC_constructRuleTree(const char* rule) {

    struct NCC_Node* rootNode = createRootNode();
    struct NCC_Node* lastNode = rootNode;
    struct NCC_Node* currentNode;
    const char* remainingSubRule = rule;

    int32_t errorsBeginning = NError.observeErrors();
    do {
        currentNode = getNextNode(&remainingSubRule);
        if (NError.observeErrors()>errorsBeginning) goto failureCleanup;
        if (!currentNode) break;

        lastNode->setNextNode(lastNode, currentNode);
        lastNode = currentNode;
        if (currentNode->type == NCC_NodeType.ACCEPT) break;
    } while (True);

    return rootNode;

failureCleanup:
    if (rootNode) rootNode->deleteTree(rootNode);
    return 0;
}

const struct NCC_NodeType NCC_NodeType = {
    .ROOT = 0,
    .ACCEPT = 1,
    .LITERAL = 2,
    .OR = 3,
    .LITERALS_RANGE = 4,
    .REPEAT = 5,
    .SUB_RULE = 6,
    .ANYTHING = 7
};