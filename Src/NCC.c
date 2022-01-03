#include <NCC.h>

#include <NSystemUtils.h>
#include <NError.h>
#include <NByteVector.h>
#include <NString.h>
#include <NCString.h>
#include <NVector.h>

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

static struct NCC_Node* constructRuleTree(struct NCC* ncc, const char* rule);
static struct NCC_Node* getNextNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule);
static struct NCC_Rule* getRule(struct NCC* ncc, const char* ruleName);
static void switchRoutes(struct NVector** route1, struct NVector** route2);
static void pushTempRouteIntoMatchRoute(struct NCC* ncc, struct NVector* tempRoute, int32_t tempRouteMark);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Node {
    int32_t type;
    void *data;
    struct NCC_Node* previousNode;
    struct NCC_Node*     nextNode;
    int32_t (*      match     )(struct NCC_Node* node, struct NCC* ncc, const char* text); // Returns match length if matched, -1 if rejected.
    int32_t (*followMatchRoute)(struct NCC_Node* node, struct NCC* ncc, const char* text); // Returns match length if matched, -1 if rejected.
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
    NString.initialize(&variable->name , "%s", name);
    NString.initialize(&variable->value, "%s", value);
    return variable;
}

void NCC_destroyVariable(struct NCC_Variable* variable) {
    NString.destroy(&variable->name );
    NString.destroy(&variable->value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rule
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Rule {
    struct NString name;
    struct NCC_Node* tree;
    NCC_onMatchListener onMatchListener;
    boolean rootRule; // True: can be matched alone. False: must be part of some other rule.
};

struct NCC_Rule* createRule(struct NCC* ncc, const char* name, const char* ruleText, NCC_onMatchListener onMatchListener, boolean rootRule) {

    // Create rule tree,
    struct NCC_Node* ruleTree = constructRuleTree(ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "createRule(): unable to construct rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create rule,
    struct NCC_Rule* rule = NMALLOC(sizeof(struct NCC_Rule), "NCC.createRule() rule");
    NString.initialize(&rule->name, "%s", name);
    rule->tree = ruleTree;
    rule->onMatchListener = onMatchListener;
    rule->rootRule = rootRule;

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
typedef int32_t (*FollowMatchMethod)(struct NCC_Node* node, struct NCC* ncc, const char* text);

static int32_t genericNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) { return 0; }

static struct NCC_Node* genericCreateNode(int32_t type, void* data, MatchMethod matchMethod, FollowMatchMethod followMatchMethod) {
    struct NCC_Node* node = NMALLOC(sizeof(struct NCC_Node), "NCC.genericCreateNode() node");
    node->type = type;
    node->data = data;
    node->match = matchMethod;
    node->followMatchRoute = followMatchMethod ? followMatchMethod : genericNodeFollowMatchRoute;

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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ROOT, 0, rootNodeMatch, 0);
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ACCEPT, 0, acceptNodeMatch, 0);
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
    NVector.pushBack(ncc->matchRoute, &node);
    return matchLength+length;
}

static int32_t literalsNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct LiteralsNodeData* nodeData = node->data;
    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Visited literals node: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&nodeData->literals), NTCOLOR(STREAM_DEFAULT));
    #endif

    int32_t length = NString.length(&nodeData->literals);
    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? length + nextNode->followMatchRoute(nextNode, ncc, &text[length]) : length;
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS, nodeData, literalsNodeMatch, literalsNodeFollowMatchRoute);
    node->deleteTree = literalsNodeDeleteTree;

    NString.initialize(&nodeData->literals, "%s", literals);

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created literals node: %s%s%s", NTCOLOR(HIGHLIGHT), literals, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
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
    NVector.pushBack(ncc->matchRoute, &node);
    return matchLength+1;
}

static int32_t literalRangeNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Visited literal-range node: %s%c%s", NTCOLOR(HIGHLIGHT), text[0], NTCOLOR(STREAM_DEFAULT));
    #endif
    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? 1 + nextNode->followMatchRoute(nextNode, ncc, &text[1]) : 1;
}

static struct NCC_Node* createLiteralRangeNode(unsigned char rangeStart, unsigned char rangeEnd) {

