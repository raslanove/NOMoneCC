#include <NSystemUtils.h>
#include <NError.h>
#include <NVector.h>
#include <NCString.h>

#include <NCC.h>

//////////////////////////////////////
// Testing helper functions
//////////////////////////////////////

void assert(struct NCC* ncc, const char*ruleName, const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength, boolean logTree) {

    boolean nccNeedsDeletion = False;
    if (!ncc) {
        ncc = NCC_createNCC();
        nccNeedsDeletion = True;
    }
    if (!ruleName) ruleName = "AssertTemp";

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, ncc, ruleName, rule, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode);
    boolean success = NCC_addRule(&ruleData);
    NCC_destroyRuleData(&ruleData);
    if (!success) {
        NERROR("HelloCC", "Couldn't add rule. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
        return ;
    }

    NCC_setRootRule(ncc, ruleName);
    struct NCC_MatchingResult matchingResult;
    struct NCC_ASTNode_Data treeData;
    boolean matched = NCC_match(ncc, text, &matchingResult, logTree ? &treeData : 0);
    if (shouldMatch && !matched) {
        NERROR("HelloCC", "assert(): Match failed. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (!shouldMatch && matched) {
        NERROR("HelloCC", "assert(): Erroneously matched. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (expectedMatchLength != matchingResult.matchLength) {
        NERROR("HelloCC", "assert(): Wrong match length. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s, Expected match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), expectedMatchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (matched && logTree && treeData.node) {

        // Get the tree in string format,
        struct NString treeString;
        NString.initialize(&treeString, "");
        NCC_ASTTreeToString(treeData.node, 0, &treeString, True);

        // Print and clean up,
        NLOGI("", "%s", NString.get(&treeString));
        NString.destroy(&treeString);
        NCC_deleteASTNode(&treeData, 0);
    }

    if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
}

boolean printListener(struct NCC_MatchingData* matchingData) {
    NLOGI("HelloCC", "ruleName: %s", NString.get(&matchingData->node.rule->ruleName));
    NLOGI("HelloCC", "        Match length: %s%d%s", NTCOLOR(HIGHLIGHT), matchingData->matchLength, NTCOLOR(STREAM_DEFAULT));
    NLOGI("HelloCC", "        Matched text: %s%s%s", NTCOLOR(HIGHLIGHT), matchingData->matchedText, NTCOLOR(STREAM_DEFAULT));
    return True;
}

//////////////////////////////////////
// Conditional acceptance test
//////////////////////////////////////

// Variable manipulation
//////////////////////////////////////

// TODO: turn into separates files, symbolTable.c and symbolTable.h...

struct NVector declaredVariables;

void addDeclaredVariable(const char* variableName) {
    struct NString* declaredVariable = NVector.emplaceBack(&declaredVariables);
    NString.initialize(declaredVariable, "%s", variableName);
}

boolean removeDeclaredVariable(const char* variableName) {
    for (int32_t i=NVector.size(&declaredVariables)-1; i>-1; i--) {
        struct NString* currentVariableName = NVector.get(&declaredVariables, i);
        if (NCString.equals(variableName, NString.get(currentVariableName))) {
            NString.destroy(currentVariableName);
            NVector.remove(&declaredVariables, i);
            return True;
        }
    }
    return False;
}

boolean isVariableDeclared(const char* variableName) {
    for (int32_t i=NVector.size(&declaredVariables)-1; i>-1; i--) {
        const char* currentVariableName = NString.get(NVector.get(&declaredVariables, i));
        if (NCString.equals(variableName, currentVariableName)) return True;
    }
    return False;
}

void destroyDeclaredVariables() {
    for (int32_t i=NVector.size(&declaredVariables)-1; i>-1; i--) NString.destroy(NVector.get(&declaredVariables, i));
    NVector.clear(&declaredVariables);
}

// Listeners
//////////////////////////////////////

boolean declarationListener(struct NCC_MatchingData* matchingData) {
    NCC_matchASTNode(matchingData);

    // Get the variable name from the identifier child,
    struct NCC_ASTNode* astNode = matchingData->node.node;
    struct NCC_ASTNode* childNode = *((struct NCC_ASTNode**) NVector.getLast(&astNode->childNodes));
    NLOGI(0, "%sDeclare:%s %s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT), NString.get(&childNode->value));
    addDeclaredVariable(NString.get(&childNode->value));
    return True;
}

void undoDeclarationListener(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* parentNode) {

    // Get the variable name from the identifier child (if any),
    struct NCC_ASTNode* astNode = node->node;
    if (NVector.size(&astNode->childNodes)) {
        struct NCC_ASTNode* childNode = *((struct NCC_ASTNode**) NVector.getLast(&astNode->childNodes));
        NLOGI(0, "%sUndeclare:%s %s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT), NString.get(&childNode->value));
        removeDeclaredVariable(NString.get(&childNode->value));
    }

    NCC_deleteASTNode(node, parentNode);
}

boolean validateAssignmentListener(struct NCC_MatchingData* matchingData) {

    // Get the two identifier children,
    struct NCC_ASTNode* astNode = matchingData->node.node;
    struct NCC_ASTNode*  leftChildNode = *((struct NCC_ASTNode**) NVector.get(&astNode->childNodes, 0));
    struct NCC_ASTNode* rightChildNode = *((struct NCC_ASTNode**) NVector.get(&astNode->childNodes, 1));

    NLOGI(0, "%sAssignment  left:%s %s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT), NString.get(& leftChildNode->value));
    NLOGI(0, "%sAssignment right:%s %s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT), NString.get(&rightChildNode->value));

    // Accept rule only if the two variable were previously declared,
    boolean declared;
    declared = isVariableDeclared(NString.get(& leftChildNode->value));
    if (!declared) return False;
    declared = isVariableDeclared(NString.get(&rightChildNode->value));

    if (declared) NCC_matchASTNode(matchingData);
    return declared;
}

//////////////////////////////////////
// Tests
//////////////////////////////////////

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)\n");

    // A number of test-cases that make sure that rules and matching behave as expected.
    struct NCC ncc;

    // Literals,
    assert(0, 0, "besm\\ Allah", "besm Allah", True, 10, False);

    // x-y
    assert(0, 0, "besm\\ Allah\\ a-z", "besm Allah x", True, 12, False);
    assert(0, 0, "besm\\ Allah\\ a-z", "besm Allah 2", False, 11, False);
    assert(0, 0, "besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12, False);

    // |
    assert(0, 0, "a|b", "a", True, 1, False);
    assert(0, 0, "abc|def", "abcef", True, 5, False);
    assert(0, 0, "abc|def", "abdef", True, 5, False);
    assert(0, 0, "abc|def", "abef", False, 2, False);
    assert(0, 0, "a|b|c|d|ef", "cf", True, 2, False);

    // {}
    assert(0, 0, "ab{cd{ef}gh}ij", "abcdefghij", True, 10, False);
    assert(0, 0, "ab{cd}|{ef}gh", "abcdgh", True, 6, False);
    assert(0, 0, "ab{cd}|{ef}gh", "abefgh", True, 6, False);
    assert(0, 0, "ab{cd}|{ef}gh", "abgh", False, 2, False);
    assert(0, 0, "a{a|b}", "ab", True, 2, False);
    assert(0, 0, "a{b|c}d", "abf", False, 2, False);

    // ^*
    assert(0, 0, "a^*bc", "abc", True, 3, False);
    assert(0, 0, "a^*bc", "bc", True, 2, False);
    assert(0, 0, "a^*bc", "aaaaabc", True, 7, False);
    assert(0, 0, "a^*", "aaaaa", True, 5, False);
    assert(0, 0, "123a^*", "123aaaaa", True, 8, False);
    assert(0, 0, "123a^*456", "123a456", True, 7, False);
    assert(0, 0, "123a^*456", "123456", True, 6, False);
    assert(0, 0, "123{ab}^*456", "123ababab456", True, 12, False);
    assert(0, 0, "{ab}^*{cd}^*", "x", True, 0, False);
    assert(0, 0, "x{ab}^*{cd}^*", "x", True, 1, False);
    assert(0, 0, "x{ab}^*{cd}^*", "xab", True, 3, False);
    assert(0, 0, "x{ab}^*{cd}^*", "xcd", True, 3, False);
    assert(0, 0, "{xyz}^*xyz", "xyzxyzxyz", True, 3, False);
    assert(0, 0, "{{xyz}^*}xyz", "xyzxyzxyz", False, 9, False);

    // *
    assert(0, 0, "*", "xyz", True, 3, False);
    assert(0, 0, "**", "xyz", True, 3, False);
    assert(0, 0, "********", "xyz", True, 3, False);
    assert(0, 0, "********abc", "xyzabc", True, 6, False);
    assert(0, 0, "*a*b*c*", "__a__c__", False, 8, False);
    assert(0, 0, "*XYZ", "abcdefgXYZ", True, 10, False);
    assert(0, 0, "{*}XYZ", "abcdefgXYZ", False, 10, False);

    // General test-cases,
    assert(0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "myVariable3", True, 11, False);
    assert(0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "3myVariable3", False, 0, False);
    assert(0, 0, "/\\**\\*/", "/*بسم الله. This is a beautiful comment.\n The is the second line in the beautiful comment.*/", True, 99, False);

    // Substitute,
    NCC_initializeNCC(&ncc);
    assert(&ncc, "Comment"      , "/\\**\\*/", "/*besm Allah*/", True, 14, False);
    assert(&ncc, "TwoComments"  , "${Comment},${Comment}", "/*first comment*/,/*second comment*/", True, 36, False);
    assert(&ncc, "ThreeComments", "${TwoComments},${Comment}", "/*first comment*/,/*second comment*/,/*thirrrrrd comment*/", True, 58, False);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Optional", "{ab}^*{cd}^*", "", True, 0, False);
    assert(&ncc, "Mandatory", "xyz", "xyz", True, 3, False);
    assert(&ncc, "ContainingOptional", "${Optional}${Mandatory}", "xyz", True, 3, False);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Milestone", "", "", True, 0, False);
    assert(&ncc, "123", "123", "123", True, 3, False);
    assert(&ncc, "ActualRule1", "${123}${Milestone}${123}", "123123", True, 6, False);
    assert(&ncc, "ActualRule2", "abc${ActualRule1}xyz", "abc123123xyz", True, 12, False);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Literal"        , "\x01-\xff", "", False, 0, False);
    assert(&ncc, "EscapedLiteral" , "\\\\${Literal}", "", False, 0, False);
    assert(&ncc, "String"         , "\" { ${Literal}|${EscapedLiteral} }^* \"", "", False, 0, False);
    assert(&ncc, "StringContainer", "${String}", "\"besm Allah \\\" :)\"", True, 18, False);
    NCC_destroyNCC(&ncc);

    // Selection,
    NCC_initializeNCC(&ncc);
    assert(&ncc,   "class",     "class", "", False, 0, False);
    assert(&ncc,    "enum",      "enum", "", False, 0, False);
    assert(&ncc,      "if",        "if", "", False, 0, False);
    assert(&ncc,    "else",      "else", "", False, 0, False);
    assert(&ncc, "keyword", "#{{class} {enum} {if} {else}}", "if", True, 2, False);

    assert(&ncc,             "digit",       "0-9", "", False, 0, False);
    assert(&ncc,         "non-digit", "_|a-z|A-Z", "", False, 0, False);    
    assert(&ncc,        "identifier", "${non-digit} {${digit} | ${non-digit}}^*", "", False, 0, False);
    assert(&ncc,     "orderMatters1", "#{{identifier} {keyword}                }", "class" ,  True, 5, False);    
    assert(&ncc,     "orderMatters2", "#{{keyword} {identifier}                }", "class" ,  True, 5, False);
    assert(&ncc,    "verifyIncluded", "#{{keyword} {identifier} == {identifier}}", "class" , False, 5, False);
    assert(&ncc, "verifyNotIncluded", "#{{keyword} {identifier} !=    {keyword}}", "class" , False, 5, False);
    assert(&ncc,     "LongestMatch1", "#{{keyword} {identifier}                }", "class1",  True, 6, False);
    assert(&ncc,     "LongestMatch2", "#{{keyword} {identifier} == {identifier}}", "class1",  True, 6, False);
    assert(&ncc,     "LongestMatch3", "#{{keyword} {identifier} != {identifier}}", "class1", False, 6, False);
    NCC_destroyNCC(&ncc);
    
    NCC_initializeNCC(&ncc);
    assert(&ncc,  "+",      "+", "", False, 0, False);
    assert(&ncc,  "-",    "\\-", "", False, 0, False);
    assert(&ncc,  "~",      "~", "", False, 0, False);
    assert(&ncc,  "!",      "!", "", False, 0, False);
    assert(&ncc, "++",     "++", "", False, 0, False);
    assert(&ncc, "--", "\\-\\-", "", False, 0, False);
    
    assert(&ncc, "unary-operator1", "#{{+}{-}{~}{!} {++}{--} == {+}{-}{~}{!}}", "++", False, 2, False);
    assert(&ncc, "unary-operator2", "#{{+}{-}{~}{!} {++}{--} !=     {++}{--}}", "++", False, 2, False);
    NCC_destroyNCC(&ncc);
    
    // Stateful parsing,
    NLOGI("", "%s================%s"  , NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%sStateful Parsing%s"  , NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%s================%s\n", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, &ncc, "", "", 0, 0, 0);
    NVector.initialize(&declaredVariables, 0, sizeof(NString));

    NCC_initializeNCC(&ncc);
    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*"));
    NCC_addRule(ruleData.set(&ruleData, "identifier" , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*")         ->setListeners(&ruleData, NCC_createASTNode,       NCC_deleteASTNode,           NCC_matchASTNode));
    NCC_addRule(ruleData.set(&ruleData, "declaration", "${identifier};")                      ->setListeners(&ruleData, NCC_createASTNode, undoDeclarationListener,        declarationListener));
    NCC_addRule(ruleData.set(&ruleData, "assignment" , "${identifier}=${identifier};")        ->setListeners(&ruleData, NCC_createASTNode,       NCC_deleteASTNode, validateAssignmentListener));
    NCC_addRule(ruleData.set(&ruleData, "document"   , "{${declaration}|${assignment}|${}}^*")->setListeners(&ruleData, NCC_createASTNode,       NCC_deleteASTNode,           NCC_matchASTNode));

    assert(&ncc, "AssignmentTest", "Test1:${} ${document}",
            "Test1:\n"
            "var1;\n"
            "var2;\n"
            "var1=var2;", True, 29, True);
    destroyDeclaredVariables();
    NLOGI("", "");
    assert(&ncc, "FailedAssignmentTest", "Test2:${} ${document}",
            "Test2:\n"
            "var1;\n"
            "var2;\n"
            "var1=var3;", True, 19, True);
    destroyDeclaredVariables();
    NCC_destroyNCC(&ncc);
    NLOGI("", "");

    // Delete test,
    NCC_initializeNCC(&ncc);
    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*")->setListeners(&ruleData, 0, 0, 0));
    NCC_addRule(ruleData.set(&ruleData, "identifier" , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*")->setListeners(&ruleData, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode));
    NCC_addRule(ruleData.set(&ruleData, "specifier"  , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*")->setListeners(&ruleData, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode));
    NCC_addRule(ruleData.set(&ruleData, "declaration", "${specifier} ${} ${identifier};")->setListeners(&ruleData, NCC_createASTNode, undoDeclarationListener, declarationListener));
    assert(&ncc, "RollBackTest", "${declaration}|${declaration}", "int a;", True, 6, True);
    destroyDeclaredVariables();    
    NCC_destroyNCC(&ncc);
    NLOGI("", "");

    NVector.destroy(&declaredVariables);
    NCC_destroyRuleData(&ruleData);
     
    NError.logAndTerminate();
}
