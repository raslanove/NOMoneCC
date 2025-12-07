
/////////////////////////////////////////////////////////
// NOMone Compiler Compiler
// Created by Omar El Sayyed on the 21st of October 2021.
/////////////////////////////////////////////////////////

#pragma once

#include <NTypes.h>
#include <NVector.h>
#include <NString.h>

//
// Usage:
//   First, we construct our rules (language definition). Then we match, firing listeners to construct
//   an AST as we proceed.
//     - Create node listener: Given the rule, construct your AST node and return it.
//     - Delete node listener: The node created in the previous step is not final. It could be rolled back.
//                             Be ready to do so if this listener is fired.
//     - Match       listener: Once the node and its children are constructed, this is fired. At this point,
//                             you may inspect the node and decide if you'll accept this match.
//

//
// Rule text is comprised of a sequence of nodes that define how text is matched. We have several
// convenient node types that should make rules definition readable and convenient.
//
// Node types:
//   Literals:        abc
//   Literal range:   a-z
//   Or:              |
//   Repeat:          ^*
//   Sub-rule:        {ruleText}
//   Substitute:      ${ruleName}
//   Anything:        *
//                    * followed by something
//   Selection:       #{{rule1} {rule2} {rule3} ...etc}
//                    #{{rule1} {rule2} {rule3} ...etc == {rule2} ...etc}
//                    #{{rule1} {rule2} {rule3} ...etc != {rule2} ...etc}
//
// Example rules:
// ╔═══════════════╤════════════════╤═══════════════════════════════════════════════════════════╗
// ║ Node Type     │ Rule Name      │ Rule Text                                                 ║
// ╟───────────────┼────────────────┼───────────────────────────────────────────────────────────╢
// ║ Literals      │ for            │ for                                                       ║
// ║ Literal range │ smallLetter    │ a-z                                                       ║
// ║ Or            │ letter         │ a-z|A-Z                                                   ║
// ║ Repeat        │ name           │ A-Za-z^*  // A name always starts with a capital letter.  ║
// ║ Sub-rule      │ namesList      │ {A-Za-z^*} {,A-Za-z^*}^*                                  ║
// ║ Substitute    │ integer        │ 0|{1-90-9^*}                                              ║
// ║               │ integerPair    │ ${integer},${integer}                                     ║
// ║ Anything      │ sentence       │ *.                                                        ║
// ║ Selection     │ keyword        │ #{{class} {enum} {if} {else}}                             ║
// ║               │ identifier     │ #{{keyword} {identifier-content} == {identifier-content}} ║
// ║               │ unary-operator │ #{{+}{-}{~}{!} {++}{--} != {++}{--}}                      ║
// ╚═══════════════╧════════════════╧═══════════════════════════════════════════════════════════╝
//
// Reserved characters (must be escaped):
//   \ | - ^ * { } $ #
// and spaces/tabs. Spaces/Tabs that are not escaped are ignored. Feel free to use them to make rules look cleaner.
//
// TODO: add @ to reserved characters?
//        => Above.
//        => NCC.isReserved()
//        => NCC.getNextNode()

