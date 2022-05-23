#include <NCC.h>

#include <NSystemUtils.h>
#include <NError.h>
#include <NByteVector.h>
#include <NString.h>
#include <NCString.h>
#include <NVector.h>

//
// Operation:
//   First, we construct our rules. Then, given a string, we find the match route. The match route is
//   constructed in an NByteVector. We push single bytes or node pointers according to the following:
//     0     : rule end marker.
//     1->254: skip this number of literals.
//     255   : rule beginning marker. Right after this, a substitute node index was pushed.
//
// TODO: bytes indicating skipped literals should be combined together. The code is there, just commented
// out because it's not working properly. Most probably it's because we can't discard stack history if it
// was combined with what came before. A suggested fix is to push a delimiter after switching stacks to
// prevent combining? Pushing and discarding the delimiter should be carefully woven together with the
// stack switching calls.

static struct NCC_Node* constructRuleTree(struct NCC* ncc, const char* rule);
static struct NCC_Node* getNextNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule);
static struct NCC_Rule* getRule(struct NCC* ncc, const char* ruleName);
static void switchRoutes(struct NByteVector** route1, struct NByteVector** route2);
static void pushTempRouteIntoMatchRoute(struct NCC* ncc, struct NByteVector* tempRoute, int32_t tempRouteMark);
static int32_t substituteNodeFollowMatchRoute(struct NCC_Rule* rule, struct NCC* ncc, const char* text);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Node {
    int32_t type;
    void *data;
    struct NCC_Node* previousNode;
    struct NCC_Node*     nextNode;
    // TODO: remove all function pointers....
    int32_t (*match)(struct NCC_Node* node, struct NCC* ncc, const char* text); // Returns match length if matched, -1 if rejected.
    void (*setPreviousNode)(struct NCC_Node* node, struct NCC_Node* previousNode);
    void (*setNextNode    )(struct NCC_Node* node, struct NCC_Node*     nextNode);
    struct NCC_Node* (*getPreviousNode)(struct NCC_Node* node);
    struct NCC_Node* (*getNextNode    )(struct NCC_Node* node);
    void (*deleteTree)(struct NCC_Node* tree);
};

