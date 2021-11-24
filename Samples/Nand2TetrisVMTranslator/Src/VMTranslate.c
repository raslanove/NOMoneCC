#include <NCC.h>
#include "CodeGeneration.h"

#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>

// TODO: remove. Provide file access utilities in the std lib instead...
#include <stdio.h>

void printMatch(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("VMTranslate", "ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NCC_Variable variable;
    while (NCC_popVariable(ncc, &variable)) {
        NLOGI("VMTranslate", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&variable.name), NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
    NLOGI("", "");
}

void NMain(int argc, char *argv[]) {

    NSystemUtils.logI("sdf", "besm Allah :)");

    // Read code file,
    FILE *file = fopen(argv[1], "rb");
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);  // Same as rewind(f).

    char* code = NSystemUtils.malloc(fileSize + 1);
    fread(code, 1, fileSize, file);
    fclose(file);

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
    //NLOGI("VMTranslate", "Generated code:\n============================\n%s", NString.get(&outputData.code));

    // Write generated code to file,
    // Generate file name,
    struct NString outputFileName;
    NString.initialize(&outputFileName);
    argv[1][NCString.length(argv[1])-3] = 0;
    NString.set(&outputFileName, "%s.asm", argv[1]);

    // Write to file,
    file = fopen(NString.get(&outputFileName), "wb");
    fwrite(NString.get(&outputData.code), 1, NString.length(&outputData.code), file);
    fclose(file);
    NString.destroy(&outputFileName);

    // Clean up,
    NString.destroy(&outputData.fileName);
    NString.destroy(&outputData.code);
    NSystemUtils.free(code);

    NError.logAndTerminate();
}
