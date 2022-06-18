#include <NCC.h>
#include "CodeGeneration.h"

#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>

static void printMatch(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount);

static void addRule(struct NCC* ncc, const char* ruleName, const char* ruleText, NCC_onConfirmedMatchListener matchListener, boolean rootRule, boolean pushVariable) {
    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, ncc, ruleName, ruleText, 0, 0, matchListener, rootRule, pushVariable, True);
    NCC_addRule(&ruleData);
    NCC_destroyRuleData(&ruleData);
}

static void specifyLanguage(struct NCC* ncc) {

    // Elements,
    addRule(ncc, "Empty", "", 0, False, True);
    addRule(ncc, "WhiteSpace", "{\\ |\t|\r|\n}^*", 0, False, True);
    addRule(ncc, "NotWhiteSpaceLiteral", "\x01-\x08 | \x0b-\x0c | \x0e-\x1f | \x21-\xff", 0, False, True);
    addRule(ncc, "LineEnd", "\n|${Empty}", 0, False, True);
    addRule(ncc, "LineComment", "//*${LineEnd}", 0, False, True);
    addRule(ncc, "Integer", "0-9 | 1-9 0-9^*", 0, False, True);
    addRule(ncc, "Identifier", "${NotWhiteSpaceLiteral}^*", 0, False, True);
    addRule(ncc, "Label", "label ${WhiteSpace} ${Identifier}", labelListener, False, True);
    addRule(ncc, "StackModifier", "${NotWhiteSpaceLiteral}^*", 0, False, True);

    // Instructions,
    addRule(ncc, "Push", "push ${WhiteSpace} ${StackModifier} ${WhiteSpace} ${Integer}", pushListener, False, True);
    addRule(ncc, "Pop" , "pop  ${WhiteSpace} ${StackModifier} ${WhiteSpace} ${Integer}",  popListener, False, True);
    addRule(ncc, "Add", "add", addListener, False, True);
    addRule(ncc, "Sub", "sub", subListener, False, True);
    addRule(ncc, "And", "and", andListener, False, True);
    addRule(ncc, "Or" , "or" ,  orListener, False, True);
    addRule(ncc, "Eq" , "eq" ,  eqListener, False, True);
    addRule(ncc, "LT" , "lt" ,  ltListener, False, True);
    addRule(ncc, "GT" , "gt" ,  gtListener, False, True);
    addRule(ncc, "Neg", "neg", negListener, False, True);
    addRule(ncc, "Not", "not", notListener, False, True);
    addRule(ncc, "Jmp", "goto ${WhiteSpace} ${Identifier}", jumpListener, False, True);
    addRule(ncc, "JNZ", "if\\-goto ${WhiteSpace} ${Identifier}", jumpNotZeroListener, False, True);
    addRule(ncc, "Function", "function ${WhiteSpace} ${Identifier} ${WhiteSpace} ${Integer}", functionListener, False, True);
    addRule(ncc, "Return", "return", returnListener, False, True);
    addRule(ncc, "Call", "call ${WhiteSpace} ${Identifier} ${WhiteSpace} ${Integer}", callListener, False, True);

    addRule(ncc, "Instruction", "${Push} | ${Pop} | ${Add} | ${Sub} | ${And} | ${Or} | ${Eq} | ${LT} | ${GT} | ${Neg} | ${Not} | ${Jmp} | ${JNZ} | ${Function} | ${Return} | ${Call}", 0, False, True);

    // Document,
    addRule(ncc, "Document", "{${WhiteSpace} | ${LineComment} | ${Label} | ${Instruction}}^*", 0, True, True);
}

