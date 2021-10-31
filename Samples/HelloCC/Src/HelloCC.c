#include <NSystemUtils.h>
#include <NError.h>

#include <NCC.h>

void assert(struct NCC* ncc, const char*ruleName, const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength) {

    boolean nccNeedsDeletion = False;
    if (!ncc) {
        ncc = NCC_createNCC();
        nccNeedsDeletion = True;
    }
    if (!ruleName) ruleName = "";

    if (!NCC_addRule(ncc, ruleName, rule)) {
        NERROR("HelloCC", "Couldn't add rule. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        return ;
    }

    int32_t matchLength = NCC_match(ncc, text);
    if (shouldMatch && matchLength!=expectedMatchLength) NERROR("HelloCC", "assert(): Match failed. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchLength, NTCOLOR(STREAM_DEFAULT));
    if (!shouldMatch && matchLength!=-1) NERROR("HelloCC", "assert(): Erroneously matched. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchLength, NTCOLOR(STREAM_DEFAULT));

    if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
    NLOGI("", "");
}

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)");

    // x-y
    assert(0, 0, "besm\\ Allah\\ a-z", "besm Allah x", True, 12);
    assert(0, 0, "besm\\ Allah\\ a-z", "besm Allah 2", False, 0);
    assert(0, 0, "besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12);

    // |
    assert(0, 0, "a|b", "a", True, 1);
    assert(0, 0, "abc|def", "abcef", True, 5);
    assert(0, 0, "abc|def", "abdef", True, 5);
    assert(0, 0, "abc|def", "abef", False, 0);
    assert(0, 0, "a|b|c|d|ef", "cf", True, 2);

    // {}
    assert(0, 0, "ab{cd{ef}gh}ij", "abcdefghij", True, 10);
    assert(0, 0, "ab{cd}|{ef}gh", "abcdgh", True, 6);
    assert(0, 0, "ab{cd}|{ef}gh", "abefgh", True, 6);
    assert(0, 0, "ab{cd}|{ef}gh", "abgh", False, 0);
    assert(0, 0, "a{a|b}", "ab", True, 2);
    assert(0, 0, "a{b|c}d", "abf", False, 0);

    // ^*
    assert(0, 0, "a^*bc", "abc", True, 3);
    assert(0, 0, "a^*bc", "bc", True, 2);
    assert(0, 0, "a^*bc", "aaaaabc", True, 7);
    assert(0, 0, "a^*", "aaaaa", True, 5);
    assert(0, 0, "123a^*", "123aaaaa", True, 8);
    assert(0, 0, "123a^*456", "123a456", True, 7);
    assert(0, 0, "123a^*456", "123456", True, 6);
    assert(0, 0, "123{ab}^*456", "123ababab456", True, 12);
    assert(0, 0, "{ab}^*{cd}^*", "x", True, 0);
    assert(0, 0, "x{ab}^*{cd}^*", "x", True, 1);
    assert(0, 0, "x{ab}^*{cd}^*", "xab", True, 3);
    assert(0, 0, "x{ab}^*{cd}^*", "xcd", True, 3);

    // *
    assert(0, 0, "*", "xyz", True, 3);
    assert(0, 0, "**", "xyz", True, 3);
    assert(0, 0, "********", "xyz", True, 3);
    assert(0, 0, "********abc", "xyzabc", True, 6);
    assert(0, 0, "*a*b*c*", "__a__c__", False, 0);

    // General test-cases,
    assert(0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "myVariable3", True, 11);
    assert(0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "3myVariable3", False, 0);
    assert(0, 0, "/\\**\\*/", "/*بسم الله. This is a beautiful comment.\n The is the second line in the beautiful comment.*/", True, 99);

    // Substitute,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);
    assert(&ncc, "Comment", "/\\**\\*/", "/*besm Allah*/", True, 14);
    assert(&ncc, "TwoComment", "${Comment},${Comment}", "/*besm Allah*/,/*besm Allah*/", True, 29);
    NCC_destroyNCC(&ncc);

    NError.logAndTerminate();
}
