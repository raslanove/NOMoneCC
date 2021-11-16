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

    NSystemUtils.logI("sdf", "besm Allah :)");

    const char* text = "// This file is part of www.nand2tetris.org\n"
                       "// and the book \"The Elements of Computing Systems\"\n"
                       "// by Nisan and Schocken, MIT Press.\n"
                       "// File name: projects/07/StackArithmetic/SimpleAdd/SimpleAdd.vm\n"
                       "\n"
                       "// Pushes and adds two constants.\n"
                       "push constant 7\n"
                       "push constant 8\n"
                       "add\n"
                       "\"besm Allah \\\" :) \\ \" \n";

    // Substitute,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // Elements,
    NCC_addRule(&ncc, "Empty", "", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "WhiteSpace", "{\\ |\t|\\n}^*", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "LineComment", "//*{\\n|${Empty}}", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "Integer", "{1-9}{0-9}^*", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "Push", "push ${WhiteSpace} constant ${WhiteSpace} ${Integer}", matchListener, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "Add", "add", matchListener, False);
    NLOGI(0, "---------");

    // TODO: to be moved to HelloCC,
    // String,
    NCC_addRule(&ncc, "Literal", "\x01-\xff", 0, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "String", "\" { ${Literal}|{\\\\${Literal}} }^* \"", 0, False);
    NLOGI(0, "---------");

    // Document,
    NCC_addRule(&ncc, "Document", "{${WhiteSpace} | ${LineComment} | ${Push} | ${Add} | ${String}}^*", matchListener, True);
    NLOGI(0, "---------");

    int32_t matchLength = NCC_match(&ncc, text);
    NCC_destroyNCC(&ncc);

    NLOGI("VMTranslate", "NMain(): matchLength: %d", matchLength);

    NError.logAndTerminate();
}
