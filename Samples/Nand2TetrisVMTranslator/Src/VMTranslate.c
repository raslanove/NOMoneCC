#include <NCC.h>

#include <NSystemUtils.h>
#include <NCString.h>
#include <NError.h>

struct OutputData {
    struct NString code;
    int32_t lastLabelIndex;
};

void emitCode(struct NCC* ncc, const char* format, ...) {
    va_list vaList;
    va_start(vaList, format);
    NString.vAppend(&((struct OutputData*) ncc->extraData)->code, format, vaList);
    va_end(vaList);
}

void emitInitializationCode(struct NCC* ncc) {
    // // Initialization,
    // (SP)
    // @0
    // (LCL)
    // @1
    // (ARG)
    // @2
    // (THIS)
    // @3
    // (THAT)
    // @4
    //
    // // Set stack pointer,
    // @256
    // D=A
    // @SP
    // M=D
    emitCode(ncc, "// Initialization,\n(SP)\n@0\n(LCL)\n@1\n(ARG)\n@2\n(THIS)\n@3\n(THAT)\n@4\n\n");
    emitCode(ncc, "// Set stack pointer,\n@256\nD=A\n@SP\nM=D\n\n");
}

void emitTerminationCode(struct NCC* ncc) {
    // // Termination,
    // (TERMINATION)
    // @TERMINATION
    // 0;JMP
    emitCode(ncc, "// Termination,\n(TERMINATION)\n@TERMINATION\n0;JMP\n");
}

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

    // Example:
    //   push constant 7
    //
    // Expecting 4 variables:
    //   Integer (7).
    //   Whitespace.
    //   Stack modifier (constant).
    //   Whitespace.

    // Offset,
    struct NCC_Variable valueVariable;
    NCC_popVariable(ncc, &valueVariable);

    // Whitespace,
    struct NCC_Variable variable;
    NCC_popVariable(ncc, &variable); NCC_destroyVariable(&variable);

    // Modifier,
    NCC_popVariable(ncc, &variable);
    if (NCString.equals(NString.get(&variable.value), "constant")) {
        // Code:
        //   // push constant value
        //   @value
        //   D=A
        //   @SP
        //   A=M
        //   M=D
        //   @SP
        //   M=M+1
        const char* valueString = NString.get(&valueVariable.value);
        emitCode(ncc, "// push constant %s\n@%s\nD=A\n@SP\nA=M\nM=D\n@SP\nM=M+1\n\n", valueString, valueString);
    }
    NCC_destroyVariable(&valueVariable);
    NCC_destroyVariable(&variable);
}

void addListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {

    // Example:
    //   add
    //
    // Expecting 0 variables.

    // Code:
    //   // add
    //   @SP
    //   M=M-1
    //   @SP
    //   A=M
    //   D=M
    //   @SP
    //   M=M-1
    //   A=M
    //   M=D+M
    //   @SP
    //   M=M+1
    emitCode(ncc, "// add\n@SP\nM=M-1\n@SP\nA=M\nD=M\n@SP\nM=M-1\nA=M\nM=D+M\n@SP\nM=M+1\n\n");
}

void emitComparisonCode(struct NCC* ncc, const char* instruction, const char* jump) {

    // Example:
    //   eq
    //
    // Expecting 0 variables.

    // Need two labels,
    struct OutputData* outputData = (struct OutputData*) ncc->extraData;
    int32_t labelIndex1 = outputData->lastLabelIndex + 1;
    int32_t labelIndex2 = labelIndex1 + 1;
    outputData->lastLabelIndex += 2;

    // Code:
    //   // eq
    //   @SP
    //   M=M-1
    //
    //   @SP
    //   A=M
    //   D=M
    //
    //   @SP
    //   M=M-1
    //   A=M
    //   D=D-M
    //
    //   @Label1
    //   D;JEQ     // Or any other compare operation.
    //
    //   @SP
    //   A=M
    //   M=0
    //
    //   @Label2
    //   0;JMP
    //
    //   (Label1)
    //   @SP
    //   A=M
    //   M=-1
    //
    //   (Label2)
    //   @SP
    //   M=M+1
    emitCode(ncc, "// %s\n@SP\nM=M-1\n@SP\nA=M\nD=M\n@SP\nM=M-1\nA=M\nD=D-M\n@Label%d\nD;%s\n@SP\nA=M\nM=0\n@Label%d\n0;JMP\n(Label%d)\n@SP\nA=M\nM=-1\n(Label%d)\n@SP\nM=M+1\n\n", instruction, labelIndex1, jump, labelIndex2, labelIndex1, labelIndex2);
}

void eqListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emitComparisonCode(ncc, "eq", "JEQ");
}

void ltListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emitComparisonCode(ncc, "lt", "JGT");
}

void gtListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emitComparisonCode(ncc, "gt", "JLT");
}

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)");

    const char* text = "push constant 17\n"
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
    int32_t matchLength = NCC_match(&ncc, text);
    emitTerminationCode(&ncc);

    NLOGI("VMTranslate", "Match length: %d\n", matchLength);
    NCC_destroyNCC(&ncc);

    // Print generated code,
    NLOGI("VMTranslate", "Generated code:\n============================\n%s", NString.get(&outputData.code));
    NString.destroy(&outputData.code);

    NError.logAndTerminate();
}
