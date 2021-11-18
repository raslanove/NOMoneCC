#include <NCC.h>
#include "CodeGeneration.h"

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

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)");

    const char* code = "push constant 17\n"
                       "push constant 17\n"
                       "eq\n"
                       "push constant 17\n"
                       "push constant 16\n"
                       "eq\n"
                       "push constant 16\n"
                       "push constant 17\n"
                       "eq\n"
                       "push constant 892\n"
                       "push constant 891\n"
                       "lt\n"
                       "push constant 891\n"
                       "push constant 892\n"
                       "lt\n"
                       "push constant 891\n"
                       "push constant 891\n"
                       "lt\n"
                       "push constant 32767\n"
                       "push constant 32766\n"
                       "gt\n"
                       "push constant 32766\n"
                       "push constant 32767\n"
                       "gt\n"
                       "push constant 32766\n"
                       "push constant 32766\n"
                       "gt";

    // Create ncc,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // Add data to accumulate output,
    struct OutputData outputData;
    NString.initialize(&outputData.code);
    outputData.lastLabelIndex = 0;
    ncc.extraData = &outputData;

    // Elements,
    NCC_addRule(&ncc, "Empty", "", 0, False);
    NCC_addRule(&ncc, "WhiteSpace", "{\\ |\t|\\n}^*", 0, False);
    NCC_addRule(&ncc, "LineComment", "//*{\\n|${Empty}}", 0, False);
    NCC_addRule(&ncc, "Integer", "{1-9}{0-9}^*", 0, False);

    // Instructions,
    NCC_addRule(&ncc, "StackModifier", "constant", 0, False);
    NCC_addRule(&ncc, "Push", "push ${WhiteSpace} ${StackModifier} ${WhiteSpace} ${Integer}", pushListener, False);
    NCC_addRule(&ncc, "Add", "add", addListener, False);
    NCC_addRule(&ncc, "Eq", "eq", eqListener, False);
    NCC_addRule(&ncc, "LT", "lt", ltListener, False);
    NCC_addRule(&ncc, "GT", "gt", gtListener, False);
    NCC_addRule(&ncc, "Instruction", "${Push} | ${Add} | ${Eq} | ${LT} | ${GT}", 0, False);

    // Document,
    NCC_addRule(&ncc, "Document", "{${WhiteSpace} | ${LineComment} | ${Instruction}}^*", 0, True);

    // Generate code,
    emitInitializationCode(&ncc);
    int32_t matchLength = NCC_match(&ncc, code);
    emitTerminationCode(&ncc);

    NLOGI("VMTranslate", "Match length: %d\n", matchLength);
    NCC_destroyNCC(&ncc);

    // Print generated code,
    NLOGI("VMTranslate", "Generated code:\n============================\n%s", NString.get(&outputData.code));
    NString.destroy(&outputData.code);

    NError.logAndTerminate();
}