// Details and limitations:
// ========================
// Left recursion:
// ---------------
// Left recursion is not allowed. This is a top-down recursive parser that fails miserably with
// left-recursion. Luckily, we support repeats. For example, for this rule:
//    postfix-expression:
//         primary-expression
//         postfix-expression [ expression ]
//         postfix-expression ( argument-expression-list | ${ε} )
//         postfix-expression .  identifier
//         postfix-expression -> identifier
//         postfix-expression ++
//         postfix-expression --
//         inline-struct
//
// The postfix-expression has left recursion that only terminates by one of two options:
//    primary-expression or inline-struct
// These two don't occur on the right side, they can only occur only once and at the very beginning
// of the postfix-expression. This, we can convert it into repeat like this:
//    NCC_addRule(ruleData.set(&ruleData, "postfix-expression",
//                             "{ ${primary-expression} | ${inline-struct} } {"
//                             "   {[  ${expression} ]} | "
//                             "   {(  ${argument-expression-list}|${ε} )} | "
//                             "   {.  ${identifier}} | "
//                             "   {-> ${identifier}} | "
//                             "   {++} | "
//                             "   {--}"
//                             "}^*"));
// But note that, this also reverses the order of evaluation ?
//
// Right recursion:
// ----------------
// We also support right-recursion. To be able to refer to a rule that is still being defined, you
// first define it as a stub, then redefine it. For example:
//    NCC_addRule   (ruleData.set(&ruleData, "conditional-expression", "STUB!"));
//    NCC_updateRule(ruleData.set(&ruleData, "conditional-expression",
//                                "${logical-or-expression} | "
//                                "{${logical-or-expression} ${} ${?} ${} ${expression} ${} ${:} ${} ${conditional-expression}}"));
//
// Or nodes:
// ---------
// Or nodes will turn the node that comes after the "|" into a separate sub-rule. Or nodes work by
// creating a tree for the node just before the "|" (lhs) and another tree for the node following
// it (rhs). This effectively puts the both nodes within braces, hence exposing them to the wildcard
// limitations (see below).
//
// If both lhs and rhs match, the or node will select the one which results in the longest match,
// not just the lhs and rhs node, but the entire tree. Example:
//    rule: {ab}|{abc}cdef
//    text: abcdef
// This will match the entire text "abcdef". The or node will select the lhs, even though it's only
// 2 characters long while the rhs is 3. This is because selecting the lhs will result in the entire
// tree matching with length 6, while choosing the rhs will only match 3 characters.
//
// Wildcard nodes:
// ---------------
// Repeat and Anything nodes are wildcard nodes. Anything nodes (*) will match anything until the
// remaining part of the sub-rule is encountered. For instance:
//   *xyz
// Will consume all the text until an "xyz" is found. This works well as long as the termination
// sequence is within the same sub-rule. If the termination sequence is part of a parent rule, it
// won't ever be reached. For example:
//   {*}xyz
// will never match anything.
//
// The same could be said for the repeat nodes (^*), depending on the contents of the repeated
// expression. For example:
//   {xyz}^*xyz
// will find an immediate match in the text "xyzxyzxyz", because the termination sequence is right
// there at the beginning. However,
//   {{xyz}^*}xyz
// will consume the entire text within the sub-rule, as it can't see the parent's termination
// sequence.
//
// Finally, if the following tree matches with 0-length, a wildcard node won't ever be able to match
// anything, since the following tree is matched immediately upon first try. A following tree with
// 0-length match is not treated as a delimiter unless the repeat is no longer able to consume any
// more text.
//
// Repeat nodes:
// -------------
// Matches a rule 0 or more times. Won't accept matches with 0 length, though. A zero-length match
// is problematic. Let's lay out how could deal with it in detail:
//    1) We accept a 0 length match unconditionally.
//    2) We match it only once.
//    3) We reject it altogether.
// Dealing with 0 length matches is a dilemma. Repeatedly accepting it can result in an infinite
// loop. Accepting it once may put us in a weird position. What if:
//   - the first time it matched with a non-zero length, then
//   - matched a zero-length (we decided to accept it once), then
//   - matched a zero-length again (shall we break from the repeat?), then
//   - matched a non-zero length (remember the AST listeners can adjust the match length).
// It's hard to come up with an intuitive behavior for such cases. As such, we chose option #3. We
// will discard any ASTs produced by a zero-length match and we'll just skip the repeat node. You
// can still use repeats on subrules that would occasionally produce zero-length matches. The node
// shall work normally, exiting only when a zero-length match occurs or if the following tree is
// matched.
//
// Selection nodes:
// ----------------
// Selection nodes can replace or nodes placed in a subrule. They are particularly efficient when we
// are oring more than 2 nodes. The backdraw is that all the ored nodes must all be substitute
// nodes. For example:
//    keyword = #{{class} {enum} {if} {else}}
// This will match the rule with the longest match from the previously defined class/enum/if/else rules.
//
// Order matters. If we have 2 matching rules with the same matching length, the first one wins:
//    ambiguous = #{{keyword} {identifier}}
// The text "class" should match both. Since {keyword} came first, it's considered a keyword. If
// their order was reversed, so would be the matched tree.
//
// Selection can also take another form:
//    #{{rule1} {rule2} {rule3} ...etc == {rule2} ...etc}
// Or:
//    #{{rule1} {rule2} {rule3} ...etc != {rule2} ...etc}
//
// The rules before the ==/!= are the "attempted rules list", while the ones following are the
// "verification rules list". When matching, selection attempts to match the attempted rules, and
// keeps track of the one with the longest successful match. That's the matching phase. It then
// proceeds to the verification phase, in which the matched rule is accepted only if it's
// include/excluded (== / !=) in the verification rules.
//
// The verification rules provide means to exclude some rules from being matched. For instance, we
// can use selection to exclude shorter matching rules:
//    nonKeywordIdentifier = #{{keyword} {identifier} == {identifier}}
// Before adding the verification rules list, the text "class" would match {keyword}, and hence
// {nonKeywordIdentifier} would be accepted. However, since we added "== {identifier}",
// {nonKeywordIdentifier} will only match if {identifier} was matched, which is not the case for the
// text "class", but is indeed the case for text "class1". This way, we don't mistake keywords for
// identifiers, or identifiers for keywords. We also successfully discern identifiers that start
// with a keyword (like "class1") from keywords followed by literals.
//
// We can also use selection to exclude longer matching rules:
//   unary-operator = #{{+}{-}{~}{!} {++}{--} != {++}{--}}
// Here, we are trying to match unary operators (+/-/~/~), not postfix-operators (++/--). We could
// easily mistake ++ as being 2 unary-operators in sequence, but not if we applied the above
// selection rule. Note that, the same effect can be achieved if we inverted the verification
// operator and rules. Like:
//   unary-operator = #{{+}{-}{~}{!} {++}{--} == {+}{-}{~}{!}}
// This would also work, but choosing one operator over the other can make your rule much clearer
// and easier to verify.
//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NCC
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct NCC_RuleData NCC_RuleData;
typedef struct NCC_Rule NCC_Rule;

