#include <NCC.h>

#include <NSystemUtils.h>
#include <NError.h>

void matchListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("HelloCC", "matchListener(): ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NString variableName, variableValue;
    NString.initialize(&variableName );
    NString.initialize(&variableValue);
    while (NCC_popVariable(ncc, &variableName, &variableValue)) NLOGI("HelloCC", "                             Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&variableName), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variableValue), NTCOLOR(STREAM_DEFAULT));
    NString.destroy(&variableName );
    NString.destroy(&variableValue);
}

void NMain() {

    const char* text = "     \n\n  \t    // besm Allah :)\n       ";
    NSystemUtils.logI("sdf", "besm Allah :)");

    // Substitute,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);
    NCC_addRule(&ncc, "WhiteSpace", "{\\ |\t|\\n}^*", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "LineComment", "//*\n", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "Document", "{${WhiteSpace} | ${LineComment}}^*", matchListener, True);
    NLOGI(0, "---------");
    int32_t matchLength = NCC_match(&ncc, text);
    NCC_destroyNCC(&ncc);

    NLOGI("VMTranslate", "NMain(): matchLength: %d", matchLength);

    NError.logAndTerminate();
}
