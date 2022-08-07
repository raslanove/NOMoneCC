#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>
#include <NCC.h>

#include "LanguageDefinition.h"

#define TEST_EXPRESSIONS  1
#define TEST_DECLARATIONS 1
#define TEST_STATEMENTS   1
#define TEST_TOKENS       1
#define TEST_PRETTIFIER   1

#define PRINT_TREES 0

struct PrettifierData {
    struct NString outString;
    struct NVector colorStack; // const char*
    int32_t indentationCount;
};

static void initializePrettifierData(struct PrettifierData* prettifierData) {
    NString.initialize(&prettifierData->outString, "");
    NVector.initialize(&prettifierData->colorStack, 0, sizeof(const char*));
    prettifierData->indentationCount = 0;
}

static void destroyPrettifierData(struct PrettifierData* prettifierData) {
    NString.destroy(&prettifierData->outString);
    NVector.destroy(&prettifierData->colorStack);
}

static void prettifierAppend(struct PrettifierData* prettifierData, const char* text) {

    // Append indentation,
    if (NCString.endsWith(NString.get(&prettifierData->outString), "\n")) {
        for (int32_t i=0; i<prettifierData->indentationCount; i++) {
            NString.append(&prettifierData->outString, "   ");
        }
    }

    // Add color,
    if (!(NCString.equals(text, " ") || NCString.equals(text, "\n"))) {
        if (NVector.size(&prettifierData->colorStack)) {
            const char* color = *(const char**) NVector.getLast(&prettifierData->colorStack);
            NString.append(&prettifierData->outString, "%s", color);
        } else {
            NString.append(&prettifierData->outString, "%s", NTCOLOR(STREAM_DEFAULT));
        }
    }

    // Append text,
    NString.append(&prettifierData->outString, "%s", text);
}

static void printLeavesImplementation(struct NCC_ASTNode* tree, struct PrettifierData* prettifierData) {
    // Moved the implementation to a separate function to remove the prettifierData from the interface.

    const char* ruleNameCString = NString.get(&tree->name);

    if (NCString.equals(ruleNameCString, "insert space")) {
        prettifierAppend(prettifierData, " ");
    } else if (NCString.equals(ruleNameCString, "insert \n")) {
        if (!NCString.endsWith(NString.get(&prettifierData->outString), "\n")) prettifierAppend(prettifierData, "\n");
    } else if (NCString.equals(ruleNameCString, "insert \ns")) {
        prettifierAppend(prettifierData, "\n");
    } else if (NCString.equals(ruleNameCString, "OB")) {
        prettifierAppend(prettifierData, "{");
        prettifierData->indentationCount++;
    } else if (NCString.equals(ruleNameCString, "CB")) {
        prettifierData->indentationCount--;
        prettifierAppend(prettifierData, "}");
    } else if (NCString.equals(ruleNameCString, "line-cont")) {
        prettifierAppend(prettifierData, " \\\n");
    } else if (NCString.equals(ruleNameCString, "line-comment") || NCString.equals(ruleNameCString, "block-comment")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(BLACK_BRIGHT));
        prettifierAppend(prettifierData, NString.get(&tree->value));
        const char *color; NVector.popBack(&prettifierData->colorStack, &color);
    } else if (NCString.equals(ruleNameCString, "POP C" )) {
        const char *color; NVector.popBack(&prettifierData->colorStack, &color);
    } else if (NCString.equals(ruleNameCString, "PSH C0")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(STREAM_DEFAULT));
    } else if (NCString.equals(ruleNameCString, "PSH C1")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(YELLOW_BOLD_BRIGHT));
    } else if (NCString.equals(ruleNameCString, "PSH C2")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(YELLOW_BRIGHT));
    } else if (NCString.equals(ruleNameCString, "PSH C3")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(MAGENTA_BOLD_BRIGHT));
    } else if (NCString.equals(ruleNameCString, "PSH C4")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(GREEN_BRIGHT));
    } else if (NCString.equals(ruleNameCString, "PSH C5")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(RED_BRIGHT));
    } else if (NCString.equals(ruleNameCString, "PSH C6")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(GREEN_BRIGHT));
    } else if (NCString.equals(ruleNameCString, "PSH C7")) {
        NVector.pushBack(&prettifierData->colorStack, &NTCOLOR(BLACK_BRIGHT));
    } else {
        int32_t childrenCount = NVector.size(&tree->childNodes);
        if (childrenCount) {
            // Not a leaf, print children,
            int32_t childrenCount = NVector.size(&tree->childNodes);
            for (int32_t i=0; i<childrenCount; i++) printLeavesImplementation(*((struct NCC_ASTNode**) NVector.get(&tree->childNodes, i)), prettifierData);
        } else {
            // Leaf node,
            prettifierAppend(prettifierData, NString.get(&tree->value));
        }
    }
}

static void printLeaves(struct NCC_ASTNode* tree, struct NString* outString) {

    struct PrettifierData prettifierData;
    initializePrettifierData(&prettifierData);
    printLeavesImplementation(tree, &prettifierData);
    NString.set(outString, "%s", NString.get(&prettifierData.outString));
    destroyPrettifierData(&prettifierData);
}

