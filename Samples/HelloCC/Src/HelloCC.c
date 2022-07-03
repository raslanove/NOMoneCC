#include <NSystemUtils.h>
#include <NError.h>
#include <NVector.h>
#include <NCString.h>

#include <NCC.h>

//////////////////////////////////////
// Testing helper functions
//////////////////////////////////////

struct NCC_ASTNode;
void NCC_ASTTreeToString(struct NCC_ASTNode* tree, struct NString* prefix, struct NString* outString);
void* NCC_createASTNode(struct NCC_RuleData* ruleData, struct NCC_ASTNode_Data* parentNode);
void  NCC_deleteASTNode(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* parentNode);

void assert(struct NCC* ncc, const char*ruleName, NCC_matchListener onMatchListener, const char* rule, const char* text, boolean shouldMatch, int32_t expectedMatchLength, boolean logTree) {

    boolean nccNeedsDeletion = False;
    if (!ncc) {
        ncc = NCC_createNCC();
        nccNeedsDeletion = True;
    }
    if (!ruleName) ruleName = "AssertTemp";

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, ncc, ruleName, rule, 0, 0, onMatchListener);
    boolean success = NCC_addRule(&ruleData);
    NCC_destroyRuleData(&ruleData);
    if (!success) {
        NERROR("HelloCC", "Couldn't add rule. Rule: %s%s%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT));
        if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
        return ;
    }

    NCC_setRootRule(ncc, ruleName);
    struct NCC_MatchingResult matchingResult;
    struct NCC_ASTNode_Data treeData;
    boolean matched = NCC_match(ncc, text, &matchingResult, logTree ? &treeData : 0);
    if (shouldMatch && !matched) {
        NERROR("HelloCC", "assert(): Match failed. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (!shouldMatch && matched) {
        NERROR("HelloCC", "assert(): Erroneously matched. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (expectedMatchLength != matchingResult.matchLength) {
        NERROR("HelloCC", "assert(): Wrong match length. Rule: %s%s%s, Text: %s%s%s, Match length: %s%d%s, Expected match length: %s%d%s", NTCOLOR(HIGHLIGHT), rule, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), text, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), matchingResult.matchLength, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), expectedMatchLength, NTCOLOR(STREAM_DEFAULT));
    } else if (logTree) {
        struct NString treeString;
        NString.initialize(&treeString, "");
        NCC_ASTTreeToString(treeData.node, 0, &treeString);
        NLOGI("", "%s", NString.get(&treeString));
        NString.destroy(&treeString);
        NCC_deleteASTNode(&treeData, 0);
    }

    if (nccNeedsDeletion) NCC_destroyAndFreeNCC(ncc);
    NLOGI("", "");
}

boolean printListener(struct NCC_MatchingData* matchingData) {
    NLOGI("HelloCC", "ruleName: %s", NString.get(&matchingData->node.rule->ruleName));
    NLOGI("HelloCC", "        Match length: %s%d%s", NTCOLOR(HIGHLIGHT), matchingData->matchLength, NTCOLOR(STREAM_DEFAULT));
    NLOGI("HelloCC", "        Matched text: %s%s%s", NTCOLOR(HIGHLIGHT), matchingData->matchedText, NTCOLOR(STREAM_DEFAULT));
    return True;
}

//////////////////////////////////////
// Conditional acceptance test
//////////////////////////////////////

struct NCC_ASTNode {
    struct NString name, value;
    struct NVector childNodes;
};

int32_t createCount=0, deleteCount=0;
void* NCC_createASTNode(struct NCC_RuleData* ruleData, struct NCC_ASTNode_Data* parentNode) {

    //NLOGI("sdf", "Create Node (%d): %s", ++createCount, NString.get(&ruleData->ruleName));

    struct NCC_ASTNode* astNode = NMALLOC(sizeof(struct NCC_ASTNode), "NCC.NCC_createASTNode() astNode");
    NString.initialize(&astNode->name, "%s", NString.get(&ruleData->ruleName));
    NString.initialize(&astNode->value, "not set yet");
    NVector.initialize(&astNode->childNodes, 0, sizeof(struct NCC_ASTNode*));

    if (parentNode) {
        struct NCC_ASTNode* parentASTNode = parentNode->node;
        NVector.pushBack(&parentASTNode->childNodes, &astNode);
    }
    return astNode;
}