struct NCC_NodeType {
    int32_t ROOT, ACCEPT, LITERALS, OR, LITERAL_RANGE, REPEAT, SUB_RULE, SUBSTITUTE, ANYTHING;
};
const struct NCC_NodeType NCC_NodeType = {
        .ROOT = 0,
        .ACCEPT = 1,
        .LITERALS = 2,
        .OR = 3,
        .LITERAL_RANGE = 4,
        .REPEAT = 5,
        .SUB_RULE = 6,
        .SUBSTITUTE = 7,
        .ANYTHING = 8
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variable
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Variable* NCC_initializeVariable(struct NCC_Variable* variable, const char* name, const char* value) {
    variable->name = name;
    NString.initialize(&variable->value, "%s", value);
    return variable;
}

void NCC_destroyVariable(struct NCC_Variable* variable) {
    NString.destroy(&variable->value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rule
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Rule {
    struct NString name;
    struct NCC_Node* tree;
    uint32_t index;
    NCC_onMatchListener onMatchListener;
    boolean rootRule; // True: can be matched alone. False: must be part of some other rule.
    boolean pushVariable; // False: matches, but the value is ignored.
    boolean popsChildrenVariables; // False: keeps the variables of nested rules.
};

static inline struct NCC_Rule* initializeRule(struct NCC_Rule* rule, const char* name, struct NCC_Node* ruleTree, NCC_onMatchListener onMatchListener, boolean rootRule, boolean pushVariable, boolean popsChildrenVariables) {
    NString.set(&rule->name, "%s", name);
    rule->tree = ruleTree;
    rule->onMatchListener = onMatchListener;
    rule->rootRule = rootRule;
    rule->pushVariable = pushVariable;
    rule->popsChildrenVariables = popsChildrenVariables;
    return rule;
}

static struct NCC_Rule* createRule(struct NCC* ncc, const char* name, const char* ruleText, NCC_onMatchListener onMatchListener, boolean rootRule, boolean pushVariable, boolean popsChildrenVariables) {

    // Create rule tree,
    struct NCC_Node* ruleTree = constructRuleTree(ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "createRule(): unable to construct rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create and initialize rule,
    struct NCC_Rule* rule = NMALLOC(sizeof(struct NCC_Rule), "NCC.createRule() rule");
    NString.initialize(&rule->name, "");
    initializeRule(rule, name, ruleTree, onMatchListener, rootRule, pushVariable, popsChildrenVariables);

    return rule;
}

static void destroyRule(struct NCC_Rule* rule) {
    NString.destroy(&rule->name);
    rule->tree->deleteTree(rule->tree);
}

static void destroyAndFreeRule(struct NCC_Rule* rule) {
    destroyRule(rule);
    NFREE(rule, "NCC.destroyAndFreeRule() rule");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic node methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void             genericSetPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode) { if (node->previousNode) node->previousNode->nextNode=0; node->previousNode = previousNode; if (previousNode) previousNode->nextNode = node; }
static void             genericSetNextNode    (struct NCC_Node* node, struct NCC_Node*     nextNode) { if (node->    nextNode) node->nextNode->previousNode=0; node->nextNode     =     nextNode; if (    nextNode) nextNode->previousNode = node; }
static struct NCC_Node* genericGetPreviousNode(struct NCC_Node* node) { return node->previousNode; }
static struct NCC_Node* genericGetNextNode    (struct NCC_Node* node) { return node->    nextNode; }

static void genericDeleteTreeNoData(struct NCC_Node* tree) {
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree, "NCC.genericDeleteTreeNoData() tree");
}

static void genericDeleteTreeWithData(struct NCC_Node* tree) {
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.genericDeleteTreeWithData() tree->data");
    NFREE(tree      , "NCC.genericDeleteTreeWithData() tree"      );
}

typedef int32_t (*      MatchMethod)(struct NCC_Node* node, struct NCC* ncc, const char* text);

static void skipNLiteralsNoCombine(struct NCC* ncc, uint32_t n) {
    while (n > 254) {
        NByteVector.pushBack(ncc->matchRoute, 254);
        n-= 254;
    }
    NByteVector.pushBack(ncc->matchRoute, n);
}

// Returns true if combined successfully, False if just pushed.
static boolean skipNLiterals(struct NCC* ncc, uint32_t n) {

    // TODO: fix combining!
    if (True) {
        skipNLiteralsNoCombine(ncc, n);
        return False;
    }

    uint8_t lastPushedValue=0;
    if (!NByteVector.popBack(ncc->matchRoute, &lastPushedValue)) {
        skipNLiteralsNoCombine(ncc, n);
        return False;
    }
    if ((lastPushedValue==0) || (lastPushedValue==255)) {
        NByteVector.pushBack(ncc->matchRoute, lastPushedValue);
        skipNLiteralsNoCombine(ncc, n);
        return False;
    }

    skipNLiteralsNoCombine(ncc, (uint32_t) n + lastPushedValue);
    return True;
}

static int32_t followMatchRoute(struct NCC* ncc, const char* text) {

    uint32_t matchLength = 0;
    uint8_t currentValue=0;
    while (NByteVector.popBack(ncc->matchRoute, &currentValue)) {

        if (currentValue==0) {
            // This rule just ended,
            break;
        } else if (currentValue == 255) {

            // If following is a substitute node index,
            uint32_t ruleIndex = 0;
            NByteVector.popBackBulk(ncc->matchRoute, &ruleIndex, ncc->ruleIndexSizeBytes);
            struct NCC_Rule* rule = *((struct NCC_Rule**) NVector.get(&ncc->rules, ruleIndex));
            matchLength += substituteNodeFollowMatchRoute(rule, ncc, &text[matchLength]);
            continue;
        }

        // This is just a skip,
        matchLength += currentValue;
    }

    return matchLength;
}

static struct NCC_Node* genericCreateNode(int32_t type, void* data, MatchMethod matchMethod) {
    struct NCC_Node* node = NMALLOC(sizeof(struct NCC_Node), "NCC.genericCreateNode() node");
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

static int32_t rootNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    return node->nextNode->match(node->nextNode, ncc, text);
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

static int32_t acceptNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    // Reaching accept node means that the strings matches the rule, even if the string is not over yet,
    return 0;
}

static struct NCC_Node* createAcceptNode(struct NCC_Node* parentNode) {
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ACCEPT, 0, acceptNodeMatch);
    node->setNextNode = acceptNodeSetNextNode;
    parentNode->setNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literals node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralsNodeData {
    struct NString literals;
};

static int32_t literalsNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct LiteralsNodeData* nodeData = node->data;
    if (!NCString.startsWith(text, NString.get(&nodeData->literals))) return -1;
    int32_t length = NString.length(&nodeData->literals);
    int32_t matchLength = node->nextNode->match(node->nextNode, ncc, &text[length]);
    if (matchLength==-1) return -1;

    skipNLiterals(ncc, length);
    return matchLength+length;
}

static void literalsNodeDeleteTree(struct NCC_Node* tree) {
    struct LiteralsNodeData* nodeData = tree->data;
    NString.destroy(&nodeData->literals);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.literalsNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.literalsNodeDeleteTree() tree"      );
}

static struct NCC_Node* createLiteralsNode(const char* literals) {
    struct LiteralsNodeData* nodeData = NMALLOC(sizeof(struct LiteralsNodeData), "NCC.createLiteralsNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS, nodeData, literalsNodeMatch);
    node->deleteTree = literalsNodeDeleteTree;

    NString.initialize(&nodeData->literals, "%s", literals);

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created literals node: %s%s%s", NTCOLOR(HIGHLIGHT), literals, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

static struct NCC_Node* breakLastLiteralIfNeeded(struct NCC_Node* literalsNode) {

    // If the node is a literals node with more than one literal, break the last literal apart,
    if (literalsNode->type == NCC_NodeType.LITERALS) {
        struct LiteralsNodeData* nodeData = literalsNode->data;
        int32_t literalsCount = NString.length(&nodeData->literals);
        if (literalsCount>1) {

            // Create a new child literals node for the last literal,
            char* literals = (char*) NString.get(&nodeData->literals);
            char* lastLiteral = &literals[literalsCount-1];
            struct NCC_Node* newLiteralsNode = createLiteralsNode(lastLiteral);

            // Remove the last literal from the parent node,
            lastLiteral[0] = 0;
            NByteVector.resize(&nodeData->literals.string, literalsCount);
            literalsNode->setNextNode(literalsNode, newLiteralsNode);

            return newLiteralsNode;
        }
    }

    return literalsNode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literal range node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralRangeNodeData {
    unsigned char rangeStart, rangeEnd;
};

static int32_t literalRangeNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct LiteralRangeNodeData* nodeData = node->data;
    unsigned char literal = (unsigned char) *text;
    if ((literal < nodeData->rangeStart) || (literal > nodeData->rangeEnd)) return -1;
    int32_t matchLength = node->nextNode->match(node->nextNode, ncc, &text[1]);
    if (matchLength==-1) return -1;

    skipNLiterals(ncc, 1);
    return matchLength+1;
}

static struct NCC_Node* createLiteralRangeNode(unsigned char rangeStart, unsigned char rangeEnd) {

    struct LiteralRangeNodeData* nodeData = NMALLOC(sizeof(struct LiteralRangeNodeData), "NCC.createLiteralRangeNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL_RANGE, nodeData, literalRangeNodeMatch);
    if (rangeStart > rangeEnd) {
        unsigned char temp = rangeStart;
        rangeStart = rangeEnd;
        rangeEnd = temp;
    }
    nodeData->rangeStart = rangeStart;
    nodeData->rangeEnd = rangeEnd;

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created literal-range node: %s%c-%c%s", NTCOLOR(HIGHLIGHT), rangeStart, rangeEnd, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common to literals and literal-range nodes
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    if (literal != '\\') return literal;

    literal = ((*in_out_rule)++)[0];
    if (literal == 0) {
        NERROR("NCC", "getEscapedLiteral(): escape character %s\\%s not followed by anything", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Just return as is,
    return literal;
}

static struct NCC_Node* handleLiteral(struct NCC_Node* parentNode, const char** in_out_rule) {

    char literal = unescapeLiteral(in_out_rule);
    if (!literal) return 0;

    // Check if this was a literal range,
    struct NCC_Node* node;
    char followingLiteral = **in_out_rule;
    if (followingLiteral == '-') {
        (*in_out_rule)++;
        if (isReserved(**in_out_rule)) {
            NERROR("NCC", "handleLiteral(): A '%s-%s' can't be followed by an unescaped '%s%c%s'", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), **in_out_rule, NTCOLOR(STREAM_DEFAULT));
            return 0;
        }
        if (!(followingLiteral = unescapeLiteral(in_out_rule))) return 0;
        node = createLiteralRangeNode(literal, followingLiteral);
    } else {

        if (parentNode->type == NCC_NodeType.LITERALS) {
            // Just append to parent,
            struct LiteralsNodeData* nodeData = parentNode->data;
            NString.append(&nodeData->literals, "%c", literal);

            #ifdef NCC_VERBOSE
            NLOGI("NCC", "Appended to literals node: %s%c%s", NTCOLOR(HIGHLIGHT), literal, NTCOLOR(STREAM_DEFAULT));
            #endif
            return parentNode;
        }

        // Parent is not of literals type. Create a new node,
        char literalString[2] = {literal, 0};
        node = createLiteralsNode(literalString);
    }

    parentNode->setNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Or node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct OrNodeData {
    struct NCC_Node* rhsTree;
    struct NCC_Node* lhsTree;
};

static int32_t orNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct OrNodeData* nodeData = node->data;

    // Match the sides on temporary routes,
    // Right hand side,
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);
    uint32_t rhsRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  rhsMatchLength = nodeData->rhsTree->match(nodeData->rhsTree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    // Left hand side,
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute2);
    uint32_t lhsRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  lhsMatchLength = nodeData->lhsTree->match(nodeData->lhsTree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute2);

    // If neither right or left matches,
    if ((rhsMatchLength==-1) && (lhsMatchLength==-1)) return -1;

    // If we needn't check the following tree twice,
    if ((rhsMatchLength==lhsMatchLength) ||
        (rhsMatchLength==-1) ||
        (lhsMatchLength==-1)) {

        int32_t matchLength = rhsMatchLength > lhsMatchLength ? rhsMatchLength : lhsMatchLength;
        int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
        if (nextNodeMatchLength==-1) {
            NByteVector.resize(ncc->tempRoute1, rhsRouteMark); // Discard RHS.
            NByteVector.resize(ncc->tempRoute2, lhsRouteMark); // Discard LHS.
            return -1;
        }

        // Push the correct temporary route,
        if (rhsMatchLength > lhsMatchLength) {
            pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, rhsRouteMark);
            NByteVector.resize(ncc->tempRoute2, lhsRouteMark); // Discard LHS.
        } else {
            pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute2, lhsRouteMark);
            NByteVector.resize(ncc->tempRoute1, rhsRouteMark); // Discard RHS.
        }
        return matchLength + nextNodeMatchLength;
    }

    // RHS and LHS match lengths are not the same. To maximize the overall match length, we have
    // to take the rest of the tree into account by matching at both right and left lengths,

    // Right hand side,
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute3);
    uint32_t rhsTreeRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  rhsTreeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[rhsMatchLength]);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute3);

    // Left hand side,
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute4);
    uint32_t lhsTreeRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  lhsTreeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[lhsMatchLength]);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute4);

    // If neither right or left trees match,
    if ((rhsTreeMatchLength==-1) && (lhsTreeMatchLength==-1)) {
        NByteVector.resize(ncc->tempRoute1, rhsRouteMark); // Discard RHS.
        NByteVector.resize(ncc->tempRoute2, lhsRouteMark); // Discard LHS.
        return -1;
    }

    // Get the final match lengths,
    rhsMatchLength += rhsTreeMatchLength;
    if (rhsTreeMatchLength==-1) rhsMatchLength = -1;
    lhsMatchLength += lhsTreeMatchLength;
    if (lhsTreeMatchLength==-1) lhsMatchLength = -1;

    // Push the correct temporary routes,
    int32_t matchLength;
    if (rhsMatchLength > lhsMatchLength) {
        matchLength = rhsMatchLength;
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute3, rhsTreeRouteMark);
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, rhsRouteMark);
        NByteVector.resize(ncc->tempRoute4, lhsTreeRouteMark);
        NByteVector.resize(ncc->tempRoute2, lhsRouteMark);
    } else {
        matchLength = lhsMatchLength;
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute4, lhsTreeRouteMark);
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute2, lhsRouteMark);
        NByteVector.resize(ncc->tempRoute3, rhsTreeRouteMark);
        NByteVector.resize(ncc->tempRoute1, rhsRouteMark);
    }

    // Or nodes don't get pushed into the route,
    return matchLength;
}

