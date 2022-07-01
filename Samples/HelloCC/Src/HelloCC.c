#include <NSystemUtils.h>
#include <NError.h>
#include <NVector.h>
#include <NCString.h>

#include <NCC.h>

//////////////////////////////////////
// Testing helper functions
//////////////////////////////////////

void assert(struct NCC* ncc, const char*ruleName, NCC_matchListener onMatchListener, const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength) {

    boolean nccNeedsDeletion = False;
    if (!ncc) {
        ncc = NCC_createNCC();
        nccNeedsDeletion = True;
    }
    if (!ruleName) ruleName = "AssertTemp";

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, ncc, ruleName, rule, 0, 0, onMatchListener);
    boolean success = NCC_addRule(&ruleData);
    NCC_destroyRuleData(&ruleData);
    if (!success) {
        NERROR("HelloCC", "Couldn't add rule. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
        return ;
    }

    NCC_setRootRule(ncc, ruleName);
    struct NCC_MatchingResult matchingResult;
    boolean matched = NCC_match(ncc, text, &matchingResult, 0);
    if (shouldMatch && !matched) {
        NERROR("HelloCC", "assert(): Match failed. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (!shouldMatch && matched) {
        NERROR("HelloCC", "assert(): Erroneously matched. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (expectedMatchLength != matchingResult.matchLength) {
        NERROR("HelloCC", "assert(): Wrong match length. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s, Expected match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), expectedMatchLength, NTCOLOR(STREAM_DEFAULT));
    }

    if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
    NLOGI("", "");
}

boolean matchListener(struct NCC_MatchingData* matchingData) {
    NLOGI("HelloCC", "ruleName: %s", NString.get(&matchingData->node.rule->ruleName));
    NLOGI("HelloCC", "        Match length: %s%d%s", NTCOLOR(HIGHLIGHT), matchingData->matchLength, NTCOLOR(STREAM_DEFAULT));
    NLOGI("HelloCC", "        Matched text: %s%s%s", NTCOLOR(HIGHLIGHT), matchingData->matchedText, NTCOLOR(STREAM_DEFAULT));
    return True;
}

//////////////////////////////////////
// Conditional acceptance test
//////////////////////////////////////

struct NVector declaredVariables;

void addDeclaredVariable(const char* variableName) {
    struct NString* declaredVariable = NVector.emplaceBack(&declaredVariables);
    NString.initialize(declaredVariable, "%s", variableName);
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

boolean declarationListener(struct NCC_MatchingData* matchingData) {
    // TODO: ....
    //addDeclaredVariable(NString.get(&variable.value));
    return True;
}

boolean validateAssignmentListener(struct NCC_MatchingData* matchingData) {

    // TODO: ....

    /*
    // Accept rule only if the two variable were previously declared,
    boolean declared = isVariableDeclared(NString.get(&variable.value));
    if (!declared) return False;
    declared = isVariableDeclared(NString.get(&variable.value));
    return declared;
    */
    return True;
}

//////////////////////////////////////
// Tests
//////////////////////////////////////

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)\n");

    // A number of test-cases that make sure that rules and matching behave as expected.
    struct NCC ncc;

    // Literals,
    assert(0, 0, 0, "besm\\ Allah", "besm Allah", True, 10);

    // x-y
    assert(0, 0, 0, "besm\\ Allah\\ a-z", "besm Allah x", True, 12);
    assert(0, 0, 0, "besm\\ Allah\\ a-z", "besm Allah 2", False, 11);
    assert(0, 0, 0, "besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12);

    // |
    assert(0, 0, 0, "a|b", "a", True, 1);
    assert(0, 0, 0, "abc|def", "abcef", True, 5);
    assert(0, 0, 0, "abc|def", "abdef", True, 5);
    assert(0, 0, 0, "abc|def", "abef", False, 2);
    assert(0, 0, 0, "a|b|c|d|ef", "cf", True, 2);

    // {}
    assert(0, 0, 0, "ab{cd{ef}gh}ij", "abcdefghij", True, 10);
    assert(0, 0, 0, "ab{cd}|{ef}gh", "abcdgh", True, 6);
    assert(0, 0, 0, "ab{cd}|{ef}gh", "abefgh", True, 6);
    assert(0, 0, 0, "ab{cd}|{ef}gh", "abgh", False, 2);
    assert(0, 0, 0, "a{a|b}", "ab", True, 2);
    assert(0, 0, 0, "a{b|c}d", "abf", False, 2);

    // ^*
    assert(0, 0, 0, "a^*bc", "abc", True, 3);
    assert(0, 0, 0, "a^*bc", "bc", True, 2);
    assert(0, 0, 0, "a^*bc", "aaaaabc", True, 7);
    assert(0, 0, 0, "a^*", "aaaaa", True, 5);
    assert(0, 0, 0, "123a^*", "123aaaaa", True, 8);
    assert(0, 0, 0, "123a^*456", "123a456", True, 7);
    assert(0, 0, 0, "123a^*456", "123456", True, 6);
    assert(0, 0, 0, "123{ab}^*456", "123ababab456", True, 12);
    assert(0, 0, 0, "{ab}^*{cd}^*", "x", True, 0);
    assert(0, 0, 0, "x{ab}^*{cd}^*", "x", True, 1);
    assert(0, 0, 0, "x{ab}^*{cd}^*", "xab", True, 3);
    assert(0, 0, 0, "x{ab}^*{cd}^*", "xcd", True, 3);
    assert(0, 0, 0, "{xyz}^*xyz", "xyzxyzxyz", True, 3);
    assert(0, 0, 0, "{{xyz}^*}xyz", "xyzxyzxyz", False, 9);

    // *
    assert(0, 0, 0, "*", "xyz", True, 3);
    assert(0, 0, 0, "**", "xyz", True, 3);
    assert(0, 0, 0, "********", "xyz", True, 3);
    assert(0, 0, 0, "********abc", "xyzabc", True, 6);
    assert(0, 0, 0, "*a*b*c*", "__a__c__", False, 8);
    assert(0, 0, 0, "*XYZ", "abcdefgXYZ", True, 10);
    assert(0, 0, 0, "{*}XYZ", "abcdefgXYZ", False, 10);

    // General test-cases,
    assert(0, 0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "myVariable3", True, 11);
    assert(0, 0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "3myVariable3", False, 0);
    assert(0, 0, 0, "/\\**\\*/", "/*بسم الله. This is a beautiful comment.\n The is the second line in the beautiful comment.*/", True, 99);

    // Substitute,
    NCC_initializeNCC(&ncc);
    assert(&ncc, "Comment"      , matchListener, "/\\**\\*/", "/*besm Allah*/", True, 14);
    assert(&ncc, "TwoComments"  , matchListener, "${Comment},${Comment}", "/*first comment*/,/*second comment*/", True, 36);
    assert(&ncc, "ThreeComments", matchListener, "${TwoComments},${Comment}", "/*first comment*/,/*second comment*/,/*thirrrrrd comment*/", True, 58);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Optional"          , matchListener, "{ab}^*{cd}^*", "", True, 0);
    assert(&ncc, "Mandatory"         , matchListener, "xyz", "xyz", True, 3);
    assert(&ncc, "ContainingOptional", matchListener, "${Optional}${Mandatory}", "xyz", True, 3);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Milestone"  , matchListener, "", "", True, 0);
    assert(&ncc, "ActualRule1", matchListener, "${Milestone}abc", "abc", True, 3);
    assert(&ncc, "ActualRule2", matchListener, "xyz", "xyz", True, 3);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Literal"        ,             0, "\x01-\xff", "", False, 0);
    assert(&ncc, "String"         ,             0, "\" { ${Literal}|{\\\\${Literal}} }^* \"", "", False, 0);
    assert(&ncc, "StringContainer", matchListener, "${String}", "\"besm Allah \\\" :)\"", True, 18);
    NCC_destroyNCC(&ncc);

    // Stateful parsing,
    NLOGI("", "%s================%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%sStateful Parsing%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%s================%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, &ncc, "", "", 0, 0, 0);
    NVector.initialize(&declaredVariables, 0, sizeof(NString));

//    NCC_initializeNCC(&ncc);
//    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*"));
//    NCC_addRule(ruleData.set(&ruleData, "identifier" , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*"));
//    NCC_addRule(ruleData.set(&ruleData, "declaration", "${identifier};")                      ->setListeners(&ruleData, 0, 0,        declarationListener));
//    NCC_addRule(ruleData.set(&ruleData, "assignment" , "${identifier}=${identifier};")        ->setListeners(&ruleData, 0, 0, validateAssignmentListener));
//    NCC_addRule(ruleData.set(&ruleData, "document"   , "{${declaration}|${assignment}|${}}^*")->setListeners(&ruleData,                          0, 0, 0));
//
//    assert(&ncc, "DocumentTest1", matchListener, True, "${} Test1: ${document}", "\n"
//            "Test1:\n"
//            "var1;\n"
//            "var2;\n"
//            "var1=var2;", True, 30);
//    destroyDeclaredVariables();
//
//    assert(&ncc, "DocumentTest2", matchListener, True, "${} Test2: ${document}", "\n"
//            "Test2:\n"
//            "var1;\n"
//            "var2;\n"
//            "var1=var3;", True, 20);
//    destroyDeclaredVariables();
//    NCC_destroyNCC(&ncc);
//
//    // Delete test,
//    NCC_initializeNCC(&ncc);
//    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*"));
//    NCC_addRule(ruleData.set(&ruleData, "identifier"    , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*"));
//    NCC_addRule(ruleData.set(&ruleData, "declaration"   , "${identifier} ${} ${identifier};")->setListeners(&ruleData, 0, rollBackListener, 0));
//    assert(&ncc, "RollBackTest", matchListener, True, "${declaration}|${declaration}", "int a;", True, 6);
//    NCC_destroyNCC(&ncc);

    NVector.destroy(&declaredVariables);
    NCC_destroyRuleData(&ruleData);

    NError.logAndTerminate();
}
