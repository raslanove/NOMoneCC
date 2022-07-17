#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>
#include <NCC.h>

#include "LanguageDefinition.h"

#define TEST_EXPRESSIONS  0
#define TEST_DECLARATIONS 0
#define TEST_STATEMENTS   1

struct PrettifierData {

};

static void printLeavesImplementation(struct NCC_ASTNode* tree, struct NString* outString, struct NString* extraString) {
    // This way the extra string needn't be re-allocated and initialized with every invocation.

#define PRINT_CHILDREN(separator) \
    int32_t childrenCount = NVector.size(&tree->childNodes); \
    if (separator) { \
        printLeavesImplementation(*((struct NCC_ASTNode**) NVector.get(&tree->childNodes, 0)), outString, extraString); \
        for (int32_t i=1; i<childrenCount; i++) { \
            NString.append(outString, "%s", separator); \
            printLeavesImplementation(*((struct NCC_ASTNode**) NVector.get(&tree->childNodes, i)), outString, extraString); \
        } \
    } else { \
        for (int32_t i=0; i<childrenCount; i++) printLeavesImplementation(*((struct NCC_ASTNode**) NVector.get(&tree->childNodes, i)), outString, extraString); \
    }

    const char* ruleNameCString = NString.get(&tree->name );
    const char*    valueCString = NString.get(&tree->value);

    if (NCString.equals(ruleNameCString, "mandatory-white-space")) {
        // Reduce this to a single white-space,
        NString.append(outString, " ");
    } else if (NCString.equals(ruleNameCString, "OB")) {
        NString.append(outString, "{\n");
    } else if (
            NCString.equals(ruleNameCString, "function-definition") ||
            NCString.equals(ruleNameCString, "init-declarator")) {
        PRINT_CHILDREN(" ")
    } else if (
            NCString.equals(ruleNameCString,        "declaration") ||
            NCString.equals(ruleNameCString,          "statement") ||
            NCString.equals(ruleNameCString, "compound-statement")) {
        PRINT_CHILDREN("")
        if (!NCString.endsWith(NString.get(outString), "\n")) NString.append(outString, "\n");
    } else {
        int32_t childrenCount = NVector.size(&tree->childNodes);
        if (childrenCount) {
            // Not a leaf, print children,
            PRINT_CHILDREN(0)
        } else {
            // Leaf node,
            NString.append(outString, "%s", valueCString);
        }
    }
}

static void printLeaves(struct NCC_ASTNode* tree, struct NString* outString) {

    struct NString extraString;
    NString.initialize(&extraString, "");
    printLeavesImplementation(tree, outString, &extraString);
    NString.destroy(&extraString);
}

static void test(struct NCC* ncc, const char* code) {

    NLOGI("", "%sTesting: %s%s", NTCOLOR(GREEN_BRIGHT), NTCOLOR(HIGHLIGHT), code);
    struct NCC_MatchingResult matchingResult;
    struct NCC_ASTNode_Data tree;
    boolean matched = NCC_match(ncc, code, &matchingResult, &tree);
    if (matched && tree.node) {
        struct NString treeString;

        // Print tree,
        NString.initialize(&treeString, "");
        NCC_ASTTreeToString(tree.node, 0, &treeString, True /* should check isatty() */);
        NLOGI(0, "%s", NString.get(&treeString));

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
               "   void* extraData;\n"
               "   struct NVector rules; // Pointers to rules, not rules, so that they don't get relocated when more rules are added.\n"
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
               "    {int a = 3 + 5;}\n"
               "}");

    /*
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
    */
    #endif

    // Clean up,
    NCC_destroyNCC(&ncc);
    NError.logAndTerminate();
}