static void orNodeDeleteTree(struct NCC_Node* tree) {
    struct OrNodeData* nodeData = tree->data;
    nodeData->rhsTree->deleteTree(nodeData->rhsTree);
    nodeData->lhsTree->deleteTree(nodeData->lhsTree);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.orNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.orNodeDeleteTree() tree"      );
}

static struct NCC_Node* createOrNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // If the parent node is a literals node with more than one literal, break the last literal apart so that it's
    // the only literal matched in the or,
    parentNode = breakLastLiteralIfNeeded(parentNode);

    // Get grand-parent node,
    struct NCC_Node* grandParentNode = parentNode->getPreviousNode(parentNode);
    if (!grandParentNode) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    struct OrNodeData* nodeData = NMALLOC(sizeof(struct OrNodeData), "NCC.createOrNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData, orNodeMatch);
    node->deleteTree = orNodeDeleteTree;

    // Remove parent from the grand-parent and attach this node instead,
    grandParentNode->setNextNode(grandParentNode, node);

    // Turn parent node into a tree and attach it as the lhs node,
    nodeData->lhsTree = createRootNode();
    nodeData->lhsTree->setNextNode(nodeData->lhsTree, parentNode);
    createAcceptNode(parentNode);

    // Create a new tree for the next node and set it as the rhs,
    nodeData->rhsTree = createRootNode();
    const char* remainingSubRule = ++(*in_out_rule); // Skip the '|'.
    if (!**in_out_rule) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the end of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }
    struct NCC_Node* rhsNode = getNextNode(ncc, nodeData->rhsTree, in_out_rule);
    if (!rhsNode) {
        NERROR("NCC", "createOrNode(): couldn't create an rhs node: %s%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }
    createAcceptNode(rhsNode);

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created or node: %s|%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sub-rule node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SubRuleNodeData {
    struct NCC_Node* subRuleTree;
};

static int32_t subRuleNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct SubRuleNodeData* nodeData = node->data;

    // Match sub-rule on a temporary route,
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);
    uint32_t tempRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  matchLength = nodeData->subRuleTree->match(nodeData->subRuleTree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    if (matchLength==-1) return -1;

    // Match next node,
    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    if (nextNodeMatchLength==-1) {
        NByteVector.resize(ncc->tempRoute1, tempRouteMark);
        return -1;
    }

    // Push the sub-rule route,
    pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, tempRouteMark);
    return matchLength + nextNodeMatchLength;
}