// We define 5 different stacks to be used while matching. That's the maximum number of stack we
// needed to exist simultaneously so far,
#define NCC_AST_NODE_STACKS_COUNT 5
// Take the or node for instance:
//    => astNodeStacks[0]: the main stack on which all nodes push their ASTs.
//    => astNodeStacks[1]: rhs of the or node.
//    => astNodeStacks[2]: lhs of the or node.
//    => astNodeStacks[3]: the tree following the rhs of the or node.
//    => astNodeStacks[4]: the tree following the lhs of the or node.
//
// While trying to find the longest match, the or node will try to match multiple paths, and will
// only push some of them to stack 0. We could have used fewer stacks, but it really makes no
// difference performance or memory wise, and separating them makes the logic much easier to
// understand.
//
// After matching, the matched AST tree should always reside in astNodeStacks[0]. Before returning,
// node Matching methods always restore the stacks they used to their previous state before
// matching. astNodeStacks[0] is the only one that changes. matchRuleTree() swaps astNodeStacks[0]
// with the one specified in the arguments, performs the match then restores the original order.
// This way, node matching methods needn't worry about which stack to use, and can always rely on
// index 0 being the main stack.


// We won't create a typedef for NCC. Maybe at some point we'll declare a global interface name NCC
// with all NCC relevant method, like we did for NVector and NString.
struct NCC {
    void* extraData;
    struct NVector rules; // A vector of pointers to rules, not rules. This way, even if the vector expands, they still point to the original rules.
    struct NCC_Rule* matchRule;
    struct NVector* astNodeStacks[NCC_AST_NODE_STACKS_COUNT]; // NCC_ASTNode_Data. To be able to discard nodes that are not needed.

