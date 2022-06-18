#include <NSystemUtils.h>
#include <NError.h>
#include <NVector.h>
#include <NCString.h>

#include <NCC.h>

//////////////////////////////////////
// Testing helper functions
//////////////////////////////////////

void assert(struct NCC* ncc, const char*ruleName, NCC_onConfirmedMatchListener onMatchListener, boolean rootRule, const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength) {

    boolean nccNeedsDeletion = False;
    if (!ncc) {
        ncc = NCC_createNCC();
        nccNeedsDeletion = True;
    }
    if (!ruleName) ruleName = "";

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, ncc, ruleName, rule, 0, 0, onMatchListener, rootRule, True, False);
    boolean success = NCC_addRule(&ruleData);
    NCC_destroyRuleData(&ruleData);
    if (!success) {
        NERROR("HelloCC", "Couldn't add rule. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
        return ;
    }

    struct NCC_MatchingResult matchingResult;
    boolean matched = NCC_match(ncc, text, &matchingResult);
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

void matchListener(struct NCC_MatchingData* matchingData) {
    NLOGI("HelloCC", "ruleName: %s, variablesCount: %d", NString.get(&matchingData->ruleData->ruleName), matchingData->variablesCount);
    struct NCC_Variable variable;
    while (NCC_popRuleVariable(matchingData->ruleData->ncc, &variable)) {
        NLOGI("HelloCC", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), variable.name, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
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

    struct NCC_Variable variable; NCC_popRuleVariable(matchingData->ruleData->ncc, &variable);
    addDeclaredVariable(NString.get(&variable.value));
    NCC_destroyVariable(&variable);

    return True;
}

boolean validateAssignmentListener(struct NCC_MatchingData* matchingData) {

    // Accept rule only if the two variable were previously declared,
    struct NCC_Variable variable; NCC_popRuleVariable(matchingData->ruleData->ncc, &variable);
    boolean declared = isVariableDeclared(NString.get(&variable.value));
    NCC_destroyVariable(&variable);
    if (!declared) return False;

    NCC_popRuleVariable(matchingData->ruleData->ncc, &variable);
    declared = isVariableDeclared(NString.get(&variable.value));
    NCC_destroyVariable(&variable);

    return declared;
}

//////////////////////////////////////
// Tests
//////////////////////////////////////

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)\n");

    // A number of test-cases that make sure that rules and matching behave as expected.

    // Literals,
    assert(0, 0, 0, True, "besm\\ Allah", "besm Allah", True, 10);

    // x-y
    assert(0, 0, 0, True, "besm\\ Allah\\ a-z", "besm Allah x", True, 12);
    assert(0, 0, 0, True, "besm\\ Allah\\ a-z", "besm Allah 2", False, 11);
    assert(0, 0, 0, True, "besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12);

    // |
    assert(0, 0, 0, True, "a|b", "a", True, 1);
    assert(0, 0, 0, True, "abc|def", "abcef", True, 5);
    assert(0, 0, 0, True, "abc|def", "abdef", True, 5);
    assert(0, 0, 0, True, "abc|def", "abef", False, 2);
    assert(0, 0, 0, True, "a|b|c|d|ef", "cf", True, 2);

    // {}
    assert(0, 0, 0, True, "ab{cd{ef}gh}ij", "abcdefghij", True, 10);
    assert(0, 0, 0, True, "ab{cd}|{ef}gh", "abcdgh", True, 6);
    assert(0, 0, 0, True, "ab{cd}|{ef}gh", "abefgh", True, 6);
    assert(0, 0, 0, True, "ab{cd}|{ef}gh", "abgh", False, 2);
    assert(0, 0, 0, True, "a{a|b}", "ab", True, 2);
    assert(0, 0, 0, True, "a{b|c}d", "abf", False, 2);

    // ^*
    assert(0, 0, 0, True, "a^*bc", "abc", True, 3);
    assert(0, 0, 0, True, "a^*bc", "bc", True, 2);
    assert(0, 0, 0, True, "a^*bc", "aaaaabc", True, 7);
    assert(0, 0, 0, True, "a^*", "aaaaa", True, 5);
    assert(0, 0, 0, True, "123a^*", "123aaaaa", True, 8);
    assert(0, 0, 0, True, "123a^*456", "123a456", True, 7);
    assert(0, 0, 0, True, "123a^*456", "123456", True, 6);
    assert(0, 0, 0, True, "123{ab}^*456", "123ababab456", True, 12);
    assert(0, 0, 0, True, "{ab}^*{cd}^*", "x", True, 0);
    assert(0, 0, 0, True, "x{ab}^*{cd}^*", "x", True, 1);
    assert(0, 0, 0, True, "x{ab}^*{cd}^*", "xab", True, 3);
    assert(0, 0, 0, True, "x{ab}^*{cd}^*", "xcd", True, 3);
    assert(0, 0, 0, True, "{xyz}^*xyz", "xyzxyzxyz", True, 3);
    assert(0, 0, 0, True, "{{xyz}^*}xyz", "xyzxyzxyz", False, 9);

    // *
    assert(0, 0, 0, True, "*", "xyz", True, 3);
    assert(0, 0, 0, True, "**", "xyz", True, 3);
    assert(0, 0, 0, True, "********", "xyz", True, 3);
    assert(0, 0, 0, True, "********abc", "xyzabc", True, 6);
    assert(0, 0, 0, True, "*a*b*c*", "__a__c__", False, 8);
    assert(0, 0, 0, True, "*XYZ", "abcdefgXYZ", True, 10);
    assert(0, 0, 0, True, "{*}XYZ", "abcdefgXYZ", False, 10);

    // General test-cases,
    assert(0, 0, 0, True, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "myVariable3", True, 11);
    assert(0, 0, 0, True, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "3myVariable3", False, 0);
    assert(0, 0, 0, True, "/\\**\\*/", "/*بسم الله. This is a beautiful comment.\n The is the second line in the beautiful comment.*/", True, 99);

    // Substitute,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);
    assert(&ncc, "Comment", matchListener, True, "/\\**\\*/", "/*besm Allah*/", True, 14);
    assert(&ncc, "TwoComments", matchListener, True, "${Comment},${Comment}", "/*first comment*/,/*second comment*/", True, 36);
    assert(&ncc, "ThreeComments", matchListener, True, "${TwoComments},${Comment}", "/*first comment*/,/*second comment*/,/*thirrrrrd comment*/", True, 58);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Optional", matchListener, False, "{ab}^*{cd}^*", "", False, 0); // The result of this assert if False because this is not a root-rule. Otherwise, the empty string should match with a match-length of 0.
    assert(&ncc, "Mandatory", matchListener, True, "xyz", "xyz", True, 3);
    assert(&ncc, "ContainingOptional", matchListener, True, "${Optional}${Mandatory}", "xyz", True, 3);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Milestone", matchListener, False, "", "", False, 0);
    assert(&ncc, "ActualRule1", matchListener, True, "${Milestone}abc", "abc", True, 3);
    assert(&ncc, "ActualRule2", matchListener, True, "xyz", "xyz", True, 3);
    NCC_destroyNCC(&ncc);

    NCC_initializeNCC(&ncc);
    assert(&ncc, "Literal", 0, False, "\x01-\xff", "", False, 0);
    assert(&ncc, "String", 0, False, "\" { ${Literal}|{\\\\${Literal}} }^* \"", "", False, 0);
    assert(&ncc, "StringContainer", matchListener, True, "${String}", "\"besm Allah \\\" :)\"", True, 18);
    NCC_destroyNCC(&ncc);

    // Stateful parsing,
    NLOGI("", "%s================%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%sStateful Parsing%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%s================%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, &ncc, "", "", 0, 0, 0, False, False, False);
    NVector.initialize(&declaredVariables, 0, sizeof(NString));
    NCC_initializeNCC(&ncc);

    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*"));
    NCC_addRule(ruleData.set(&ruleData, "identifier" , "a-z|A-z|_ {a-z|A-z|_|0-9}^*")->setFlags(&ruleData, False, True, False));
    NCC_addRule(ruleData.set(&ruleData, "declaration", "${identifier};")                      ->setListeners(&ruleData,        declarationListener, 0, 0));
    NCC_addRule(ruleData.set(&ruleData, "assignment" , "${identifier}=${identifier};")        ->setListeners(&ruleData, validateAssignmentListener, 0, 0));
    NCC_addRule(ruleData.set(&ruleData, "document"   , "{${declaration}|${assignment}|${}}^*")->setListeners(&ruleData,                          0, 0, 0));

    assert(&ncc, "DocumentTest1", matchListener, True, "${} Test1: ${document}", "\n"
            "Test1:\n"
            "var1;\n"
            "var2;\n"
            "var1=var2;", True, 30);
    destroyDeclaredVariables();

    assert(&ncc, "DocumentTest2", matchListener, True, "${} Test2: ${document}", "\n"
            "Test2:\n"
            "var1;\n"
            "var2;\n"
            "var1=var3;", True, 20);
    destroyDeclaredVariables();

    NCC_destroyNCC(&ncc);
    NVector.destroy(&declaredVariables);
    NCC_destroyRuleData(&ruleData);

    NError.logAndTerminate();
}