    struct LiteralRangeNodeData* nodeData = NMALLOC(sizeof(struct LiteralRangeNodeData), "NCC.createLiteralRangeNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL_RANGE, nodeData, literalRangeNodeMatch, literalRangeNodeFollowMatchRoute);
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

    // Check if this was a literals range,
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
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);
    int32_t rhsRouteMark = NVector.size(ncc->matchRoute);
    int32_t rhsMatchLength = nodeData->rhsTree->match(nodeData->rhsTree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    switchRoutes(&ncc->matchRoute, &ncc->tempRoute2);
    int32_t lhsRouteMark = NVector.size(ncc->matchRoute);
    int32_t lhsMatchLength = nodeData->lhsTree->match(nodeData->lhsTree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute2);

    int32_t matchLength = rhsMatchLength > lhsMatchLength ? rhsMatchLength : lhsMatchLength;
    if (matchLength==-1) return -1;

    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    if (nextNodeMatchLength==-1) {
        NVector.resize(ncc->tempRoute1, rhsRouteMark);
        NVector.resize(ncc->tempRoute2, lhsRouteMark);
        return -1;
    }

    // Push the correct temporary route,
    if (rhsMatchLength > lhsMatchLength) {
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, rhsRouteMark);
        NVector.resize(ncc->tempRoute2, lhsRouteMark);
    } else {
        pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute2, lhsRouteMark);
        NVector.resize(ncc->tempRoute1, rhsRouteMark);
    }

    // Or nodes don't get pushed into the route,
    return matchLength + nextNodeMatchLength;
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

    // Get grand-parent node,
    struct NCC_Node* grandParentNode = parentNode->getPreviousNode(parentNode);
    if (!grandParentNode) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    struct OrNodeData* nodeData = NMALLOC(sizeof(struct OrNodeData), "NCC.createOrNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData, orNodeMatch, 0);
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
    int32_t tempRouteMark = NVector.size(ncc->matchRoute);
    int32_t matchLength = nodeData->subRuleTree->match(nodeData->subRuleTree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    if (matchLength==-1) return -1;

    // Match next node,
    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    if (nextNodeMatchLength==-1) {
        NVector.resize(ncc->tempRoute1, tempRouteMark);
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
    // TODO: remove the dependency on byte vector, use memcpy...
    struct NByteVector subRule;
    NByteVector.create(2, &subRule);
    int32_t closingBracesRequired=1;
    boolean subRuleComplete=False;
    for (int32_t i=0;; i++) {
        char currentChar = ((*in_out_rule)++)[0];
        if (currentChar=='{') {
            closingBracesRequired++;
        } else if (currentChar=='}') {
            if (!--closingBracesRequired) {
                subRuleComplete = True;
                break;
            }
        } else if (!currentChar) {
            break;
        }
        NByteVector.pushBack(&subRule, currentChar);
    }
    NByteVector.pushBack(&subRule, 0);

    // Make sure the sub-rule is well-formed,
    if (!NByteVector.size(&subRule)) {
        NERROR("NCC", "createSubRuleNode(): can't have empty sub-rules %s{}%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        goto malformedSubRuleExit;
    }
    if (!subRuleComplete) {
        NERROR("NCC", "createSubRuleNode(): couldn't find a matching %s}%s in %s%s%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), subRuleBeginning, NTCOLOR(STREAM_DEFAULT));
        goto malformedSubRuleExit;
    }

    // Create sub-rule tree,
    struct NCC_Node* subRuleTree = constructRuleTree(ncc, subRule.objects);
    if (!subRuleTree) {
        NERROR("NCC", "createSubRuleNode(): couldn't create sub-rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), subRuleBeginning, NTCOLOR(STREAM_DEFAULT));
        goto  malformedSubRuleExit;
    }

    // Create the sub-rule node,
    struct SubRuleNodeData* nodeData = NMALLOC(sizeof(struct SubRuleNodeData), "NCC.createSubRuleNode() nodeData");
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUB_RULE, nodeData, subRuleNodeMatch, 0);
    node->deleteTree = subRuleNodeDeleteTree;
    nodeData->subRuleTree = subRuleTree;

    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Created sub-rule node: %s{%s}%s", NTCOLOR(HIGHLIGHT), subRule.objects, NTCOLOR(STREAM_DEFAULT));
    #endif
    NByteVector.destroy(&subRule);

    parentNode->setNextNode(parentNode, node);
    return node;

malformedSubRuleExit:
    NByteVector.destroy(&subRule);
    return 0;
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
    int32_t matchRouteMark = NVector.size(ncc->matchRoute);
    int32_t followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, text);
    if (followingSubRuleMatchLength>0) return followingSubRuleMatchLength;

    // Following sub-rule didn't match, attempt repeating (on the temporary route),
    int32_t tempRouteMark = NVector.size(ncc->tempRoute1);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);
    int32_t matchLength = nodeData->repeatedNode->match(nodeData->repeatedNode, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    if (matchLength<1) {
        // Nothing matched, discard,
        NVector.resize(ncc->tempRoute1, tempRouteMark);
        return followingSubRuleMatchLength;
    }

    // Something matched, attempt repeating. Discard any matches that could have been added by the following sub-rule,
    // TODO: If "Discarding!" never gets printed, remove...
    if (NVector.size(ncc->matchRoute) != matchRouteMark) NLOGI("sdf", "Discarding!");
    NVector.resize(ncc->matchRoute, matchRouteMark);

    // Repeat,
    int32_t repeatMatchLength = repeatNodeMatch(node, ncc, &text[matchLength]);
    if (repeatMatchLength==-1) {
        // Didn't end properly, discard,
        NVector.resize(ncc->tempRoute1, tempRouteMark);
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.REPEAT, nodeData, repeatNodeMatch, 0);
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

    int32_t tempRouteMark = NVector.size(ncc->tempRoute1);
    int32_t totalMatchLength=0;
    int32_t followingSubRuleMatchLength;
    do {
        // Check if the following sub-rule matches,
        int32_t matchRouteMark = NVector.size(ncc->matchRoute);
        followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, &text[totalMatchLength]);
        if (followingSubRuleMatchLength>0) goto conclude;

        // Following sub-rule didn't match,
        NVector.resize(ncc->matchRoute, matchRouteMark);

        // If text ended,
        if (!text[totalMatchLength]) {
            if (followingSubRuleMatchLength==-1) {
                NVector.resize(ncc->tempRoute1, tempRouteMark);
                return -1;
            }
            goto conclude;
        }

        // Text didn't end, advance,
        NVector.pushBack(ncc->tempRoute1, &node);
        totalMatchLength++;
    } while (True);

conclude:
    // Push the anything node route,
    pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, tempRouteMark);
    return totalMatchLength + followingSubRuleMatchLength;
}

static int32_t anythingNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Visited anything node: %s%c%s", NTCOLOR(HIGHLIGHT), text[0], NTCOLOR(STREAM_DEFAULT));
    #endif
    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? 1 + nextNode->followMatchRoute(nextNode, ncc, &text[1]) : 1;
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ANYTHING, nodeData, anythingNodeMatch, anythingNodeFollowMatchRoute);
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
    int32_t tempRouteMark = NVector.size(ncc->matchRoute);
    int32_t matchLength = nodeData->rule->tree->match(nodeData->rule->tree, ncc, text);
    switchRoutes(&ncc->matchRoute, &ncc->tempRoute1);

    // Return immediately if no match,
    if (matchLength==-1) return -1;

    // Match next nodes,
    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    if (nextNodeMatchLength==-1) {
        NVector.resize(ncc->tempRoute1, tempRouteMark);
        return -1;
    }

    // Push 0 to mark the end of the rule route,
    struct NCC_Node* termination=0;
    NVector.pushBack(ncc->matchRoute, &termination);

    // Push the rule route,
    pushTempRouteIntoMatchRoute(ncc, ncc->tempRoute1, tempRouteMark);

    // Push this node,
    NVector.pushBack(ncc->matchRoute, &node);
    return matchLength + nextNodeMatchLength;
}

