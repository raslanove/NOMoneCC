#include "CodeGeneration.h"
#include <NCC.h>

#include <NCString.h>
#include <NError.h>
#include <NSystemUtils.h>

static void emitCode(struct NCC* ncc, const char* format, ...) {
    va_list vaList;
    va_start(vaList, format);
    NString.vAppend(&((struct OutputData*) ncc->extraData)->code, format, vaList);
    va_end(vaList);
}

void emitInitializationCode(struct NCC* ncc) {

    // // Set variable names,
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
    //
    // // Set local segment,
    // @300
    // D=A
    // @LCL
    // M=D
    //
    // // Set argument segment,
    // @400
    // D=A
    // @ARG
    // M=D
    //
    // // Set this segment,
    // @3000
    // D=A
    // @THIS
    // M=D
    //
    // // Set that segment,
    // @3010
    // D=A
    // @THAT
    // M=D

    emitCode(ncc, "// Set variable names,\n(SP)\n@0\n(LCL)\n@1\n(ARG)\n@2\n(THIS)\n@3\n(THAT)\n@4\n\n");
    emitCode(ncc, "// Set stack pointer,\n@256\nD=A\n@SP\nM=D\n\n");
    emitCode(ncc, "// Set local segment,\n@300\nD=A\n@LCL\nM=D\n\n// Set argument segment,\n@400\nD=A\n@ARG\nM=D\n\n// Set this segment,\n@3000\nD=A\n@THIS\nM=D\n\n// Set that segment,\n@3010\nD=A\n@THAT\nM=D\n\n");
}