    // Error reporting,
    struct NVector parentStack;       // A vector of NCC_Node*. Used to keep track of the node-matching call-stack.
    struct NVector maxMatchRuleStack; // A vector const char*. It contains the names of all the substitute nodes that
                                      // were in the parentStack at the moment the longest match was set.
    int32_t maxMatchLength;           // The length of the longest match during the last match operation.
    const char* textBeginning;        // A pointer to the text currently being matched.
};

typedef struct NCC_ASTNode_Data {
    void* node;
    NCC_RuleData* rule;
} NCC_ASTNode_Data;

typedef struct NCC_MatchingResult {
    int32_t matchLength;
    boolean terminate;
} NCC_MatchingResult;

typedef struct NCC_MatchingData {
    NCC_ASTNode_Data node;
    const char* matchedText;
    int32_t matchLength;
    boolean terminate;
} NCC_MatchingData;

typedef void*   (*NCC_createASTNodeListener)(NCC_RuleData* ruleData, NCC_ASTNode_Data* parentNode);
typedef void    (*NCC_deleteASTNodeListener)(NCC_ASTNode_Data* node, NCC_ASTNode_Data* parentNode);
typedef boolean (*NCC_ruleMatchListener)(NCC_MatchingData* matchingData);  // Returns true if node accepted. Also, may modify the match length and the terminate fields, influencing the rest of the match operation.

typedef struct NCC_RuleData {
    struct NCC* ncc;
    struct NString ruleName;
    struct NString ruleText;
    NCC_createASTNodeListener createASTNodeListener;
    NCC_deleteASTNodeListener deleteASTNodeListener;
    NCC_ruleMatchListener ruleMatchListener;
    NCC_RuleData* (*set)(NCC_RuleData* ruleData, const char* ruleName, const char* ruleText);
    NCC_RuleData* (*setListeners)(NCC_RuleData* ruleData, NCC_createASTNodeListener createASTNodeListener, NCC_deleteASTNodeListener deleteASTNodeListener, NCC_ruleMatchListener ruleMatchListener);
} NCC_RuleData;

struct NCC* NCC_initializeNCC(struct NCC* ncc);
struct NCC* NCC_createNCC();
void NCC_destroyNCC(struct NCC* ncc);
void NCC_destroyAndFreeNCC(struct NCC* ncc);

NCC_RuleData* NCC_initializeRuleData(NCC_RuleData* ruleData, struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_createASTNodeListener createNodeListener, NCC_deleteASTNodeListener deleteNodeListener, NCC_ruleMatchListener matchListener);
void NCC_destroyRuleData(NCC_RuleData* ruleData);

boolean NCC_addRule(NCC_RuleData* ruleData);
boolean NCC_updateRule(NCC_RuleData* ruleData);
boolean NCC_setRootRule(struct NCC* ncc, const char* ruleName);
boolean NCC_match(struct NCC* ncc, const char* text, NCC_MatchingResult* outResult, NCC_ASTNode_Data* outNode); // Returns True if matched. Sets outResult and outNode.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic AST construction methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct NCC_ASTNode {
    struct NString name, value;
    struct NVector childNodes;
    NCC_RuleData* rule;
    void* extraData;
} NCC_ASTNode;

void*   NCC_createASTNode(NCC_RuleData* ruleData, NCC_ASTNode_Data* astParentNodeData);
void    NCC_deleteASTNode(NCC_ASTNode_Data* node, NCC_ASTNode_Data* astParentNode);
boolean NCC_matchASTNode (NCC_MatchingData* matchingData);

void NCC_ASTTreeToString(NCC_ASTNode* tree, struct NString* prefix, struct NString* outString, boolean printColored);