static int32_t substituteNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct SubstituteNodeData *nodeData = node->data;

    // Remember the variables stack position,
    int32_t variablesStackPosition = NVector.size(&ncc->variables);

    // Match,
    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    int32_t matchLength = nextNode ? nextNode->followMatchRoute(nextNode, ncc, text) : 0; // This substitute node can by empty.

    // Perform rule action,
    ncc->currentCallStackBeginning = variablesStackPosition;
    int32_t newVariablesCount = NVector.size(&ncc->variables) - variablesStackPosition;
    if (nodeData->rule->onMatchListener) nodeData->rule->onMatchListener(ncc, &nodeData->rule->name, newVariablesCount);

    // Pop any variables that were not popped,
    int remainingVariablesCount = NVector.size(&ncc->variables) - variablesStackPosition;
    for (;remainingVariablesCount; remainingVariablesCount--) {
        // Possible performance improvement: Destroying variables in place without popping, then
        // adjusting the vector size. Or even better, reusing variables.
        struct NCC_Variable currentVariable;
        NVector.popBack(&ncc->variables, &currentVariable);
        NCC_destroyVariable(&currentVariable);
    }

    // Save the match,
    char *matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeFollowMatchRoute() matchedText");
    NSystemUtils.memcpy(matchedText, text, matchLength);
    matchedText[matchLength] = 0;

    struct NCC_Variable match;
    NCC_initializeVariable(&match, NString.get(&nodeData->rule->name), matchedText);
    NFREE(matchedText, "NCC.substituteNodeFollowMatchRoute() matchedText");
    NVector.pushBack(&ncc->variables, &match);

    // Follow next nodes,
    #ifdef NCC_VERBOSE
    NLOGI("NCC", "Visited substitute node %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&match.name), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&match.value), NTCOLOR(STREAM_DEFAULT));
    #endif
    nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? matchLength + nextNode->followMatchRoute(nextNode, ncc, &text[matchLength]) : matchLength;
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUBSTITUTE, nodeData, substituteNodeMatch, substituteNodeFollowMatchRoute);
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
    ncc->matchRoute = NVector.create(0, sizeof(struct NCC_Node*));
    ncc->tempRoute1 = NVector.create(0, sizeof(struct NCC_Node*));
    ncc->tempRoute2 = NVector.create(0, sizeof(struct NCC_Node*));
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
    NVector.destroyAndFree(ncc->matchRoute);
    NVector.destroyAndFree(ncc->tempRoute1);
    NVector.destroyAndFree(ncc->tempRoute2);
}

