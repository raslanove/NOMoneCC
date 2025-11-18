
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
//   Or:              |
//   Literal range:   a-z
//   Repeat:          ^*
//   Sub-rule:        ${name}
//                    {rule}
//   Anything:        *
//                    * followed by something
//   Selection:       #{{rule1} {rule2} {rule3} ...etc}
//                    #{{rule1} {rule2} {rule3} ...etc == {rule2} ...etc}
//                    #{{rule1} {rule2} {rule3} ...etc != {rule2} ...etc}
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
//   Selection    :         keyword         = #{{class} {enum} {if} {else}}
//                          identifier      = #{{keyword} {identifier-content} == {identifier-content}}
//                          unary-operator  = #{{+}{-}{~}{!} {++}{--} != {++}{--}}
//
// Reserved characters (must be escaped):
//   \ | - ^ * { } $ #
// and spaces/tabs. Spaces/Tabs that are not escaped are ignored. Feel free to use them to make rules look cleaner.
//
// TODO: add @ to reserved characters?

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
// Wildcard nodes:
// ---------------
// Anything nodes (*) will match anything until the remaining part of the sub-rule is encountered.
// For instance:
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
// Or nodes:
// ---------
// Or nodes will turn the node that comes after the "|" into a separate sub-rule. Or nodes work by
// creating a tree for the node just before the "|" (lhs) and another tree for the node following
// it (rhs). This effectively puts the next node within braces ({rhs}), hence exposing it to the
// wildcard limitations.
//
// Selection nodes:
// ----------------
// Selection nodes can replace or nodes. They are particularly efficient when we are oring more than
// 2 nodes. The backdraw is that all the ored nodes must all be substitute nodes. For example:
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

struct NCC_RuleData;
struct NCC_Rule;

#define NCC_AST_NODE_STACKS_COUNT 5
struct NCC {
    void* extraData;
    struct NVector rules; // Pointers to rules, not rules. This way, even if the vector expands, they still point to the original rules.
    struct NCC_Rule* matchRule;
    struct NVector* astNodeStacks[NCC_AST_NODE_STACKS_COUNT]; // NCC_ASTNode_Data. To be able to discard nodes that are not needed.

    // Error reporting,
    struct NVector parentStack; // struct NCC_Node*.
    struct NVector maxMatchRuleStack; // const char*
    int32_t maxMatchLength;
    const char* textBeginning;
};

struct NCC_ASTNode_Data {
    void* node;
    struct NCC_RuleData* rule;
};

struct NCC_MatchingResult {
    int32_t matchLength;
    boolean terminate;
};

struct NCC_MatchingData {
    struct NCC_ASTNode_Data node;
    const char* matchedText;
    int32_t matchLength;
    boolean terminate;
};

typedef void*   (*NCC_createNodeListener)(struct NCC_RuleData* ruleData, struct NCC_ASTNode_Data* parentNode);
typedef void    (*NCC_deleteNodeListener)(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* parentNode);
typedef boolean (*NCC_matchListener)(struct NCC_MatchingData* matchingData);  // Returns true if node accepted. Also, may set the match length and the terminate fields.

struct NCC_RuleData {
    struct NCC* ncc;
    struct NString ruleName;
    struct NString ruleText;
    NCC_createNodeListener createNodeListener;
    NCC_deleteNodeListener deleteNodeListener;
    NCC_matchListener matchListener;
    struct NCC_RuleData* (*set)(struct NCC_RuleData* ruleData, const char* ruleName, const char* ruleText);
    struct NCC_RuleData* (*setListeners)(struct NCC_RuleData* ruleData, NCC_createNodeListener createNodeListener, NCC_deleteNodeListener deleteNodeListener, NCC_matchListener matchListener);
};

struct NCC* NCC_initializeNCC(struct NCC* ncc);
struct NCC* NCC_createNCC();
void NCC_destroyNCC(struct NCC* ncc);
void NCC_destroyAndFreeNCC(struct NCC* ncc);

struct NCC_RuleData* NCC_initializeRuleData(struct NCC_RuleData* ruleData, struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_createNodeListener createNodeListener, NCC_deleteNodeListener deleteNodeListener, NCC_matchListener matchListener);
void NCC_destroyRuleData(struct NCC_RuleData* ruleData);

boolean NCC_addRule(struct NCC_RuleData* ruleData);
boolean NCC_updateRule(struct NCC_RuleData* ruleData);
boolean NCC_setRootRule(struct NCC* ncc, const char* ruleName);
boolean NCC_match(struct NCC* ncc, const char* text, struct NCC_MatchingResult* outResult, struct NCC_ASTNode_Data* outNode); // Returns True if matched. Sets outResult and outNode.

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic AST construction methods
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct NCC_ASTNode {
    struct NString name, value;
    struct NVector childNodes;
    struct NCC_RuleData* rule;
    void* extraData;
};

void*   NCC_createASTNode(struct NCC_RuleData* ruleData, struct NCC_ASTNode_Data* astParentNodeData);
void    NCC_deleteASTNode(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* astParentNode);
boolean NCC_matchASTNode (struct NCC_MatchingData* matchingData);

void NCC_ASTTreeToString(struct NCC_ASTNode* tree, struct NString* prefix, struct NString* outString, boolean printColored);
