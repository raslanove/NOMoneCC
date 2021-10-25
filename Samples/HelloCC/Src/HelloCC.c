#include <NSystemUtils.h>
#include <NError.h>

#include <NCC.h>

void assert(const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength) {
    struct NCC_Node* tree = NCC_constructRuleTree(rule);
    if (!tree) {
        NERROR("HelloCC", "Couldn't create tree. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        return ;
    }

    int32_t matchLength = tree->match(tree, text);
    if (shouldMatch && matchLength!=expectedMatchLength) NERROR("HelloCC", "assert(): Match failed. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchLength, NTCOLOR(STREAM_DEFAULT));
    if (!shouldMatch && matchLength>0) NERROR("HelloCC", "assert(): Erroneously matched. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchLength, NTCOLOR(STREAM_DEFAULT));

    tree->deleteTree(tree);
    NLOGI("", "");
}

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)");

    assert("besm\\ Allah\\ a-z", "besm Allah x", True, 12);
    assert("besm\\ Allah\\ a-z", "besm Allah 2", False, 0);
    assert("besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12);

    assert("a|b", "a", True, 1);
    assert("abc|def", "abcef", True, 5);
    assert("abc|def", "abdef", True, 5);
    assert("abc|def", "abef", False, 0);
    assert("a|b|c|d|ef", "cf", True, 2);

    NError.popDestroyAndFreeErrors(0);
    NError.logAndTerminate();
}
