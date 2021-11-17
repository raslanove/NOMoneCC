#include <NCC.h>

#include <NSystemUtils.h>
#include <NCString.h>
#include <NError.h>

struct OutputData {
    struct NString code;
};

void emitCode(struct NCC* ncc, const char* code) {
    NString.append(&((struct OutputData*) ncc->extraData)->code, "%s", code);
}

void emitInitializationCode(struct NCC* ncc) {
    emitCode(ncc, "// Initialization,\n(SP)\n@0\n(LCL)\n@1\n(ARG)\n@2\n(THIS)\n@3\n(THAT)\n@4\n\n");
    //emitCode(ncc, "// Set stack pointer,\n@256\nD=A\n@SP\nM=D\n\n");
}

void emitTerminationCode(struct NCC* ncc) {
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
        emitCode(ncc, "// push constant");
        emitCode(ncc, NString.get(&valueVariable.value));
        emitCode(ncc, "\n@");
        emitCode(ncc, NString.get(&valueVariable.value));
        emitCode(ncc, "\nD=A\n@SP\nA=M\nM=D\n@SP\nM=M+1\n\n");
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

    // Create ncc,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // Add data to accumulate output,
    struct OutputData outputData;
    NString.initialize(&outputData.code);
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
    NCC_addRule(&ncc, "Instruction", "${Push} | ${Add}", 0, False);

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
