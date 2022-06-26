#include <NCC.h>

#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>

#ifndef NCC_VERBOSE
#define NCC_VERBOSE 0
#endif

//
// Operation:
//   First, we construct our rules. Then, we match, firing listeners as we proceed.
//     - Create node listener: Given the rule, construct your AST node and return it.
//     - Delete node listener: The node created in the previous step is not final. It could be rolled back.
//                             Be ready to do so if this listener is fired.
//     - Match       listener: Once the node and its children are constructed, this is fired. At this point,
//                             you may inspect the node and decide if you'll accept this match.
//

// TODO: bytes indicating skipped literals should be combined together. The code is there, just commented
// out because it's not working properly. Most probably it's because we can't discard stack history if it
// was combined with what came before. A suggested fix is to push a delimiter after switching stacks to
// prevent combining? Pushing and discarding the delimiter should be carefully woven together with the
// stack switching calls.

static struct NCC_Node* constructRuleTree(struct NCC* ncc, const char* rule);
static struct NCC_Node* getNextNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule);
static struct NCC_Rule* getRule(struct NCC* ncc, const char* ruleName);
static void discardTree(struct NCC_MatchingResult* tree, struct NCC_ASTNode_Data* parentNode);
static boolean matchTree(
        struct NCC* ncc, struct NCC_Node* tree, const char* text, struct NCC_MatchingResult* matchingResult,
        int32_t lengthToAddIfTerminated, struct NCC_MatchingResult** astsToDeleteIfTerminated, int32_t astsToDeleteCount);