void NCC_destroyAndFreeNCC(struct NCC* ncc) {
    NCC_destroyNCC(ncc);
    NFREE(ncc, "NCC.NCC_destroyAndFreeNCC() ncc");
}

boolean NCC_addRule(struct NCC* ncc, const char* name, const char* ruleText, NCC_onMatchListener onMatchListener, boolean rootRule) {

    // TODO: check for existing name...

    struct NCC_Rule* rule = createRule(ncc, name, ruleText, onMatchListener, rootRule);
    if (!rule) {
        NERROR("NCC", "NCC_addRule(): unable to create rule: %s%s%s", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    NVector.pushBack(&ncc->rules, &rule);
    return True;
}

static struct NCC_Rule* getRule(struct NCC* ncc, const char* ruleName) {

    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {
        struct NCC_Rule* currentRule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        if (NCString.equals(ruleName, NString.get(&currentRule->name))) return currentRule;
    }
    return 0;
}

static void switchRoutes(struct NVector** route1, struct NVector** route2) {
    struct NVector* temp = *route1;
    *route1 = *route2;
    *route2 = temp;
}

static void pushTempRouteIntoMatchRoute(struct NCC* ncc, struct NVector* tempRoute, int32_t tempRouteMark) {
    // Pops from the temp route until the mark then pushes them into the match route.
    int32_t currentMatchRoutePosition = NVector.size(ncc->matchRoute);
    int32_t newNodesCount = NVector.size(tempRoute) - tempRouteMark;
    NVector.resize(ncc->matchRoute, currentMatchRoutePosition + newNodesCount);
    NSystemUtils.memcpy(
            NVector.get(ncc->matchRoute, currentMatchRoutePosition),
            NVector.get(tempRoute, tempRouteMark),
            newNodesCount*sizeof(struct NCC_Node*));
    NVector.resize(tempRoute, tempRouteMark);
}

int32_t NCC_match(struct NCC* ncc, const char* text) {

    // Find the longest match,
    int32_t maxMatchLength=-1;
    struct NCC_Rule* maxMatchRule=0;
    struct NVector maxMatchRoute;
    NVector.initialize(&maxMatchRoute, 0, sizeof(struct NCC_Node*));

    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {

        struct NCC_Rule* rule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        if (!rule->rootRule) continue;

        // Reset routes,
        NVector.reset(ncc->matchRoute);
        NVector.reset(ncc->tempRoute1);
        NVector.reset(ncc->tempRoute2);

        // Match rule,
        int32_t matchLength = rule->tree->match(rule->tree, ncc, text);
        if (matchLength > maxMatchLength) {
            // Keep route,
            maxMatchLength = matchLength;
            maxMatchRule = rule;
            NVector.resize(&maxMatchRoute, NVector.size(ncc->matchRoute));
            NSystemUtils.memcpy(
                    NVector.get( &maxMatchRoute, 0),
                    NVector.get(ncc->matchRoute, 0),
                    NVector.size(ncc->matchRoute)*sizeof(struct NCC_Node*));
        }
    }

    if (maxMatchLength==-1) goto conclude;

    // Follow the longest match route,
    if (NVector.size(&maxMatchRoute)) {

        // Set the match route,
        NVector.reset(ncc->matchRoute);
        pushTempRouteIntoMatchRoute(ncc, &maxMatchRoute, 0);

        // Follow route,
        struct NCC_Node* routeStart;
        NVector.popBack(ncc->matchRoute, &routeStart);
        routeStart->followMatchRoute(routeStart, ncc, text);
    }

conclude:

    // Perform rule action,
    ncc->currentCallStackBeginning = 0;
    if (maxMatchRule && maxMatchRule->onMatchListener) maxMatchRule->onMatchListener(ncc, &maxMatchRule->name, NVector.size(&ncc->variables));

    // Empty the variables stack,
    for (int32_t i=NVector.size(&ncc->variables)-1; i>=0; i--) NCC_destroyVariable(NVector.get(&ncc->variables, i));
    NVector.reset(&ncc->variables);

    // Destroy the max match route,
    NVector.destroy(&maxMatchRoute);

    return maxMatchLength;
}

boolean NCC_popVariable(struct NCC* ncc, struct NCC_Variable* outVariable) {

    if (NVector.size(&ncc->variables) <= ncc->currentCallStackBeginning) return False;

    // The variable needn't be initialized, we'll pop into it,
    if (!NVector.popBack(&ncc->variables, outVariable)) return False;

    return True;
}
