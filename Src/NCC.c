#include <NCC.h>

#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>
#include <NVector.h>

#ifndef NCC_VERBOSE
#define NCC_VERBOSE 0
#endif

//
// You might want to read the documentation in NCC.h before reading this.
//

// We have 2 types of trees:
//   - Rule tree:
//       => The product of parsing rule text. It's what language definition is all about.
//       => Implementation detail, totally transparent to the user. A user may as well not know they
//          exist at all.
//       => Can have sub-trees. When a text is matched, it goes through a specific path through the tree.
//       => The result of matching text is simple:
//            -> Whether matching was successful or not.
//            -> The length of the matched text.
//          Sometimes this is not very useful, as it doesn't give much information about the match.
//          That's where ASTs come into play, to identify the exact path that the text took through
//          our rule tree.
//   - AST:
//       => As we define our rules, we can also provide listeners to create/delete AST nodes. These
//          nodes detail the path which the subject text took while propagating through our rule tree.
//       => Users have to parse these manually, or implement their logic in the AST node match listeners.
//
// In our implementation, trees nodes are regular graph nodes. However, we as we construct AST tree
// nodes, we also push them to stacks. This makes it easier to cull an entire branch of the tree
// (roll it back) if we took a wrong turn while matching. Lots of the matching is based on trial. If
// a path fails, we cull its branch and try the next one. If multiple paths match, we take the
// longest match and cull the others.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Macros, types and prototypes
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct NCC_Node NCC_Node;

static NCC_Node* constructRuleTree(struct NCC* ncc, const char* ruleText);
static NCC_Node* getNextNode(struct NCC* ncc, NCC_Node* parentNode, const char** in_out_rule);
static void switchStacks(struct NVector** stack1, struct NVector** stack2);
static void pushASTStack(struct NCC* ncc, struct NVector* stack, int32_t stackMark);

// Matching result with extra details (AST related details) that are necessary for implementation
// only and not exposed to the user,
typedef struct MatchedASTTree {
    NCC_MatchingResult result;
    NCC_ASTNode_Data* astParentNode;
    struct NVector **astNodesStack;
    uint32_t astStackMark;
} MatchedASTTree;

static boolean matchRuleTree(
        struct NCC* ncc, NCC_Node* ruleTree, const char* text,
        MatchedASTTree* outMatchingResult, NCC_ASTNode_Data* astParentNode, struct NVector** astStack,
        int32_t lengthToAddIfTerminated, MatchedASTTree** astTreesToDiscardIfTerminated, int32_t astTreesToDiscardCount);
static void discardMatchingResult(MatchedASTTree* tree);

// A convenient macro to be used inside node matching methods. It creates 2 variables to capture
// the results of matching (treeName and treeNameMatched) and automatically handles termination,
#define COMMA , // See: https://stackoverflow.com/questions/20913103/is-it-possible-to-pass-a-brace-enclosed-initializer-as-a-macro-parameter#comment31397917_20913103
#define MatchTree(treeName, ruleTree, text, astParentNode, nccStack, lengthToAddIfTerminated, deleteList, deleteCount) \
    MatchedASTTree treeName; \
    boolean treeName ## Matched = matchRuleTree( \
            ncc, ruleTree, text, \
            &treeName, astParentNode, &ncc->nccStack, \
            lengthToAddIfTerminated, (MatchedASTTree*[]) deleteList, deleteCount); \
    if (treeName.result.terminate) { \
        *outResult = treeName.result; \
        return treeName ## Matched; \
    }

#define DiscardMatchingResult(tree) discardMatchingResult(tree);

// Pushes the matched ast tree into NCC's stack 0 and adjusts the match length,
#define AcceptMatchResult(tree) { \
    pushASTStack(ncc, *(tree).astNodesStack, (tree).astStackMark); \
    outResult->matchLength += (tree).result.matchLength; }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A little trick to make an enum into an object,
struct NCC_NodeType {
    int32_t ROOT, LITERALS, LITERAL_RANGE, OR, SUB_RULE, REPEAT, ANYTHING, SUBSTITUTE, SELECTION;
};
const struct NCC_NodeType NCC_NodeType = {
    .ROOT = 0,              // The topmost node of rules trees. Exists for convenience, so that all
                            // the other node types can rely on having a parent node. Doesn't do
                            // anything in particular.
    .LITERALS = 1,          // Matches a single or multiple characters exactly. Example: abc
    .LITERAL_RANGE = 2,     // Matches a single character in the specified range. Example: a-z
    .OR = 3,                // Extracts the previous and next nodes. Checks which one will result in
                            // the longest match (doesn't just compare node match lengths, but the
                            // entire tree). Example:
                            //    rule: {ab}|{abc}cdef
                            //    text: abcdef
    .SUB_RULE = 4,          // Groups (and isolates) several nodes into 1. Example: {A-Za-z^*}
    .REPEAT = 5,            // Repeats the previous node 0 or more times. {A-Z|a-z|0-9}^*
    .ANYTHING = 6,          // Matches nothing or everything until the following tree is matched (if
                            // there is a following tree) or the end of text is encountered.
                            // Example: {A-Z|a-z|0-9}^*END
                            // Note: whether there's a following tree or not is tricky, read the
                            // "Wildcard nodes" and "Or nodes" explanation in "NCC.h".
    .SUBSTITUTE = 7,        // Subrule with a name. Fires listeners to create and manipulate AST
                            // nodes as it matches. Example: ${Identifier}
    .SELECTION = 8          // Tries a bunch of different named rules (attempted rules list), gets
                            // the longest match, then either:
                            //   => accepts it. Or,
                            //   => accepts it only if it belongs to a subset of the initial
                            //      attempted rules list (verification rules list).
                            //   => rejects it only if it belongs to a subset of the initial
                            //      attempted rules list (verification rules list).
                            // Example: #{{+}{-}{~}{!} {++}{--} != {++}{--}}
                            // See NCC.h for more.
};

// Nodes of the rule trees,
typedef struct NCC_Node {
    int32_t type;
    void *data;
    NCC_Node* previousNode;
    NCC_Node*     nextNode;
} NCC_Node;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Node methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// In this section, we manually implement virtual functions. We need to be able to call the correct
// code without checking the node type. We create a lookup table for every function. The table can
// be indexed by the node type to get the correct function.

// Some functions' implementations work across multiple node types. We call these generic and reuse them,
static void genericSetNextNode(NCC_Node* node, NCC_Node* nextNode);

