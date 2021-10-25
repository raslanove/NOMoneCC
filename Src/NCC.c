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

static struct NCC_Node* getNextNode(struct NCC_Node* parentNode, const char** in_out_rule);

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

static void             genericSetPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode) { if (node->previousNode) node->previousNode->nextNode=0; node->previousNode = previousNode; if (previousNode) previousNode->nextNode = node; }
static void             genericSetNextNode    (struct NCC_Node* node, struct NCC_Node*     nextNode) { if (node->    nextNode) node->nextNode->previousNode=0; node->nextNode     =     nextNode; if (    nextNode) nextNode->previousNode = node; }
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
    return matchLength > 0 ? matchLength-1 : 0; // Subtract the one added by the accept node.
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL, nodeData, literalNodeMatch);
    nodeData->literal = literal;

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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS_RANGE, nodeData, literalsRangeNodeMatch);
    if (rangeStart > rangeEnd) {
        char temp = rangeStart;
        rangeStart = rangeEnd;
        rangeEnd = temp;
    }
    nodeData->rangeStart = rangeStart;
    nodeData->rangeEnd = rangeEnd;

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

struct OrNodeData {
    struct NCC_Node* rhsTree;
    struct NCC_Node* lhsTree;
};

static int32_t orNodeMatch(struct NCC_Node* node, const char* text) {
    struct OrNodeData* nodeData = node->data;

    int32_t rhsMatchLength = nodeData->rhsTree->match(nodeData->rhsTree, text);
    int32_t lhsMatchLength = nodeData->lhsTree->match(nodeData->lhsTree, text);

    int32_t matchLength = rhsMatchLength > lhsMatchLength ? rhsMatchLength : lhsMatchLength;
    if (!matchLength) return 0;

    return matchLength + node->nextNode->match(node->nextNode, &text[matchLength]);
}

static void orNodeDeleteTree(struct NCC_Node* tree) {
    struct OrNodeData* nodeData = tree->data;
    nodeData->rhsTree->deleteTree(nodeData->rhsTree);
    nodeData->lhsTree->deleteTree(nodeData->lhsTree);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

static struct NCC_Node* createOrNode(struct NCC_Node* parentNode, const char** in_out_rule) {
    struct OrNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData, orNodeMatch);
    node->deleteTree = orNodeDeleteTree;

    // Remove parent from the grand-parent and attach this node instead,
    struct NCC_Node* grandParentNode = parentNode->getPreviousNode(parentNode);
    grandParentNode->setNextNode(grandParentNode, node);

    // Turn parent node into a tree and attach it as the lhs node,
    nodeData->lhsTree = createRootNode();
    nodeData->lhsTree->setNextNode(nodeData->lhsTree, parentNode);
    parentNode->setNextNode(parentNode, createAcceptNode());

    // Create a new tree for the next node and set it as the rhs,
    nodeData->rhsTree = createRootNode();
    const char* remainingSubRule = ++(*in_out_rule); // Skip the '|'.
    if (!**in_out_rule) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the end of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }
    struct NCC_Node* rhsNode = getNextNode(nodeData->rhsTree, in_out_rule);
    if (!rhsNode) {
        NERROR("NCC", "createOrNode(): couldn't create an rhs node: %s%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }
    rhsNode->setNextNode(rhsNode, createAcceptNode());

    // TODO: remove...
    NLOGI("NCC", "Created or node: %s|%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
    return node;
}







static struct NCC_Node* getNextNode(struct NCC_Node* parentNode, const char** in_out_rule) {

    char currentChar;
    while ((currentChar = **in_out_rule) == ' ') (*in_out_rule)++;

    struct NCC_Node* node;
    switch (currentChar) {
        case 0:
            node = createAcceptNode();
            break;
        case '$':
            break;
        case '*':
            break;
        case '{':
            break;
        case '^':
            break;
        case '|':
            node = createOrNode(parentNode, in_out_rule); break;
        case '-':
            NERROR("NCC", "getNextNode(): a '%s-%s' must always be preceded by a literal", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        default: {
            node = handleLiteral(in_out_rule); break;
        }
    }

    // Only "Or" nodes attach themselves,
    if (node && node->type != NCC_NodeType.OR) parentNode->setNextNode(parentNode, node);

    return node;
}

struct NCC_Node* NCC_constructRuleTree(const char* rule) {

    struct NCC_Node* rootNode = createRootNode();

    struct NCC_Node* currentNode = rootNode;
    const char* remainingSubRule = rule;
    int32_t errorsBeginning = NError.observeErrors();
    do {
        currentNode = getNextNode(currentNode, &remainingSubRule);
        if (!currentNode || NError.observeErrors()>errorsBeginning) break;
        if (currentNode->type == NCC_NodeType.ACCEPT) return rootNode;
    } while (True);

    // Failed,
    rootNode->deleteTree(rootNode);
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