#define COMMA ,
#define MatchTree(treeName, treeNode, parentNode, text, lengthToAddIfTerminated, deleteList, deleteCount) \
    struct NCC_MatchingResult treeName; \
    treeName.astNode = (parentNode); \
    boolean treeName ## Matched = matchTree( \
            ncc, treeNode, text, &treeName, \
            lengthToAddIfTerminated, (struct NCC_MatchingResult*[]) deleteList, deleteCount); \
    if (treeName.terminate) { \
        *matchingResult = treeName; \
        return treeName ## Matched; \
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_NodeType {
    int32_t ROOT, ACCEPT, LITERALS, OR, LITERAL_RANGE, REPEAT, SUB_RULE, SUBSTITUTE, ANYTHING;
};
const struct NCC_NodeType NCC_NodeType = {
    .ROOT = 0,
    .ACCEPT = 1,
    .LITERALS = 2,
    .LITERAL_RANGE = 3,
    .OR = 4,
    .SUB_RULE = 5,
    .REPEAT = 6,
    .ANYTHING = 7,
    .SUBSTITUTE = 8
};

struct NCC_Node {
    int32_t type;
    void *data;
    struct NCC_Node* previousNode;
    struct NCC_Node*     nextNode;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void             genericSetPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode);
static void             genericSetNextNode    (struct NCC_Node* node, struct NCC_Node*     nextNode);
static struct NCC_Node* genericGetPreviousNode(struct NCC_Node* node);
static struct NCC_Node* genericGetNextNode    (struct NCC_Node* node);

static void genericDeleteTreeNoData  (struct NCC_Node* tree);
static void genericDeleteTreeWithData(struct NCC_Node* tree);

static void    rootNodeSetPreviousNode (struct NCC_Node* node, struct NCC_Node* previousNode);
static boolean rootNodeMatch           (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);

static void    acceptNodeSetNextNode   (struct NCC_Node* node, struct NCC_Node* nextNode);
static boolean acceptNodeMatch         (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);

static boolean literalsNodeMatch       (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
static void    literalsNodeDeleteTree  (struct NCC_Node* tree);

static boolean literalRangeNodeMatch   (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);

static boolean orNodeMatch             (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
static void    orNodeDeleteTree        (struct NCC_Node* tree);

static boolean subRuleNodeMatch        (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
static void    subRuleNodeDeleteTree   (struct NCC_Node* tree);

static boolean repeatNodeMatch         (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
static void    repeatNodeDeleteTree    (struct NCC_Node* tree);

static boolean anythingNodeMatch       (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
static void    anythingNodeDeleteTree  (struct NCC_Node* tree);

static boolean substituteNodeMatch     (struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
static void    substituteNodeDeleteTree(struct NCC_Node* tree);

typedef boolean          (*NCC_Node_match          )(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult);
typedef void             (*NCC_Node_setPreviousNode)(struct NCC_Node* node, struct NCC_Node* previousNode);
typedef void             (*NCC_Node_setNextNode    )(struct NCC_Node* node, struct NCC_Node*     nextNode);
typedef struct NCC_Node* (*NCC_Node_getPreviousNode)(struct NCC_Node* node);
typedef struct NCC_Node* (*NCC_Node_getNextNode    )(struct NCC_Node* node);
typedef void             (*NCC_Node_deleteTree     )(struct NCC_Node* tree);

//                                                       Root                     Accept                   Literals                Literals range             Or                      Sub-rule                Repeat                  Anything                Substitute
static NCC_Node_match           nodeMatch          [] = {rootNodeMatch          , acceptNodeMatch        , literalsNodeMatch     , literalRangeNodeMatch    , orNodeMatch           , subRuleNodeMatch      , repeatNodeMatch       , anythingNodeMatch     , substituteNodeMatch     };
static NCC_Node_setPreviousNode nodeSetPreviousNode[] = {rootNodeSetPreviousNode, genericSetPreviousNode , genericSetPreviousNode, genericSetPreviousNode   , genericSetPreviousNode, genericSetPreviousNode, genericSetPreviousNode, genericSetPreviousNode, genericSetPreviousNode  };
static NCC_Node_setNextNode     nodeSetNextNode    [] = {genericSetNextNode     , acceptNodeSetNextNode  , genericSetNextNode    , genericSetNextNode       , genericSetNextNode    , genericSetNextNode    , genericSetNextNode    , genericSetNextNode    , genericSetNextNode      };
static NCC_Node_getPreviousNode nodeGetPreviousNode[] = {genericGetPreviousNode , genericGetPreviousNode , genericGetPreviousNode, genericGetPreviousNode   , genericGetPreviousNode, genericGetPreviousNode, genericGetPreviousNode, genericGetPreviousNode, genericGetPreviousNode  };
static NCC_Node_getNextNode     nodeGetNextNode    [] = {genericGetNextNode     , genericGetNextNode     , genericGetNextNode    , genericGetNextNode       , genericGetNextNode    , genericGetNextNode    , genericGetNextNode    , genericGetNextNode    , genericGetNextNode      };
static NCC_Node_deleteTree      nodeDeleteTree     [] = {genericDeleteTreeNoData, genericDeleteTreeNoData, literalsNodeDeleteTree, genericDeleteTreeWithData, orNodeDeleteTree      , subRuleNodeDeleteTree , repeatNodeDeleteTree  , anythingNodeDeleteTree, substituteNodeDeleteTree};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rule
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Rule {
    struct NCC_RuleData data;
    struct NCC_Node* tree;
    uint32_t index;
};

static struct NCC_RuleData* ruleDataSet(struct NCC_RuleData* ruleData, const char* ruleName, const char* ruleText,  boolean isRoot) {
    NString.set(&ruleData->ruleName, "%s", ruleName);
    NString.set(&ruleData->ruleText, "%s", ruleText);
    ruleData->isRoot = isRoot;
    return ruleData;
}

static struct NCC_RuleData* ruleDataSetListeners(struct NCC_RuleData* ruleData, NCC_createNodeListener createNodeListener, NCC_deleteNodeListener deleteNodeListener, NCC_matchListener matchListener) {
    ruleData->createNodeListener = createNodeListener;
    ruleData->deleteNodeListener = deleteNodeListener;
    ruleData->matchListener = matchListener;
    return ruleData;
}

struct NCC_RuleData* NCC_initializeRuleData(struct NCC_RuleData* ruleData, struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_createNodeListener createNodeListener, NCC_deleteNodeListener deleteNodeListener, NCC_matchListener matchListener, boolean isRoot) {

    ruleData->ncc = ncc;
    NString.initialize(&ruleData->ruleName, "%s", ruleName);
    NString.initialize(&ruleData->ruleText, "%s", ruleText);
    ruleData->createNodeListener = createNodeListener;
    ruleData->deleteNodeListener = deleteNodeListener;
    ruleData->matchListener = matchListener;
    ruleData->isRoot = isRoot;

    ruleData->set = ruleDataSet;
    ruleData->setListeners = ruleDataSetListeners;

    return ruleData;
}

void NCC_destroyRuleData(struct NCC_RuleData* ruleData) {
    NString.destroy(&ruleData->ruleName);
    NString.destroy(&ruleData->ruleText);
}

static struct NCC_Rule* createRule(struct NCC_RuleData* ruleData) {

    // Create rule tree,
    const char* ruleText = NString.get(&ruleData->ruleText);
    struct NCC_Node* ruleTree = constructRuleTree(ruleData->ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "createRule(): unable to construct rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create and initialize rule,
    struct NCC_Rule* rule = NMALLOC(sizeof(struct NCC_Rule), "NCC.createRule() rule");
    rule->tree = ruleTree;
    rule->data = *ruleData;  // Copy all members. But note that, copying strings is dangerous due
                             // to memory allocations. They have to be handled manually.
    const char* ruleName = NString.get(&ruleData->ruleName);
    NString.initialize(&ruleData->ruleName, "%s", ruleName);
    NString.initialize(&ruleData->ruleText, "%s", ruleText);

    return rule;
}

static void destroyRule(struct NCC_Rule* rule) {
    NCC_destroyRuleData(&rule->data);
    nodeDeleteTree[rule->tree->type](rule->tree);
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
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree, "NCC.genericDeleteTreeNoData() tree");
}

static void genericDeleteTreeWithData(struct NCC_Node* tree) {
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.genericDeleteTreeWithData() tree->data");
    NFREE(tree      , "NCC.genericDeleteTreeWithData() tree"      );
}

static struct NCC_Node* genericCreateNode(int32_t type, void* data) {
    struct NCC_Node* node = NMALLOC(sizeof(struct NCC_Node), "NCC.genericCreateNode() node");
    node->type = type;
    node->data = data;
    node->previousNode = 0;
    node->nextNode = 0;
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Root node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void rootNodeSetPreviousNode(struct NCC_Node* node, struct NCC_Node* previousNode) {
    NERROR("NCC.c", "%ssetPreviousNode()%s shouldn't be called on a %sroot%s node", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
}

static boolean rootNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    return nodeMatch[node->nextNode->type](node->nextNode, ncc, text, matchingResult);
}

static struct NCC_Node* createRootNode() {
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ROOT, 0);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accept node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void acceptNodeSetNextNode(struct NCC_Node* node, struct NCC_Node* nextNode) {
    NERROR("NCC.c", "%ssetNextNode()%s shouldn't be called on an %saccept%s node", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
}

static boolean acceptNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
    return True; // Reaching accept node means that the strings matches the rule, even if the string is not over yet,
}

static struct NCC_Node* createAcceptNode(struct NCC_Node* parentNode) {
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ACCEPT, 0);
    nodeSetNextNode[parentNode->type](parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literals node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralsNodeData {
    struct NString literals;
};

static boolean literalsNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct LiteralsNodeData* nodeData = node->data;

    if (!NCString.startsWith(text, NString.get(&nodeData->literals))) {
        NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
        return False;
    }

    int32_t length = NString.length(&nodeData->literals);
    boolean matched = nodeMatch[node->nextNode->type](node->nextNode, ncc, &text[length], matchingResult);
    matchingResult->matchLength += length;
    return matched;
}

static void literalsNodeDeleteTree(struct NCC_Node* tree) {
    struct LiteralsNodeData* nodeData = tree->data;
    NString.destroy(&nodeData->literals);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.literalsNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.literalsNodeDeleteTree() tree"      );
}

static struct NCC_Node* createLiteralsNode(const char* literals) {
    struct LiteralsNodeData* nodeData = NMALLOC(sizeof(struct LiteralsNodeData), "NCC.createLiteralsNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS, nodeData);

    NString.initialize(&nodeData->literals, "%s", literals);

    #if NCC_VERBOSE
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
            nodeSetNextNode[literalsNode->type](literalsNode, newLiteralsNode);

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

static boolean literalRangeNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct LiteralRangeNodeData* nodeData = node->data;

    unsigned char literal = (unsigned char) *text;
    if ((literal < nodeData->rangeStart) || (literal > nodeData->rangeEnd)) {
        NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
        return False;
    }

    boolean matched = nodeMatch[node->nextNode->type](node->nextNode, ncc, &text[1], matchingResult);
    matchingResult->matchLength++;
    return matched;
}

static struct NCC_Node* createLiteralRangeNode(unsigned char rangeStart, unsigned char rangeEnd) {

    struct LiteralRangeNodeData* nodeData = NMALLOC(sizeof(struct LiteralRangeNodeData), "NCC.createLiteralRangeNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL_RANGE, nodeData);
    if (rangeStart > rangeEnd) {
        unsigned char temp = rangeStart;
        rangeStart = rangeEnd;
        rangeEnd = temp;
    }
    nodeData->rangeStart = rangeStart;
    nodeData->rangeEnd = rangeEnd;

    #if NCC_VERBOSE
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

            #if NCC_VERBOSE
            NLOGI("NCC", "Appended to literals node: %s%c%s", NTCOLOR(HIGHLIGHT), literal, NTCOLOR(STREAM_DEFAULT));
            #endif
            return parentNode;
        }

        // Parent is not of literals type. Create a new node,
        char literalString[2] = {literal, 0};
        node = createLiteralsNode(literalString);
    }

    nodeSetNextNode[parentNode->type](parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Or node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct OrNodeData {
    struct NCC_Node* rhsTree;
    struct NCC_Node* lhsTree;
};

static boolean orNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct OrNodeData* nodeData = node->data;
    struct NCC_ASTNode_Data parentNode = matchingResult->astNode;

    // Match the sides on temporary routes,
    // Right hand side,
    MatchTree(rhs, nodeData->rhsTree, parentNode, text, 0, {&rhs}, 1)

    // Left hand side,
    MatchTree(lhs, nodeData->lhsTree, parentNode, text, 0, {&rhs COMMA &lhs}, 2)

    // If neither right or left matches,
    if ((!rhsMatched) && (!lhsMatched)) {
        *matchingResult = rhs.matchLength > lhs.matchLength ? rhs : lhs;
        return False;
    }

    // If we needn't check the following tree twice,
    if ((rhs.matchLength==lhs.matchLength) ||
        (!rhsMatched) ||
        (!lhsMatched)) {

        // Get the matched tree if only one side matched, or the rule that occurs first (lhs) otherwise,
        struct NCC_MatchingResult* matchedTree = lhsMatched ? &lhs : &rhs;
        MatchTree(nextNode, node->nextNode, parentNode, &text[matchedTree->matchLength], matchedTree->matchLength, {&nextNode COMMA &lhs COMMA &rhs}, 3)
        NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
        matchingResult->matchLength = nextNode.matchLength + matchedTree->matchLength;
        if (!nextNodeMatched) {
            if (lhsMatched) discardTree(&lhs, &parentNode);
            if (rhsMatched) discardTree(&rhs, &parentNode);
            return False;
        }

        // Discard the unused tree (if any),
        if (lhsMatched && rhsMatched) discardTree(&rhs, &parentNode);
        return True;
    }

    // RHS and LHS match lengths are not the same. To maximize the overall match length, we have
    // to take the rest of the tree into account by matching at both right and left lengths,

    // Right hand side,
    MatchTree(rhsTree, node->nextNode, parentNode, &text[rhs.matchLength], rhs.matchLength, {&rhsTree COMMA &lhs COMMA &rhs}, 3)

    // Left hand side,
    MatchTree(lhsTree, node->nextNode, parentNode, &text[lhs.matchLength], lhs.matchLength, {&lhsTree COMMA &rhsTree COMMA &lhs COMMA &rhs}, 4)

    // If neither right or left trees match,
    int32_t rhsMatchLength = rhs.matchLength + rhsTree.matchLength;
    int32_t lhsMatchLength = lhs.matchLength + lhsTree.matchLength;
    if ((!rhsTreeMatched) && (!lhsTreeMatched)) {

        // Return the result with the longest match,
        if (rhsMatchLength > lhsMatchLength) {
            *matchingResult = rhsTree;
            matchingResult->matchLength = rhsMatchLength;
        } else {
            *matchingResult = lhsTree;
            matchingResult->matchLength = lhsMatchLength;
        }
        discardTree(&lhs, &parentNode);
        discardTree(&rhs, &parentNode);
        return False;
    }

    // Get the final match lengths (at least one side should have matched),
    if (!rhsTreeMatched) rhsMatchLength = -100000000; // Since a match can be of a negative length, I used a number that's ridiculously small.
    if (!lhsTreeMatched) lhsMatchLength = -100000000;

    // Push the correct temporary routes,
    NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
    if (rhsMatchLength > lhsMatchLength) {
        discardTree(&lhsTree, &parentNode);
        discardTree(&lhs    , &parentNode);
        matchingResult->matchLength += rhsTree.matchLength;
        matchingResult->matchLength += rhs    .matchLength;
    } else {
        discardTree(&rhsTree, &parentNode);
        discardTree(&rhs    , &parentNode);
        matchingResult->matchLength += lhsTree.matchLength;
        matchingResult->matchLength += lhs    .matchLength;
    }

    // Or nodes don't get pushed into the route,
    return True;
}

static void orNodeDeleteTree(struct NCC_Node* tree) {
    struct OrNodeData* nodeData = tree->data;
    nodeDeleteTree[nodeData->rhsTree->type](nodeData->rhsTree);
    nodeDeleteTree[nodeData->lhsTree->type](nodeData->lhsTree);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.orNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.orNodeDeleteTree() tree"      );
}

static struct NCC_Node* createOrNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // If the parent node is a literals node with more than one literal, break the last literal apart so that it's
    // the only literal matched in the or,
    parentNode = breakLastLiteralIfNeeded(parentNode);

    // Get grand-parent node,
    struct NCC_Node* grandParentNode = nodeGetPreviousNode[parentNode->type](parentNode);
    if (!grandParentNode) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    struct OrNodeData* nodeData = NMALLOC(sizeof(struct OrNodeData), "NCC.createOrNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData);

    // Remove parent from the grand-parent and attach this node instead,
    nodeSetNextNode[grandParentNode->type](grandParentNode, node);

    // Turn parent node into a tree and attach it as the lhs node,
    nodeData->lhsTree = createRootNode();
    nodeSetNextNode[nodeData->lhsTree->type](nodeData->lhsTree, parentNode);
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

    #if NCC_VERBOSE
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

static boolean subRuleNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct SubRuleNodeData* nodeData = node->data;

    // Match sub-rule on a temporary route,
    MatchTree(subRule, nodeData->subRuleTree, text, tempRoute1, {}, 0, {&subRule}, 1)
    if (!subRuleMatched) {
        *matchingResult = subRule.result;
        return False;
    }

    // Match next node,
    MatchTree(followingTree, node->nextNode, &text[subRule.result.matchLength], matchRoute, {&subRule}, 1, {&followingTree COMMA &subRule}, 2)
    *matchingResult = followingTree.result;
    if (!followingTreeMatched) {
        matchingResult->matchLength += subRule.result.matchLength;
        DiscardTree(&subRule)
        return False;
    }

    // Push the sub-rule route,
    PushTree(subRule)

    return True;
}

static void subRuleNodeDeleteTree(struct NCC_Node* tree) {
    struct SubRuleNodeData* nodeData = tree->data;
    nodeDeleteTree[nodeData->subRuleTree->type](nodeData->subRuleTree);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUB_RULE, nodeData);
    nodeData->subRuleTree = subRuleTree;

    #if NCC_VERBOSE
    NLOGI("NCC", "Created sub-rule node: %s{%s}%s", NTCOLOR(HIGHLIGHT), subRule, NTCOLOR(STREAM_DEFAULT));
    #endif
    nodeSetNextNode[parentNode->type](parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Repeat node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RepeatNodeData {
    struct NCC_Node* repeatedNode;
    struct NCC_Node* followingSubRule;
};

static boolean repeatNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct RepeatNodeData* nodeData = node->data;

    // If there's no following subrule, match as much as you can, and always return True,
    if (!nodeData->followingSubRule) {
        MatchTree(repeatedNode, nodeData->repeatedNode, text, tempRoute1, {}, 0, {&repeatedNode}, 1)
        if (!repeatedNodeMatched || repeatedNode.result.matchLength==0) {
            if (repeatedNodeMatched) DiscardTree(&repeatedNode)
            NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
            return True;
        }

        // Attempt matching again,
        repeatNodeMatch(node, ncc, &text[repeatedNode.result.matchLength], matchingResult);
        if (!matchingResult->terminate) PushTree(repeatedNode)
        return True;
    }

    // Check if the following sub-rule matches,
    MatchTree(followingSubRule, nodeData->followingSubRule, text, matchRoute, {}, 0, {&followingSubRule}, 1)
    *matchingResult = followingSubRule.result;
    if (followingSubRuleMatched && followingSubRule.result.matchLength != 0) return True;

    // TODO: if following subrule is the end of this sub-rule, we should check the next of the current sub-rule, which
    // should be kept in a separate stack? This should sometimes terminate the repeats earlier than expected. Should
    // we do it at all? Maybe do it conditional, or using another operator (something other than the *) ?

    // Following sub-rule didn't match, attempt repeating (on the temporary route),
    MatchTree(repeatedNode, nodeData->repeatedNode, text, tempRoute1, {}, 0, {&followingSubRule COMMA &repeatedNode}, 2)
    if (!repeatedNodeMatched || repeatedNode.result.matchLength==0) {
        if (repeatedNodeMatched) DiscardTree(&repeatedNode)
        if (followingSubRuleMatched) return True;
        matchingResult->matchLength += repeatedNode.result.matchLength;
        return False;
    }

    // Something matched, attempt repeating. Discard any matches that could have been added by the following sub-rule,
    // TODO: If "Discarding!" never gets printed, remove...
    if (NByteVector.size(ncc->matchRoute) != followingSubRule.routeMark) {
        NLOGI("sdf", "Discarding!");
        //NLOGW("sdf", "followingSubRuleMatchLength: %d, size(ncc->matchRoute): %d, matchRouteMark: %d", followingSubRuleMatchLength, NByteVector.size(ncc->matchRoute), matchRouteMark);
        DiscardTree(&followingSubRule)
    }

    // Repeat,
    boolean matched = repeatNodeMatch(node, ncc, &text[repeatedNode.result.matchLength], matchingResult);
    if (matchingResult->terminate || !matched) {
        // Didn't end properly, discard,
        matchingResult->matchLength += repeatedNode.result.matchLength;
        DiscardTree(&repeatedNode)
        return False;
    }

    // Push the repeated node route,
    PushTree(repeatedNode)
    return True;
}

static void repeatNodeDeleteTree(struct NCC_Node* tree) {
    struct RepeatNodeData* nodeData = tree->data;
    nodeDeleteTree[nodeData->repeatedNode->type](nodeData->repeatedNode);
    if (nodeData->followingSubRule) nodeDeleteTree[nodeData->followingSubRule->type](nodeData->followingSubRule);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.repeatNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.repeatNodeDeleteTree() tree"      );
}

static struct NCC_Node* createRepeatNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // If the parent node is a literals node with more than one literal, break the last literal apart so that it's
    // the only literal repeated,
    parentNode = breakLastLiteralIfNeeded(parentNode);

    // Get grand-parent node,
    struct NCC_Node* grandParentNode = nodeGetPreviousNode[parentNode->type](parentNode);
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.REPEAT, nodeData);

    // Remove parent from the grand-parent and attach this node instead,
    nodeSetNextNode[grandParentNode->type](grandParentNode, node);

    // Turn parent node into a tree and attach it as the repeated node,
    nodeData->repeatedNode = createRootNode();
    nodeSetNextNode[nodeData->repeatedNode->type](nodeData->repeatedNode, parentNode);
    createAcceptNode(parentNode);

    // Create a new tree for the remaining text and set it as the following sub-rule,
    nodeData->followingSubRule = 0;
    if (*in_out_rule[0]) {
        nodeData->followingSubRule = constructRuleTree(ncc, *in_out_rule);
        if (!nodeData->followingSubRule) {
            NERROR("NCC", "createRepeatNode(): Couldn't create the following sub-rule tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
            return 0;  // Since this node is already attached to the tree, it gets cleaned up automatically.
        }
    }

    // The remainder of the tree was already added to the following sub-rule, no need to continue parsing,
    while (**in_out_rule) (*in_out_rule)++;

    #if NCC_VERBOSE
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

static boolean anythingNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct AnythingNodeData* nodeData = node->data;

    // If no following subrule, then match the entire text,
    int32_t totalMatchLength=0;
    if (!nodeData->followingSubRule) {
        while (text[totalMatchLength]) totalMatchLength++;
        NSystemUtils.memset(matchingResult, 0, sizeof(struct NCC_MatchingResult));
        goto conclude;
    }

    // There's a following subrule. Stop as soon as it's matched,
    do {
        // Check if the following sub-rule matches,
        struct MatchedTree followingSubRule;
        boolean followingSubRuleMatched = matchTree(ncc, nodeData->followingSubRule, &text[totalMatchLength],
                                                    &followingSubRule, &ncc->matchRoute,
                                                    0, 0, (struct MatchedTree*[]) {&followingSubRule}, 1);
        if (followingSubRule.result.terminate) {
            *matchingResult = followingSubRule.result;
            matchingResult->matchLength += totalMatchLength;
            return followingSubRuleMatched;
        }

        if (followingSubRuleMatched && followingSubRule.result.matchLength > 0) {
            *matchingResult = followingSubRule.result;
            goto conclude;
        }

        // If text ended,
        if (!text[totalMatchLength]) {
            *matchingResult = followingSubRule.result;
            if (!followingSubRuleMatched) {
                matchingResult->matchLength += totalMatchLength;
                return False;
            }
            goto conclude;
        }

        // Following sub-rule didn't match, or had a zero-length match,
        if (followingSubRuleMatched) DiscardTree(&followingSubRule)

        // Text didn't end, advance,
        totalMatchLength++;
    } while (True);

    conclude:
    skipNLiterals(ncc, totalMatchLength);
    matchingResult->matchLength += totalMatchLength;
    return True;
}

static void anythingNodeDeleteTree(struct NCC_Node* tree) {
    struct AnythingNodeData* nodeData = tree->data;
    if (nodeData->followingSubRule) nodeDeleteTree[nodeData->followingSubRule->type](nodeData->followingSubRule);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.anythingNodeDeleteTree() tree->data");
    NFREE(tree, "NCC.anythingNodeDeleteTree() tree");
}

static struct NCC_Node* createAnythingNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the *,
    (*in_out_rule)++;

    // If some text remains, create a new tree for it and set it as the following sub-rule,
    struct NCC_Node* followingSubRule=0;
    if (*in_out_rule[0]) {
        followingSubRule = constructRuleTree(ncc, *in_out_rule);
        if (!followingSubRule) {
            NERROR("NCC", "createAnythingNode(): Couldn't create the remaining sub-rule tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
            return 0;
        }
    }

    // Create node,
    struct AnythingNodeData* nodeData = NMALLOC(sizeof(struct AnythingNodeData), "NCC.createAnythingNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ANYTHING, nodeData);
    nodeData->followingSubRule = followingSubRule;

    // The remainder of the tree was already added to the following sub-rule, no need to continue parsing,
    while (**in_out_rule) (*in_out_rule)++;

    #if NCC_VERBOSE
    NLOGI("NCC", "Created anything node: %s*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
    #endif
    nodeSetNextNode[parentNode->type](parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Substitute node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SubstituteNodeData {
    struct NCC_Rule* rule;
};

static void popChildrenVariables(struct NCC* ncc, int32_t variablesStackStartPosition) {

    int32_t remainingVariablesCount = NVector.size(&ncc->variables) - variablesStackStartPosition;
    for (;remainingVariablesCount; remainingVariablesCount--) {
        // Possible performance improvement: Destroying variables in place without popping, then
        // adjusting the vector size. Or even better, reusing variables.
        struct NCC_Variable currentVariable;
        NVector.popBack(&ncc->variables, &currentVariable);
        NCC_destroyVariable(&currentVariable);
    }
}

static boolean substituteNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text, struct NCC_MatchingResult* matchingResult) {
    struct SubstituteNodeData *nodeData = node->data;

    boolean accepted;
    char* matchedText=0;
    boolean popVariables=True;
    boolean discardRule=True;

    // Match rule on a temporary route. We couldn't use MatchTree because of the special "terminate" handling,
    uint32_t variablesStackPosition = NVector.size(&ncc->variables);
    struct MatchedTree rule;
    accepted = matchTree(ncc, nodeData->rule->tree, text,
                         &rule, &ncc->tempRoute1,
                         0, 0, (struct MatchedTree*[]) {&rule}, 1);
    if (rule.result.terminate || !accepted) {
        *matchingResult = rule.result;
        discardRule = False;
        goto finish;
    }

    // Found a match (an unconfirmed one, though). Report,
    if (nodeData->rule->data.onUnconfirmedMatchListener) {

        // Copy the matched text so that we can zero terminate it,
        int32_t matchLength = rule.result.matchLength;
        matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeMatch() matchedText");
        NSystemUtils.memcpy(matchedText, text, matchLength);
        matchedText[matchLength] = 0;        // Terminate the string.

        struct NCC_MatchingData matchingData;
        matchingData.ruleData = &nodeData->rule->data;
        matchingData.matchedText = matchedText;
        matchingData.matchLength = matchLength;
        matchingData.variablesCount = NVector.size(&ncc->variables) - variablesStackPosition;

        // Pre-initialize result with convenient values,
        matchingData.outResult.terminate = False;
        matchingData.outResult.matchLength = matchLength;
        matchingData.outResult.pushVariable = nodeData->rule->data.pushVariable;
        matchingData.outResult.couldNeedRollBack = nodeData->rule->data.onRollBackMatchListener != 0;

        ncc->currentCallStackBeginning = variablesStackPosition;
        accepted = nodeData->rule->data.onUnconfirmedMatchListener(&matchingData);

        if (matchingData.outResult.terminate || !accepted) {
            *matchingResult = matchingData.outResult;
            goto finish;
        }

        matchingData.outResult.couldNeedRollBack &= nodeData->rule->data.onRollBackMatchListener != 0;
        rule.result = matchingData.outResult;
    } else {

        // Use defaults,
        rule.result.pushVariable      = nodeData->rule->data.pushVariable;
        rule.result.couldNeedRollBack = nodeData->rule->data.onRollBackMatchListener != 0;
    }

    // Match next nodes,
    int32_t matchLength = rule.result.matchLength;
    struct MatchedTree nextNode;
    // TODO: do we always need to discard self on terminate? Shouldn't it be already discarded?
    accepted = matchTree(ncc, node->nextNode, &text[matchLength],
                         &nextNode, &ncc->matchRoute,
                         0, 0, (struct MatchedTree*[]) {&nextNode, &rule}, 2);
    *matchingResult = nextNode.result;
    if (nextNode.result.terminate || !accepted) {
        matchingResult->matchLength += rule.result.matchLength;
        goto finish;
    }

    // Next node matched,
    discardRule = False;

    // Pop children variables if needed,
    if (nodeData->rule->data.popsChildrenVariables) popChildrenVariables(ncc, variablesStackPosition);

    // Now push this variable if needed,
    popVariables = False;
    if (rule.result.pushVariable) {
        if (!matchedText) {
            matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeMatch() matchedText");
            NSystemUtils.memcpy(matchedText, text, matchLength);
            matchedText[matchLength] = 0;        // Terminate the string.
        }
        struct NCC_Variable match;
        NCC_initializeVariable(&match, NString.get(&nodeData->rule->data.ruleName), matchedText);
        NVector.pushBack(&ncc->variables, &match);
    }

    // Check if pushing this node is useful,
    if (rule.result.pushVariable || rule.result.couldNeedRollBack || nodeData->rule->data.onConfirmedMatchListener || nodeData->rule->data.popsChildrenVariables) {

        // Push 0 to mark the end of the rule route,
        NByteVector.pushBack(ncc->matchRoute, 0);

        // Push the rule route and this node's index,
        PushTree(rule)
        NByteVector.pushBackBulk(ncc->matchRoute, &nodeData->rule->index, ncc->ruleIndexSizeBytes);

        // Push 255 to denote the preceding substitute node index,
        NByteVector.pushBack(ncc->matchRoute, 255);

    } else {
        // Push the rule route only,
        PushTree(rule)
    }

    finish:

    if (matchedText) NFREE(matchedText, "NCC.substituteNodeMatch() matchedText");
    if (popVariables) popChildrenVariables(ncc, variablesStackPosition);
    if (discardRule) DiscardTree(&rule)
    return accepted;
}

static int32_t substituteNodeFollowMatchRoute(struct NCC_Rule* rule, const char* text, struct MatchedTree* tree, boolean rollBack) {

    struct NCC* ncc = rule->data.ncc;
    struct NByteVector* route = *tree->route;

    // Remember the variables stack position,
    uint32_t variablesStackPosition = NVector.size(&ncc->variables);

    // Match,
    uint8_t nextValue = NByteVector.get(route, NByteVector.size(route)-1);
    int32_t matchLength;
    if (nextValue) {
        matchLength = followMatchRoute(ncc, text, tree, rollBack);
    } else {
        // Found a 0 (this substitute node can by empty),
        matchLength = 0;
        NByteVector.popBack(route, &nextValue); // Discard the 0.
    }

    char *matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeFollowMatchRoute() matchedText");
    NSystemUtils.memcpy(matchedText, text, matchLength);
    matchedText[matchLength] = 0;        // Terminate the string.

    // Perform rule action,
    ncc->currentCallStackBeginning = variablesStackPosition;
    boolean pushVariable = rule->data.pushVariable;
    if ((rule->data.onConfirmedMatchListener && !rollBack) || (rule->data.onRollBackMatchListener && rollBack)) {

        struct NCC_MatchingData matchingData;
        matchingData.ruleData = &rule->data;
        matchingData.matchedText = matchedText;
        matchingData.matchLength = matchLength;
        matchingData.variablesCount = NVector.size(&ncc->variables) - variablesStackPosition;

        // Pre-initialize result with convenient values,
        matchingData.outResult.terminate = False;
        matchingData.outResult.pushVariable = pushVariable;

        if (rollBack) {
            rule->data.onRollBackMatchListener(&matchingData);
        } else {
            rule->data.onConfirmedMatchListener(&matchingData);
        }

        // TODO: handle terminate...
        pushVariable = matchingData.outResult.pushVariable;
    }

    // Pop any variables that should be popped,
    if (rule->data.popsChildrenVariables) popChildrenVariables(ncc, variablesStackPosition);

    // Save the match,
    if (pushVariable) {
        struct NCC_Variable match;
        NCC_initializeVariable(&match, NString.get(&rule->data.ruleName), matchedText);
        NVector.pushBack(&ncc->variables, &match);
    }

    #if NCC_VERBOSE
        NLOGI("NCC", "Visited substitute node %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&rule->data.ruleName), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchedText, NTCOLOR(STREAM_DEFAULT));
    #endif

    NFREE(matchedText, "NCC.substituteNodeFollowMatchRoute() matchedText");

    // Follow next nodes,
    return matchLength;
}

static void substituteNodeDeleteTree(struct NCC_Node* tree) {
    // Note: we don't free the rules, we didn't allocate them.
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUBSTITUTE, nodeData);
    nodeData->rule = rule;

    #if NCC_VERBOSE
    NLOGI("NCC", "Created substitute node: %s${%s}%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
    #endif

    NFREE(ruleName, "NCC.createSubstituteNode() ruleName 2");
    nodeSetNextNode[parentNode->type](parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions
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
    nodeDeleteTree[rootNode->type](rootNode);
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

static struct NCC_Rule* getRule(struct NCC* ncc, const char* ruleName) {

    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {
        struct NCC_Rule* currentRule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        if (NCString.equals(ruleName, NString.get(&currentRule->data.ruleName))) return currentRule;
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

static void discardTree(struct NCC_MatchingResult* tree, struct NCC_ASTNode_Data* parentNode) {
    if (!tree->astNode.node) return;
    NCC_deleteNodeListener deleteListener = tree->astNode.rule->deleteNodeListener;
    if (deleteListener) deleteListener(&tree->astNode, parentNode);
    tree->astNode.rule = 0;
    tree->astNode.node = 0;
}

static boolean matchTree(
        struct NCC* ncc, struct NCC_Node* tree, const char* text, struct NCC_MatchingResult* matchingResult,
        int32_t lengthToAddIfTerminated, struct NCC_MatchingResult** astsToDeleteIfTerminated, int32_t astsToDeleteCount) {

    // Match,
    struct NCC_ASTNode_Data parentNode = matchingResult->astNode;
    boolean matched = nodeMatch[tree->type](tree, ncc, text, matchingResult);

    // Return immediately if termination didn't take place,
    if (!matchingResult->terminate) return matched;

    // Termination took place,
    matchingResult->matchLength += lengthToAddIfTerminated;

    // Delete ast nodes,
    for (int32_t i=0; i<astsToDeleteCount; i++) discardTree(astsToDeleteIfTerminated[i], &parentNode);

    return matched;
}

static void emptyVariablesStack(struct NCC* ncc) {
    for (int32_t i=NVector.size(&ncc->variables)-1; i>=0; i--) NCC_destroyVariable(NVector.get(&ncc->variables, i));
    NVector.clear(&ncc->variables);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NCC
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

boolean NCC_addRule(struct NCC_RuleData* ruleData) {

    // Check if a rule with this name already exists,
    const char* ruleName = NString.get(&ruleData->ruleName);
    if (getRule(ruleData->ncc, ruleName)) {
        NERROR("NCC", "NCC_addRule(): unable to create rule %s%s%s. A rule with the same name exists.", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    struct NCC_Rule* rule = createRule(ruleData);
    if (!rule) {
        NERROR("NCC", "NCC_addRule(): unable to create rule %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&ruleData->ruleText), NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Set the rule index and the rule index size,
    struct NCC* ncc = ruleData->ncc;
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

boolean NCC_updateRule(struct NCC_RuleData* ruleData) {

    // Fetch rule,
    struct NCC* ncc = ruleData->ncc;
    const char* ruleName = NString.get(&ruleData->ruleName);
    struct NCC_Rule* rule = getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "NCC_updateRule(): unable to update rule %s%s%s. Rule doesn't exist.", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Create new rule tree,
    const char* ruleText = NString.get(&ruleData->ruleText);
    struct NCC_Node* ruleTree = constructRuleTree(ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "NCC_updateRule(): unable to construct rule tree: %s%s%s. Failed to update rule: %s%s%s.", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Dispose of the old rule-tree and set the new one,
    nodeDeleteTree[rule->tree->type](rule->tree);
    rule->tree = ruleTree;

    // Reinitialize rule data by copying all members. But note that copying strings is dangerous due
    // to memory allocations. Strings have to be handled manually,
    struct NString ruleNameString = rule->data.ruleName;
    struct NString ruleTextString = rule->data.ruleText;
    rule->data = *ruleData;
    rule->data.ruleName = ruleNameString;
    rule->data.ruleText = ruleTextString;
    NString.set(&rule->data.ruleName, "%s", ruleName);
    NString.set(&rule->data.ruleText, "%s", ruleText);

    return True;
}

boolean NCC_match(struct NCC* ncc, const char* text, struct NCC_MatchingResult* outResult) {

    // TODO: needs cleanup...

    // Find the longest match,
    int32_t maxMatchLength=-100000000;
    struct NCC_Rule* maxMatchRule=0;
    struct NByteVector* maxMatchRoute = NByteVector.create(0);
    struct NCC_MatchingResult matchingResult;
    boolean foundMatch = False;
    boolean callListener = True;
    NSystemUtils.memset(outResult, 0, sizeof(struct NCC_MatchingResult));

    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {

        struct NCC_Rule* rule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        if (!rule->data.rootRule) continue;

        // Reset routes,
        // TODO: do we need to clear these?
        NByteVector.clear(ncc->matchRoute);
        NByteVector.clear(ncc->tempRoute1);
        NByteVector.clear(ncc->tempRoute2);
        NByteVector.clear(ncc->tempRoute3);
        NByteVector.clear(ncc->tempRoute4);

        // Match rule,
        boolean matched = nodeMatch[rule->tree->type](rule->tree, ncc, text, &matchingResult);
        if (matchingResult.terminate) {
            *outResult = matchingResult;
            callListener = False;
            goto conclude;
        }

        // If this is the first match,
        if (matched && !foundMatch) {
            // Forget any mismatching rule selected before,
            maxMatchLength=-100000000;
            foundMatch = True;
        }

        // If no match found yet, or found a better match,
        if ((matched || !foundMatch) && (matchingResult.matchLength > maxMatchLength)) {

            // Keep route,
            maxMatchLength = matchingResult.matchLength;
            maxMatchRule = rule;
            *outResult = matchingResult;

            // TODO: do we need this ....
            NByteVector.resize(maxMatchRoute, NByteVector.size(ncc->matchRoute));
            NSystemUtils.memcpy(
                    maxMatchRoute->objects,
                    ncc->matchRoute->objects,
                    NByteVector.size(ncc->matchRoute));
        }

        // TODO: roll back the previously matched rule...

        // Delete any remaining variables,
        emptyVariablesStack(ncc);
    }

    if (!foundMatch) goto conclude;

    // Follow the longest match route,
    if (NByteVector.size(maxMatchRoute)) {

        // Follow the max match route,
        NByteVector.clear(ncc->matchRoute);
        struct MatchedTree tree;
        tree.route = &maxMatchRoute;
        tree.routeMark = 0;
        tree.text = text;
        followMatchRoute(ncc, text, &tree, False);
    }

    conclude:

    // Perform rule action,
    ncc->currentCallStackBeginning = 0;
    if (callListener && foundMatch && maxMatchRule->data.onConfirmedMatchListener) {

        char *matchedText = NMALLOC(maxMatchLength+1, "NCC.NCC_match() matchedText");
        NSystemUtils.memcpy(matchedText, text, maxMatchLength);
        matchedText[maxMatchLength] = 0;        // Terminate the string.

        struct NCC_MatchingData matchingData;
        matchingData.ruleData = &maxMatchRule->data;
        matchingData.matchedText = matchedText;
        matchingData.matchLength = maxMatchLength;
        matchingData.variablesCount = NVector.size(&ncc->variables);

        maxMatchRule->data.onConfirmedMatchListener(&matchingData);

        NFREE(matchedText, "NCC.NCC_match() matchedText");
    }

    // Empty the variables stack,
    emptyVariablesStack(ncc);

    // Destroy the max match route,
    NByteVector.destroyAndFree(maxMatchRoute);

    return foundMatch;
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
