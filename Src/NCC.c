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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: add an NVector<NCC_Node*> for the next node log in every node. This
// marks the correct match path, and can be followed later to do the code generation...
struct NCC_Node {
    int32_t type;
    void *data;
    struct NCC_Node* previousNode;
    struct NCC_Node*     nextNode;
    int32_t (*match)(struct NCC_Node* node, struct NCC* ncc, const char* text); // Returns match length if matched, 0 if rejected.
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
};

struct NCC_Rule* createRule(struct NCC* ncc, const char* name, const char* ruleText) {

    // Create rule tree,
    struct NCC_Node* ruleTree = constructRuleTree(ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "createRule(): unable to construct rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create rule,
    struct NCC_Rule* rule = NSystemUtils.malloc(sizeof(struct NCC_Rule));

    // Set name,
    NString.initialize(&rule->name);
    NString.set(&rule->name, "%s", name);

    // Set tree,
    rule->tree = ruleTree;

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

static struct NCC_Node* genericCreateNode(int32_t type, void* data, int32_t (*matchMethod)(struct NCC_Node* node, struct NCC* ncc, const char* text)) {
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
    // Reaching accept node means that the strings matches the rule, even if the string is not over
    // yet,
    return 0;
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

static int32_t literalNodeMatch(struct NCC_Node* node, struct NCC* ncc, const char* text) {
    struct LiteralNodeData* nodeData = node->data;
    if (*text != nodeData->literal) return -1;
    int32_t matchLength = node->nextNode->match(node->nextNode, ncc, &text[1]);
    return matchLength!=-1 ? matchLength+1 : -1;
}

static struct NCC_Node* createLiteralNode(const char literal) {
    struct LiteralNodeData* nodeData = NSystemUtils.malloc(sizeof(struct LiteralNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL, nodeData, literalNodeMatch);
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
    return matchLength!=-1 ? matchLength+1 : -1;
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

    int32_t rhsMatchLength = nodeData->rhsTree->match(nodeData->rhsTree, ncc, text);
    int32_t lhsMatchLength = nodeData->lhsTree->match(nodeData->lhsTree, ncc, text);

    int32_t matchLength = rhsMatchLength > lhsMatchLength ? rhsMatchLength : lhsMatchLength;
    if (matchLength==-1) return -1;

    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    return nextNodeMatchLength!=-1 ? matchLength + nextNodeMatchLength : -1;
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData, orNodeMatch);
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

    int32_t matchLength = nodeData->subRuleTree->match(nodeData->subRuleTree, ncc, text);
    if (matchLength==-1) return -1;

    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    return nextNodeMatchLength!=-1 ? matchLength + nextNodeMatchLength : -1;
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUB_RULE, nodeData, subRuleNodeMatch);
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

    int32_t totalMatchLength=0;
    do {
        // Check if the following sub-rule matches,
        // TODO: should reset the following sub-rule first...
        int32_t followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, &text[totalMatchLength]);
        if (followingSubRuleMatchLength>0) return totalMatchLength + followingSubRuleMatchLength;

        // Following sub-rule didn't match, attempt repeating,
        int32_t matchLength = nodeData->repeatedNode->match(nodeData->repeatedNode, ncc, &text[totalMatchLength]);
        if (matchLength<1) return followingSubRuleMatchLength==0 ? totalMatchLength : -1;
        totalMatchLength += matchLength;
    } while (True);
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
    // TODO: for now, we only support *.
    if (repeatCount != '*') {
        NERROR("NCC", "createRepeatNode(): expecting %s*%s after %s^%s, found %s%c%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), repeatCount, NTCOLOR(STREAM_DEFAULT));
        return 0;
    } else {
        // Skip the *,
        (*in_out_rule)++;
    }

    // Create node,
    struct RepeatNodeData* nodeData = NSystemUtils.malloc(sizeof(struct RepeatNodeData));
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.REPEAT, nodeData, repeatNodeMatch);
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

    int32_t totalMatchLength=0;
    do {
        // Check if the following sub-rule matches,
        // TODO: should reset the following sub-rule first...
        int32_t followingSubRuleMatchLength = nodeData->followingSubRule->match(nodeData->followingSubRule, ncc, &text[totalMatchLength]);
        if (followingSubRuleMatchLength>0) return totalMatchLength + followingSubRuleMatchLength;

        // Following sub-rule didn't match,
        // If text ended,
        if (!text[totalMatchLength]) return followingSubRuleMatchLength==0 ? totalMatchLength : -1;

        // Text didn't end, advance,
        totalMatchLength++;
    } while (True);
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.ANYTHING, nodeData, anythingNodeMatch);
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

    // Remember the variables stack position,
    int32_t variablesStackPosition = NVector.size(&ncc->variables);

    // Match,
    int32_t matchLength = nodeData->rule->tree->match(nodeData->rule->tree, ncc, text);
    // TODO: perform rule action, passing along the stack position...

    // Pop any pushed variables,
    int32_t newVariablesCount = NVector.size(&ncc->variables) - variablesStackPosition;
    for (;newVariablesCount; newVariablesCount--) {
        struct NCC_Variable currentVariable;
        NVector.popBack(&ncc->variables, &currentVariable);
        destroyVariable(&currentVariable);
    }

    // Return immediately if no match,
    if (matchLength == -1) return -1;

    // A match occurred, save it,
    char *matchedText = NSystemUtils.malloc(matchLength+1);
    NSystemUtils.memcpy(matchedText, text, matchLength);
    matchedText[matchLength] = 0;

    struct NCC_Variable match;
    initializeVariable(&match, NString.get(&nodeData->rule->name), matchedText);
    NSystemUtils.free(matchedText);
    NVector.pushBack(&ncc->variables, &match);

    // Match next nodes,
    int32_t nextNodeMatchLength = node->nextNode->match(node->nextNode, ncc, &text[matchLength]);
    return nextNodeMatchLength!=-1 ? matchLength + nextNodeMatchLength : -1;
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
    struct NCC_Node* node = genericCreateNode(NCC_NodeType.SUBSTITUTE, nodeData, substituteNodeMatch);
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
}

void NCC_destroyAndFreeNCC(struct NCC* ncc) {
    NCC_destroyNCC(ncc);
    NSystemUtils.free(ncc);
}

boolean NCC_addRule(struct NCC* ncc, const char* name, const char* ruleText) {

    // TODO: check for existing name...

    struct NCC_Rule* rule = createRule(ncc, name, ruleText);
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

int32_t NCC_match(struct NCC* ncc, const char* text) {

    // TODO: perform actions as well...

    // Find the longest match,
    int32_t maxMatchLength=-1;
    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {
        struct NCC_Rule* rule = *((struct NCC_Rule**) NVector.get(&ncc->rules, i));
        int32_t matchLength = rule->tree->match(rule->tree, ncc, text);
        if (matchLength > maxMatchLength) maxMatchLength = matchLength;
    }

    return maxMatchLength;
}
