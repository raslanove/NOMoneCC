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

    const char* code = "push constant 111\n"
                       "push constant 333\n"
                       "push constant 888\n"
                       "pop static 8\n"
                       "pop static 3\n"
                       "pop static 1\n"
                       "push static 3\n"
                       "push static 1\n"
                       "sub\n"
                       "push static 8\n"
                       "add";

    // Create ncc,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // Add data to accumulate output,
    struct OutputData outputData;
    NString.initialize(&outputData.fileName);
    NString.set(&outputData.fileName, "foo"); // TODO: get from command line arguments (exclude .vm extension)...
    NString.initialize(&outputData.code);
    outputData.lastLabelIndex = 0;
    ncc.extraData = &outputData;

    // Elements,
    NCC_addRule(&ncc, "Empty", "", 0, False);
    NCC_addRule(&ncc, "WhiteSpace", "{\\ |\t|\\n}^*", 0, False);
    NCC_addRule(&ncc, "NotWhiteSpaceLiteral", "\x01-\x08 | \x0b-\x1f | \x21-\xff", 0, False);
    NCC_addRule(&ncc, "LineComment", "//*{\\n|${Empty}}", 0, False);
    NCC_addRule(&ncc, "Integer", "0-9 | 1-9 0-9^*", 0, False);

    // Instructions,
    NCC_addRule(&ncc, "StackModifier", "${NotWhiteSpaceLiteral}^*", 0, False);
    NCC_addRule(&ncc, "Push", "push ${WhiteSpace} ${StackModifier} ${WhiteSpace} ${Integer}", pushListener, False);
    NCC_addRule(&ncc, "Pop" , "pop  ${WhiteSpace} ${StackModifier} ${WhiteSpace} ${Integer}",  popListener, False);
    NCC_addRule(&ncc, "Add", "add", addListener, False);
    NCC_addRule(&ncc, "Sub", "sub", subListener, False);
    NCC_addRule(&ncc, "And", "and", andListener, False);
    NCC_addRule(&ncc, "Or" , "or" ,  orListener, False);
    NCC_addRule(&ncc, "Eq" , "eq" ,  eqListener, False);
    NCC_addRule(&ncc, "LT" , "lt" ,  ltListener, False);
    NCC_addRule(&ncc, "GT" , "gt" ,  gtListener, False);
    NCC_addRule(&ncc, "Neg", "neg", negListener, False);
    NCC_addRule(&ncc, "Not", "not", notListener, False);
    NCC_addRule(&ncc, "Instruction", "${Push} | ${Pop} | ${Add} | ${Sub} | ${And} | ${Or} | ${Eq} | ${LT} | ${GT} | ${Neg} | ${Not}", 0, False);

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
    NString.destroy(&outputData.fileName);
    NString.destroy(&outputData.code);

    NError.logAndTerminate();
}
