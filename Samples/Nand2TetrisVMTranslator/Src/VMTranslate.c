#include <NCC.h>

#include <NSystemUtils.h>
#include <NError.h>

void printMatch(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("VMTranslate", "ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NCC_Variable variable;
    while (NCC_popVariable(ncc, &variable)) {
        NLOGI("VMTranslate", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&variable.name), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
    NLOGI("", "");
}

void pushListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    printMatch(ncc, ruleName, variablesCount);
}

void addListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    printMatch(ncc, ruleName, variablesCount);
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

    // Instructions,
    NCC_addRule(&ncc, "Push", "push ${WhiteSpace} constant ${WhiteSpace} ${Integer}", pushListener, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "Add", "add", addListener, False);
    NLOGI(0, "---------");
    NCC_addRule(&ncc, "Instruction", "${Push} | ${Add}", printMatch, False);
    NLOGI(0, "---------");

    // Document,
    NCC_addRule(&ncc, "Document", "{${WhiteSpace} | ${LineComment} | ${Instruction}}^*", printMatch, True);
    NLOGI(0, "---------");

    // Match,
    int32_t matchLength = NCC_match(&ncc, text);
    NLOGI("VMTranslate", "NMain(): matchLength: %d", matchLength);
    NCC_destroyNCC(&ncc);

    NError.logAndTerminate();
}