static void test(struct NCC* ncc, const char* code) {

    NLOGI("", "%sTesting: %s%s", NTCOLOR(GREEN_BRIGHT), NTCOLOR(BLUE_BRIGHT), code);
    struct NCC_MatchingResult matchingResult;
    struct NCC_ASTNode_Data tree;
    boolean matched = NCC_match(ncc, code, &matchingResult, &tree);
    if (matched && tree.node) {
        struct NString treeString;

        // Print tree,
        NString.initialize(&treeString, "");
        #if PRINT_TREES
        NCC_ASTTreeToString(tree.node, 0, &treeString, True /* should check isatty() */);
        NLOGI(0, "%s", NString.get(&treeString));
        #endif

        // Print leaves,
        NString.set(&treeString, "");
        printLeaves(tree.node, &treeString);
        NLOGI(0, "%s", NString.get(&treeString));

        // Cleanup,
        NString.destroy(&treeString);
        NCC_deleteASTNode(&tree, 0);
    }
    int32_t codeLength = NCString.length(code);
    if (matched && matchingResult.matchLength == codeLength) {
        NLOGI("test()", "Success!");
    } else {
        NERROR("test()", "Failed! Match: %s, length: %d", matched ? "True" : "False", matchingResult.matchLength);
    }
    NLOGI("", "");
}

void NMain() {

    NSystemUtils.logI("", "besm Allah :)\n\n");

    // Language definition,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);
    defineLanguage(&ncc);

    // Test,
    #if TEST_EXPRESSIONS
    test(&ncc, "\"besm Allah\" //asdasdasdas\n  \"AlRa7maan AlRa7eem\"");
    test(&ncc, "a++");
    test(&ncc, "a++++"); // Parses, but should fail because a++ is not assignable.
    test(&ncc, "a * b");
    test(&ncc, "a * b / c % d");
    test(&ncc, "a + b");
    test(&ncc, "a * b + c / d");
    test(&ncc, "a << 2 >> 3");
    test(&ncc, "a < 2 > 3 >= 4");
    test(&ncc, "a < 2 + 3 >= 4");
    test(&ncc, "a == b");
    test(&ncc, "a & b");
    test(&ncc, "a ^ b");
    test(&ncc, "a | b");
    test(&ncc, "a && b");
    test(&ncc, "a || b");
    test(&ncc, "a ? b : c");
    test(&ncc, "a = b");
    test(&ncc, "a = a * b / c % ++d + 5");
    test(&ncc, "(a * b) + (c / d)");
    #endif

    #if TEST_DECLARATIONS
    test(&ncc, "int a;");
    test(&ncc, "int a, b;");        // Fails when typedef is enabled because declaration starts with declaration-specifiers, which
                                    // includes an identifier-based element (typedef-name), so the first identifier is grouped
                                    // together with the specifiers, thus, init-declarator-list is missing its first identifier
                                    // before the comma, so it doesn't match.
    test(&ncc, "int a = 5;");
    test(&ncc, "int a = 5, b;");
    test(&ncc, "struct NCC ncc;");
    test(&ncc, "struct MyStruct { int a, b; float c; } myStructInstance;");
    test(&ncc, "struct NCC {\n"
               "   /* Testing block comments. */\n"
               "   void* extraData; \\\n"
               "   struct NVector rules; // Pointers to rules, not rules, so that they \\\n"
               "                            don't get relocated when more rules are added.\n"
               "   int noha:12; // Testing static initialization.\n"
               "   struct NVector variables;\n"
               "   struct NByteVector *matchRoute, *tempRoute1, *tempRoute2, *tempRoute3, *tempRoute4; // Pointers to nodes. TODO: maybe turn them into an array?\n"
               "};");
    // TODO: enable when typedef is implemented...
    //test(&ncc, "uint32_t a;");      // Fails because it requires a typedef-ed type uint32_t.
    test(&ncc, "int NCC_getRuleVariable(struct NCC* ncc, int index, struct NCC_Variable* outVariable);");

    // TODO: Use the complex statements from your own project for testing...
    #endif

    #if TEST_STATEMENTS
    test(&ncc, "\n"
               "void main(void) {\n"
               "    int a = 3 + 5;\n"
               "}");

    // A fake example that avoids the type-def issues,
    test(&ncc, "\n"
               "void variadicFunction(int firstArgument, ...) {\n"
               "    struct va_list vaList;\n"
               "    va_start(vaList, firstArgument);\n"
               "    int* argument = va_arg(vaList, sizeof(int*));\n"
               "    *argument = 123;\n"
               "    va_end(vaList);\n"
               "}\n"
               "\n"
               "void main(void) {\n"
               "    int a;\n"
               "    variadicFunction(567, &a);\n"
               "}\n");

    test(&ncc, "void main() {\n"
               "   int a ,b, c;\n"
               "   c = a ++ + ++ b;\n"
               "}");
    #endif

    #if TEST_TOKENS
    test(&ncc, "int a, b;");
    test(&ncc, "int integer;");
    test(&ncc, "struct structure;");
    test(&ncc, "void externalFunction1();");
    #endif

    #if TEST_PRETTIFIER
    test(&ncc, "void main(void){{int a=3+5;}}");
    test(&ncc, "void variadicFunction(int firstArgument,...){struct va_list vaList;va_start(vaList,firstArgument);int*argument=va_arg(vaList,sizeof(int*));*argument=123;va_end(vaList);}void main(void){int a;variadicFunction(567,&a);}");
    test(&ncc, "void main(){int a,b,c;c=a++ + ++b;}");
    test(&ncc, "void extern alFunction1();void extern alFunction2();");
    test(&ncc, "void main(){for (int i=0; i<100; i++);}");
    test(&ncc, "void main(void) {"
               "   for (int i=0; i<100; i++) {"
               "      printf(\"besm Allah\");"
               "   }"
               "   NError.logAndTerminate();"
               "}");
    #endif

    // Clean up,
    NCC_destroyNCC(&ncc);
    NError.logAndTerminate();
}