void deleteASTNode(struct NCC_ASTNode* astNode, struct NCC_ASTNode_Data* parentNode) {

    //NLOGI("sdf", "Delete node (%d): %s", ++deleteCount, NString.get(&astNode->value));

    // Destroy members,
    NString.destroy(&astNode->name);
    NString.destroy(&astNode->value);

    // Delete children,
    struct NCC_ASTNode* currentChild;
    while (NVector.popBack(&astNode->childNodes, &currentChild)) deleteASTNode(currentChild, 0);
    NVector.destroy(&astNode->childNodes);

    // Delete node,
    NFREE(astNode, "NCC.NCC_deleteASTNode() astNode");

    // Remove from parent (if any),
    if (parentNode) {
        struct NCC_ASTNode* parentASTNode = parentNode->node;
        int32_t nodeIndex = NVector.getFirstInstanceIndex(&parentASTNode->childNodes, &astNode);
        if (nodeIndex!=-1) NVector.remove(&parentASTNode->childNodes, nodeIndex);
    }
}

void NCC_deleteASTNode(struct NCC_ASTNode_Data* node, struct NCC_ASTNode_Data* parentNode) {
    deleteASTNode((struct NCC_ASTNode*) node->node, parentNode);
}

boolean NCC_matchASTNode(struct NCC_MatchingData* matchingData) {
    struct NCC_ASTNode* astNode = matchingData->node.node;
    NString.set(&astNode->value, "%s", matchingData->matchedText);
    return True;
}

void NCC_ASTTreeToString(struct NCC_ASTNode* tree, struct NString* prefix, struct NString* outString) {

    // 179 = │, 192 = └ , 195 = ├. But somehow, this doesn't work. Had to use unicode...?

    // Prepare new prefix,
    struct NString* newPrefix;
    if (prefix) {
        struct NString* temp1 = NString.replace(NString.get(prefix), "─", " ");
        struct NString* temp2 = NString.replace(NString.get(temp1 ), "├", "│");
        NString.destroyAndFree(temp1);
        newPrefix = NString.replace(NString.get(temp2), "└", " ");
        NString.destroyAndFree(temp2);
    } else {
        newPrefix = NString.create("");
    }

    // Print this node,
    if (prefix) NString.append(outString, "%s", NString.get(prefix));

    // Tree value could span multiple lines, remove line-breaks,
    int32_t childrenCount = NVector.size(&tree->childNodes);
    if (NCString.lastIndexOf(NString.get(&tree->value), "\n")!=-1) {
        struct NString  temp1;
        struct NString *temp2;
        NString.initialize(&temp1, "\n%s%s", NString.get(newPrefix), childrenCount ? "│" : " ");
        temp2 = NString.replace(NString.get(&tree->value), "\n", NString.get(&temp1));
        NString.append(outString, "%s: %s%s\n", NString.get(&tree->name), NString.get(temp2), NString.get(&temp1));
        NString.destroy(&temp1);
        NString.destroyAndFree(temp2);
    } else {
        NString.append(outString, "%s: %s\n", NString.get(&tree->name), NString.get(&tree->value));
    }

    // Print children,
    struct NString childPrefix;
    NString.initialize(&childPrefix, "");
    for (int32_t i=0; i<childrenCount; i++) {
        boolean lastChild = (i==(childrenCount-1));
        NString.set(&childPrefix, "%s%s", NString.get(newPrefix), lastChild ? "└─" : "├─");
        struct NCC_ASTNode* currentChild = *((struct NCC_ASTNode**) NVector.get(&tree->childNodes, i));
        NCC_ASTTreeToString(currentChild, &childPrefix, outString);
        if (lastChild) NString.append(outString, "%s\n", NString.get(newPrefix));
    }
    NString.destroyAndFree(newPrefix);
    NString.destroy(&childPrefix);
}

// TODO: print tree ....
//    tree node
//    ├─── tree node
//    │     ├─── tree node
//    │     └─── tree node
//    └─── tree node



struct NVector declaredVariables;

void addDeclaredVariable(const char* variableName) {
    struct NString* declaredVariable = NVector.emplaceBack(&declaredVariables);
    NString.initialize(declaredVariable, "%s", variableName);
}

boolean isVariableDeclared(const char* variableName) {
    for (int32_t i=NVector.size(&declaredVariables)-1; i>-1; i--) {
        const char* currentVariableName = NString.get(NVector.get(&declaredVariables, i));
        if (NCString.equals(variableName, currentVariableName)) return True;
    }
    return False;
}