static void subRuleNodeDeleteTree(struct NCC_Node* tree) {
    struct SubRuleNodeData* nodeData = tree->data;
    nodeData->subRuleTree->deleteTree(nodeData->subRuleTree);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.subRuleNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.subRuleNodeDeleteTree() tree"      );
}

static struct NCC_Node* createSubRuleNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the '{'.
    const char* subRuleBeginning = (*in_out_rule)++;

    // Find the matching closing braces,
    int32_t closingBracesRequired=1;
    boolean subRuleComplete=False;
    int32_t subRuleLength=0, spacesCount=0;
    do {
        char currentChar = (*in_out_rule)[subRuleLength];
        if (currentChar=='{') {
            closingBracesRequired++;
        } else if (currentChar=='}') {
            if (!--closingBracesRequired) {
                subRuleComplete = True;
                break;
            }
        } else if (currentChar == ' ') {
            spacesCount++;
        } else if (!currentChar) {
            break;
        }
        subRuleLength++;
    } while (True);

    // Make sure the sub-rule is well-formed,
    if ((subRuleLength-spacesCount) == 0) {
        NERROR("NCC", "createSubRuleNode(): can't have empty sub-rules %s{}%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }
    if (!subRuleComplete) {
        NERROR("NCC", "createSubRuleNode(): couldn't find a matching %s}%s in %s%s%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), subRuleBeginning, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Copy the sub-rule (because the rule text is const char*, can't substitute a zero wherever I need),
    char* subRule = NMALLOC(subRuleLength+1, "NCC.createSubRuleNode() subRule");
    NSystemUtils.memcpy(subRule, *in_out_rule, subRuleLength);
    subRule[subRuleLength] = 0;        // Terminate the string.
    (*in_out_rule) += subRuleLength+1; // Advance the rule pointer to right after the closing brace.

    // Create sub-rule tree,
    struct NCC_Node* subRuleTree = constructRuleTree(ncc, subRule);
    NFREE(subRule, "NCC.createSubRuleNode() subRule");
    if (!subRuleTree) {
        NERROR("NCC", "createSubRuleNode(): couldn't create sub-rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), subRuleBeginning, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create the sub-rule node,
    struct SubRuleNodeData* nodeData = NMALLOC(sizeof(struct SubRuleNodeData), "NCC.createSubRuleNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUB_RULE, nodeData, subRuleNodeMatch);
    node->deleteTree = subRuleNodeDeleteTree;
    nodeData->subRuleTree = subRuleTree;

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created sub-rule node: %s{%s}%s", NTCOLOR(HIGHLIGHT), subRule, NTCOLOR(STREAM_DEFAULT));
    #endif
    parentNode->setNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Repeat node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RepeatNodeData {
    struct NCC_Node* repeatedNode;
    struct NCC_Node* followingSubRule;
};

static int32_t repeatNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct RepeatNodeData* nodeData = node->data;

    // Check if the following sub-rule matches,
    uint32_t matchRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, text);
    if (followingSubRuleMatchLength>0) return followingSubRuleMatchLength;

    // TODO: if following subrule is the end of this sub-rule, we should check the next of the current sub-rule, which
    // should be kept in a separate stack? This should sometimes terminate the repeats earlier than expected. Should
    // we do it at all? Maybe do it conditional, or using another operator (something other than the *) ?

    // Following sub-rule didn't match, attempt repeating (on the temporary route),
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);
    uint32_t tempRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  matchLength = nodeData->repeatedNode->match(nodeData->repeatedNode, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    if (matchLength<1) {
        // Nothing matched, discard,
        NByteVector.resize(ncc->tempRoute1, tempRouteMark);
        return followingSubRuleMatchLength;
    }

    // Something matched, attempt repeating. Discard any matches that could have been added by the following sub-rule,
    // TODO: If "Discarding!" never gets printed, remove...
    if (NByteVector.size(ncc->matchRoute) != matchRouteMark) {
        NLOGI("sdf", "Discarding!");
        //NLOGW("sdf", "followingSubRuleMatchLength: %d, size(ncc->matchRoute): %d, matchRouteMark: %d", followingSubRuleMatchLength, NByteVector.size(ncc->matchRoute), matchRouteMark);
        NByteVector.resize(ncc->matchRoute, matchRouteMark);
    }

    // Repeat,
    int32_t repeatMatchLength = repeatNodeMatch(node, ncc, &text[matchLength]);
    if (repeatMatchLength==-1) {
        // Didn't end properly, discard,
        NByteVector.resize(ncc->tempRoute1, tempRouteMark);
        return -1;
    }

    // Push the repeated node route,
    pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, tempRouteMark);
    return matchLength + repeatMatchLength;
}

static void repeatNodeDeleteTree(struct NCC_Node* tree) {
    struct RepeatNodeData* nodeData = tree->data;
    nodeData->repeatedNode->deleteTree(nodeData->repeatedNode);
    nodeData->followingSubRule->deleteTree(nodeData->followingSubRule);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.repeatNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.repeatNodeDeleteTree() tree"      );
}

static struct NCC_Node* createRepeatNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // If the parent node is a literals node with more than one literal, break the last literal apart so that it's
    // the only literal repeated,
    parentNode = breakLastLiteralIfNeeded(parentNode);

    // Get grand-parent node,
    struct NCC_Node* grandParentNode = parentNode->getPreviousNode(parentNode);
    if (!grandParentNode) {
        NERROR("NCC", "createRepeatNode(): %s^%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Parse the ^ expression,
    char repeatCount = (++(*in_out_rule))[0];
    if (repeatCount != '*') {
        NERROR("NCC", "createRepeatNode(): expecting %s*%s after %s^%s, found %s%c%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), repeatCount, NTCOLOR(STREAM_DEFAULT));
        return 0;
    } else {
        // Skip the *,
        (*in_out_rule)++;
    }

    // Create node,
    struct RepeatNodeData* nodeData = NMALLOC(sizeof(struct RepeatNodeData), "NCC.createRepeatNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.REPEAT, nodeData, repeatNodeMatch);
    node->deleteTree = repeatNodeDeleteTree;

    // Remove parent from the grand-parent and attach this node instead,
    grandParentNode->setNextNode(grandParentNode, node);

    // Turn parent node into a tree and attach it as the repeated node,
    nodeData->repeatedNode = createRootNode();
    nodeData->repeatedNode->setNextNode(nodeData->repeatedNode, parentNode);
    createAcceptNode(parentNode);

    // Create a new tree for the remaining text and set it as the following sub-rule,
    nodeData->followingSubRule = constructRuleTree(ncc, *in_out_rule);
    if (!nodeData->followingSubRule) {
        NERROR("NCC", "createRepeatNode(): Couldn't create the following sub-rule tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
        return 0;  // Since this node is already attached to the tree, it gets cleaned up automatically.
    }

    // The remainder of the tree was already added to the following sub-rule, no need to continue parsing,
    while (**in_out_rule) (*in_out_rule)++;

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created repeat node: %s^*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Anything node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct AnythingNodeData {
    struct NCC_Node* followingSubRule;
};

static int32_t anythingNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct AnythingNodeData* nodeData = node->data;

    int32_t  totalMatchLength=0;
    int32_t  followingSubRuleMatchLength;
    do {
        // Check if the following sub-rule matches,
        uint32_t matchRouteMark = NByteVector.size(ncc->matchRoute);
        followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, &text[totalMatchLength]);
        if (followingSubRuleMatchLength>0) goto conclude;

        // Following sub-rule didn't match, or had a zero-length match,
        NByteVector.resize(ncc->matchRoute, matchRouteMark);

        // If text ended,
        if (!text[totalMatchLength]) {
            if (followingSubRuleMatchLength==-1) return -1;
            goto conclude;
        }

        // Text didn't end, advance,
        totalMatchLength++;
    } while (True);

conclude:
    skipNLiterals(ncc, totalMatchLength);
    return totalMatchLength + followingSubRuleMatchLength;
}

static void anythingNodeDeleteTree(struct NCC_Node* tree) {
    struct AnythingNodeData* nodeData = tree->data;
    nodeData->followingSubRule->deleteTree(nodeData->followingSubRule);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.anythingNodeDeleteTree() tree->data");
    NFREE(tree, "NCC.anythingNodeDeleteTree() tree");
}

static struct NCC_Node* createAnythingNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the *,
    (*in_out_rule)++;

    // Create a new tree for the remaining text and set it as the following sub-rule,
    struct NCC_Node* followingSubRule = constructRuleTree(ncc, *in_out_rule);
    if (!followingSubRule) {
        NERROR("NCC", "createAnythingNode(): Couldn't create the remaining sub-rule tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    struct AnythingNodeData* nodeData = NMALLOC(sizeof(struct AnythingNodeData), "NCC.createAnythingNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ANYTHING, nodeData, anythingNodeMatch);
    node->deleteTree = anythingNodeDeleteTree;
    nodeData->followingSubRule = followingSubRule;

    // The remainder of the tree was already added to the following sub-rule, no need to continue parsing,
    while (**in_out_rule) (*in_out_rule)++;

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created anything node: %s*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
    #endif
    parentNode->setNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Substitute node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SubstituteNodeData {
    struct NCC_Rule* rule;
};

static int32_t substituteNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct SubstituteNodeData *nodeData = node->data;

    // Match rule on a temporary route,
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);
    uint32_t tempRouteMark = NByteVector.size(ncc->matchRoute);
    int32_t  matchLength = nodeData->rule->tree->match(nodeData->rule->tree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    // Return immediately if no match,
    if (matchLength==-1) return -1;

    // Match next nodes,
    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    if (nextNodeMatchLength==-1) {
        NByteVector.resize(ncc->tempRoute1, tempRouteMark);
        return -1;
    }

    // Check if pushing this node is useful,
    if (nodeData->rule->pushVariable || nodeData->rule->onMatchListener || nodeData->rule->popsChildrenVariables) {

        // Push 0 to mark the end of the rule route,
        NByteVector.pushBack(ncc->matchRoute, 0);

        // Push the rule route and this node's index,
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, tempRouteMark);
        NByteVector.pushBackBulk(ncc->matchRoute, &nodeData->rule->index, ncc->ruleIndexSizeBytes);

        // Push 255 to denote the preceding substitute node index,
        NByteVector.pushBack(ncc->matchRoute, 255);

    } else {
        // Push the rule route only,
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, tempRouteMark);
    }

    return matchLength + nextNodeMatchLength;
}

static int32_t substituteNodeFollowMatchRoute(struct NCC_Rule* rule, struct NCC* ncc, const char* text) {

    // Remember the variables stack position,
    uint32_t variablesStackPosition = NVector.size(&ncc->variables);

    // Match,
    uint8_t nextValue = NByteVector.get(ncc->matchRoute, NByteVector.size(ncc->matchRoute)-1);
    int32_t matchLength;
    if (nextValue) {
        matchLength = followMatchRoute(ncc, text);
    } else {
        // Found a 0 (this substitute node can by empty),
        matchLength = 0;
        NByteVector.popBack(ncc->matchRoute, &nextValue); // Discard the 0.
    }

    // Perform rule action,
    ncc->currentCallStackBeginning = variablesStackPosition;
    uint32_t newVariablesCount = NVector.size(&ncc->variables) - variablesStackPosition;
    if (rule->onMatchListener) rule->onMatchListener(ncc, &rule->name, newVariablesCount);

    // Pop any variables that were not popped,
    if (rule->popsChildrenVariables) {
        int32_t remainingVariablesCount = NVector.size(&ncc->variables) - variablesStackPosition;
        for (;remainingVariablesCount; remainingVariablesCount--) {
            // Possible performance improvement: Destroying variables in place without popping, then
            // adjusting the vector size. Or even better, reusing variables.
            struct NCC_Variable currentVariable;
            NVector.popBack(&ncc->variables, &currentVariable);
            NCC_destroyVariable(&currentVariable);
        }
    }

    // Save the match,
    struct NCC_Variable match;
    if (rule->pushVariable) {
        char *matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeFollowMatchRoute() matchedText");
        NSystemUtils.memcpy(matchedText, text, matchLength);
        matchedText[matchLength] = 0;

        NCC_initializeVariable(&match, NString.get(&rule->name), matchedText);
        NFREE(matchedText, "NCC.substituteNodeFollowMatchRoute() matchedText");
        NVector.pushBack(&ncc->variables, &match);
    }

    // Follow next nodes,
    #ifdef NCC_VERBOSE
        if (rule->pushVariable) {
            NLOGI("NCC", "Visited substitute node %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), match.name, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&match.value), NTCOLOR(STREAM_DEFAULT));
        } else {
            char *matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeFollowMatchRoute() matchedText");
            NSystemUtils.memcpy(matchedText, text, matchLength);
            matchedText[matchLength] = 0;
            NLOGI("NCC", "Visited substitute node %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&rule->name), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchedText, NTCOLOR(STREAM_DEFAULT));
            NFREE(matchedText, "NCC.substituteNodeFollowMatchRoute() matchedText");
        }
    #endif
    return matchLength;
}

static void substituteNodeDeleteTree(struct NCC_Node* tree) {
    // Note: we don't free the rules, we didn't allocate them.
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NFREE(tree->data, "NCC.substituteNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.substituteNodeDeleteTree() tree"      );
}

static struct NCC_Node* createSubstituteNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the '$'.
    const char* ruleBeginning = (*in_out_rule)++;

    // Skip the '{',
    if (*((*in_out_rule)++) != '{') {
        NERROR("NCC", "createSubstituteNode(): unescaped %s$%ss must be followed by %s{%ss", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Find the matching closing braces,
    const char* ruleNameBeginning = *in_out_rule;
    int32_t ruleNameLength=0;
    do {
        char currentChar = *((*in_out_rule)++);
        if (currentChar=='}') break;
        if (!currentChar) {
            NERROR("NCC", "createSubstituteNode(): couldn't find a matching %s}%s in %s%s%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleBeginning, NTCOLOR(STREAM_DEFAULT));
            return 0;
        }
        ruleNameLength++;
    } while(True);

    // Extract rule name,
    char *ruleName = NMALLOC(ruleNameLength+1, "NCC.createSubstituteNode() ruleName");
    NSystemUtils.memcpy(ruleName, ruleNameBeginning, ruleNameLength);
    ruleName[ruleNameLength] = 0;

    // Look for a match within our defined rules,
    struct NCC_Rule* rule = getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "createSubstituteNode(): couldn't find a rule named: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        NFREE(ruleName, "NCC.createSubstituteNode() ruleName 1");
        return 0;
    }

    // Create the node,
    struct SubstituteNodeData* nodeData = NMALLOC(sizeof(struct SubstituteNodeData), "NCC.createSubstituteNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUBSTITUTE, nodeData, substituteNodeMatch);
    node->deleteTree = substituteNodeDeleteTree;
    nodeData->rule = rule;

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created substitute node: %s${%s}%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
    #endif

    NFREE(ruleName, "NCC.createSubstituteNode() ruleName 2");
    parentNode->setNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NCC
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static struct NCC_Node* constructRuleTree(struct NCC* ncc, const char* rule) {

    struct NCC_Node* rootNode = createRootNode();

    struct NCC_Node* currentNode = rootNode;
    const char* remainingSubRule = rule;
    int32_t errorsBeginning = NError.observeErrors();
    do {
        currentNode = getNextNode(ncc, currentNode, &remainingSubRule);
        if (!currentNode || NError.observeErrors()>errorsBeginning) break;
        if (currentNode->type == NCC_NodeType.ACCEPT) return rootNode;
    } while (True);

    // Failed,
    rootNode->deleteTree(rootNode);
    return 0;
}

static struct NCC_Node* getNextNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    char currentChar;
    while ((currentChar = **in_out_rule) == ' ') (*in_out_rule)++;

    switch (currentChar) {
        case   0: return createAcceptNode(parentNode);
        case '$': return createSubstituteNode(ncc, parentNode, in_out_rule);
        case '*': return createAnythingNode(ncc, parentNode, in_out_rule);
        case '{': return createSubRuleNode(ncc, parentNode, in_out_rule);
        case '^': return createRepeatNode(ncc, parentNode, in_out_rule);
        case '|': return createOrNode(ncc, parentNode, in_out_rule);
        case '-':
            NERROR("NCC", "getNextNode(): a '%s-%s' must always be preceded by a literal", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        default: return handleLiteral(parentNode, in_out_rule);
    }
}

struct NCC* NCC_initializeNCC(struct NCC* ncc) {
    ncc->extraData = 0;
    NVector.initialize(&ncc->rules, 0, sizeof(struct NCC_Rule*));
    NVector.initialize(&ncc->variables, 0, sizeof(struct NCC_Variable));
    ncc->matchRoute = NByteVector.create(0);
    ncc->tempRoute1 = NByteVector.create(0);
    ncc->tempRoute2 = NByteVector.create(0);
    ncc->tempRoute3 = NByteVector.create(0);
    ncc->tempRoute4 = NByteVector.create(0);
    return ncc;
}

struct NCC* NCC_createNCC() {
    struct NCC* ncc = NMALLOC(sizeof(struct NCC), "NCC.NCC_createNCC() ncc");
    return NCC_initializeNCC(ncc);
}

void NCC_destroyNCC(struct NCC* ncc) {

    // Rules,
    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) destroyAndFreeRule(*((struct NCC_Rule**) NVector.get(&ncc->rules, i)));
    NVector.destroy(&ncc->rules);

    // Variables,
    for (int32_t i=NVector.size(&ncc->variables)-1; i>=0; i--)
        NCC_destroyVariable(NVector.get(&ncc->variables, i));
    NVector.destroy(&ncc->variables);

    // Routes,
    NByteVector.destroyAndFree(ncc->matchRoute);
    NByteVector.destroyAndFree(ncc->tempRoute1);
    NByteVector.destroyAndFree(ncc->tempRoute2);
    NByteVector.destroyAndFree(ncc->tempRoute3);
    NByteVector.destroyAndFree(ncc->tempRoute4);
}

void NCC_destroyAndFreeNCC(struct NCC* ncc) {
    NCC_destroyNCC(ncc);
    NFREE(ncc, "NCC.NCC_destroyAndFreeNCC() ncc");
}

boolean NCC_addRule(struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_onMatchListener onMatchListener, boolean rootRule, boolean pushVariable, boolean popsChildrenVariables) {

    // Check if a rule with this name already exists,
    if (getRule(ncc, ruleName)) {
        NERROR("NCC", "NCC_addRule(): unable to create rule %s%s%s. A rule with the same name exists.", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    struct NCC_Rule* rule = createRule(ncc, ruleName, ruleText, onMatchListener, rootRule, pushVariable, popsChildrenVariables);
    if (!rule) {
        NERROR("NCC", "NCC_addRule(): unable to create rule %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Set the rule index and the rule index size,
    rule->index = NVector.size(&ncc->rules);
    if (rule->index < 256) {
        ncc->ruleIndexSizeBytes = 1;
    } else if (rule->index < 65536) {
        ncc->ruleIndexSizeBytes = 2;
    } else if (rule->index < 16777216) {
        ncc->ruleIndexSizeBytes = 3;
    } else {
        ncc->ruleIndexSizeBytes = 4;
    }

    NVector.pushBack(&ncc->rules, &rule);
    return True;
}

boolean NCC_updateRule(struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_onMatchListener onMatchListener, boolean rootRule, boolean pushVariable, boolean popsChildrenVariables) {

    struct NCC_Rule* rule = getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "NCC_updateRule(): unable to update rule %s%s%s. Rule doesn't exist.", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Create new rule tree,
    struct NCC_Node* ruleTree = constructRuleTree(ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "NCC_updateRule(): unable to construct rule tree: %s%s%s. Failed to update rule: %s%s%s.", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Dispose of old rule-tree,
    rule->tree->deleteTree(rule->tree);

    // Reinitialize rule,
    initializeRule(rule, ruleName, ruleTree, onMatchListener, rootRule, pushVariable, popsChildrenVariables);

    return True;
}

static struct NCC_Rule* getRule(struct NCC* ncc, const char* ruleName) {

    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {
        struct NCC_Rule* currentRule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        if (NCString.equals(ruleName, NString.get(&currentRule->name))) return currentRule;
    }
    return 0;
}

static void switchRoutes(struct NByteVector** route1, struct NByteVector** route2) {
    struct NByteVector* temp = *route1;
    *route1 = *route2;
    *route2 = temp;
}

static void pushTempRouteIntoMatchRoute(struct NCC* ncc, struct NByteVector* tempRoute, int32_t tempRouteMark) {

    // Effectively pops bytes from the temp route until the mark then pushes them into the match route.
    int32_t tempRouteSize = NByteVector.size(tempRoute);
    /*
    // TODO: fix combining!
    int32_t newBytesCount = tempRouteSize - tempRouteMark;
    if (!newBytesCount) return;

    // Combine until no more combination is possible,
    int32_t remainingBytesStartIndex = tempRouteMark;
    while (remainingBytesStartIndex < tempRouteSize) {
        uint8_t currentValue = NByteVector.get(tempRoute, remainingBytesStartIndex);
        if ((currentValue==0) || (currentValue==255)) break;
        remainingBytesStartIndex++;
        if (!skipNLiterals(ncc, currentValue)) break;
    }
    */

    int32_t remainingBytesStartIndex = tempRouteMark;

    // Move the rest,
    int32_t remainingBytesCount = tempRouteSize - remainingBytesStartIndex;
    if (!remainingBytesCount) return;
    int32_t currentMatchRoutePosition = NByteVector.size(ncc->matchRoute);
    NByteVector.resize(ncc->matchRoute, currentMatchRoutePosition + remainingBytesCount);
    NSystemUtils.memcpy(
            &ncc->matchRoute->objects[currentMatchRoutePosition],
            &tempRoute->objects[remainingBytesStartIndex],
            remainingBytesCount);
    NByteVector.resize(tempRoute, tempRouteMark);
}

int32_t NCC_match(struct NCC* ncc, const char* text) {

    // Find the longest match,
    int32_t maxMatchLength=-1;
    struct NCC_Rule* maxMatchRule=0;
    struct NByteVector* maxMatchRoute = NByteVector.create(0);

    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {

        struct NCC_Rule* rule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        if (!rule->rootRule) continue;

        // Reset routes,
        NByteVector.clear(ncc->matchRoute);
        NByteVector.clear(ncc->tempRoute1);
        NByteVector.clear(ncc->tempRoute2);
        NByteVector.clear(ncc->tempRoute3);
        NByteVector.clear(ncc->tempRoute4);

        // Match rule,
        int32_t matchLength = rule->tree->match(rule->tree, ncc, text);
        if (matchLength > maxMatchLength) {
            // Keep route,
            maxMatchLength = matchLength;
            maxMatchRule = rule;
            NByteVector.resize(maxMatchRoute, NByteVector.size(ncc->matchRoute));
            NSystemUtils.memcpy(
                    maxMatchRoute->objects,
                    ncc->matchRoute->objects,
                    NByteVector.size(ncc->matchRoute));
        }
    }

    if (maxMatchLength==-1) goto conclude;

    // Follow the longest match route,
    if (NByteVector.size(maxMatchRoute)) {

        // Follow the max match route,
        NByteVector.clear(ncc->matchRoute);
        switchRoutes(&ncc->matchRoute, &maxMatchRoute);
        followMatchRoute(ncc, text);
        switchRoutes(&ncc->matchRoute, &maxMatchRoute);
    }

    conclude:

    // Perform rule action,
    ncc->currentCallStackBeginning = 0;
    if (maxMatchRule && maxMatchRule->onMatchListener) maxMatchRule->onMatchListener(ncc, &maxMatchRule->name, NVector.size(&ncc->variables));

    // Empty the variables stack,
    for (int32_t i=NVector.size(&ncc->variables)-1; i>=0; i--) NCC_destroyVariable(NVector.get(&ncc->variables, i));
    NVector.clear(&ncc->variables);

    // Destroy the max match route,
    NByteVector.destroyAndFree(maxMatchRoute);

    return maxMatchLength;
}

boolean NCC_popRuleVariable(struct NCC* ncc, struct NCC_Variable* outVariable) {

    if (NVector.size(&ncc->variables) <= ncc->currentCallStackBeginning) return False;

    // The variable needn't be initialized, we'll pop into it,
    if (!NVector.popBack(&ncc->variables, outVariable)) return False;

    return True;
}

boolean NCC_getRuleVariable(struct NCC* ncc, uint32_t index, struct NCC_Variable* outVariable) {

    int64_t availableVariablesCount = (int64_t) NVector.size(&ncc->variables) - ncc->currentCallStackBeginning;
    if (index >= availableVariablesCount) return False;

    // The variable needn't be initialized, we'll pop into it,
    *outVariable = *(struct NCC_Variable*) NVector.get(&ncc->variables, ncc->currentCallStackBeginning + index);

    return True;
}

void NCC_discardRuleVariables(struct NCC* ncc) {
    struct NCC_Variable variable;
    while (NCC_popRuleVariable(ncc, &variable)) NCC_destroyVariable(&variable);
}
