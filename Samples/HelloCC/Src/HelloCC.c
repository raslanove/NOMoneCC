#include <NSystemUtils.h>
#include <NError.h>

#include <NCC.h>

void assert(struct NCC* ncc, const char*ruleName, NCC_onMatchListener onMatchListener, boolean rootRule, const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength) {

    boolean nccNeedsDeletion = False;
    if (!ncc) {
        ncc = NCC_createNCC();
        nccNeedsDeletion = True;
    }
    if (!ruleName) ruleName = "";

    if (!NCC_addRule(ncc, ruleName, rule, onMatchListener, rootRule)) {
        NERROR("HelloCC", "Couldn't add rule. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        return ;
    }

    int32_t matchLength = NCC_match(ncc, text);
    if (shouldMatch && matchLength!=expectedMatchLength) NERROR("HelloCC", "assert(): Match failed. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchLength, NTCOLOR(STREAM_DEFAULT));
    if (!shouldMatch && matchLength!=-1) NERROR("HelloCC", "assert(): Erroneously matched. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchLength, NTCOLOR(STREAM_DEFAULT));

    if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
    NLOGI("", "");
}

void matchListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("HelloCC", "ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NCC_Variable variable;
    while (NCC_popVariable(ncc, &variable)) {
        NLOGI("HelloCC", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&variable.name), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
}

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)");

    // x-y
    assert(0, 0, 0, True, "besm\\ Allah\\ a-z", "besm Allah x", True, 12);
    assert(0, 0, 0, True, "besm\\ Allah\\ a-z", "besm Allah 2", False, 0);
    assert(0, 0, 0, True, "besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12);

    // |
    assert(0, 0, 0, True, "a|b", "a", True, 1);
    assert(0, 0, 0, True, "abc|def", "abcef", True, 5);
    assert(0, 0, 0, True, "abc|def", "abdef", True, 5);
    assert(0, 0, 0, True, "abc|def", "abef", False, 0);
    assert(0, 0, 0, True, "a|b|c|d|ef", "cf", True, 2);

    // {}
    assert(0, 0, 0, True, "ab{cd{ef}gh}ij", "abcdefghij", True, 10);
    assert(0, 0, 0, True, "ab{cd}|{ef}gh", "abcdgh", True, 6);
    assert(0, 0, 0, True, "ab{cd}|{ef}gh", "abefgh", True, 6);
    assert(0, 0, 0, True, "ab{cd}|{ef}gh", "abgh", False, 0);
    assert(0, 0, 0, True, "a{a|b}", "ab", True, 2);
    assert(0, 0, 0, True, "a{b|c}d", "abf", False, 0);

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
    assert(0, 0, 0, True, "{{xyz}^*}xyz", "xyzxyzxyz", False, 0);

    // *
    assert(0, 0, 0, True, "*", "xyz", True, 3);
    assert(0, 0, 0, True, "**", "xyz", True, 3);
    assert(0, 0, 0, True, "********", "xyz", True, 3);
    assert(0, 0, 0, True, "********abc", "xyzabc", True, 6);
    assert(0, 0, 0, True, "*a*b*c*", "__a__c__", False, 0);
    assert(0, 0, 0, True, "*XYZ", "abcdefgXYZ", True, 10);
    assert(0, 0, 0, True, "{*}XYZ", "abcdefgXYZ", False, 0);

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
    assert(&ncc, "Optional", matchListener, False, "{ab}^*{cd}^*", "", False, 0);
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

    NError.logAndTerminate();
}