void destroyDeclaredVariables() {
    for (int32_t i=NVector.size(&declaredVariables)-1; i>-1; i--) NString.destroy(NVector.get(&declaredVariables, i));
    NVector.clear(&declaredVariables);
}

boolean declarationListener(struct NCC_MatchingData* matchingData) {
    // TODO: ....
    //addDeclaredVariable(NString.get(&variable.value));
    return True;
}

boolean validateAssignmentListener(struct NCC_MatchingData* matchingData) {

    // TODO: ....

    /*
    // Accept rule only if the two variable were previously declared,
    boolean declared = isVariableDeclared(NString.get(&variable.value));
    if (!declared) return False;
    declared = isVariableDeclared(NString.get(&variable.value));
    return declared;
    */
    return True;
}

//////////////////////////////////////
// Tests
//////////////////////////////////////

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)\n");

    // A number of test-cases that make sure that rules and matching behave as expected.
    struct NCC ncc;

//    // Literals,
//    assert(0, 0, 0, "besm\\ Allah", "besm Allah", True, 10);
//
//    // x-y
//    assert(0, 0, 0, "besm\\ Allah\\ a-z", "besm Allah x", True, 12);
//    assert(0, 0, 0, "besm\\ Allah\\ a-z", "besm Allah 2", False, 11);
//    assert(0, 0, 0, "besm\\ Allah\\ \\a-\\z", "besm Allah x", True, 12);
//
//    // |
//    assert(0, 0, 0, "a|b", "a", True, 1);
//    assert(0, 0, 0, "abc|def", "abcef", True, 5);
//    assert(0, 0, 0, "abc|def", "abdef", True, 5);
//    assert(0, 0, 0, "abc|def", "abef", False, 2);
//    assert(0, 0, 0, "a|b|c|d|ef", "cf", True, 2);
//
//    // {}
//    assert(0, 0, 0, "ab{cd{ef}gh}ij", "abcdefghij", True, 10);
//    assert(0, 0, 0, "ab{cd}|{ef}gh", "abcdgh", True, 6);
//    assert(0, 0, 0, "ab{cd}|{ef}gh", "abefgh", True, 6);
//    assert(0, 0, 0, "ab{cd}|{ef}gh", "abgh", False, 2);
//    assert(0, 0, 0, "a{a|b}", "ab", True, 2);
//    assert(0, 0, 0, "a{b|c}d", "abf", False, 2);
//
//    // ^*
//    assert(0, 0, 0, "a^*bc", "abc", True, 3);
//    assert(0, 0, 0, "a^*bc", "bc", True, 2);
//    assert(0, 0, 0, "a^*bc", "aaaaabc", True, 7);
//    assert(0, 0, 0, "a^*", "aaaaa", True, 5);
//    assert(0, 0, 0, "123a^*", "123aaaaa", True, 8);
//    assert(0, 0, 0, "123a^*456", "123a456", True, 7);
//    assert(0, 0, 0, "123a^*456", "123456", True, 6);
//    assert(0, 0, 0, "123{ab}^*456", "123ababab456", True, 12);
//    assert(0, 0, 0, "{ab}^*{cd}^*", "x", True, 0);
//    assert(0, 0, 0, "x{ab}^*{cd}^*", "x", True, 1);
//    assert(0, 0, 0, "x{ab}^*{cd}^*", "xab", True, 3);
//    assert(0, 0, 0, "x{ab}^*{cd}^*", "xcd", True, 3);
//    assert(0, 0, 0, "{xyz}^*xyz", "xyzxyzxyz", True, 3);
//    assert(0, 0, 0, "{{xyz}^*}xyz", "xyzxyzxyz", False, 9);
//
//    // *
//    assert(0, 0, 0, "*", "xyz", True, 3);
//    assert(0, 0, 0, "**", "xyz", True, 3);
//    assert(0, 0, 0, "********", "xyz", True, 3);
//    assert(0, 0, 0, "********abc", "xyzabc", True, 6);
//    assert(0, 0, 0, "*a*b*c*", "__a__c__", False, 8);
//    assert(0, 0, 0, "*XYZ", "abcdefgXYZ", True, 10);
//    assert(0, 0, 0, "{*}XYZ", "abcdefgXYZ", False, 10);
//
//    // General test-cases,
//    assert(0, 0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "myVariable3", True, 11);
//    assert(0, 0, 0, "{a-z|A-Z}{a-z|A-Z|0-9}^*", "3myVariable3", False, 0);
//    assert(0, 0, 0, "/\\**\\*/", "/*بسم الله. This is a beautiful comment.\n The is the second line in the beautiful comment.*/", True, 99);
//
//    // Substitute,
//    NCC_initializeNCC(&ncc);
//    assert(&ncc, "Comment", printListener, "/\\**\\*/", "/*besm Allah*/", True, 14);
//    assert(&ncc, "TwoComments", printListener, "${Comment},${Comment}",
//           "/*first comment*/,/*second comment*/", True, 36);
//    assert(&ncc, "ThreeComments", printListener, "${TwoComments},${Comment}",
//           "/*first comment*/,/*second comment*/,/*thirrrrrd comment*/", True, 58);
//    NCC_destroyNCC(&ncc);
//
//    NCC_initializeNCC(&ncc);
//    assert(&ncc, "Optional", printListener, "{ab}^*{cd}^*", "", True, 0);
//    assert(&ncc, "Mandatory", printListener, "xyz", "xyz", True, 3);
//    assert(&ncc, "ContainingOptional", printListener, "${Optional}${Mandatory}", "xyz", True, 3);
//    NCC_destroyNCC(&ncc);
//
//    NCC_initializeNCC(&ncc);
//    assert(&ncc, "Milestone", printListener, "", "", True, 0);
//    assert(&ncc, "ActualRule1", printListener, "${Milestone}abc", "abc", True, 3);
//    assert(&ncc, "ActualRule2", printListener, "xyz", "xyz", True, 3);
//    NCC_destroyNCC(&ncc);
//
//    NCC_initializeNCC(&ncc);
//    assert(&ncc, "Literal"        ,             0, "\x01-\xff", "", False, 0);
//    assert(&ncc, "String"         ,             0, "\" { ${Literal}|{\\\\${Literal}} }^* \"", "", False, 0);
//    assert(&ncc, "StringContainer", printListener, "${String}", "\"besm Allah \\\" :)\"", True, 18);
//    NCC_destroyNCC(&ncc);

    // Stateful parsing,
    NLOGI("", "%s================%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%sStateful Parsing%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));
    NLOGI("", "%s================%s", NTCOLOR(GREEN_BOLD_BRIGHT), NTCOLOR(STREAM_DEFAULT));

    struct NCC_RuleData ruleData;
    NCC_initializeRuleData(&ruleData, &ncc, "", "", 0, 0, 0);
    NVector.initialize(&declaredVariables, 0, sizeof(NString));

    NCC_initializeNCC(&ncc);
    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*"));
    NCC_addRule(ruleData.set(&ruleData, "identifier" , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*")         ->setListeners(&ruleData, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode));
    NCC_addRule(ruleData.set(&ruleData, "declaration", "${identifier};")                      ->setListeners(&ruleData, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode));
    NCC_addRule(ruleData.set(&ruleData, "assignment" , "${identifier}=${identifier};")        ->setListeners(&ruleData, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode)); //validateAssignmentListener));
    NCC_addRule(ruleData.set(&ruleData, "document"   , "{${declaration}|${assignment}|${}}^*")->setListeners(&ruleData, NCC_createASTNode, NCC_deleteASTNode, NCC_matchASTNode));

    assert(&ncc, "DocumentTest1", 0, "${} Test1: ${document}", "\n"
            "Test1:\n"
            "var1;\n"
            "var2;\n"
            "var1=var2;", True, 30, True);
    destroyDeclaredVariables();

//
//    assert(&ncc, "DocumentTest2", printListener, True, "${} Test2: ${document}", "\n"
//            "Test2:\n"
//            "var1;\n"
//            "var2;\n"
//            "var1=var3;", True, 20);
    destroyDeclaredVariables();
    NCC_destroyNCC(&ncc);
//
//    // Delete test,
//    NCC_initializeNCC(&ncc);
//    NCC_addRule(ruleData.set(&ruleData, "", "{\\ |\t|\r|\n}^*"));
//    NCC_addRule(ruleData.set(&ruleData, "identifier"    , "a-z|A-Z|_ {a-z|A-Z|_|0-9}^*"));
//    NCC_addRule(ruleData.set(&ruleData, "declaration"   , "${identifier} ${} ${identifier};")->setListeners(&ruleData, 0, rollBackListener, 0));
//    assert(&ncc, "RollBackTest", printListener, True, "${declaration}|${declaration}", "int a;", True, 6);
//    NCC_destroyNCC(&ncc);

    NVector.destroy(&declaredVariables);
    NCC_destroyRuleData(&ruleData);

    NError.logAndTerminate();
}