static void printMatch(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("VMTranslate", "ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NCC_Variable variable;
    while (NCC_popRuleVariable(ncc, &variable)) {
        NLOGI("VMTranslate", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), variable.name, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
    NLOGI("", "");
}

static boolean checkArguments(int argc, char *argv[]) {

    // Check arguments count,
    if (argc != 2) {
        NLOGI("", "%sUsage%s: %s <file or folder>", NTCOLOR(HIGHLIGHT), NTCOLOR(STREAM_DEFAULT), argv[0]);
        return False;
    }
    char* path = argv[1];

    // Check valid arguments,
    int32_t pathType = NSystemUtils.getDirectoryEntryType(path, False);
    if (!(pathType==NDirectoryEntryType.REGULAR_FILE || pathType==NDirectoryEntryType.DIRECTORY)) {
        NLOGI("", "Expected file or folder. Found: %s%s%s", NTCOLOR(HIGHLIGHT), path, NTCOLOR(STREAM_DEFAULT));
        return False;
    }

    // Check valid extension,
    if (pathType==NDirectoryEntryType.REGULAR_FILE) {
        int32_t fileNameLength = NCString.length(path);
        const char* extension = fileNameLength > 3 ? &path[fileNameLength-3] : "";
        if (!NCString.equals(extension, ".vm")) {
            NLOGI("", "Expected a .vm file. Found: %s%s%s", NTCOLOR(HIGHLIGHT), path, NTCOLOR(STREAM_DEFAULT));
            return False;
        }
    }

    return True;
}

static struct NString* translateSingleFile(struct NCC* ncc, char* filePath, struct OutputData* outputData) {

    // Read input file,
    uint32_t fileSize = NSystemUtils.getFileSize(filePath, False);
    char* code = NMALLOC(fileSize+1, "VMTranslate.NMain() code 1");
    NSystemUtils.readFromFile(filePath, False, 0, 0, code);
    code[fileSize] = 0;

    // Generate output file name,
    filePath[NCString.length(filePath)-3] = 0;
    struct NString* outputFileName = NString.create("%s.asm", filePath);

    // Set file name in output data,
    NString.set(&outputData->fileName, filePath);

    // Generate code,
    emitInitializationCode(ncc, VARIABLES);
    struct NCC_MatchingResult matchingResult;
    boolean matched = NCC_match(ncc, code, &matchingResult);
    emitTerminationCode(ncc);

    // Log and cleanup,
    NLOGI("VMTranslate", "Matched: %s, length: %d\n", matched ? "True" : "False", matchingResult.matchLength);
    NFREE(code, "VMTranslate.NMain() code 1");

    return outputFileName;
}

static struct NString* translateDirectory(struct NCC* ncc, char* directoryPath, struct OutputData* outputData) {

    // Emit initialization code for folder samples,
    //emitInitializationCode(ncc, VARIABLES | STACK_POINTER | SEGMENTS | SYS_INIT);
    emitInitializationCode(ncc, VARIABLES | STACK_POINTER | SYS_INIT);

    // Translate .vm files,
    boolean matched = True;
    int32_t matchLength = 0;
    struct NVector* directoryContents = NSystemUtils.listDirectoryEntries(directoryPath, False);
    for (int32_t i=NVector.size(directoryContents)-1; i>=0; i--) {

        // Look for files only,
        struct NDirectoryEntry* directoryEntry = NVector.get(directoryContents, i);
        if (directoryEntry->type != NDirectoryEntryType.REGULAR_FILE) continue;

        // Look for .vm files only,
        int32_t fileNameLength = NCString.length(directoryEntry->name);
        const char* extension = fileNameLength > 3 ? &directoryEntry->name[fileNameLength-3] : "";
        if (!NCString.equals(extension, ".vm")) continue;

        // Begin file translation,
        NLOGI("", "Translating %s", directoryEntry->name);
        emitCode(ncc, "// Beginning of %s\n\n", directoryEntry->name);

        // Read input file,
        struct NString* fileFullPath = NString.create("%s/%s", directoryPath, directoryEntry->name);
        uint32_t fileSize = NSystemUtils.getFileSize(NString.get(fileFullPath), False);
        char* code = NMALLOC(fileSize + 1, "VMTranslate.NMain() code 2");
        NSystemUtils.readFromFile(NString.get(fileFullPath), False, 0, 0, code);
        code[fileSize] = 0;

        // Set file name in output data,
        directoryEntry->name[NCString.length(directoryEntry->name)-3] = 0;
        NString.set(&outputData->fileName, directoryEntry->name);

        // Generate code,
        struct NCC_MatchingResult matchingResult;
        matched &= NCC_match(ncc, code, &matchingResult);
        matchLength += matchingResult.matchLength;
        emitCode(ncc, "// End of %s\n\n", directoryEntry->name);

        // Cleanup,
        NString.destroyAndFree(fileFullPath);
        NFREE(code, "VMTranslate.NMain() code 2");
    }

    // Log and cleanup,
    NLOGI("VMTranslate", "Match: %s, length: %d\n", matched ? "True" : "False", matchLength);
    NSystemUtils.destroyAndFreeDirectoryEntryVector(directoryContents);

    // Generate output file name,
    char* fullPath = NSystemUtils.getFullPath(directoryPath);
    char* directoryName = &fullPath[NCString.lastIndexOf(fullPath, "/") + 1];
    struct NString* outputFileName = NString.create("%s/%s.asm", fullPath, directoryName);
    NFREE(fullPath, "VMTranslate.translateDirectory() fullPath");
    return outputFileName;
}

void NMain(int argc, char *argv[]) {

    NSystemUtils.logI("", "%sbesm Allah%s :)", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));

    // Extract path argument,
    if (!checkArguments(argc, argv)) return;
    char* path = argv[1];
    int32_t pathType = NSystemUtils.getDirectoryEntryType(path, False);

    // Create ncc,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // Add data to accumulate output,
    struct OutputData outputData;
    NString.initialize(&outputData.fileName, "");
    NString.initialize(&outputData.code, "");
    outputData.lastLabelIndex = 0;
    ncc.extraData = &outputData;

    // Set language specification,
    specifyLanguage(&ncc);

    // Translate code file(s),
    struct NString* outputFileName = (pathType==NDirectoryEntryType.REGULAR_FILE) ? translateSingleFile(&ncc, path, &outputData) : translateDirectory(&ncc, path, &outputData);

    // Write generated code to file,
    NSystemUtils.writeToFile(NString.get(outputFileName), NString.get(&outputData.code), NString.length(&outputData.code), False);
    NString.destroyAndFree(outputFileName);

    // Clean up,
    NCC_destroyNCC(&ncc);
    NString.destroy(&outputData.fileName);
    NString.destroy(&outputData.code);
    NError.logAndTerminate();
}
