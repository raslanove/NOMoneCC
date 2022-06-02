#include "CodeGeneration.h"
#include <NCC.h>

#include <NCString.h>
#include <NError.h>
#include <NSystemUtils.h>

static void emitCallCode(struct NCC* ncc, const char* functionName, int32_t argumentsCount);

void emitCode(struct NCC* ncc, const char* format, ...) {
    va_list vaList;
    va_start(vaList, format);
    NString.vAppend(&((struct OutputData*) ncc->extraData)->code, format, vaList);
    va_end(vaList);
}

void emitInitializationCode(struct NCC* ncc, InitializationFlags flags) {

    if (flags &     VARIABLES) {
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
    }
    if (flags & STACK_POINTER) emitCode(ncc, "// Set stack pointer,\n@256\nD=A\n@SP\nM=D\n\n");
    if (flags &      SEGMENTS) emitCode(ncc, "// Set local segment,\n@300\nD=A\n@LCL\nM=D\n\n// Set argument segment,\n@400\nD=A\n@ARG\nM=D\n\n// Set this segment,\n@3000\nD=A\n@THIS\nM=D\n\n// Set that segment,\n@3010\nD=A\n@THAT\nM=D\n\n");
    if (flags &      SYS_INIT) {
        emitCode(ncc, "// Boot,\n");
        emitCallCode(ncc, "Sys.init", 0);
    }
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

static void emitPushCode(struct NCC* ncc, const char* modifier, const char* offsetOrValue) {

    if (NCString.equals(modifier, "local")) {
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

        // TODO: try this later...
        // Note: this can be optimized as following:
        // @index
        // D=A
        // @LCL
        // A=M+D
        // D=M
        //
        // @SP
        // M=M+1
        // A=M-1
        // M=D

        emitCode(ncc, "// push local %s\n@%s\nD=A\n@LCL\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, offsetOrValue);
    } else if (NCString.equals(modifier, "argument")) {
        emitCode(ncc, "// push argument %s\n@%s\nD=A\n@ARG\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, offsetOrValue);
    } else if (NCString.equals(modifier, "this")) {
        emitCode(ncc, "// push this %s\n@%s\nD=A\n@THIS\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, offsetOrValue);
    } else if (NCString.equals(modifier, "that")) {
        emitCode(ncc, "// push that %s\n@%s\nD=A\n@THAT\nA=M\nA=A+D\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, offsetOrValue);
    } else if (NCString.equals(modifier, "pointer")) {
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
        if (NCString.equals(offsetOrValue, "0")) {
            emitCode(ncc, "// push pointer 0\n@THIS\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n");
        } else if (NCString.equals(offsetOrValue, "1")) {
            emitCode(ncc, "// push pointer 1\n@THAT\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n");
        } else {
            NERROR("CodeGeneration", "pushListener(): pointer index can only be 0 or 1. Found: %s%s%s", NTCOLOR(HIGHLIGHT), offsetOrValue, NTCOLOR(STREAM_DEFAULT));
            return ; // TODO: Should set parsing termination flag...
        }
    } else if (NCString.equals(modifier, "temp")) {
        int32_t index = 5 + NCString.parseInteger(offsetOrValue);
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
        emitCode(ncc, "// push temp %s\n@%d\nD=M\n\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, index);
    } else if (NCString.equals(modifier, "constant")) {
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
        emitCode(ncc, "// push constant %s\n@%s\nD=A\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, offsetOrValue);
    } else if (NCString.equals(modifier, "static")) {

        // Prepare static variable name,
        struct NString staticVariableName;
        struct OutputData* outputData = (struct OutputData*) ncc->extraData;
        int32_t index = NCString.parseInteger(offsetOrValue);
        NString.initialize(&staticVariableName, "%s.%d", NString.get(&outputData->fileName), index);

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
        emitCode(ncc, "// push static %s\n@%s\nD=M\n@SP\nA=M\nM=D\n\n@SP\nM=M+1\n\n", offsetOrValue, NString.get(&staticVariableName));
        NString.destroy(&staticVariableName);
    } else {
        NERROR("CodeGeneration", "pushListener(): expected local|argument|this|that|pointer|temp|constant|static. Found: %s%s%s", NTCOLOR(HIGHLIGHT), modifier, NTCOLOR(STREAM_DEFAULT));
        return ; // TODO: Should set parsing termination flag...
    }
}

static void emitPopCode(struct NCC* ncc, const char* modifier, const char* offset) {

    if (NCString.equals(modifier, "local")) {
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
        emitCode(ncc, "// pop local %s\n@%s\nD=A\n@LCL\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", offset, offset);
    } else if (NCString.equals(modifier, "argument")) {
        emitCode(ncc, "// pop argument %s\n@%s\nD=A\n@ARG\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", offset, offset);
    } else if (NCString.equals(modifier, "this")) {
        emitCode(ncc, "// pop this %s\n@%s\nD=A\n@THIS\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", offset, offset);
    } else if (NCString.equals(modifier, "that")) {
        emitCode(ncc, "// pop that %s\n@%s\nD=A\n@THAT\nD=M+D\n\n@SP\nA=M\nM=D\n\n@SP\nM=M-1\nA=M\nD=M\n\n@SP\nA=M+1\nA=M\nM=D\n\n", offset, offset);
    } else if (NCString.equals(modifier, "pointer")) {
        // Code:
        //   // pop pointer 0 (or 1)
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @THIS  // or THAT.
        //   M=D
        if (NCString.equals(offset, "0")) {
            emitCode(ncc, "// pop pointer 0\n@SP\nM=M-1\nA=M\nD=M\n\n@THIS\nM=D\n\n");
        } else if (NCString.equals(offset, "1")) {
            emitCode(ncc, "// pop pointer 1\n@SP\nM=M-1\nA=M\nD=M\n\n@THAT\nM=D\n\n");
        } else {
            NERROR("CodeGeneration", "popListener(): pointer index can only be 0 or 1. Found: %s%s%s", NTCOLOR(HIGHLIGHT), offset, NTCOLOR(STREAM_DEFAULT));
            return ; // TODO: Should set parsing termination flag...
        }
    } else if (NCString.equals(modifier, "temp")) {
        int32_t index = 5 + NCString.parseInteger(offset);
        // Code:
        //   // pop temp index
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @index
        //   M=D
        emitCode(ncc, "// pop temp %s\n@SP\nM=M-1\nA=M\nD=M\n\n@%d\nM=D\n\n", offset, index);
    } else if (NCString.equals(modifier, "static")) {

        // Prepare static variable name,
        struct NString staticVariableName;
        struct OutputData* outputData = (struct OutputData*) ncc->extraData;
        int32_t index = NCString.parseInteger(offset);
        NString.initialize(&staticVariableName, "%s.%d", NString.get(&outputData->fileName), index);

        // Code:
        //   // pop static index
        //   @SP
        //   M=M-1
        //   A=M
        //   D=M
        //
        //   @staticVariableName
        //   M=D
        emitCode(ncc, "// pop static %s\n@SP\nM=M-1\nA=M\nD=M\n\n@%s\nM=D\n\n", offset, NString.get(&staticVariableName));
        NString.destroy(&staticVariableName);
    } else {
        NERROR("CodeGeneration", "popListener(): expected local|argument|this|that|pointer|temp|static. Found: %s%s%s", NTCOLOR(HIGHLIGHT), modifier, NTCOLOR(STREAM_DEFAULT));
        return ; // TODO: Should set parsing termination flag...
    }
}

void pushListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   push constant 7
    //
    // Expecting 4 variables:
    //   Integer (7, offset or value).
    //   Whitespace.
    //   Stack modifier (constant).
    //   Whitespace.

    // Offset or value,
    struct NCC_Variable offsetOrValueVariable; NCC_popRuleVariable(ncc, &offsetOrValueVariable);

    // Whitespace,
    struct NCC_Variable whiteSpaceVariable; NCC_popRuleVariable(ncc, &whiteSpaceVariable); NCC_destroyVariable(&whiteSpaceVariable);

    // Modifier,
    struct NCC_Variable modifierVariable; NCC_popRuleVariable(ncc, &modifierVariable);

    // Emit code,
    emitPushCode(ncc, NString.get(&modifierVariable.value), NString.get(&offsetOrValueVariable.value));

    // Clean up,
    NCC_destroyVariable(&offsetOrValueVariable);
    NCC_destroyVariable(&modifierVariable);
}

void popListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   pop local 7
    //
    // Expecting 4 variables:
    //   Integer (7).
    //   Whitespace.
    //   Stack modifier (local).
    //   Whitespace.

    // Offset,
    struct NCC_Variable offsetVariable; NCC_popRuleVariable(ncc, &offsetVariable);

    // Whitespace,
    struct NCC_Variable whiteSpaceVariable; NCC_popRuleVariable(ncc, &whiteSpaceVariable); NCC_destroyVariable(&whiteSpaceVariable);

    // Modifier,
    struct NCC_Variable modifierVariable; NCC_popRuleVariable(ncc, &modifierVariable);

    // Emit code,
    emitPopCode(ncc, NString.get(&modifierVariable.value), NString.get(&offsetVariable.value));

    // Clean up,
    NCC_destroyVariable(&offsetVariable);
    NCC_destroyVariable(&modifierVariable);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// 1-operand arithmetic
////////////////////////////////////////////////////////////////////////////////////////////////////

void negListener(struct NCC_MatchingData* matchingData) {

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
    emitCode(matchingData->ruleData->ncc, "// neg\nD=0\n@SP\nM=M-1\nA=M\nM=D-M\n@SP\nM=M+1\n\n");
}

void notListener(struct NCC_MatchingData* matchingData) {

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
    emitCode(matchingData->ruleData->ncc, "// not\n@SP\nM=M-1\nA=M\nM=!M\n@SP\nM=M+1\n\n");
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

void addListener(struct NCC_MatchingData* matchingData) {
    emit2OperandArithmeticCode(matchingData->ruleData->ncc, "add", '+');
}

void subListener(struct NCC_MatchingData* matchingData) {
    emit2OperandArithmeticCode(matchingData->ruleData->ncc, "sub", '-');
}

void andListener(struct NCC_MatchingData* matchingData) {
    emit2OperandArithmeticCode(matchingData->ruleData->ncc, "and", '&');
}

void  orListener(struct NCC_MatchingData* matchingData) {
    emit2OperandArithmeticCode(matchingData->ruleData->ncc, "or", '|');
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
    //   @NLabel1
    //   D;JEQ     // Or any other compare operation.
    //
    //   @SP
    //   A=M
    //   M=0
    //
    //   @NLabel2
    //   0;JMP
    //
    //   (NLabel1)
    //   @SP
    //   A=M
    //   M=-1
    //
    //   (NLabel2)
    //   @SP
    //   M=M+1
    emitCode(ncc, "// %s\n@SP\nM=M-1\nA=M\nD=M\n@SP\nM=M-1\nA=M\nD=D-M\n@NLabel%d\nD;%s\n@SP\nA=M\nM=0\n@NLabel%d\n0;JMP\n(NLabel%d)\n@SP\nA=M\nM=-1\n(NLabel%d)\n@SP\nM=M+1\n\n", instruction, labelIndex1, jump, labelIndex2, labelIndex1, labelIndex2);
}

void  eqListener(struct NCC_MatchingData* matchingData) {
    emitComparisonCode(matchingData->ruleData->ncc, "eq", "JEQ");
}

void  ltListener(struct NCC_MatchingData* matchingData) {
    emitComparisonCode(matchingData->ruleData->ncc, "lt", "JGT");
}

void  gtListener(struct NCC_MatchingData* matchingData) {
    emitComparisonCode(matchingData->ruleData->ncc, "gt", "JLT");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Flow control
////////////////////////////////////////////////////////////////////////////////////////////////////

static void emitLabelCode(struct NCC* ncc, const char* labelName) {
    // Code:
    //   // label LabelName
    //   (LabelName)
    emitCode(ncc, "// label %s\n(%s)\n\n", labelName, labelName);
}

void labelListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   label LabelName
    //
    // Expecting 2 variables:
    //   LabelName.
    //   Whitespace.

    // Label name,
    struct NCC_Variable labelNameVariable;
    NCC_popRuleVariable(ncc, &labelNameVariable);
    emitLabelCode(ncc, NString.get(&labelNameVariable.value));
    NCC_destroyVariable(&labelNameVariable);
}

void jumpListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   goto LabelName
    //
    // Expecting 2 variables:
    //   LabelName.
    //   Whitespace.

    // Label name,
    struct NCC_Variable labelNameVariable;
    NCC_popRuleVariable(ncc, &labelNameVariable);
    const char* labelNameString = NString.get(&labelNameVariable.value);

    // Code:
    //   // goto LABEL_NAME
    //   @LABEL_NAME
    //   0;JMP
    emitCode(ncc, "// goto %s\n@%s\n0;JMP\n\n", labelNameString, labelNameString);

    NCC_destroyVariable(&labelNameVariable);
}

void jumpNotZeroListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   if-goto LabelName
    //
    // Expecting 2 variables:
    //   LabelName.
    //   Whitespace.

    // Label name,
    struct NCC_Variable labelNameVariable;
    NCC_popRuleVariable(ncc, &labelNameVariable);
    const char* labelNameString = NString.get(&labelNameVariable.value);

    // Code:
    //   // if-goto LABEL_NAME
    //   @SP
    //   M=M-1
    //   A=M
    //   D=M
    //
    //   @LABEL_NAME
    //   D;JNE
    emitCode(ncc, "// if-goto %s\n@SP\nM=M-1\nA=M\nD=M\n\n@%s\nD;JNE\n\n", labelNameString, labelNameString);

    NCC_destroyVariable(&labelNameVariable);
}

void functionListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   function FunctionName 2
    //
    // Expecting 4 variables:
    //   Integer (local variables count).
    //   Whitespace.
    //   Function name.
    //   Whitespace.

    // Local variables count,
    struct NCC_Variable localVariablesCountVariable; NCC_popRuleVariable(ncc, &localVariablesCountVariable);
    const char* localVariablesCountString = NString.get(&localVariablesCountVariable.value);
    int32_t localVariablesCount = NCString.parseInteger(localVariablesCountString);

    // Whitespace,
    struct NCC_Variable whiteSpaceVariable; NCC_popRuleVariable(ncc, &whiteSpaceVariable); NCC_destroyVariable(&whiteSpaceVariable);

    // Function name,
    struct NCC_Variable functionNameVariable; NCC_popRuleVariable(ncc, &functionNameVariable);
    const char* functionNameString = NString.get(&functionNameVariable.value);

    // Start comment,
    emitCode(ncc, "// function %s %s (start)\n\n", functionNameString, localVariablesCountString);

    // Label,
    emitLabelCode(ncc, functionNameString);

    // Push zeroes for local variables,
    for (; localVariablesCount!=0; localVariablesCount--) emitPushCode(ncc, "constant", "0");

    // End comment,
    emitCode(ncc, "// function %s %s (end)\n\n", functionNameString, localVariablesCountString);

    // Clean up,
    NCC_destroyVariable(&functionNameVariable);
    NCC_destroyVariable(&localVariablesCountVariable);
}

void returnListener(struct NCC_MatchingData* matchingData) {

    // Example:
    //   return
    //
    // Expecting 0 variables.

    // Code:
    //   // return (start)
    //
    //   // [SP] = [LCL-5]  (copy the return address)
    //   @5
    //   D=A
    //   @LCL
    //   A=M-D
    //   D=M
    //   @SP
    //   A=M
    //   M=D
    //
    //   // [SP+1] = ARG+1  (copy ARG+1)
    //   @ARG
    //   D=M+1
    //   @SP
    //   A=M+1
    //   M=D
    //
    //   // [ARG] = [SP-1]  (set the return value)
    //   @SP
    //   A=M-1
    //   D=M
    //   @ARG
    //   A=M
    //   M=D
    //
    //   // THAT = [LCL-1]
    //   @LCL
    //   A=M-1
    //   D=M
    //   @THAT
    //   M=D
    //
    //   // THIS = [LCL-2]
    //   @2
    //   D=A
    //   @LCL
    //   A=M-D
    //   D=M
    //   @THIS
    //   M=D
    //
    //   // ARG = [LCL-3]
    //   @3
    //   D=A
    //   @LCL
    //   A=M-D
    //   D=M
    //   @ARG
    //   M=D
    //
    //   // LCL = [LCL-4]
    //   @4
    //   D=A
    //   @LCL
    //   A=M-D
    //   D=M
    //   @LCL
    //   M=D
    //
    //   // [[SP+1]] = [SP]  (copy return address into old LCL+1)
    //   @SP
    //   A=M
    //   D=M
    //   @SP
    //   A=M+1
    //   A=M
    //   M=D
    //
    //   // SP = [SP+1]  (set SP to old LCL+1)
    //   @SP
    //   A=M+1
    //   D=M
    //   @SP
    //   M=D
    //
    //   // Jmp to [SP]
    //   A=M
    //   A=M
    //   0;JMP
    //
    //   // return (end)

    emitCode(matchingData->ruleData->ncc, "// return (start)\n\n// [SP] = [LCL-5]  (copy the return address)\n@5\nD=A\n@LCL\nA=M-D\nD=M\n@SP\nA=M\nM=D\n\n// [SP+1] = ARG+1  (copy ARG+1)\n@ARG\nD=M+1\n@SP\nA=M+1\nM=D\n\n// [ARG] = [SP-1]  (set the return value)\n@SP\nA=M-1\nD=M\n@ARG\nA=M\nM=D\n\n// THAT = [LCL-1]\n@LCL\nA=M-1\nD=M\n@THAT\nM=D\n\n// THIS = [LCL-2]\n@2\nD=A\n@LCL\nA=M-D\nD=M\n@THIS\nM=D\n\n// ARG = [LCL-3]\n@3\nD=A\n@LCL\nA=M-D\nD=M\n@ARG\nM=D\n\n// LCL = [LCL-4]\n@4\nD=A\n@LCL\nA=M-D\nD=M\n@LCL\nM=D\n\n// [[SP+1]] = [SP]  (copy return address into old LCL+1)\n@SP\nA=M\nD=M\n@SP\nA=M+1\nA=M\nM=D\n\n// SP = [SP+1]  (set SP to old LCL+1)\n@SP\nA=M+1\nD=M\n@SP\nM=D\n\n// Jmp to [SP]\nA=M\nA=M\n0;JMP\n\n// return (end)\n\n");
}

static void emitCallCode(struct NCC* ncc, const char* functionName, int32_t argumentsCount) {

    // Get a new label index,
    struct OutputData* outputData = (struct OutputData*) ncc->extraData;
    int32_t returnLabelIndex = ++outputData->lastLabelIndex;

    // Code:
    //   // call functionName argumentsCount (begin)
    //
    //   // push returnAddress
    //   @ReturnLabel
    //   D=A
    //   @SP
    //   M=M+1
    //   A=M-1
    //   M=D
    //
    //   // push LCL
    //   @LCL
    //   D=M
    //   @SP
    //   M=M+1
    //   A=M-1
    //   M=D
    //
    //   // push ARG
    //   @ARG
    //   D=M
    //   @SP
    //   M=M+1
    //   A=M-1
    //   M=D
    //
    //   // push THIS
    //   @THIS
    //   D=M
    //   @SP
    //   M=M+1
    //   A=M-1
    //   M=D
    //
    //   // push THAT
    //   @THAT
    //   D=M
    //   @SP
    //   M=M+1
    //   A=M-1
    //   M=D
    //
    //   // ARG = SP - (5+argumentsCount)
    //   @backTrackOffset
    //   D=A
    //   @SP
    //   D=M-D
    //   @ARG
    //   M=D
    //
    //   // LCL = SP
    //   @SP
    //   D=M
    //   @LCL
    //   M=D
    //
    //   // goto functionName
    //   @functionName
    //   0;JMP
    //
    //   (returnAddress)
    //
    //   // call functionName argumentsCount (end)

    emitCode(ncc, "// call %s %d (begin)\n\n// push returnAddress\n@NLabel%d\nD=A\n@SP\nM=M+1\nA=M-1\nM=D\n\n// push LCL\n@LCL\nD=M\n@SP\nM=M+1\nA=M-1\nM=D\n\n// push ARG\n@ARG\nD=M\n@SP\nM=M+1\nA=M-1\nM=D\n\n// push THIS\n@THIS\nD=M\n@SP\nM=M+1\nA=M-1\nM=D\n\n// push THAT\n@THAT\nD=M\n@SP\nM=M+1\nA=M-1\nM=D\n\n// ARG = SP - (5+argumentsCount)\n@%d\nD=A\n@SP\nD=M-D\n@ARG\nM=D\n\n// LCL = SP\n@SP\nD=M\n@LCL\nM=D\n\n// goto %s\n@%s\n0;JMP\n\n(NLabel%d)\n\n// call %s %d (end)\n\n",
             functionName, argumentsCount, returnLabelIndex, 5+argumentsCount, functionName, functionName, returnLabelIndex, functionName, argumentsCount);
}

void callListener(struct NCC_MatchingData* matchingData) {
    struct NCC* ncc = matchingData->ruleData->ncc;

    // Example:
    //   call FunctionName 2
    //
    // Expecting 4 variables:
    //   Integer (local variables count).
    //   Whitespace.
    //   Function name.
    //   Whitespace.

    // Local variables count,
    struct NCC_Variable argumentsCountVariable; NCC_popRuleVariable(ncc, &argumentsCountVariable);
    int32_t argumentsCount = NCString.parseInteger(NString.get(&argumentsCountVariable.value));

    // Whitespace,
    struct NCC_Variable whiteSpaceVariable; NCC_popRuleVariable(ncc, &whiteSpaceVariable); NCC_destroyVariable(&whiteSpaceVariable);

    // Function name,
    struct NCC_Variable functionNameVariable; NCC_popRuleVariable(ncc, &functionNameVariable);
    const char* functionNameString = NString.get(&functionNameVariable.value);

    // Generate code,
    emitCallCode(ncc, functionNameString, argumentsCount);

    // Clean up,
    NCC_destroyVariable(&functionNameVariable);
    NCC_destroyVariable(&argumentsCountVariable);
}