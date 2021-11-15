#include <NCC.h>
#include <NSystemUtils.h>
#include <NError.h>
#include <NByteVector.h>
#include <NString.h>
#include <NCString.h>
#include <NVector.h>

//
// Rules:
//   Exact match   :         for             = for
//   Literals range:         smallLetter     = a-z
//   Or            :         letter          = a-z|A-Z
//   Repeat        :         name            = A-Za-z^*
//   Sub-rule      :         namesList       = {A-Za-z^*}|{{A-Za-z^*}{,A-Za-z^*}^*}
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
//   Note: now that we've removed the support for limited repeat, this ambiguity doesn't exist at all,
//         still, spaces must still be escaped, for they can be used to make rules look a lot cleaner.
//
// Node types:
//   literal:         a
//   or:              |
//   literals range:  a-z
//   repeat:          ^*
//   sub-rule:        ${name} or {rule}
//   anything:        *   or  * followed by something

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
    int32_t ROOT, ACCEPT, LITERAL, OR, LITERALS_RANGE, REPEAT, SUB_RULE, SUBSTITUTE, ANYTHING;
};
const struct NCC_NodeType NCC_NodeType = {
        .ROOT = 0,
        .ACCEPT = 1,
        .LITERAL = 2,
        .OR = 3,
        .LITERALS_RANGE = 4,
        .REPEAT = 5,
        .SUB_RULE = 6,
        .SUBSTITUTE = 7,
        .ANYTHING = 8
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variable
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_Variable {
    struct NString name;
    struct NString value;
};

static struct NCC_Variable* initializeVariable(struct NCC_Variable* variable, const char* name, const char* value) {
    NString.initialize(&variable->name );
    NString.initialize(&variable->value);
    NString.set(&variable->name , "%s",  name);
    NString.set(&variable->value, "%s", value);
    return variable;
}

static void destroyVariable(struct NCC_Variable* variable) {
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
    struct NCC_Rule* rule = NSystemUtils.malloc(sizeof(struct NCC_Rule));

    NString.initialize(&rule->name);
    NString.set(&rule->name, "%s", name);

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
    NSystemUtils.free(rule);
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
    NSystemUtils.free(tree);
}

static void genericDeleteTreeWithData(struct NCC_Node* tree) {
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

typedef int32_t (*      MatchMethod)(struct NCC_Node* node, struct NCC* ncc, const char* text);
typedef int32_t (*FollowMatchMethod)(struct NCC_Node* node, struct NCC* ncc, const char* text);

static int32_t genericNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) { return 0; }

static struct NCC_Node* genericCreateNode(int32_t type, void* data, MatchMethod matchMethod, FollowMatchMethod followMatchMethod) {
    struct NCC_Node* node = NSystemUtils.malloc(sizeof(struct NCC_Node));
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

static struct NCC_Node* createAcceptNode() {
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ACCEPT, 0, acceptNodeMatch, 0);
    node->setNextNode = acceptNodeSetNextNode;
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literal node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralNodeData {
    char literal;
};

static int32_t literalNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct LiteralNodeData* nodeData = node->data;
    if (*text != nodeData->literal) return -1;
    int32_t matchLength = node->nextNode->match(node->nextNode, ncc, &text[1]);
    if (matchLength==-1) return -1;
    NVector.pushBack(ncc->matchRoute, &node);
    return matchLength+1;
}

static int32_t literalNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    // TODO: enable only when verbose...
    if (text[0] == ' ') {
        NLOGI("NCC", "Visited literal node: %sSpace%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
    } else if (text[0] == '\n') {
        NLOGI("NCC", "Visited literal node: %sLine break%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
    } else if (text[0] == '\t') {
        NLOGI("NCC", "Visited literal node: %sTab%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
    } else {
        NLOGI("NCC", "Visited literal node: %s%c%s", NTCOLOR(HIGHLIGHT), text[0], NTCOLOR(STREAM_DEFAULT));
    }

    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? 1 + nextNode->followMatchRoute(nextNode, ncc, &text[1]) : 1;
}

static struct NCC_Node* createLiteralNode(const char literal) {
    struct LiteralNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL, nodeData, literalNodeMatch, literalNodeFollowMatchRoute);
    nodeData->literal = literal;

    // TODO: remove...
    NLOGI("NCC", "Created literal node: %s%c%s", NTCOLOR(HIGHLIGHT), literal, NTCOLOR(STREAM_DEFAULT));
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literals range node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct LiteralsRangeNodeData {
    char rangeStart, rangeEnd;
};

static int32_t literalsRangeNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct LiteralsRangeNodeData* nodeData = node->data;
    char literal = *text;
    if ((literal < nodeData->rangeStart) || (literal > nodeData->rangeEnd)) return -1;
    int32_t matchLength = node->nextNode->match(node->nextNode, ncc, &text[1]);
    if (matchLength==-1) return -1;
    NVector.pushBack(ncc->matchRoute, &node);
    return matchLength+1;
}

static int32_t literalsRangeNodeFollowMatchRoute(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    NLOGI("NCC", "Visited literals-range node: %s%c%s", NTCOLOR(HIGHLIGHT), text[0], NTCOLOR(STREAM_DEFAULT));
    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? 1 + nextNode->followMatchRoute(nextNode, ncc, &text[1]) : 1;
}

static struct NCC_Node* createLiteralsRangeNode(char rangeStart, char rangeEnd) {

    struct LiteralsRangeNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralsRangeNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS_RANGE, nodeData, literalsRangeNodeMatch, literalsRangeNodeFollowMatchRoute);
    if (rangeStart > rangeEnd) {
        char temp = rangeStart;
        rangeStart = rangeEnd;
        rangeEnd = temp;
    }
    nodeData->rangeStart = rangeStart;
    nodeData->rangeEnd = rangeEnd;

    // TODO: remove...
    NLOGI("NCC", "Created range literal node: %s%c-%c%s", NTCOLOR(HIGHLIGHT), rangeStart, rangeEnd, NTCOLOR(STREAM_DEFAULT));
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
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

static struct NCC_Node* createOrNode(struct NCC* ncc, struct NCC_Node* parentNode, const char** in_out_rule) {

    // Get grand-parent node,
    struct NCC_Node* grandParentNode = parentNode->getPreviousNode(parentNode);
    if (!grandParentNode) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    struct OrNodeData* nodeData = NSystemUtils.malloc(sizeof(struct OrNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData, orNodeMatch, 0);
    node->deleteTree = orNodeDeleteTree;

    // Remove parent from the grand-parent and attach this node instead,
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
    struct NCC_Node* rhsNode = getNextNode(ncc, nodeData->rhsTree, in_out_rule);
    if (!rhsNode) {
        NERROR("NCC", "createOrNode(): couldn't create an rhs node: %s%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }
    rhsNode->setNextNode(rhsNode, createAcceptNode());

    // TODO: remove...
    NLOGI("NCC", "Created or node: %s|%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
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
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

static struct NCC_Node* createSubRuleNode(struct NCC* ncc, const char** in_out_rule) {

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
    struct SubRuleNodeData* nodeData = NSystemUtils.malloc(sizeof(struct SubRuleNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUB_RULE, nodeData, subRuleNodeMatch, 0);
    node->deleteTree = subRuleNodeDeleteTree;
    nodeData->subRuleTree = subRuleTree;

    // TODO: remove...
    NLOGI("NCC", "Created sub-rule node: %s{%s}%s", NTCOLOR(HIGHLIGHT), subRule.objects, NTCOLOR(STREAM_DEFAULT));
    NByteVector.destroy(&subRule);

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
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
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
    struct RepeatNodeData* nodeData = NSystemUtils.malloc(sizeof(struct RepeatNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.REPEAT, nodeData, repeatNodeMatch, 0);
    node->deleteTree = repeatNodeDeleteTree;

    // Remove parent from the grand-parent and attach this node instead,
    grandParentNode->setNextNode(grandParentNode, node);

    // Turn parent node into a tree and attach it as the repeated node,
    nodeData->repeatedNode = createRootNode();
    nodeData->repeatedNode->setNextNode(nodeData->repeatedNode, parentNode);
    parentNode->setNextNode(parentNode, createAcceptNode());

    // Create a new tree for the remaining text and set it as the following sub-rule,
    nodeData->followingSubRule = constructRuleTree(ncc, *in_out_rule);
    if (!nodeData->followingSubRule) {
        NERROR("NCC", "createRepeatNode(): Couldn't create the following sub-rule tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
        return 0;  // Since this node is already attached to the tree, it gets cleaned up automatically.
    }

    // The remainder of the tree was already added to the following sub-rule, no need to continue parsing,
    while (**in_out_rule) (*in_out_rule)++;

    // TODO: remove...
    NLOGI("NCC", "Created repeat node: %s^*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
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
        followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, &text[totalMatchLength]);
        if (followingSubRuleMatchLength>0) goto conclude;

        // Following sub-rule didn't match,
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
    NLOGI("NCC", "Visited anything node: %s%c%s", NTCOLOR(HIGHLIGHT), text[0], NTCOLOR(STREAM_DEFAULT));
    struct NCC_Node* nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? 1 + nextNode->followMatchRoute(nextNode, ncc, &text[1]) : 1;
}

static void anythingNodeDeleteTree(struct NCC_Node* tree) {
    struct AnythingNodeData* nodeData = tree->data;
    nodeData->followingSubRule->deleteTree(nodeData->followingSubRule);
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

static struct NCC_Node* createAnythingNode(struct NCC* ncc, const char** in_out_rule) {

    // Skip the *,
    (*in_out_rule)++;

    // Create a new tree for the remaining text and set it as the following sub-rule,
    struct NCC_Node* followingSubRule = constructRuleTree(ncc, *in_out_rule);
    if (!followingSubRule) {
        NERROR("NCC", "createAnythingNode(): Couldn't create the remaining sub-rule tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    struct AnythingNodeData* nodeData = NSystemUtils.malloc(sizeof(struct AnythingNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ANYTHING, nodeData, anythingNodeMatch, anythingNodeFollowMatchRoute);
    node->deleteTree = anythingNodeDeleteTree;
    nodeData->followingSubRule = followingSubRule;

    // The remainder of the tree was already added to the following sub-rule, no need to continue parsing,
    while (**in_out_rule) (*in_out_rule)++;

    // TODO: remove...
    NLOGI("NCC", "Created anything node: %s*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
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
        destroyVariable(&currentVariable);
    }

    // Save the match,
    char *matchedText = NSystemUtils.malloc(matchLength+1);
    NSystemUtils.memcpy(matchedText, text, matchLength);
    matchedText[matchLength] = 0;

    struct NCC_Variable match;
    initializeVariable(&match, NString.get(&nodeData->rule->name), matchedText);
    NSystemUtils.free(matchedText);
    NVector.pushBack(&ncc->variables, &match);

    // Follow next nodes,
    NLOGI("NCC", "Visited substitute node %s%s%s: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&match.name), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&match.value), NTCOLOR(STREAM_DEFAULT));
    nextNode=0; NVector.popBack(ncc->matchRoute, &nextNode);
    return nextNode ? matchLength + nextNode->followMatchRoute(nextNode, ncc, &text[matchLength]) : matchLength;
}

static void substituteNodeDeleteTree(struct NCC_Node* tree) {
    // Note: we don't free the rules, we didn't allocate them.
    if (tree->nextNode) tree->nextNode->deleteTree(tree->nextNode);
    NSystemUtils.free(tree->data);
    NSystemUtils.free(tree);
}

static struct NCC_Node* createSubstituteNode(struct NCC* ncc, const char** in_out_rule) {

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
    char *ruleName = NSystemUtils.malloc(ruleNameLength+1);
    NSystemUtils.memcpy(ruleName, ruleNameBeginning, ruleNameLength);
    ruleName[ruleNameLength] = 0;

    // Look for a match within our defined rules,
    struct NCC_Rule* rule = getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "createSubstituteNode(): couldn't find a rule named: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        NSystemUtils.free(ruleName);
        return 0;
    }

    // Create the node,
    struct SubstituteNodeData* nodeData = NSystemUtils.malloc(sizeof(struct SubstituteNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUBSTITUTE, nodeData, substituteNodeMatch, substituteNodeFollowMatchRoute);
    node->deleteTree = substituteNodeDeleteTree;
    nodeData->rule = rule;

    // TODO: remove...
    NLOGI("NCC", "Created substitute node: %s${%s}%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));

    NSystemUtils.free(ruleName);
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

    struct NCC_Node* node;
    switch (currentChar) {
        case 0:
            node = createAcceptNode();
            break;
        case '$':
            node = createSubstituteNode(ncc, in_out_rule); break;
        case '*':
            node = createAnythingNode(ncc, in_out_rule); break;
        case '{':
            node = createSubRuleNode(ncc, in_out_rule); break;
        case '^':
            node = createRepeatNode(ncc, parentNode, in_out_rule); break;
        case '|':
            node = createOrNode(ncc, parentNode, in_out_rule); break;
        case '-':
            NERROR("NCC", "getNextNode(): a '%s-%s' must always be preceded by a literal", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        default: {
            node = handleLiteral(in_out_rule); break;
        }
    }

    // Only "Or" and "Repeat" nodes attach themselves,
    if (node && (node->type!=NCC_NodeType.OR) && (node->type!=NCC_NodeType.REPEAT)) parentNode->setNextNode(parentNode, node);

    return node;
}

struct NCC* NCC_initializeNCC(struct NCC* ncc) {
    NVector.initialize(0, sizeof(struct NCC_Rule*), &ncc->rules);
    NVector.initialize(0, sizeof(struct NCC_Variable), &ncc->variables);
    ncc->matchRoute = NVector.create(0, sizeof(struct NCC_Node*));
    ncc->tempRoute1 = NVector.create(0, sizeof(struct NCC_Node*));
    ncc->tempRoute2 = NVector.create(0, sizeof(struct NCC_Node*));
    return ncc;
}

struct NCC* NCC_createNCC() {
    struct NCC* ncc = NSystemUtils.malloc(sizeof(struct NCC));
    return NCC_initializeNCC(ncc);
}

void NCC_destroyNCC(struct NCC* ncc) {

    // Rules,
    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) destroyAndFreeRule(*((struct NCC_Rule**) NVector.get(&ncc->rules, i)));
    NVector.destroy(&ncc->rules);

    // Variables,
    for (int32_t i=NVector.size(&ncc->variables)-1; i>=0; i--) destroyVariable(NVector.get(&ncc->variables, i));
    NVector.destroy(&ncc->variables);

    // Routes,
    NVector.destroyAndFree(ncc->matchRoute);
    NVector.destroyAndFree(ncc->tempRoute1);
    NVector.destroyAndFree(ncc->tempRoute2);
}

void NCC_destroyAndFreeNCC(struct NCC* ncc) {
    NCC_destroyNCC(ncc);
    NSystemUtils.free(ncc);
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
    NVector.initialize(0, sizeof(struct NCC_Node*), &maxMatchRoute);

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
    //NLOGE("sdf", "Size: %d", NVector.size(&maxMatchRoute));
    if (NVector.size(&maxMatchRoute)) {

        // Set the match route,
        NVector.reset(ncc->matchRoute);
        pushTempRouteIntoMatchRoute(ncc, &maxMatchRoute, 0);

        // Follow route,
        struct NCC_Node* routeStart;
        NVector.popBack(ncc->matchRoute, &routeStart);
        //NLOGE("sdf", "%d", routeStart->type);
        routeStart->followMatchRoute(routeStart, ncc, text);
    }

conclude:

    // Perform rule action,
    ncc->currentCallStackBeginning = 0;
    if (maxMatchRule && maxMatchRule->onMatchListener) maxMatchRule->onMatchListener(ncc, &maxMatchRule->name, NVector.size(&ncc->variables));

    // Empty the variables stack,
    for (int32_t i=NVector.size(&ncc->variables)-1; i>=0; i--) destroyVariable(NVector.get(&ncc->variables, i));
    NVector.reset(&ncc->variables);

    // Destroy the max match route,
    NVector.destroy(&maxMatchRoute);

    return maxMatchLength;
}

boolean NCC_popVariable(struct NCC* ncc, struct NString* outName, struct NString* outValue) {

    if (NVector.size(&ncc->variables) <= ncc->currentCallStackBeginning) return False;

    struct NCC_Variable variable; // Needn't be initialized, we'll pop into it.
    if (!NVector.popBack(&ncc->variables, &variable)) return False;
    NString.set(outName , "%s", NString.get(&variable.name ));
    NString.set(outValue, "%s", NString.get(&variable.value));
    destroyVariable(&variable);

    return True;
}