// Node specific implementations used to populate the function lookup tables,
static boolean rootNodeMatch             (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    rootNodeDeleteTree        (NCC_Node* tree);

static boolean literalsNodeMatch         (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    literalsNodeDeleteTree    (NCC_Node* tree);

static boolean literalRangeNodeMatch     (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    literalRangeNodeDeleteTree(NCC_Node* tree);

static boolean orNodeMatch               (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    orNodeDeleteTree          (NCC_Node* tree);

static boolean subRuleNodeMatch          (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    subRuleNodeDeleteTree     (NCC_Node* tree);

static boolean repeatNodeMatch           (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    repeatNodeDeleteTree      (NCC_Node* tree);

static boolean anythingNodeMatch         (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    anythingNodeDeleteTree    (NCC_Node* tree);

static boolean substituteNodeMatch       (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    substituteNodeDeleteTree  (NCC_Node* tree);

static boolean selectionNodeMatch        (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
static void    selectionNodeDeleteTree   (NCC_Node* tree);

// Actual tables,
typedef boolean (*NCC_Node_match     )   (NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult);
typedef void    (*NCC_Node_deleteTree)   (NCC_Node* tree);

/*╔═══════════════════════════════╤════════════════════╤═════════════════════════╤═════════════════════════════╤═════════════════════════════════╤═══════════════════════╤════════════════════════════╤═══════════════════════════╤═════════════════════════════╤═══════════════════════════════╤═══════════════════════════════╗*/
/*║   Method                       ╲   Node            │   Root                  │   Literals                  │   Literals range                │   Or                  │   Sub-rule                 │   Repeat                  │   Anything                  │   Substitute                  │   Selection                   ║*/
/*╟─────────────────────────────────┴──────────────────┼─────────────────────────┼─────────────────────────────┼─────────────────────────────────┼───────────────────────┼────────────────────────────┼───────────────────────────┼─────────────────────────────┼───────────────────────────────┼───────────────────────────────╢*/
/*║*/ static NCC_Node_match      nodeMatch     [] = {/*│*/ rootNodeMatch     , /*│*/ literalsNodeMatch     , /*│*/ literalRangeNodeMatch     , /*│*/ orNodeMatch     , /*│*/ subRuleNodeMatch     , /*│*/ repeatNodeMatch     , /*│*/ anythingNodeMatch     , /*│*/ substituteNodeMatch     , /*│*/ selectionNodeMatch     }; /*║*/
/*║*/ static NCC_Node_deleteTree nodeDeleteTree[] = {/*│*/ rootNodeDeleteTree, /*│*/ literalsNodeDeleteTree, /*│*/ literalRangeNodeDeleteTree, /*│*/ orNodeDeleteTree, /*│*/ subRuleNodeDeleteTree, /*│*/ repeatNodeDeleteTree, /*│*/ anythingNodeDeleteTree, /*│*/ substituteNodeDeleteTree, /*│*/ selectionNodeDeleteTree}; /*║*/
/*╚════════════════════════════════════════════════════╧═════════════════════════╧═════════════════════════════╧═════════════════════════════════╧═══════════════════════╧════════════════════════════╧═══════════════════════════╧═════════════════════════════╧═══════════════════════════════╧═══════════════════════════════╝*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rule
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// NCC_Rule implementation depends on NCC_Node definition, and NCC_Node implementation depends on NCC_Rule. To avoid
// having to define on of them multiple times or out of context, we define and implement NCC_Rule here, just after
// NCC_Node is completely defined.

// NCC_Rule is an internal detail that is not exposed to the user. Only NCC_RuleData is exposed. A rule has:
//   => A name, which can be used to refer to it later in substitute nodes, and is very useful when parsing ASTs.
//   => A rule tree, constructed from the rule text specified by the user.
//   => AST creation and manipulation listeners (optional). AST nodes are the sole responsibility of the user. Yet,
//      we provide generic AST handling functions (See "Generic AST construction methods" in NCC.h).
typedef struct NCC_Rule {
    NCC_RuleData data; // We could have flattened the rule data here, but that would only add unnecessary complexity.
    NCC_Node* tree;
} NCC_Rule;

static NCC_RuleData* ruleDataSet(NCC_RuleData* ruleData, const char* ruleName, const char* ruleText) {
    NString.set(&ruleData->ruleName, "%s", ruleName);
    NString.set(&ruleData->ruleText, "%s", ruleText);
    return ruleData;
}

static NCC_RuleData* ruleDataSetListeners(NCC_RuleData* ruleData, NCC_createASTNodeListener createNodeListener, NCC_deleteASTNodeListener deleteNodeListener, NCC_ruleMatchListener matchListener) {
    ruleData->createASTNodeListener = createNodeListener;
    ruleData->deleteASTNodeListener = deleteNodeListener;
    ruleData->ruleMatchListener = matchListener;

    // Sanity check,
    if (createNodeListener && !deleteNodeListener) {
        NERROR("NCC", "ruleDataSetListeners(): a create AST node listener was provided with no delete listener for rule: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&ruleData->ruleName), NTCOLOR(STREAM_DEFAULT));
    } else if (!createNodeListener && deleteNodeListener) {
        NERROR("NCC", "ruleDataSetListeners(): a delete AST node listener was provided with no create listener for rule: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&ruleData->ruleName), NTCOLOR(STREAM_DEFAULT));
    }

    return ruleData;
}

NCC_RuleData* NCC_initializeRuleData(NCC_RuleData* ruleData, const char* ruleName, const char* ruleText, NCC_createASTNodeListener createNodeListener, NCC_deleteASTNodeListener deleteNodeListener, NCC_ruleMatchListener matchListener) {

    NString.initialize(&ruleData->ruleName, "%s", ruleName);
    NString.initialize(&ruleData->ruleText, "%s", ruleText);
    ruleData->createASTNodeListener = createNodeListener;
    ruleData->deleteASTNodeListener = deleteNodeListener;
    ruleData->ruleMatchListener = matchListener;

    ruleData->set = ruleDataSet;
    ruleData->setListeners = ruleDataSetListeners;

    // Sanity check,
    if (createNodeListener && !deleteNodeListener) {
        NERROR("NCC", "NCC_initializeRuleData(): a create AST node listener was provided with no delete listener for rule: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
    } else if (!createNodeListener && deleteNodeListener) {
        NERROR("NCC", "NCC_initializeRuleData(): a delete AST node listener was provided with no create listener for rule: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
    }

    return ruleData;
}

void NCC_destroyRuleData(NCC_RuleData* ruleData) {
    NString.destroy(&ruleData->ruleName);
    NString.destroy(&ruleData->ruleText);
}

static void destroyRule(NCC_Rule* rule) {
    NCC_destroyRuleData(&rule->data);

    // Deleting the parent node triggers deleting the children, hence the entire tree,
    nodeDeleteTree[rule->tree->type](rule->tree);
}

static void destroyAndFreeRule(NCC_Rule* rule) {
    destroyRule(rule);
    NFREE(rule, "NCC.destroyAndFreeRule() rule");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic node methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void genericSetNextNode(NCC_Node* node, NCC_Node* nextNode) {
    // Detach the current next node from this node,
    if (node->nextNode) node->nextNode->previousNode=0;

    // Set the new next node,
    node->nextNode = nextNode;

    // Set the new next node's previous node,
    if (nextNode) nextNode->previousNode = node;
}

// Every node is an NCC_Node, only the data attached differs. This creates the NCC_Node and sets the
// data. Every node create method calls this,
static NCC_Node* genericCreateNode(int32_t type, void* data) {
    NCC_Node* node = NMALLOC(sizeof(NCC_Node), "NCC.genericCreateNode() node");
    node->type = type;
    node->data = data;
    node->previousNode = 0;
    node->nextNode = 0;
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Root node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static boolean rootNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {

    // Root nodes don't do any matching themselves. If we have next nodes, we'll invoke them and be done,
    if (node->nextNode) {
        return nodeMatch[node->nextNode->type](node->nextNode, ncc, text, astParentNode, outResult);
    } else {
        // No tree to match, which matches everything and consumes 0 length. Just zero the result
        // and call it a day,
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        return True;  // Success!
    }
}

static void rootNodeDeleteTree(NCC_Node* tree) {
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree, "NCC.rootNodeDeleteTree() tree");
}

static NCC_Node* createRootNode() {
    NCC_Node* node = genericCreateNode(NCC_NodeType.ROOT, 0);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literals node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LiteralsNodeData {
    struct NString literals;
} LiteralsNodeData;

static boolean literalsNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {
    LiteralsNodeData* nodeData = node->data;

    if (!NCString.startsWith(text, NString.get(&nodeData->literals))) {
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        return False;
    }

    // Successful match, check next node,
    int32_t length = NString.length(&nodeData->literals);
    if (node->nextNode) {
        boolean matched = nodeMatch[node->nextNode->type](node->nextNode, ncc, &text[length], astParentNode, outResult);
        outResult->matchLength += length;
        return matched;
    }

    // No next node,
    NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
    outResult->matchLength = length;
    return True;
}

static void literalsNodeDeleteTree(NCC_Node* tree) {
    LiteralsNodeData* nodeData = tree->data;
    NString.destroy(&nodeData->literals);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.literalsNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.literalsNodeDeleteTree() tree"      );
}

static NCC_Node* createLiteralsNode(const char* literals) {
    LiteralsNodeData* nodeData = NMALLOC(sizeof(LiteralsNodeData), "NCC.createLiteralsNode() nodeData");
    NCC_Node* node = genericCreateNode(NCC_NodeType.LITERALS, nodeData);

    NString.initialize(&nodeData->literals, "%s", literals);

    #if NCC_VERBOSE
    NLOGI("NCC", "Created literals node: %s%s%s", NTCOLOR(HIGHLIGHT), literals, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

// Literals node is greedy, it tries to consume all the consecutive literals of the rule text in
// a single node. However, in our design, every literal should act as if it's a node of its own. The
// create methods of nodes that need to act on the previous node (like or and repeat nodes) call
// this function to break the last literal into a new separate node. This way, we are back to our
// desired behavior, where every literal is virtually separate. If a group of literals is intended,
// enclose them in a sub-rule: {literalsToBeGrouped},
static NCC_Node* breakLastLiteralIfNeeded(NCC_Node* literalsNode) {

    // Only consider literals nodes,
    if (literalsNode->type != NCC_NodeType.LITERALS) return literalsNode;

    // Only consider nodes with more than one literal,
    LiteralsNodeData* nodeData = literalsNode->data;
    int32_t literalsCount = NString.length(&nodeData->literals);
    if (literalsCount<2) return literalsNode;

    // Break the last literal apart. Create a new child literals node for the last literal,
    char* literals = (char*) NString.get(&nodeData->literals);
    char* lastLiteral = &literals[literalsCount-1];
    NCC_Node* newLiteralsNode = createLiteralsNode(lastLiteral);
    genericSetNextNode(literalsNode, newLiteralsNode);

    // Remove the last literal from the parent node,
    lastLiteral[0] = 0;
    NByteVector.resize(&nodeData->literals.string, literalsCount);

    return newLiteralsNode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Literal range node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LiteralRangeNodeData {
    unsigned char rangeStart, rangeEnd;
} LiteralRangeNodeData;

static boolean literalRangeNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {
    LiteralRangeNodeData* nodeData = node->data;

    // The literal to be matched,
    unsigned char literal = (unsigned char) *text;

    // Fail if out of range,
    if ((literal < nodeData->rangeStart) || (literal > nodeData->rangeEnd)) {
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        return False;
    }

    // Successful match, check next node,
    if (node->nextNode) {
        boolean matched = nodeMatch[node->nextNode->type](node->nextNode, ncc, &text[1], astParentNode, outResult);
        outResult->matchLength++;
        return matched;
    }

    // No next node,
    NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
    outResult->matchLength = 1;
    return True;
}

static void literalRangeNodeDeleteTree(NCC_Node* tree) {
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.literalRangeNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.literalRangeNodeDeleteTree() tree"      );
}

static NCC_Node* createLiteralRangeNode(unsigned char rangeStart, unsigned char rangeEnd) {

    LiteralRangeNodeData* nodeData = NMALLOC(sizeof(LiteralRangeNodeData), "NCC.createLiteralRangeNode() nodeData");
    NCC_Node* node = genericCreateNode(NCC_NodeType.LITERAL_RANGE, nodeData);

    // Always set rangeStart to the smaller of the two values,
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
// Common to creating literals and literal-range nodes
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Literals and literal-range are different from other nodes in one distinct way. Every other node type that can be
// encountered in rule text can be identified immediately based on how it's written. For example:
//    => subrule start with a "{".
//    => substitute starts with an "$" (TODO: add "@").
//    => selection starts with a "#".
//    => anything starts with a "*".
//    => or starts with a "|", while repeat starts with "^*" (the are only identified when their special character is
//       encountered. The last node is then associated with the node.
//       or node.
// However, literal and literal-range can start with literally anything (pun-intended :D), and don't have fixed size.
// That's why we have this section, and in particular the function handleLiteral() to handle any escaped characters and
// characters not preserved for the other nodes.

// Literals that can't follow a hyphen (-) in a literal-range,
static boolean isReserved(const char literal) {
    switch (literal) {
        case  ' ':
        case '\t':
        case  '$':
        case  '#':
        // TODO: @ ?
        case  '*':
        case  '{':
        case  '}':
        case  '^':
        case  '|':
        case  '-':
            return True;
        default:
            return False;
    }
}

// Get the next literal. If it's escaped, skip the escape character (\) and return the next one,
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

static NCC_Node* handleLiteral(NCC_Node* parentNode, const char** in_out_rule) {

    char literal = unescapeLiteral(in_out_rule);
    if (!literal) return 0;

    // Check if this was a literal range,
    NCC_Node* node;
    char followingLiteral = **in_out_rule;
    if (followingLiteral == '-') {
        (*in_out_rule)++;
        followingLiteral = **in_out_rule;
        if (!followingLiteral) {
            NERROR("NCC", "handleLiteral(): An unescaped '%s-%s' can't come at a rule's end", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        } else if (isReserved(followingLiteral)) {
            NERROR("NCC", "handleLiteral(): A '%s-%s' can't be followed by an unescaped '%s%c%s'", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), followingLiteral, NTCOLOR(STREAM_DEFAULT));
            return 0;
        }
        if (!(followingLiteral = unescapeLiteral(in_out_rule))) return 0;
        node = createLiteralRangeNode(literal, followingLiteral);
    } else {

        // If the parent node is a literals node, just append to it and return,
        if (parentNode->type == NCC_NodeType.LITERALS) {
            LiteralsNodeData* nodeData = parentNode->data;
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

    // Attach to parent node,
    genericSetNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Or node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct OrNodeData {
    NCC_Node* rhsTree;
    NCC_Node* lhsTree;
} OrNodeData;

static boolean orNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {
    OrNodeData* nodeData = node->data;

    // Push this node as a parent to the rhs and lhs,
    // TODO: Do we really need to push every node? After all, we only ever check the substitute nodes...
    NVector.pushBack(&ncc->parentStack, &node);

    // Match the sides on temporary stacks,
    // Right hand side,
    MatchTree(rhs, nodeData->rhsTree, text, astParentNode, astNodeStacks[1], 0, {&rhs}, 1)

    // Left hand side,
    MatchTree(lhs, nodeData->lhsTree, text, astParentNode, astNodeStacks[2], 0, {&rhs COMMA &lhs}, 2)

    // Remove this node from the parent stack,
    NVector.popBack(&ncc->parentStack, &node);

    // If neither right or left matched,
    if ((!rhsMatched) && (!lhsMatched)) {
        // Return the result with the longest match length,
        *outResult = rhs.result.matchLength > lhs.result.matchLength ? rhs.result : lhs.result;
        return False;
    }

    // At this point, either LHS, RHS or both matched. We now need to attempt matching the rest of
    // the text that comes after them.

    // If only one side matched, or both matched but the match length is identical, we needn't check
    // the following tree twice,
    if ((rhs.result.matchLength==lhs.result.matchLength) ||
        (!rhsMatched) ||
        (!lhsMatched)) {

        // Get the matched tree of the only side that matched, or the rule that occurs first (lhs)
        // otherwise,
        MatchedASTTree* matchedTree = lhsMatched ? &lhs : &rhs;

        // If there's a following tree,
        if (node->nextNode) {

            // In stacks, the first item to be popped is the last to be pushed. That's why we always
            // push the astNodeStack of the next/child nodes before the current. Hence, we'll match
            // the following tree on astNodeStacks[0]. Later on, we're going to push one of the two
            // sides matched earlier,
            MatchTree(nextNode, node->nextNode, &text[matchedTree->result.matchLength], astParentNode, astNodeStacks[0], matchedTree->result.matchLength, {&nextNode COMMA &lhs COMMA &rhs}, 3)

            // Termination is already handled in the MatchTree macro. Reaching this far means not
            // terminated. What remains is handling if the node was not matched,
            *outResult = nextNode.result;   // To set the length (will be amended later) and reset terminate.
            if (!nextNodeMatched) {
                DiscardMatchingResult(&lhs)
                DiscardMatchingResult(&rhs)
                outResult->matchLength += matchedTree->result.matchLength;
                return False;
            }
        } else {

            // Clear the terminate flag (or any other members we may add in the future),
            NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        }

        // Since we haven't returned yet, then either the following tree matched or there was no
        // following tree. If both lhs and rhs matched (in case they had the same match length), we
        // need to discard the unused side's tree,
        if (lhsMatched && rhsMatched) DiscardMatchingResult(&rhs)

        // Push the ast stack of the matched side to astNodeStacks[0] and adjust the match length,
        AcceptMatchResult(*matchedTree)
        return True;
    }

    // So far we handled:
    //    => Both sides (lhs and rhs) not matched.
    //    => One side only matched.
    //    => Both side matched, but have the same match length.
    //
    // What remains is when both sides match but with different match lengths. To maximize the
    // overall match length, we have to take the rest of the tree into account by attempting to
    // match at both right and left side lengths,

    // If no following tree, just accept the longer match,
    if (!node->nextNode) {
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        if (rhs.result.matchLength > lhs.result.matchLength) {
            DiscardMatchingResult(&lhs)
            AcceptMatchResult(rhs)
        } else {
            DiscardMatchingResult(&rhs)
            AcceptMatchResult(lhs)
        }
        return True;
    }

    // Right hand side,
    MatchTree(rhsTree, node->nextNode, &text[rhs.result.matchLength], astParentNode, astNodeStacks[3], rhs.result.matchLength, {&rhsTree COMMA &lhs COMMA &rhs}, 3)

    // Left hand side,
    MatchTree(lhsTree, node->nextNode, &text[lhs.result.matchLength], astParentNode, astNodeStacks[4], lhs.result.matchLength, {&lhsTree COMMA &rhsTree COMMA &lhs COMMA &rhs}, 4)

    // If neither right or left trees match,
    int32_t totalRHSMatchLength = rhs.result.matchLength + rhsTree.result.matchLength;
    int32_t totalLHSMatchLength = lhs.result.matchLength + lhsTree.result.matchLength;
    if ((!rhsTreeMatched) && (!lhsTreeMatched)) {

        // Return the result with the longest match,
        if (totalRHSMatchLength > totalLHSMatchLength) {
            *outResult = rhsTree.result;
            outResult->matchLength = totalRHSMatchLength;
        } else {
            *outResult = lhsTree.result;
            outResult->matchLength = totalLHSMatchLength;
        }
        DiscardMatchingResult(&lhs)
        DiscardMatchingResult(&rhs)
        return False;
    }

    // At least one side should have matched,
    NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
    if (!lhsTreeMatched ||                                              // RHS only matched, or,
        (rhsTreeMatched && totalRHSMatchLength>totalLHSMatchLength)) {  // Both matched, but rhs length > lhs length.
        DiscardMatchingResult(&lhsTree)
        DiscardMatchingResult(&lhs)
        AcceptMatchResult(rhsTree)
        AcceptMatchResult(rhs)
    } else {
        DiscardMatchingResult(&rhsTree)
        DiscardMatchingResult(&rhs)
        AcceptMatchResult(lhsTree)
        AcceptMatchResult(lhs)
    }

    // Or nodes themselves don't get pushed onto the stack. Just return,
    return True;
}

static void orNodeDeleteTree(NCC_Node* tree) {
    OrNodeData* nodeData = tree->data;
    nodeDeleteTree[nodeData->rhsTree->type](nodeData->rhsTree);
    nodeDeleteTree[nodeData->lhsTree->type](nodeData->lhsTree);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.orNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.orNodeDeleteTree() tree"      );
}

static char** skipWhiteSpaces(const char** in_out_rule) {
    // Skip spaces or tabs,
    char currentChar = **in_out_rule;
    while ((currentChar == ' ') || (currentChar == '\t')) {
        (*in_out_rule)++;
        currentChar = **in_out_rule;
    }
    return (char**) in_out_rule;
}

static NCC_Node* createOrNode(struct NCC* ncc, NCC_Node* parentNode, const char** in_out_rule) {

    // If the parent nod (the one just before the "|") is a literals node with more than one literal,
    // break the last literal apart so that it's the only literal matched in the or,
    parentNode = breakLastLiteralIfNeeded(parentNode);

    // Get grand-parent node (that would be the root node if only one real node comes before the | character),
    NCC_Node* grandParentNode = parentNode->previousNode;
    if (!grandParentNode) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    OrNodeData* nodeData = NMALLOC(sizeof(OrNodeData), "NCC.createOrNode() nodeData");
    NCC_Node* node = genericCreateNode(NCC_NodeType.OR, nodeData);

    // Remove parent from the grand-parent and attach this node instead,
    genericSetNextNode(grandParentNode, node);

    // Turn parent node into a tree and attach it as the lhs node,
    nodeData->lhsTree = createRootNode();
    genericSetNextNode(nodeData->lhsTree, parentNode);

    // Create a new tree for the next node and set it as the rhs,
    nodeData->rhsTree = createRootNode();
    const char* remainingSubRule =  ++(*in_out_rule); // Skip the '|'.
    remainingSubRule = *skipWhiteSpaces(in_out_rule); // Skip whitespaces.
    if (!**in_out_rule) {
        NERROR("NCC", "createOrNode(): %s|%s can't come at the end of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }
    NCC_Node* rhsNode = getNextNode(ncc, nodeData->rhsTree, in_out_rule); // This will automatically attach it to rhsTree.
    if (!rhsNode) {
        NERROR("NCC", "createOrNode(): couldn't create an rhs node: %s%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
        return 0; // Since this node is already attached to the tree, it gets cleaned up automatically.
    }

    #if NCC_VERBOSE
    NLOGI("NCC", "Created or node: %s|%s%s", NTCOLOR(HIGHLIGHT), remainingSubRule, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sub-rule node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SubRuleNodeData {
    NCC_Node* subRuleTree;
} SubRuleNodeData;

static boolean subRuleNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {
    SubRuleNodeData* nodeData = node->data;

    // Match sub-rule on temporary stack 1,
    NVector.pushBack(&ncc->parentStack, &node);
    MatchTree(subRule, nodeData->subRuleTree, text, astParentNode, astNodeStacks[1], 0, {&subRule}, 1)
    NVector.popBack(&ncc->parentStack, &node);
    if (!subRuleMatched) {
        *outResult = subRule.result;
        return False;
    }

    // Match next node,
    if (node->nextNode) {
        MatchTree(followingTree, node->nextNode, &text[subRule.result.matchLength], astParentNode, astNodeStacks[0], subRule.result.matchLength, {&followingTree COMMA &subRule}, 2)
        *outResult = followingTree.result;
        if (!followingTreeMatched) {
            outResult->matchLength += subRule.result.matchLength;
            DiscardMatchingResult(&subRule)
            return False;
        }
    } else {
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
    }

    // Push the sub-rule stack,
    AcceptMatchResult(subRule)

    return True;
}

static void subRuleNodeDeleteTree(NCC_Node* tree) {
    SubRuleNodeData* nodeData = tree->data;
    nodeDeleteTree[nodeData->subRuleTree->type](nodeData->subRuleTree);
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.subRuleNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.subRuleNodeDeleteTree() tree"      );
}

static NCC_Node* createSubRuleNode(struct NCC* ncc, NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the '{'.
    const char* subRuleBeginning = (*in_out_rule)++;

    // Find the matching closing braces,
    int32_t closingBracesRequired=1;
    boolean subRuleComplete=False;
    int32_t subRuleLength=0, whiteSpacesCount=0;
    do {
        char currentChar = (*in_out_rule)[subRuleLength];
        if (currentChar=='{') {
            closingBracesRequired++;
        } else if (currentChar=='}') {
            if (!--closingBracesRequired) {
                subRuleComplete = True;
                break;
            }
        } else if ((currentChar == ' ') || (currentChar == '\t')) {
            whiteSpacesCount++;
        } else if (!currentChar) {
            // Text finished before we found the matching closing braces. Subrule is not complete.
            break;
        }
        subRuleLength++;
    } while (True);

    // Make sure the sub-rule is well-formed. Can't have a sub-rule made up entirely of whitespaces,
    if ((subRuleLength-whiteSpacesCount) == 0) {
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
    NCC_Node* subRuleTree = constructRuleTree(ncc, subRule);
    NFREE(subRule, "NCC.createSubRuleNode() subRule");
    if (!subRuleTree) {
        // We deliberately chose to print "subRuleBeginning" over "subRule" because it shows more context,
        NERROR("NCC", "createSubRuleNode(): couldn't create sub-rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), subRuleBeginning, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create the sub-rule node,
    SubRuleNodeData* nodeData = NMALLOC(sizeof(SubRuleNodeData), "NCC.createSubRuleNode() nodeData");
    NCC_Node* node = genericCreateNode(NCC_NodeType.SUB_RULE, nodeData);
    nodeData->subRuleTree = subRuleTree;

    #if NCC_VERBOSE
    NLOGI("NCC", "Created sub-rule node: %s{%s}%s", NTCOLOR(HIGHLIGHT), subRule, NTCOLOR(STREAM_DEFAULT));
    #endif
    genericSetNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Repeat node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct RepeatNodeData {
    NCC_Node* repeatedNode;
} RepeatNodeData;

static boolean repeatNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {

    // This matches a rule "zero" or more times, and as such, there are no failed matches. A failed
    // match is a successful match of 0 repeats. This returns false only if there's a following tree
    // that couldn't be matched.

    // This is a recursive match function. If it matches once, we'll try to match again.
    // Question: Is the order important? If it's not, can't we do this iteratively? If the root node
    //           contains too many repeats, won't this cause an overflow?
    // Answer: The order is indeed important. And recursion won't really be an issue unless there
    //         are too many matches of a single repeat rule. Luckily, subrules and substitute nodes
    //         limit the depth of rule trees, so most of the recursion is rolled back. Even if we
    //         decide to do it iteratively, we still have to keep error tracking data, and the
    //         implementation will be harder to read and debug. With today's ram sizes, I say we
    //         keep it recursive, and make recursion as lean as possible. Besides, this problem
    //         isn't in the repeat node only. All the nodes in our system use recursion to push
    //         their children to the stack before themselves.

    // TODO: add recursionDepth member, and MAX_RECURSION_DEPTH to allow recursive calls to return
    //       even if the repeat node is not done completely. A parent non-recursive function then
    //       should use stack switching and aggregation to repeatedly call the recursive function
    //       while keeping ast's proper ordering...

    RepeatNodeData* nodeData = node->data;

    // If there are no following nodes, match as much as you can, and always return True,
    if (!node->nextNode) {
        NVector.pushBack(&ncc->parentStack, &node);
        MatchTree(repeatedNode, nodeData->repeatedNode, text, astParentNode, astNodeStacks[1], 0, {&repeatedNode}, 1)
        NVector.popBack(&ncc->parentStack, &node);
        if (!repeatedNodeMatched) {
            // Unlike other nodes, this is not considered a failed match. It's an accepted match of
            // 0 repeats,
            NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
            return True;
        } else if (repeatedNode.result.matchLength==0) {
            // We won't accept successful matches with 0 length. This might throw us in an infinite
            // loop. So instead of trying to make it work, we'll just reject it here. We'll reduce
            // matches of 0 length to just this node matching zero times (not matching anything at
            // all). Read more in "NCC.h",
            DiscardMatchingResult(&repeatedNode)
            NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
            return True;
        }

        // Attempt matching again (which will work, with at least 0 repeats, since we have no
        // following tree),
        repeatNodeMatch(node, ncc, &text[repeatedNode.result.matchLength], astParentNode, outResult);
        if (outResult->terminate) {
            outResult->matchLength += repeatedNode.result.matchLength;
            return True;
        }

        // Push our non-zero match on top of any other repeats (which are definitely successful)
        // that could have been made,
        AcceptMatchResult(repeatedNode)
        return True;
    }

    // We have a following node. Check if its tree matches,
    MatchTree(followingTree, node->nextNode, text, astParentNode, astNodeStacks[0], 0, {&followingTree}, 1)
    *outResult = followingTree.result;
    // If the following tree allows matching 0 characters, then this repeat node is never going to
    // match anything. We'll only treat a zero-length following tree as a delimiter if the repeat
    // node no longer matches. On the other hand, if we've met a delimiter of non-zero length, then
    // we should stop repeating immediately,
    if (followingTreeMatched && followingTree.result.matchLength!=0) return True;

    // Following tree didn't match or matched with 0 length, attempt repeating (on stack[1]),
    NVector.pushBack(&ncc->parentStack, &node);
    MatchTree(repeatedNode, nodeData->repeatedNode, text, astParentNode, astNodeStacks[1], 0, {&followingTree COMMA &repeatedNode}, 2)
    NVector.popBack(&ncc->parentStack, &node);

    // See if this repeat has reached an end,
    if (!repeatedNodeMatched || repeatedNode.result.matchLength==0) {
        // Can't accept a repeat of 0 length,
        if (repeatedNodeMatched) DiscardMatchingResult(&repeatedNode)

        // No more repeats then. Conclude nicely,
        if (followingTreeMatched) return True;

        // Unable to repeat or match the following tree. That's a match failure,
        outResult->matchLength += repeatedNode.result.matchLength; // Failed matches still set matchLength to the longest match they could achieve.
        return False;
    }

    // Something matched. Discard the zero-length match of the following tree (if any),
    DiscardMatchingResult(&followingTree)
    /*
    if (NVector.size(ncc->astNodeStacks[0]) != followingTree.astStackMark) {
        NLOGI("sdf", "Discarding!");
        NCC_ASTNode_Data *nodeData;
        nodeData = NVector.getLast(ncc->astNodeStacks[0]);
        NLOGE("sdf", "Discarded name: %s", NString.get(&nodeData->rule->ruleName));
    }
    */

    // Attempt repeating,
    boolean matched = repeatNodeMatch(node, ncc, &text[repeatedNode.result.matchLength], astParentNode, outResult);
    if (outResult->terminate || !matched) {
        // Didn't end properly, discard,
        outResult->matchLength += repeatedNode.result.matchLength;
        DiscardMatchingResult(&repeatedNode)
        return False;
    }

    // Push the repeated node,
    AcceptMatchResult(repeatedNode)
    return True;
}

static void repeatNodeDeleteTree(NCC_Node* tree) {
    // Delete the repeated node,
    RepeatNodeData* nodeData = tree->data;
    nodeDeleteTree[nodeData->repeatedNode->type](nodeData->repeatedNode);

    // Delete the following tree,
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);

    // Free our data structures,
    NFREE(tree->data, "NCC.repeatNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.repeatNodeDeleteTree() tree"      );
}

static NCC_Node* createRepeatNode(NCC_Node* parentNode, const char** in_out_rule) {

    // If the parent node is a literals node with more than one literal, break the last literal
    // apart so that it's the only literal repeated,
    parentNode = breakLastLiteralIfNeeded(parentNode);

    // Get grand-parent node,
    NCC_Node* grandParentNode = parentNode->previousNode;
    if (!grandParentNode) {
        // The only way a node would have no grandparent is when it's at the very top of the tree,
        // in which case it's only going to have a parent node (root node),
        NERROR("NCC", "createRepeatNode(): %s^%s can't come at the beginning of a rule/sub-rule", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Parse the ^ expression,
    char repeatCount = (++(*in_out_rule))[0];
    if (repeatCount == '*') {
        // Skip the *,
        (*in_out_rule)++;
    } else {
        // TODO: maybe support a fixed number of repeats (^3), or a range (^3..7)?
        NERROR("NCC", "createRepeatNode(): expecting %s*%s after %s^%s, found %s%c%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), repeatCount, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Create node,
    RepeatNodeData* nodeData = NMALLOC(sizeof(RepeatNodeData), "NCC.createRepeatNode() nodeData");
    NCC_Node* node = genericCreateNode(NCC_NodeType.REPEAT, nodeData);

    // Remove parent from the grand-parent and attach this node (repeat) instead,
    genericSetNextNode(grandParentNode, node);

    // Turn parent node into a tree and attach it as the repeated node,
    nodeData->repeatedNode = createRootNode();
    genericSetNextNode(nodeData->repeatedNode, parentNode);

    #if NCC_VERBOSE
    NLOGI("NCC", "Created repeat node: %s^*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
    #endif
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Anything node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static boolean anythingNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {

    // If no following tree, then match the entire text,
    int32_t totalMatchLength=0;
    if (!node->nextNode) {
        while (text[totalMatchLength]) totalMatchLength++;
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        outResult->matchLength = totalMatchLength;
        return True;
    }

    // There's a following tree. Stop as soon as it's matched,
    do {
        // Check if the following tree matches,
        MatchTree(followingTree, node->nextNode, &text[totalMatchLength], astParentNode, astNodeStacks[0], totalMatchLength, {&followingTree}, 1)

        // Same as with repeat nodes, if the following tree allows matching 0 characters, then this
        // anything node is never going to match anything. We'll only treat a zero-length following
        // tree as a delimiter if there is no more text to match. On the other hand, if we've met
        // a delimiter of non-zero length, then we should stop consuming text immediately.

        // If following tree matched,
        if (followingTreeMatched && followingTree.result.matchLength > 0) {
            // We have found our delimiter. Match gracefully,
            *outResult = followingTree.result;
            outResult->matchLength += totalMatchLength;
            return True;
        }

        // If text ended, whatever the following tree returned is our result, even if it's a match
        // of 0 length,
        if (!text[totalMatchLength]) {
            *outResult = followingTree.result;
            outResult->matchLength += totalMatchLength;
            return followingTreeMatched;
        }

        // At this point:
        //    - the text has not ended,
        //    - the following tree didn't match, or had a zero-length match (which we won't accept).
        if (followingTreeMatched) DiscardMatchingResult(&followingTree)

        // Advance!
        totalMatchLength++;
    } while (True);
}

static void anythingNodeDeleteTree(NCC_Node* tree) {
    // This node has no data member to delete.
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree, "NCC.anythingNodeDeleteTree() tree");
}

static NCC_Node* createAnythingNode(NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the *,
    (*in_out_rule)++;

    // Create node,
    NCC_Node* node = genericCreateNode(NCC_NodeType.ANYTHING, 0);

    #if NCC_VERBOSE
    NLOGI("NCC", "Created anything node: %s*%s%s", NTCOLOR(HIGHLIGHT), *in_out_rule, NTCOLOR(STREAM_DEFAULT));
    #endif
    genericSetNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Substitute node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SubstituteNodeData {
    NCC_Rule* rule;
} SubstituteNodeData;

static boolean substituteNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {
    SubstituteNodeData *nodeData = node->data;

    // This node attempts to match the rule specified when it was declared, and calls listeners to
    // create/delete AST nodes in the process:
    //    - If we are currently not silent, and the rule has listeners to create an AST node, we
    //      create a new AST node and use it while matching the rule tree as the parent. Otherwise,
    //      we just use astParentNode.
    //    - We attempt to match the rule. If failed, we roll back any (delete) any created AST nodes.
    //    - If match succeeds, that doesn't mean that we should accept it. We call the rule match
    //      listener to confirm the match. If it fails, we clean up and return.
    //    - If by matching this node we went further into the text to be matched than any previous
    //      moment during matching, we keep information about the match length and the stack trace
    //      of all the substitute nodes leading up to this moment.
    //    - If match succeeds, we move on to match the following tree as usual with other nodes.
    //    - Finally, if we haven't created a new AST node, we push the ASTs generated by matching
    //      the rule to the primary stack. If we have created a new one, we only push it, as the
    //      other nodes would be attached to it as children.

    // Variables needed for cleanup and return value,
    boolean newAstNodeCreated=False, deleteAstNode=False, discardRule, accepted;

    // Prepare an AST node data (newAstNode) and attach a new AST node to it,
    NCC_ASTNode_Data newAstNode = { .rule=&nodeData->rule->data };
    NCC_createASTNodeListener createASTNode = newAstNode.rule->createASTNodeListener;
    if (createASTNode) { // TODO: check if not silent...
        newAstNode.node = createASTNode ? createASTNode(newAstNode.rule, astParentNode) : 0;
        newAstNodeCreated = deleteAstNode = (newAstNode.node!=0);
    }

    // Match rule on a temporary stack,
    MatchedASTTree rule;
    NVector.pushBack(&ncc->parentStack, &node);
    accepted = discardRule = matchRuleTree(ncc, nodeData->rule->tree, text,
                                           &rule, newAstNodeCreated ? &newAstNode : astParentNode, &ncc->astNodeStacks[1],
                                           0, 0, 0);
    NVector.popBack(&ncc->parentStack, &node);
    if (rule.result.terminate || !accepted) {
        // Couldn't match rule tree. Nothing more to do,
        *outResult = rule.result;
        goto finish;
    }

    // Found a match (an unconfirmed one, though). Report,
    // TODO: check if not silent...
    if (nodeData->rule->data.ruleMatchListener) {

        // Copy the matched text so that we can zero terminate it,
        int32_t matchLength = rule.result.matchLength;
        char* matchedText = NMALLOC(matchLength+1, "NCC.substituteNodeMatch() matchedText");
        NSystemUtils.memcpy(matchedText, text, matchLength);
        matchedText[matchLength] = 0;        // 0-terminate the string.

        // Call the match listener,
        NCC_MatchingData matchingData;
        matchingData.node = newAstNode;
        matchingData.matchedText = matchedText;
        matchingData.matchLength = matchLength;
        matchingData.terminate = False;

        accepted = nodeData->rule->data.ruleMatchListener(&matchingData);
        NFREE(matchedText, "NCC.substituteNodeMatch() matchedText");

        // The rule match listener is allowed to terminate the matching or override the match length,
        rule.result.matchLength = matchingData.matchLength;
        rule.result.terminate   = matchingData.terminate  ;

        // If match rejected, set the result and return gracefully.
        if (matchingData.terminate || !accepted) {
            *outResult = rule.result;
            goto finish;
        }
    }

    // Confirmed match. If the total match length (not just this node, the ENTIRE match operation)
    // exceeds the maximum recorded this far, we need to collect some information for possible error
    // reporting,
    int32_t totalMatchLength = rule.result.matchLength + (((intptr_t) text) - ((intptr_t) ncc->textBeginning));
    if (totalMatchLength > ncc->maxMatchLength) {
        ncc->maxMatchLength = totalMatchLength;

        // Copy the names of all the substitute nodes' rules in the parent stack into the max match
        // stack,
        NVector.clear(&ncc->maxMatchRuleStack);
        int32_t parentNodesCount = NVector.size(&ncc->parentStack);
        for (int32_t i=0; i<parentNodesCount; i++) {
            NCC_Node* currentParentNode = *(NCC_Node**) NVector.get(&ncc->parentStack, i);
            if (currentParentNode->type == NCC_NodeType.SUBSTITUTE) {
                SubstituteNodeData *parentNodeData = currentParentNode->data;
                const char* ruleName = NString.get(&parentNodeData->rule->data.ruleName);
                NVector.pushBack(&ncc->maxMatchRuleStack, &ruleName);
            }
        }

        // Add this node to the stack too,
        const char* ruleName = NString.get(&nodeData->rule->data.ruleName);
        NVector.pushBack(&ncc->maxMatchRuleStack, &ruleName);

        // Set the expected next node as well (if no next, check parent stack next (recursively
        // until you find a next)),
        // TODO: implement node.toString() ...
    }

    // Match following tree,
    int32_t matchLength = rule.result.matchLength;
    if (node->nextNode) {
        MatchedASTTree nextNode;
        // TODO: do we always need to discard self on terminate? Shouldn't it be already discarded?
        //       We probably don't need to. After all, a terminate or match failure will discard
        //       the tree in this function, the only function where ASTs are created. We can add
        //       a few checks to make sure they really aren't needed.
        accepted = matchRuleTree(ncc, node->nextNode, &text[matchLength],
                                 &nextNode, astParentNode, &ncc->astNodeStacks[0],
                                 0, (MatchedASTTree*[]) {&nextNode}, 1);
        *outResult = nextNode.result;
        if (nextNode.result.terminate || !accepted) {
            outResult->matchLength += rule.result.matchLength;
            goto finish;
        }
    } else {
        // No following tree, prepare output,
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
    }

    // Following tree matched or no following tree,
    discardRule = deleteAstNode = False;
    if (newAstNodeCreated) {

        // Any AST nodes created while matching the rule are already the children of our newly
        // created AST node. As such, we needn't push them into the stack. Remove child nodes
        // without deleting them,
        NVector.resize(*rule.astNodesStack, rule.astStackMark);

        // Push our new AST node,
        NVector.pushBack(ncc->astNodeStacks[0], &newAstNode);
        outResult->matchLength += rule.result.matchLength;
    } else {
        // Push the child nodes into the primary stack,
        AcceptMatchResult(rule)
    }

    finish:
    if (discardRule) DiscardMatchingResult(&rule)
    if (deleteAstNode) {
        NCC_deleteASTNodeListener deleteListener = newAstNode.rule->deleteASTNodeListener;
        if (deleteListener) deleteListener(&newAstNode, astParentNode);
    }
    return accepted;
}

static void substituteNodeDeleteTree(NCC_Node* tree) {
    // Note: we don't free the rules, we didn't allocate them.
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);
    NFREE(tree->data, "NCC.substituteNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.substituteNodeDeleteTree() tree"      );
}

static NCC_Node* createSubstituteNode(struct NCC* ncc, NCC_Node* parentNode, const char** in_out_rule) {

    // TODO: handle '@'...
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
    NCC_Rule* rule = NCC_getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "createSubstituteNode(): couldn't find a rule named: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        NFREE(ruleName, "NCC.createSubstituteNode() ruleName 1");
        return 0;
    }

    // Create the node,
    SubstituteNodeData* nodeData = NMALLOC(sizeof(SubstituteNodeData), "NCC.createSubstituteNode() nodeData");
    NCC_Node* node = genericCreateNode(NCC_NodeType.SUBSTITUTE, nodeData);
    nodeData->rule = rule;

    #if NCC_VERBOSE
    NLOGI("NCC", "Created substitute node: %s${%s}%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
    #endif

    NFREE(ruleName, "NCC.createSubstituteNode() ruleName 2");
    genericSetNextNode(parentNode, node);
    return node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Selection node
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SelectionNodeData {
    struct NVector    attemptedRules;  // NCC_Rule*
    struct NVector verificationRules;  // NCC_Rule*
    NCC_Node* substituteNode;   // Used in matching.
    boolean matchIfIncluded;    // Indicates the verification mode. If true, accept if the matched rule is included in the verification rules, reject otherwise.
} SelectionNodeData;

static boolean selectionNodeMatch(NCC_Node* node, struct NCC* ncc, const char* text, NCC_ASTNode_Data* astParentNode, NCC_MatchingResult* outResult) {
    SelectionNodeData *nodeData = node->data;

    // Tries all rules in the attemptedRules list, picks the one with longest match length among the
    // the successful matches. If there's a verificationRules list, it goes on to accept (==) or
    // reject (!=) based on whether the picked rule belongs to the verification list.

    // Look for the longest successful match in the attempted rules list,
    MatchedASTTree longestMatchRule;
    const char* longestMatchRuleName=0;
    boolean matchFound=False;
    #define VERY_NEGATIVE_MATCH_LENGTH (-10000000)   // Outrageously negative, to make sure any match is longer.
    outResult->matchLength = VERY_NEGATIVE_MATCH_LENGTH;
    int32_t currentNodeStackIndex=1;    // We'll match on temporary stacks 1 and 2, switching as needed.
    int32_t attemptedRulesCount = NVector.size(&nodeData->attemptedRules);
    for (int32_t i=0; i<attemptedRulesCount; i++) {
        NCC_Rule* attemptedRule = *(NCC_Rule**) NVector.get(&nodeData->attemptedRules, i);

        // Matching through a substitute node, this way the top-most rule can be pushed,
        // We'll wrap the rule into a substitute node. This is very convenient, for we can just use
        // the substitute node match, and it'll take care of AST handling for us,
        // TODO: handle silence...
        ((SubstituteNodeData*) nodeData->substituteNode->data)->rule = attemptedRule;
        NVector.pushBack(&ncc->parentStack, &node);
        MatchTree(rule, nodeData->substituteNode, text, astParentNode, astNodeStacks[currentNodeStackIndex], 0, {&rule}, 1)
        NVector.popBack(&ncc->parentStack, &node);

        // Even if we don't find a match, we still want to keep the maximum match length for error
        // reporting. If we haven't found a match yet, then a simple comparison will do,
        if (!matchFound && rule.result.matchLength > outResult->matchLength) *outResult = rule.result;

        // If not a match, no further handling needed,
        if (!ruleMatched) continue;

        // Valid match, compare to the longest (if any),
        if (matchFound) {
            if (rule.result.matchLength > longestMatchRule.result.matchLength) {
                // The new rule is longer, discard the previous match,
                DiscardMatchingResult(&longestMatchRule)

                // Set the new one as the longest,
                longestMatchRule = rule;
                longestMatchRuleName = NString.get(&attemptedRule->data.ruleName);

                // Switch to the other temporary stack, to be able to discard this rule's stack if
                // a better match is found,
                currentNodeStackIndex = 3 - currentNodeStackIndex;  // To switch between 1 and 2.
            } else {
                // Not a longer match, discard and go on,
                DiscardMatchingResult(&rule)
            }
        } else {
            // This is the first match. It's the longest so far,
            matchFound = True;
            longestMatchRule = rule;
            longestMatchRuleName = NString.get(&attemptedRule->data.ruleName);

            // Switch to the other temporary stack, to be able to discard this rule's stack if
            // a better match is found,
            currentNodeStackIndex = 3 - currentNodeStackIndex;  // To switch between 1 and 2.
        }
    }

    // If no match found, no need to continue,
    if (!matchFound) {
        // Clear the result of no rule did that already (unlikely?),
        if (outResult->matchLength==VERY_NEGATIVE_MATCH_LENGTH) NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
        return False;
    }

    // Now, let's find the match in the verification rules (if any),
    matchFound = False;
    int32_t verificationRulesCount = NVector.size(&nodeData->verificationRules);
    for (int32_t i=0; i<verificationRulesCount; i++) {
        NCC_Rule* rule = *(NCC_Rule**) NVector.get(&nodeData->verificationRules, i);
        if (NCString.equals(longestMatchRuleName, NString.get(&rule->data.ruleName))) {
            matchFound = True;
            break;
        }
    }

    // If either match found when it shouldn't or no match found when it should, reject,
    if (matchFound ^ nodeData->matchIfIncluded) {
        *outResult = longestMatchRule.result;
        DiscardMatchingResult(&longestMatchRule)
        return False;
    }

    // Verified, match next node as usual,
    if (node->nextNode) {
        MatchTree(followingTree, node->nextNode, &text[longestMatchRule.result.matchLength], astParentNode, astNodeStacks[0], longestMatchRule.result.matchLength, {&followingTree COMMA &longestMatchRule}, 2)
        *outResult = followingTree.result;
        if (!followingTreeMatched) {
            outResult->matchLength += longestMatchRule.result.matchLength;
            DiscardMatchingResult(&longestMatchRule)
            return False;
        }
    } else {
        NSystemUtils.memset(outResult, 0, sizeof(NCC_MatchingResult));
    }

    // Push the matched rule stack,
    AcceptMatchResult(longestMatchRule)
    return True;
}

static void selectionNodeDeleteTree(NCC_Node* tree) {

    // Note: we don't free the rules, we didn't allocate them.
    SelectionNodeData* nodeData = tree->data;
    NVector.destroy(&nodeData->   attemptedRules);
    NVector.destroy(&nodeData->verificationRules);

    // We've create the substitute node during the initialization of this node. It needs to be freed
    // as well,
    substituteNodeDeleteTree(nodeData->substituteNode);

    // Delete next nodes,
    if (tree->nextNode) nodeDeleteTree[tree->nextNode->type](tree->nextNode);

    // Free our data structures and self,
    NFREE(tree->data, "NCC.selectionNodeDeleteTree() tree->data");
    NFREE(tree      , "NCC.selectionNodeDeleteTree() tree"      );
}

static NCC_Node* createSelectionNode(struct NCC* ncc, NCC_Node* parentNode, const char** in_out_rule) {

    // Skip the '#'.
    const char* ruleBeginning = (*in_out_rule)++;

    // Skip the '{',
    if (*((*in_out_rule)++) != '{') {
        NERROR("NCC", "createSelectionNode(): unescaped %s#%ss must be followed by %s{%ss", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
        return 0;
    }

    // Prepare data structures,
    SelectionNodeData* nodeData = NMALLOC(sizeof(SelectionNodeData), "NCC.createSelectionNode() nodeData");
    NVector.initialize(&nodeData->   attemptedRules, 0, sizeof(NCC_Rule*));
    NVector.initialize(&nodeData->verificationRules, 0, sizeof(NCC_Rule*));
    nodeData->matchIfIncluded = False;

    // Parse the node text,
    struct NString ruleName;
    NString.initialize(&ruleName, "");
    boolean verificationModeSet=False;
    do {
        char currentChar = *((*in_out_rule)++);

        // TODO: allow having $ or @ or nothing before individual subrules, to indicate pushing/non-pushing...
        if (currentChar=='{') {

            // Parse rule name,
            NString.set(&ruleName, "");
            do {
                currentChar = *((*in_out_rule)++);
                if (currentChar=='}') break;
                if (!currentChar) {
                    NERROR("NCC", "createSelectionNode(): couldn't find a matching %s}%s in %s%s%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleBeginning, NTCOLOR(STREAM_DEFAULT));
                    goto finish;
                }
                NString.append(&ruleName, "%c", currentChar);
            } while(True);

            // Some badly-formed-rule checks,
            NCC_Rule* rule=0;
            if (!verificationModeSet) {
                // Check if the rule exists,
                rule = NCC_getRule(ncc, NString.get(&ruleName));
                if (!rule) {
                    NERROR("NCC", "createSelectionNode(): couldn't find a rule named: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&ruleName), NTCOLOR(STREAM_DEFAULT));
                    goto finish;
                }
            } else {
                // Verification rules must be a subset of the attempted rules list. Look for this
                // rule in the attempted rules list,
                int32_t attemptedRulesCount = NVector.size(&nodeData->attemptedRules);
                for (int32_t i=0; i<attemptedRulesCount; i++) {
                    NCC_Rule* attemptedRule = *(NCC_Rule**) NVector.get(&nodeData->attemptedRules, i);
                    if (NCString.equals(NString.get(&ruleName), NString.get(&attemptedRule->data.ruleName))) {
                        // Rule found,
                        rule = attemptedRule;
                        break;
                    }
                }

                // If not found,
                if (!rule) {
                    NERROR("NCC", "createSelectionNode(): couldn't find a rule named: %s%s%s in the attempted rules list", NTCOLOR(HIGHLIGHT), NString.get(&ruleName), NTCOLOR(STREAM_DEFAULT));
                    goto finish;
                }
            }

            // Add to the appropriate list,
            NVector.pushBack(verificationModeSet ? &nodeData->verificationRules : &nodeData->attemptedRules, &rule);

        } else if (currentChar=='}') {

            // If no rules were specified,
            if (!NVector.size(&nodeData->attemptedRules)) {
                NERROR("NCC", "createSelectionNode(): Can't have a selection node without any attempted rules: %s%s%s", NTCOLOR(HIGHLIGHT), ruleBeginning, NTCOLOR(STREAM_DEFAULT));
                goto finish;
            }

            // If an == operator was specified but no verification rules followed,
            if (nodeData->matchIfIncluded && !NVector.size(&nodeData->verificationRules)) {
                NERROR("NCC", "createSelectionNode(): Selection node would never match anything: %s%s%s", NTCOLOR(HIGHLIGHT), ruleBeginning, NTCOLOR(STREAM_DEFAULT));
                goto finish;
            }

            // Everything looks great, move on and finalize creating this node,
            break;
        } else if ((currentChar == ' ') || (currentChar == '\t')) {
            // Just skip.
        } else if ((currentChar == '=') || (currentChar == '!')) {

            // If we've already seen an operator before,
            if (verificationModeSet) {
                NERROR("NCC", "createSelectionNode(): Can't set matching mode more than once in %s%s%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleBeginning, NTCOLOR(STREAM_DEFAULT));
                goto finish;
            }

            // Set the verification mode,
            nodeData->matchIfIncluded = (currentChar == '=');
            verificationModeSet = True;

            // Whether the operator is "==" or "!=", the second character is always an "=". We just
            // keep it to look like a logic operator for C junkies like me (C language, not Cocaine),
            char nextChar = *((*in_out_rule)++);
            if (nextChar != '=') {
                NERROR("NCC", "createSelectionNode(): expected %s%c=%s, found %s%c%c%s", NTCOLOR(HIGHLIGHT), currentChar, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), currentChar, nextChar, NTCOLOR(STREAM_DEFAULT));
                goto finish;
            }
        } else if (!currentChar) {
            NERROR("NCC", "createSelectionNode(): couldn't find a matching %s}%s in %s%s%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), ruleBeginning, NTCOLOR(STREAM_DEFAULT));
            goto finish;
        } else {
            // TODO: add $ and @...
            NERROR("NCC", "createSelectionNode(): expected %s==%s or %s!=%s or %s{%s, found %s%c%s", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), currentChar, NTCOLOR(STREAM_DEFAULT));
            goto finish;
        }
    } while (True);

    // The node text should be completely parsed by now, create the node,
    NCC_Node* node = genericCreateNode(NCC_NodeType.SELECTION, nodeData);

    // Create a substitute node to be used in matching,
    SubstituteNodeData* substituteNodeData = NMALLOC(sizeof(SubstituteNodeData), "NCC.createSelectionNode() substituteNodeData");
    nodeData->substituteNode = genericCreateNode(NCC_NodeType.SUBSTITUTE, substituteNodeData);
    substituteNodeData->rule = 0;

    #if NCC_VERBOSE
    NLOGI("NCC", "Created selection node");
    #endif

    genericSetNextNode(parentNode, node);
    NString.destroy(&ruleName);
    return node;

    finish:
    // Clean up,
    NString.destroy(&ruleName);
    NVector.destroy(&nodeData->   attemptedRules);
    NVector.destroy(&nodeData->verificationRules);
    NFREE(nodeData, "NCC.createSelectionNode() nodeData");
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Constructs a rule tree from rule text,
static NCC_Node* constructRuleTree(struct NCC* ncc, const char* ruleText) {

    // Every rule tree must start with a root node,
    NCC_Node* rootNode = createRootNode();

    // Start parsing the rule text,
    NCC_Node* currentNode = rootNode;
    const char* remainingText = ruleText;
    int32_t oldErrorsCount = NError.observeErrors();
    do {
        // Parse next node,
        currentNode = getNextNode(ncc, currentNode, &remainingText);

        // If new errors observed,
        if (NError.observeErrors()>oldErrorsCount) break;

        // No errors. If we no longer get new nodes, then we've finished the text,
        if (!currentNode) return rootNode;
    } while (True);

    // Failed,
    nodeDeleteTree[rootNode->type](rootNode);
    return 0;
}

// Identifies and creates the next rule tree node from the rule text. "in_out_rule" will be modified
// to point after the returned node text. This function is used to systematically parse rule text
// into a rule tree,
static NCC_Node* getNextNode(struct NCC* ncc, NCC_Node* parentNode, const char** in_out_rule) {

    // Skip unescaped spaces or tabs,
    char currentChar = **skipWhiteSpaces(in_out_rule);

    // Handle different token types,
    switch (currentChar) {
        case   0: return 0;
        case '#': return createSelectionNode (ncc, parentNode, in_out_rule);
        case '$': return createSubstituteNode(ncc, parentNode, in_out_rule);

        // TODO: add '@' to create a non-pushing (silent) substitute node...

        case '*': return createAnythingNode  (     parentNode, in_out_rule);
        case '{': return createSubRuleNode   (ncc, parentNode, in_out_rule);
        case '^': return createRepeatNode    (     parentNode, in_out_rule);
        case '|': return createOrNode        (ncc, parentNode, in_out_rule);
        case '-':
            NERROR("NCC", "getNextNode(): a '%s-%s' must always be preceded by a literal", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT));
            return 0;
        default: return handleLiteral(parentNode, in_out_rule);
    }
}

// Could as well be named switch vectors,
static void switchStacks(struct NVector** stack1, struct NVector** stack2) {
    struct NVector* temp = *stack1;
    *stack1 = *stack2;
    *stack2 = temp;
}

// Pushes all the AST nodes after the mark into the NCC's main AST node stack (astNodeStacks[0]),
static void pushASTStack(struct NCC* ncc, struct NVector* stack, int32_t stackMark) {

    // Get the number of nodes to be moved,
    int32_t stackSize = NVector.size(stack);
    int32_t entriesToPush = stackSize - stackMark;
    if (!entriesToPush) return;

    // Moves all the entries after the stack mark to ncc->astNodeStacks[0],
    int32_t currentMainStackPosition = NVector.size(ncc->astNodeStacks[0]);
    NVector.resize(ncc->astNodeStacks[0], currentMainStackPosition + entriesToPush);
    NSystemUtils.memcpy(
            ncc->astNodeStacks[0]->objects + (currentMainStackPosition * sizeof(NCC_ASTNode_Data)),
            stack                ->objects + (stackMark                * sizeof(NCC_ASTNode_Data)),
            entriesToPush * sizeof(NCC_ASTNode_Data));

    // Shrink the original stack to discard the moved nodes,
    NVector.resize(stack, stackMark);
}

// Discards any AST nodes created when matching the provided tree by calling the appropriate delete
// listeners,
static void discardMatchingResult(MatchedASTTree* tree) {

    struct NVector* stack = *tree->astNodesStack;
    NCC_ASTNode_Data currentNode;
    while (NVector.size(stack) > tree->astStackMark) {
        NVector.popBack(stack, &currentNode);

        // Get that specific rule's delete listener,
        NCC_deleteASTNodeListener deleteListener = currentNode.rule->deleteASTNodeListener;
        if (deleteListener) {
            deleteListener(&currentNode, tree->astParentNode);
        } else {
            // This shouldn't be reachable. When a create listener is specified, a delete one MUST
            // be provided,
            //NERROR("NCC", "discardMatchingResult(): no delete listener for rule: %s%s%s. Unable to discard AST node", NTCOLOR(HIGHLIGHT), NString.get(&currentNode.rule->ruleName), NTCOLOR(STREAM_DEFAULT));
        }
    }
}

// Parses "text" to see if it matches "ruleTree" according to the rule definitions in "ncc". Fills
// "outMatchingResult" with match info, attaches the constructed AST to "astParentNode" and pushes
// its nodes to "astStack". If one of the AST manipulation listeners decided to reject this match
// (terminate it) midway, "lengthToAddIfTerminated" is added to the match length, and the specified
// "astTrees" are discarded,
static boolean matchRuleTree(
        struct NCC* ncc, NCC_Node* ruleTree, const char* text,
        MatchedASTTree* outMatchingResult, NCC_ASTNode_Data* astParentNode, struct NVector** astStack,
        int32_t lengthToAddIfTerminated, MatchedASTTree** astTreesToDiscardIfTerminated, int32_t astTreesToDiscardCount) {

    // Match,
    outMatchingResult->astParentNode = astParentNode;
    outMatchingResult->astNodesStack = astStack;
    outMatchingResult->astStackMark = NVector.size(*astStack);
    switchStacks(&ncc->astNodeStacks[0], astStack);
    boolean matched = nodeMatch[ruleTree->type](ruleTree, ncc, text, astParentNode, &outMatchingResult->result);
    switchStacks(&ncc->astNodeStacks[0], astStack);

    // Return immediately if termination didn't take place,
    if (!outMatchingResult->result.terminate) return matched;

    // Termination took place. We still report the maximum length matched during the rejected match,
    outMatchingResult->result.matchLength += lengthToAddIfTerminated;

    // Discard trees,
    for (int32_t i=0; i<astTreesToDiscardCount; i++) DiscardMatchingResult(astTreesToDiscardIfTerminated[i]);

    return matched;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NCC
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define NCC_MATCH_RULE_NAME "_NCC_match()_"
struct NCC* NCC_initializeNCC(struct NCC* ncc) {
    ncc->extraData = 0;
    NVector.initialize(&ncc->rules            , 0, sizeof(NCC_Rule*));
    NVector.initialize(&ncc->parentStack      , 0, sizeof(NCC_Node*));
    NVector.initialize(&ncc->maxMatchRuleStack, 0, sizeof(const char*));
    for (int32_t i=0; i<NCC_AST_NODE_STACKS_COUNT; i++) ncc->astNodeStacks[i] = NVector.create(0, sizeof(NCC_ASTNode_Data));

    // Only substitute nodes push AST nodes. When we match a rule, we only match its rule tree, not
    // a substitute node referring to the rule. As such, the rule being matched won't appear in the
    // AST tree. We use "matchRule" to wrap rules being matched into a substitute node,
    NCC_RuleData matchRuleData;
    NCC_initializeRuleData(&matchRuleData, NCC_MATCH_RULE_NAME, "", 0, 0, 0);
    NCC_addRule(ncc, &matchRuleData);
    NCC_destroyRuleData(&matchRuleData);
    ncc->matchRule = NCC_getRule(ncc, NCC_MATCH_RULE_NAME);

    return ncc;
}

struct NCC* NCC_createNCC() {
    struct NCC* ncc = NMALLOC(sizeof(struct NCC), "NCC.NCC_createNCC() ncc");
    return NCC_initializeNCC(ncc);
}

void NCC_destroyNCC(struct NCC* ncc) {

    // Rules,
    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) destroyAndFreeRule(*((NCC_Rule**) NVector.get(&ncc->rules, i)));
    NVector.destroy(&ncc->rules);

    // Stacks,
    NVector.destroy(&ncc->parentStack);
    NVector.destroy(&ncc->maxMatchRuleStack);
    for (int32_t i=0; i<NCC_AST_NODE_STACKS_COUNT; i++) NVector.destroyAndFree(ncc->astNodeStacks[i]);
}

void NCC_destroyAndFreeNCC(struct NCC* ncc) {
    NCC_destroyNCC(ncc);
    NFREE(ncc, "NCC.NCC_destroyAndFreeNCC() ncc");
}

// Creates a rule and adds it to the NCC,
boolean NCC_addRule(struct NCC* ncc, NCC_RuleData* ruleData) {

    // Check if a rule with this name already exists,
    const char* ruleName = NString.get(&ruleData->ruleName);
    if (NCC_getRule(ncc, ruleName)) {
        NERROR("NCC", "NCC_addRule(): unable to create rule %s%s%s. A rule with the same name exists.", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Create rule tree,
    const char* ruleText = NString.get(&ruleData->ruleText);
    NCC_Node* ruleTree = constructRuleTree(ncc, ruleText);
    if (!ruleTree) {
        NERROR("NCC", "NCC_addRule(): unable to construct rule tree: %s%s%s", NTCOLOR(HIGHLIGHT), ruleText, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Create and initialize rule,
    NCC_Rule* rule = NMALLOC(sizeof(NCC_Rule), "NCC.NCC_addRule() rule");
    rule->tree = ruleTree;
    rule->data = *ruleData;  // Copy all members. But note that, copying strings is dangerous due
                             // to memory allocations. For every string in ruleData, we now have
                             // two NStrings pointing to the same memory block.
                             // Just create new NStrings overwriting the ones we just copied,
    NString.initialize(&rule->data.ruleName, "%s", ruleName);
    NString.initialize(&rule->data.ruleText, "%s", ruleText);

    // Add to ncc,
    NVector.pushBack(&ncc->rules, &rule);
    return True;
}

// Returns the rule with the specified name from this ncc if found, NULL otherwise,
NCC_Rule* NCC_getRule(struct NCC* ncc, const char* ruleName) {
    for (int32_t i=NVector.size(&ncc->rules)-1; i>=0; i--) {
        NCC_Rule* currentRule = *((NCC_Rule**) NVector.get(&ncc->rules, i));
        if (NCString.equals(ruleName, NString.get(&currentRule->data.ruleName))) return currentRule;
    }
    return 0;
}

// Returns the rule data of the specified rule (duh!),
NCC_RuleData* NCC_getRuleData(struct NCC* ncc, const char* ruleName) {
    NCC_Rule* rule = NCC_getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "NCC_getRuleData(): couldn't find rule: %s%s%s", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return 0;
    }
    return &rule->data;
}

boolean NCC_updateRule(struct NCC* ncc, NCC_RuleData* ruleData) {

    // Fetch rule,
    const char* ruleName = NString.get(&ruleData->ruleName);
    NCC_Rule* rule = NCC_getRule(ncc, ruleName);
    if (!rule) {
        NERROR("NCC", "NCC_updateRule(): unable to update rule %s%s%s. Rule doesn't exist.", NTCOLOR(HIGHLIGHT), ruleName, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Create new rule tree,
    const char* ruleText = NString.get(&ruleData->ruleText);
    if (!NCC_updateRuleText(ncc, rule, ruleText)) return False;

    // Reinitialize rule data by copying all members. But note that copying strings is dangerous due
    // to memory allocations. Strings have to be handled carefully,
    if (ruleData != &rule->data) {   // We needn't copy onto ourselves. It's dangerous in fact.

        // Keep our old strings,
        struct NString ruleNameString = rule->data.ruleName;
        struct NString ruleTextString = rule->data.ruleText;

        // Overwrite,
        rule->data = *ruleData;

        // Restore our old strings,
        rule->data.ruleName = ruleNameString;
        rule->data.ruleText = ruleTextString;
        NString.set(&rule->data.ruleName, "%s", ruleName);
        NString.set(&rule->data.ruleText, "%s", ruleText);
    }

    return True;
}

boolean NCC_updateRuleText(struct NCC* ncc, NCC_Rule* rule, const char* newRuleText) {

    // Create new rule tree,
    NCC_Node* ruleTree = constructRuleTree(ncc, newRuleText);
    if (!ruleTree) {
        NERROR("NCC", "NCC_updateRuleText(): unable to construct rule tree: %s%s%s. Failed to update rule: %s%s%s.", NTCOLOR(HIGHLIGHT), newRuleText, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&rule->data.ruleName), NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Dispose of the old rule-tree and set the new one,
    nodeDeleteTree[rule->tree->type](rule->tree);
    rule->tree = ruleTree;

    // Update rule data,
    NString.set(&rule->data.ruleText, "%s", newRuleText);

    return True;
}

boolean NCC_match(struct NCC* ncc, NCC_Rule* rule, const char* text, NCC_MatchingResult* outResult, NCC_ASTNode_Data* outNode) {

    // Wrap the rule into a substitute node so it can appear in the AST tree,
    NCC_Node* ruleTreeToBeMatched;
    if (rule->data.createASTNodeListener || rule->data.ruleMatchListener) {

        // Prepare the wrapping rule text,
        struct NString wrappingRuleText;
        NString.initialize(&wrappingRuleText, "${%s}", NString.get(&rule->data.ruleName));

        // Only update if the rule to be matched changed since the last time,
        if (!NCString.equals(
                NString.get(&ncc->matchRule->data.ruleText),
                NString.get(&wrappingRuleText))) {
            NCC_updateRuleText(ncc, ncc->matchRule, NString.get(&wrappingRuleText));
        }

        // Cleanup and set the wrapping rule's tree as the one to be matched,
        NString.destroy(&wrappingRuleText);
        ruleTreeToBeMatched = ncc->matchRule->tree;

    } else {
        // The rule won't show in the tree anyway, match directly,
        ruleTreeToBeMatched = rule->tree;
    }

    // Prepare for matching,
    ncc->maxMatchLength = 0;
    ncc->textBeginning = text;
    NVector.clear(&ncc->maxMatchRuleStack);

    // Match,
    MatchedASTTree ruleTree;
    boolean matched = matchRuleTree(ncc, ruleTreeToBeMatched, text,
                                    &ruleTree, 0, &ncc->astNodeStacks[0],
                                    0, (MatchedASTTree *[]) {&ruleTree}, 1);
    *outResult = ruleTree.result;
    if (matched && !ruleTree.result.terminate) {

        // If an output node is expected, return it,
        if (outNode) {
            // TODO: .... there could be more than one node on the stack?...
            if (!NVector.popBack(ncc->astNodeStacks[0], outNode)) NSystemUtils.memset(outNode, 0, sizeof(NCC_ASTNode_Data));
        } else {

            // Delete the unused tree,
            NCC_ASTNode_Data tempNode;
            while (NVector.popBack(ncc->astNodeStacks[0], &tempNode)) {
                NCC_deleteASTNodeListener deleteListener = tempNode.rule->deleteASTNodeListener;
                if (deleteListener) deleteListener(&tempNode, 0);
            }
        }
    }

    return matched;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic AST construction methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The user is supposed to define his own AST nodes and listeners, but for convenience, we provide
// a generic implementation that fits most use cases.

void* NCC_createASTNode(NCC_RuleData* ruleData, NCC_ASTNode_Data* astParentNodeData) {

    // Create an instance of our user-defined AST node (in this case, NCC_ASTNode). This function
    // can return a pointer to anything. It's the listeners' job to make sense of it,
    NCC_ASTNode* astNode = NMALLOC(sizeof(NCC_ASTNode), "NCC.NCC_createASTNode() astNode");
    NString.initialize(&astNode->name, "%s", NString.get(&ruleData->ruleName));
    NString.initialize(&astNode->value, "not set yet");
    NVector.initialize(&astNode->childNodes, 0, sizeof(NCC_ASTNode*));
    astNode->rule = ruleData;

    // Attach this node to the parent. Again, this is our user-defined implementation, which
    // supports having children,
    if (astParentNodeData) {
        NCC_ASTNode* parentASTNode = astParentNodeData->node;
        NVector.pushBack(&parentASTNode->childNodes, &astNode);
    }

    // Return whatever we cooked in here,
    return astNode;
}

static inline void deleteASTNode(NCC_ASTNode* astNode, NCC_ASTNode_Data* astParentNodeData) {

    // Destroy members,
    NString.destroy(&astNode->name);
    NString.destroy(&astNode->value);

    // Delete children. Luckily, the last AST node to be created is the first to be deleted. So, if
    // all nodes on a stack are being deleted, the children are collected first (and detached from
    // the parent), then the parent is deleted. In such case, there won't be an children, and the
    // loop below will do no harm,
    NCC_ASTNode* currentChild;
    while (NVector.popBack(&astNode->childNodes, &currentChild)) {
        if (currentChild->rule->deleteASTNodeListener == NCC_deleteASTNode) {
            // Using the generic listener,
            deleteASTNode(currentChild, 0); // Needn't remove from parent because parent is dying anyway.
        } else if (!currentChild->rule->deleteASTNodeListener) {
            // This shouldn't be reachable. When a create listener is specified, a delete one MUST
            // be provided,
            //NERROR("NCC", "deleteASTNode(): no delete listener for rule: %s%s%s. Unable to discard AST node", NTCOLOR(HIGHLIGHT), NString.get(&currentChild->rule->ruleName), NTCOLOR(STREAM_DEFAULT));
        } else {
            // Has a user-defined listener,
            NCC_ASTNode_Data nodeData;
            nodeData.node = currentChild;
            nodeData.rule = currentChild->rule;
            NCC_ASTNode_Data parentNodeData;
            parentNodeData.node = astNode;
            parentNodeData.rule = astNode->rule;
            currentChild->rule->deleteASTNodeListener(&nodeData, &parentNodeData);
        }
    }
    NVector.destroy(&astNode->childNodes);

    // Delete node,
    NFREE(astNode, "NCC.NCC_deleteASTNode() astNode");

    // Remove from parent (if any),
    if (astParentNodeData) {
        NCC_ASTNode* parentASTNode = astParentNodeData->node;
        int32_t nodeIndex = NVector.getFirstInstanceIndex(&parentASTNode->childNodes, &astNode);
        if (nodeIndex!=-1) NVector.remove(&parentASTNode->childNodes, nodeIndex);
    }
}

void NCC_deleteASTNode(NCC_ASTNode_Data* node, NCC_ASTNode_Data* astParentNode) {
    deleteASTNode((NCC_ASTNode*) node->node, astParentNode);
}

boolean NCC_matchASTNode(NCC_MatchingData* matchingData) {

    // When an AST node is created, it's exact value is still not parsed yet. Only when the match
    // listener is called is the value defined. Set the value here. You may also alter the match
    // length or terminate the matching,
    NCC_ASTNode* astNode = matchingData->node.node;
    NString.set(&astNode->value, "%s", matchingData->matchedText);
    return True;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pretty printing trees
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: convert into a generic function that can print any PrintableTree { data, getChild(index), getChildrenCount(), getName(), getValue() ...etc },
//       then create NCC_ASTTreeToString() that wraps NCC_ASTNode in PrintableTree and uses it.
void NCC_ASTTreeToString(NCC_ASTNode* tree, struct NString* prefix, struct NString* outString, boolean printColored) {

    // Unicode box drawing block:
    // Single Lines:
    //    ─, │, ┌, ┐, └, ┘, ├, ┤, ┬, ┴, ┼
    // Double Lines:
    //    ═, ║, ╔, ╗, ╚, ╝, ╠, ╣, ╦, ╩, ╬
    // Light Dashed Lines (light dash, heavy dash, light dot, heavy dot):
    //    ┄, ┅, ┆, ┇, ┈, ┉, ┊, ┋
    // Various combinations of single and double lines:
    //    ━, ┃, ┍, ┎, ┏, ┑, ┒, ┓, ┕, ┖, ┗, ┙, ┚, ┛, ┝, ┞, ┟, ┠, ┡, ┢, ┣, ┥, ┦, ┧, ┨, ┩, ┪, ┫, ┭, ┮, ┯, ┰, ┱, ┲, ┳, ┵, ┶, ┷, ┸, ┹, ┺, ┻, ┽, ┾, ┿, ╀, ╁, ╂, ╃, ╄, ╅, ╆, ╇, ╈, ╉, ╊, ╋, ╌, ╍, ╎, ╏, ╒, ╓, ╕, ╖, ╘, ╙, ╛, ╜, ╞, ╟, ╡, ╢, ╤, ╥, ╧, ╨, ╪, ╫
    // Arc and Diagonal Lines:
    //   ╭, ╮, ╯, ╰, ╱, ╲, ╳
    // Half and Light Lines (various partials and weights):
    //   ╴, ╵, ╶, ╷, ╸, ╹, ╺, ╻, ╼, ╽, ╾, ╿

    // Prepare children prefix from the initial one,
    boolean lastChild;
    struct NString* childrenPrefix;
    if (prefix) {
        const char* prefixCString = NString.get(prefix);
        lastChild = NCString.contains(prefixCString, "└");

        struct NString* temp1 = NString.replace(prefixCString, "─", " ");
        struct NString* temp2 = NString.replace(NString.get(temp1 ), "├", "│");
        NString.destroyAndFree(temp1);
        childrenPrefix = NString.replace(NString.get(temp2), "└", " ");
        NString.destroyAndFree(temp2);

        // First line uses the plain old prefix,
        NString.append(outString, "%s", prefixCString);
    } else {
        lastChild = False;
        childrenPrefix = NString.create("");
    }
    const char* childrenPrefixCString = NString.get(childrenPrefix);

    // Prepare node name,
    struct NString *nodeName = NString.replace(NString.get(&tree->name), "\n", "\\n");

    // Tree value could span multiple lines, remove line-breaks,
    int32_t childrenCount = NVector.size(&tree->childNodes);
    boolean containsLineBreak = NCString.contains(NString.get(&tree->value), "\n");
    if (containsLineBreak) {
        struct NString  temp1;
        struct NString *temp2;
        NString.initialize(&temp1, "\n%s%s", childrenPrefixCString, childrenCount ? "│" : " ");
        temp2 = NString.replace(NString.get(&tree->value), "\n", NString.get(&temp1));
        NString.append(outString, "%s:%s%s", NString.get(nodeName), NString.get(&temp1), NString.get(temp2));
        if (!NCString.endsWith(NString.get(temp2), "│")) NString.append(outString, "%s", NString.get(&temp1));
        NString.append(outString, "\n");
        NString.destroy(&temp1);
        NString.destroyAndFree(temp2);
    } else {
        if (printColored) {
            NString.append(outString, "%s: %s%s%s\n", NString.get(nodeName), NTCOLOR(BLUE_BACKGROUND), NString.get(&tree->value), NTCOLOR(STREAM_DEFAULT));
        } else {
            NString.append(outString, "%s: %s\n", NString.get(nodeName), NString.get(&tree->value));
        }
    }

    NString.destroyAndFree(nodeName);

    // Print children,
    struct NString childPrefix;
    NString.initialize(&childPrefix, "");
    for (int32_t i=0; i<childrenCount; i++) {
        boolean lastChild = (i==(childrenCount-1));
        NString.set(&childPrefix, "%s%s", childrenPrefixCString, lastChild ? "└─" : "├─");
        NCC_ASTNode* currentChild = *((NCC_ASTNode**) NVector.get(&tree->childNodes, i));
        NCC_ASTTreeToString(currentChild, &childPrefix, outString, printColored);
    }

    // Extra line break if this was the last child of its parent,
    boolean containsContinuation = NCString.contains(childrenPrefixCString, "│");
    if (lastChild && !containsLineBreak && containsContinuation) {
        NString.trimEnd(childrenPrefix, " ");
        if (!NCString.endsWith(NString.get(outString), "│\n")) NString.append(outString, "%s\n", childrenPrefixCString);
    }

    NString.destroyAndFree(childrenPrefix);
    NString.destroy(&childPrefix);
}

// TODO: print tree ....
//    tree node
//    ├─── tree node
//    │     ├─── tree node
//    │     └─── tree node
//    └─── tree node