void emitTerminationCode(struct NCC* ncc) {
    // // Termination,
    // (TERMINATION)
    // @TERMINATION
    // 0;JMP
    emitCode(ncc, "// Termination,\n(TERMINATION)\n@TERMINATION\n0;JMP\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory manipulation
////////////////////////////////////////////////////////////////////////////////////////////////////

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
    const char* valueString = NString.get(&valueVariable.value);

    // Whitespace,
    struct NCC_Variable variable;
    NCC_popVariable(ncc, &variable); NCC_destroyVariable(&variable);

    // Modifier,
    NCC_popVariable(ncc, &variable);
    if (NCString.equals(NString.get(&variable.value), "local")) {
        // Code:
        //   // push local index (or argument/this/that)
        //   @index
        //   D=A
        //   @LCL      // or ARG/THIS/THAT
        //   A=M
        //   A=A+D
        //   D=M
        //
        //   @SP
        //   A=M
        //   M=D
        //
        //   @SP
        //   M=M+1
        emitCode(ncc, "// push local %s\n@%s\nD=A\n@LCL\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "argument")) {
        emitCode(ncc, "// push argument %s\n@%s\nD=A\n@ARG\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "this")) {
        emitCode(ncc, "// push this %s\n@%s\nD=A\n@THIS\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "that")) {
        emitCode(ncc, "// push that %s\n@%s\nD=A\n@THAT\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "pointer")) {
        // Code:
        //   // push pointer 0 (or 1)
        //   @THIS  // or THAT.
        //   D=M
        //
        //   @SP
        //   A=M
        //   M=D
        //
        //   @SP
        //   M=M+1
        if (NCString.equals(valueString, "0")) {
            emitCode(ncc, "// push pointer 0\n@THIS\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n");
        } else if (NCString.equals(valueString, "1")) {
            emitCode(ncc, "// push pointer 1\n@THAT\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n");
        } else {
            NERROR("CodeGeneration", "pushListener(): pointer index can only be 0 or 1. Found: %s%s%s", NTCOLOR(HIGHLIGHT), valueString, NTCOLOR(STREAM_DEFAULT));
            return ; // TODO: Should set parsing termination flag...
        }
    } else if (NCString.equals(NString.get(&variable.value), "temp")) {
        int32_t index = 5 + NCString.parseInteger(valueString);
        // Code:
        //   // push temp index
        //   @index
        //   D=M
        //
        //   @SP
        //   A=M
        //   M=D
        //
        //   @SP
        //   M=M+1
        emitCode(ncc, "// push temp %s\n@%d\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, index);
    } else if (NCString.equals(NString.get(&variable.value), "constant")) {
        // Code:
        //   // push constant value
        //   @value
        //   D=A
        //   @SP
        //   A=M
        //   M=D
        //
        //   @SP
        //   M=M+1
        emitCode(ncc, "// push constant %s\n@%s\nD=A\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "static")) {

        // Prepare static variable name,
        struct NString staticVariableName;
        NString.initialize(&staticVariableName);

        struct OutputData* outputData = (struct OutputData*) ncc->extraData;
        int32_t index = NCString.parseInteger(valueString);
        NString.set(&staticVariableName, "%s.%d", NString.get(&outputData->fileName), index);

        // Code:
        //   // push static index
        //   @staticVariableName
        //   D=M
        //   @SP
        //   A=M
        //   M=D
        //
        //   @SP
        //   M=M+1
        emitCode(ncc, "// push static %s\n@%s\nD=M\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", valueString, NString.get(&staticVariableName));
        NString.destroy(&staticVariableName);
    } else {
        NERROR("CodeGeneration", "pushListener(): expected local|argument|this|that|pointer|temp|constant|static. Found: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        return ; // TODO: Should set parsing termination flag...
    }

    NCC_destroyVariable(&valueVariable);
    NCC_destroyVariable(&variable);
}

void popListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {

    // Example:
    //   pop local 7
    //
    // Expecting 4 variables:
    //   Integer (7).
    //   Whitespace.
    //   Stack modifier (local).
    //   Whitespace.

    // Offset,
    struct NCC_Variable valueVariable;
    NCC_popVariable(ncc, &valueVariable);
    const char* valueString = NString.get(&valueVariable.value);

    // Whitespace,
    struct NCC_Variable variable;
    NCC_popVariable(ncc, &variable); NCC_destroyVariable(&variable);

    // Modifier,
    NCC_popVariable(ncc, &variable);
    if (NCString.equals(NString.get(&variable.value), "local")) {
        // Code:
        //   // pop local index (or argument/this/that)
        //   @index
        //   D=A
        //   @LCL      // or ARG/THIS/THAT
        //   D=M+D
        //
        //   @SP
        //   A=M
        //   M=D
        //
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @SP
        //   A=M+1
        //   A=M
        //   M=D
        emitCode(ncc, "// pop local %s\n@%s\nD=A\n@LCL\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "argument")) {
        emitCode(ncc, "// pop argument %s\n@%s\nD=A\n@ARG\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "this")) {
        emitCode(ncc, "// pop this %s\n@%s\nD=A\n@THIS\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "that")) {
        emitCode(ncc, "// pop that %s\n@%s\nD=A\n@THAT\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", valueString, valueString);
    } else if (NCString.equals(NString.get(&variable.value), "pointer")) {
        // Code:
        //   // pop pointer 0 (or 1)
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @THIS  // or THAT.
        //   M=D
        if (NCString.equals(valueString, "0")) {
            emitCode(ncc, "// pop pointer 0\n@SP\nM=M-1\nA=M\nD=M\n\n@THIS\nM=D\n\n");
        } else if (NCString.equals(valueString, "1")) {
            emitCode(ncc, "// pop pointer 1\n@SP\nM=M-1\nA=M\nD=M\n\n@THAT\nM=D\n\n");
        } else {
            NERROR("CodeGeneration", "popListener(): pointer index can only be 0 or 1. Found: %s%s%s", NTCOLOR(HIGHLIGHT), valueString, NTCOLOR(STREAM_DEFAULT));
            return ; // TODO: Should set parsing termination flag...
        }
    } else if (NCString.equals(NString.get(&variable.value), "temp")) {
        int32_t index = 5 + NCString.parseInteger(valueString);
        // Code:
        //   // pop temp index
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @index
        //   M=D
        emitCode(ncc, "// pop temp %s\n@SP\nM=M-1\nA=M\nD=M\n\n@%d\nM=D\n\n", valueString, index);
    } else if (NCString.equals(NString.get(&variable.value), "static")) {

        // Prepare static variable name,
        struct NString staticVariableName;
        NString.initialize(&staticVariableName);

        struct OutputData* outputData = (struct OutputData*) ncc->extraData;
        int32_t index = NCString.parseInteger(valueString);
        NString.set(&staticVariableName, "%s.%d", NString.get(&outputData->fileName), index);

        // Code:
        //   // pop static index
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @staticVariableName
        //   M=D
        emitCode(ncc, "// pop static %s\n@SP\nM=M-1\nA=M\nD=M\n\n@%s\nM=D\n\n", valueString, NString.get(&staticVariableName));
        NString.destroy(&staticVariableName);
    } else {
        NERROR("CodeGeneration", "popListener(): expected local|argument|this|that|pointer|temp|static. Found: %s%s%s", NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        return ; // TODO: Should set parsing termination flag...
    }

    NCC_destroyVariable(&valueVariable);
    NCC_destroyVariable(&variable);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// 1-operand arithmetic
////////////////////////////////////////////////////////////////////////////////////////////////////

void negListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {

    // Example:
    //   neg
    //
    // Expecting 0 variables.

    // Code:
    //   // neg
    //   D=0
    //   @SP
    //   M=M-1
    //   A=M
    //   M=D-M
    //   @SP
    //   M=M+1
    emitCode(ncc, "// neg\nD=0\n@SP\nM=M-1\nA=M\nM=D-M\n@SP\nM=M+1\n\n");
}

void notListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {

    // Example:
    //   not
    //
    // Expecting 0 variables.

    // Code:
    //   // not
    //   @SP
    //   M=M-1
    //   A=M
    //   M=!M
    //   @SP
    //   M=M+1
    emitCode(ncc, "// not\n@SP\nM=M-1\nA=M\nM=!M\n@SP\nM=M+1\n\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// 2-operand arithmetic
////////////////////////////////////////////////////////////////////////////////////////////////////

inline static void emit2OperandArithmeticCode(struct NCC* ncc, const char* instruction, char operator) {
    // Example:
    //   add
    //
    // Expecting 0 variables.

    // Code:
    //   // add
    //   @SP
    //   M=M-1
    //   A=M
    //   D=M
    //
    //   @SP
    //   M=M-1
    //   A=M
    //   M=M+D    // Or another operator.
    //
    //   @SP
    //   M=M+1
    emitCode(ncc, "// %s\n@SP\nM=M-1\nA=M\nD=M\n@SP\nM=M-1\nA=M\nM=M%cD\n@SP\nM=M+1\n\n", instruction, operator);
}

void addListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emit2OperandArithmeticCode(ncc, "add", '+');
}

void subListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emit2OperandArithmeticCode(ncc, "sub", '-');
}

void andListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emit2OperandArithmeticCode(ncc, "and", '&');
}

void  orListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emit2OperandArithmeticCode(ncc, "or", '|');
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Comparison
////////////////////////////////////////////////////////////////////////////////////////////////////

inline static void emitComparisonCode(struct NCC* ncc, const char* instruction, const char* jump) {

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
    emitCode(ncc, "// %s\n@SP\nM=M-1\nA=M\nD=M\n@SP\nM=M-1\nA=M\nD=D-M\n@Label%d\nD;%s\n@SP\nA=M\nM=0\n@Label%d\n0;JMP\n(Label%d)\n@SP\nA=M\nM=-1\n(Label%d)\n@SP\nM=M+1\n\n", instruction, labelIndex1, jump, labelIndex2, labelIndex1, labelIndex2);
}

void  eqListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emitComparisonCode(ncc, "eq", "JEQ");
}

void  ltListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emitComparisonCode(ncc, "lt", "JGT");
}

void  gtListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    emitComparisonCode(ncc, "gt", "JLT");
}