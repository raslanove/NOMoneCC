
#include "LanguageDefinition.h"

#include <NCC.h>
#include <NSystemUtils.h>

static boolean printListener(struct NCC_MatchingData* matchingData) {
    NLOGI("HelloCC", "ruleName: %s", NString.get(&matchingData->node.rule->ruleName));
    NLOGI("HelloCC", "        Match length: %s%d%s", NTCOLOR(HIGHLIGHT), matchingData->matchLength, NTCOLOR(STREAM_DEFAULT));
    NLOGI("HelloCC", "        Matched text: %s%s%s", NTCOLOR(HIGHLIGHT), matchingData->matchedText, NTCOLOR(STREAM_DEFAULT));
    return True;
}

static boolean rejectingPrintListener(struct NCC_MatchingData* matchingData) {
    //printListener(matchingData);
    return False;
}

typedef struct RuleDefinitionData {
    struct NCC* ncc;
    NCC_RuleData plainRuleData, pushingRuleData, printRuleData, specialRuleData;
} RuleDefinitionData;

static void addRule(RuleDefinitionData* rdd, const char* ruleName, const char* ruleText) {
    NCC_addRule(rdd->ncc, rdd->plainRuleData.set(&rdd->plainRuleData, ruleName, ruleText));
}

static void addPushingRule(RuleDefinitionData* rdd, const char* ruleName, const char* ruleText) {
    NCC_addRule(rdd->ncc, rdd->pushingRuleData.set(&rdd->pushingRuleData, ruleName, ruleText));
}

static void addPrintRule(RuleDefinitionData* rdd, const char* ruleName, const char* ruleText) {
    NCC_addRule(rdd->ncc, rdd->printRuleData.set(&rdd->printRuleData, ruleName, ruleText));
}

static void addSpecialRule(RuleDefinitionData* rdd, const char* ruleName, const char* ruleText) {
    NCC_addRule(rdd->ncc, rdd->specialRuleData.set(&rdd->specialRuleData, ruleName, ruleText));
}

static void updateRule(RuleDefinitionData* rdd, const char* ruleName, const char* ruleText) {
    NCC_Rule* rule = NCC_getRule(rdd->ncc, ruleName);
    NCC_updateRuleText(rdd->ncc, rule, ruleText);
}

void definePreprocessing(struct NCC* ncc) {

    // =====================================
    // Preprocessing directives,
    // =====================================

    // Header name,
    //NCC_addRule(ncc, "h-char", "\x01-\\\x09 | \x0b-\xff", 0, False, False, False); // All characters except new-line.
    //NCC_addRule(ncc, "header-name", "{<${h-char}^*>} | {\"${h-char}^*\"}", 0, False, False, False);

    // Preprocessing number,
    // TODO:...
}

void defineLanguage(struct NCC* ncc) {

    // Notes:
    // ======
    //  Leave right recursion as is.
    //  Convert left recursion into repeat or right recursion (note that right recursion inverses the order of operations).
    //    Example:
    //    ========
    //      Rule:
    //      -----
    //         shift-expression:
    //            additive-expression
    //            shift-expression << additive-expression
    //            shift-expression >> additive-expression
    //      Becomes:
    //      --------
    //         shift-expression:
    //            ${additive-expression} {
    //               { << ${additive-expression}} |
    //               { >> ${additive-expression}}
    //            }^*
    //      Or:
    //      --
    //         shift-expression:
    //            ${additive-expression} |
    //            { ${additive-expression} << ${shift-expression}} |
    //            { ${additive-expression} >> ${shift-expression}}
    //

    // TODO: do we need a ${} when all unnecessary whitespaces should be removed during pre-processing?
    //       ${} could necessary for code coloring, and not for compiling. This should be more obvious
    //       upon implementation.

    RuleDefinitionData rdd = { .ncc = ncc };
    NCC_initializeRuleData(&rdd.  plainRuleData, "", "",                 0,                 0,                      0);
    NCC_initializeRuleData(&rdd.pushingRuleData, "", "", NCC_createASTNode, NCC_deleteASTNode,       NCC_matchASTNode);
    NCC_initializeRuleData(&rdd.  printRuleData, "", "",                 0,                 0,          printListener);
    NCC_initializeRuleData(&rdd.specialRuleData, "", "",                 0,                 0, rejectingPrintListener);

    // =====================================
    // Lexical rules,
    // =====================================

    // Tokens,
    addPushingRule(&rdd,              "+",              "+");
    addPushingRule(&rdd,              "-",            "\\-");
    addPushingRule(&rdd,              "*",            "\\*");
    addPushingRule(&rdd,              "/",              "/");
    addPushingRule(&rdd,              "%",              "%");
    addPushingRule(&rdd,              "!",              "!");
    addPushingRule(&rdd,              "~",              "~");
    addPushingRule(&rdd,              "&",              "&");
    addPushingRule(&rdd,              "|",            "\\|");
    addPushingRule(&rdd,              "^",            "\\^");
    addPushingRule(&rdd,             "<<",             "<<");
    addPushingRule(&rdd,             ">>",             ">>");
    addPushingRule(&rdd,              "=",              "=");
    addPushingRule(&rdd,             "+=",             "+=");
    addPushingRule(&rdd,             "-=",        "\\-\\-=");
    addPushingRule(&rdd,             "*=",           "\\*=");
    addPushingRule(&rdd,             "/=",             "/=");
    addPushingRule(&rdd,             "%=",             "%=");
    addPushingRule(&rdd,            "<<=",            "<<=");
    addPushingRule(&rdd,            ">>=",            ">>=");
    addPushingRule(&rdd,             "^=",           "\\^=");
    addPushingRule(&rdd,             "&=",             "&=");
    addPushingRule(&rdd,             "|=",           "\\|=");
    addPushingRule(&rdd,             "==",             "==");
    addPushingRule(&rdd,             "!=",             "!=");
    addPushingRule(&rdd,              "<",              "<");
    addPushingRule(&rdd,              ">",              ">");
    addPushingRule(&rdd,             "<=",             "<=");
    addPushingRule(&rdd,             ">=",             ">=");
    addPushingRule(&rdd,             "&&",             "&&");
    addPushingRule(&rdd,             "||",         "\\|\\|");
    addPushingRule(&rdd,              "(",              "(");
    addPushingRule(&rdd,              ")",              ")");
    addPushingRule(&rdd,              "[",              "[");
    addPushingRule(&rdd,              "]",              "]");
    addPushingRule(&rdd,             "OB",            "\\{");
    addPushingRule(&rdd,             "CB",            "\\}");
    addPushingRule(&rdd,              ":",              ":");
    addPushingRule(&rdd,              ";",              ";");
    addPushingRule(&rdd,              "?",              "?");
    addPushingRule(&rdd,              ",",              ",");
    addPushingRule(&rdd,              ".",              ".");
    addPushingRule(&rdd,             "->",           "\\->");
    addPushingRule(&rdd,             "++",             "++");
    addPushingRule(&rdd,             "--",         "\\-\\-");
    addPushingRule(&rdd,            "...",            "...");
    addPushingRule(&rdd,       "pointer*",            "\\*");
    addPushingRule(&rdd,         "struct",         "struct");
    addPushingRule(&rdd,          "union",          "union");
    addPushingRule(&rdd,           "enum",           "enum");
    addPushingRule(&rdd,         "sizeof",         "sizeof");
    addPushingRule(&rdd,             "if",             "if");
    addPushingRule(&rdd,           "else",           "else");
    addPushingRule(&rdd,          "while",          "while");
    addPushingRule(&rdd,             "do",             "do");
    addPushingRule(&rdd,            "for",            "for");
    addPushingRule(&rdd,       "continue",       "continue");
    addPushingRule(&rdd,          "break",          "break");
    addPushingRule(&rdd,         "return",         "return");
    addPushingRule(&rdd,         "switch",         "switch");
    addPushingRule(&rdd,           "case",           "case");
    addPushingRule(&rdd,        "default",        "default");
    addPushingRule(&rdd,           "goto",           "goto");
    addPushingRule(&rdd,           "void",           "void");
    addPushingRule(&rdd,           "char",           "char");
    addPushingRule(&rdd,          "short",          "short");
    addPushingRule(&rdd,            "int",            "int");
    addPushingRule(&rdd,           "long",           "long");
    addPushingRule(&rdd,          "float",          "float");
    addPushingRule(&rdd,         "double",         "double");
    addPushingRule(&rdd,         "signed",         "signed");
    addPushingRule(&rdd,       "unsigned",       "unsigned");
    addPushingRule(&rdd,        "typedef",        "typedef");
    addPushingRule(&rdd,         "extern",         "extern");
    addPushingRule(&rdd,         "static",         "static");
    addPushingRule(&rdd,          "const",          "const");
    addPushingRule(&rdd,       "restrict",       "restrict");
    addPushingRule(&rdd,       "volatile",       "volatile");
    addPushingRule(&rdd,         "inline",         "inline");
    addPushingRule(&rdd,      "_Noreturn",      "_Noreturn");
    addPushingRule(&rdd,           "auto",           "auto");
    addPushingRule(&rdd,       "register",       "register");
    addPushingRule(&rdd,        "_Atomic",        "_Atomic");
    addPushingRule(&rdd,  "_Thread_local",  "_Thread_local");
    addPushingRule(&rdd,          "_Bool",          "_Bool");
    addPushingRule(&rdd,       "_Complex",       "_Complex");
    addPushingRule(&rdd, "_Static_assert", "_Static_assert");
    addPushingRule(&rdd,       "_Alignof",       "_Alignof");
    addPushingRule(&rdd,       "_Alineas",       "_Alineas");

    // Color markers,
    addPushingRule(&rdd, "POP C" , "");
    addPushingRule(&rdd, "PSH C0", "");
    addPushingRule(&rdd, "PSH C1", "");
    addPushingRule(&rdd, "PSH C2", "");
    addPushingRule(&rdd, "PSH C3", "");
    addPushingRule(&rdd, "PSH C4", "");
    addPushingRule(&rdd, "PSH C5", "");
    addPushingRule(&rdd, "PSH C6", "");
    addPushingRule(&rdd, "PSH C7", "");

    // Space markers (forward declaration),
    addPushingRule(&rdd, "insert space", "");

    // Spaces and comments,
    addRule       (&rdd, "ε", "");
    addPushingRule(&rdd, "line-cont", "\\\\\n");
    addRule       (&rdd, "white-space", "{\\ |\\\t|\r|\n|${line-cont}} {\\ |\\\t|\r|\n|${line-cont}}^*");
    addPushingRule(&rdd, "line-comment", "${white-space} // {{* \\\\\n}^*} * \n|${ε}");
    addPushingRule(&rdd, "block-comment", "${white-space} /\\* * \\*/");
    addRule       (&rdd, "ignorable", "#{{white-space} {line-comment} {block-comment}}");
    addRule       (&rdd,  "",              "${ignorable}^*");
    addRule       (&rdd, " ", "${ignorable} ${ignorable}^*");

    // Space markers (implementation),
    addRule       (&rdd, "+ ", "${} ${insert space}");
    addPushingRule(&rdd, "insert \n" , "");
    addPushingRule(&rdd, "insert \ns", "");
    addRule       (&rdd, "+\n" , "${} ${insert \n}");
    addRule       (&rdd, "+\ns", "${} ${insert \ns}");

    // TODO: use the non-ignorable white-spaces where they should be (like, between "int" and "a" in "int a;").

    addRule       (&rdd, "digit", "0-9");
    addRule       (&rdd, "non-zero-digit", "1-9");
    addRule       (&rdd, "non-digit", "_|a-z|A-Z");
    addRule       (&rdd, "hexadecimal-prefix", "0x|X");
    addRule       (&rdd, "hexadecimal-digit", "0-9|a-f|A-F");
    addRule       (&rdd, "hex-quad", "${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}");
    addRule       (&rdd, "universal-character-name", "{\\\\u ${hex-quad}} | {\\\\U ${hex-quad} ${hex-quad}}");

    // Identifier,
    addRule       (&rdd, "identifier-non-digit", "${non-digit} | ${universal-character-name}");
    addPushingRule(&rdd, "identifier", "${identifier-non-digit} {${digit} | ${identifier-non-digit}}^*");

    // Constants,
    // Integer constant,
    addRule       (&rdd, "decimal-constant", "${non-zero-digit} ${digit}^*");
    addRule       (&rdd, "octal-constant", "0 0-7^*");
    addRule       (&rdd, "hexadecimal-constant", "${hexadecimal-prefix} ${hexadecimal-digit} ${hexadecimal-digit}^*");
    addRule       (&rdd, "integer-suffix", "{ u|U l|L|{ll}|{LL}|${ε} } | { l|L|{ll}|{LL} u|U|${ε} }");
    addPushingRule(&rdd, "integer-constant", "${decimal-constant}|${octal-constant}|${hexadecimal-constant} ${integer-suffix}|${ε}");

    // Decimal floating point,
    addRule       (&rdd, "fractional-constant", "{${digit}^* . ${digit} ${digit}^*} | {${digit} ${digit}^* . }");
    addRule       (&rdd, "exponent-part", "e|E +|\\-|${ε} ${digit} ${digit}^*");
    addRule       (&rdd, "floating-suffix", "f|l|F|L");
    addRule       (&rdd, "decimal-floating-constant",
                            "{${fractional-constant} ${exponent-part}|${ε} ${floating-suffix}|${ε}} | "
                            "{${digit} ${digit}^* ${exponent-part} ${floating-suffix}|${ε}}");

    // Hexadecimal floating point,
    addRule       (&rdd, "hexadecimal-fractional-constant",
                            "{${hexadecimal-digit}^* . ${hexadecimal-digit} ${hexadecimal-digit}^*} | "
                            "{${hexadecimal-digit} ${hexadecimal-digit}^* . }");
    addRule       (&rdd, "binary-exponent-part", "p|P +|\\-|${ε} ${digit} ${digit}^*");
    addRule       (&rdd, "hexadecimal-floating-constant",
                            "${hexadecimal-prefix} ${hexadecimal-fractional-constant}|{${hexadecimal-digit}${hexadecimal-digit}^*} ${binary-exponent-part} ${floating-suffix}|${ε}");

    // Floating point constant,
    addPushingRule(&rdd, "floating-constant", "${decimal-floating-constant} | ${hexadecimal-floating-constant}");

    // Enumeration constant,
    addPushingRule(&rdd, "enumeration-constant", "${identifier}");

    // Character constant (supporting unknown escape sequences which are implementation defined. We'll pass the escaped character like gcc and clang do),
    addRule       (&rdd, "c-char", "\x01-\\\x09 | \x0b-\x5b | \x5d-\xff"); // All characters except new-line and backslash (\). "\x09" is "\t", and is reserved, hence we needed to escape it.
    addRule       (&rdd, "c-char-with-backslash-without-uUxX", "\x01-\\\x09 | \x0b-\x54 | \x56-\x57| \x59-\x74 | \x76-\x77 | \x79-\xff"); // All characters except new-line, 'u', 'U', 'x' and 'X'.
    addRule       (&rdd, "hexadecimal-escape-sequence", "\\\\x ${hexadecimal-digit} ${hexadecimal-digit}^*");
    addPushingRule(&rdd, "character-constant", "L|u|U|${ε} ' { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* '");

    // Constant,
    //addPushingRule(&rdd, "constant", "${integer-constant} | ${floating-constant} | ${enumeration-constant} | ${character-constant}");
    //addPushingRule(&rdd, "constant", "#{{integer-constant} {floating-constant} {enumeration-constant} {character-constant}}");
    addPushingRule(&rdd, "constant", "${PSH C2} #{{integer-constant} {floating-constant} {enumeration-constant} {character-constant}} ${POP C}");

    // String literal,
    // See: https://stackoverflow.com/a/13087264/1942069   and   https://stackoverflow.com/a/13445170/1942069
    addPushingRule(&rdd, "string-literal-fragment", "{u8}|u|U|L|${ε} \" { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* \"");
    addPushingRule(&rdd, "string-literal", "${PSH C3} ${string-literal-fragment} {${} ${string-literal-fragment}}|${ε} ${POP C}");

    // =====================================
    // Phrase structure,
    // =====================================

    // -------------------------------------
    // Expressions,
    // -------------------------------------

    // Primary expression,
    addPushingRule(&rdd, "expression", "STUB!");
    addRule       (&rdd, "generic-selection", "STUB!");
    addPushingRule(&rdd, "primary-expression",
                            "${identifier} | "
                            "${constant} | "
                            "${string-literal} | "
                            "{ ${(} ${} ${expression} ${} ${)} } | "
                            "${generic-selection}");

    // Generic selection,
    // See: https://www.geeksforgeeks.org/_generic-keyword-c/
    //#define INC(x) _Generic((x), long double: INCl, default: INC, float: INCf)(x)
    //NLOGE("", "%d\n", _Generic(1, int: 7, float:1, double:2, long double:3, default:0);
    addPushingRule(&rdd, "assignment-expression", "STUB!");
    addRule       (&rdd, "generic-assoc-list", "STUB!");
    updateRule    (&rdd, "generic-selection",
                            "_Generic ${} ${(} ${} ${assignment-expression} ${} ${,} ${} ${generic-assoc-list} ${} ${)}");

    // Generic assoc list,
    addRule       (&rdd, "generic-association", "STUB!");
    updateRule    (&rdd, "generic-assoc-list",
                            "${generic-association} {"
                            "   ${} ${,} ${} ${generic-association}"
                            "}^*");

    // Generic association,
    addRule       (&rdd, "type-name", "STUB!");
    updateRule    (&rdd, "generic-association",
                            "{${type-name} ${} ${:} ${} ${assignment-expression}} |"
                            "{default      ${} ${:} ${} ${assignment-expression}}");

    // Postfix expression,
    addRule       (&rdd, "argument-expression-list", "STUB!");
    addRule       (&rdd, "initializer-list", "STUB!");
    addRule       (&rdd, "postfix-expression-contents",
                            "${primary-expression} | "
                            "{ ${(} ${} ${type-name} ${} ${)} ${} ${OB} ${} ${initializer-list} ${} {${,} ${+ }}|${ε} ${} ${CB} }");
    addPushingRule(&rdd, "postfix-expression",
                            "${postfix-expression-contents} {"
                            "   {${} ${[}  ${} ${expression} ${} ${]} } | "
                            "   {${} ${(}  ${} ${argument-expression-list}|${ε} ${} ${)} } | "
                            "   {${} ${.}  ${} ${PSH C4} ${identifier} ${POP C}} | "
                            "   {${} ${->} ${} ${identifier}} | "
                            "   {${} ${++} } | "
                            "   {${} ${--} }"
                            "}^*");

    // Argument expression list,
    updateRule    (&rdd, "argument-expression-list",
                            "${assignment-expression} {"
                            "   ${} ${,} ${+ } ${assignment-expression}"
                            "}^*");

    // Unary expression,
    addPushingRule(&rdd, "unary-expression", "STUB!");
    addRule       (&rdd, "unary-operator", "STUB!");
    addPushingRule(&rdd, "cast-expression", "STUB!");
    updateRule    (&rdd, "unary-expression",
                            "${postfix-expression} | "
                            "{ ${++} ${} ${unary-expression} } | "
                            "{ ${--} ${} ${unary-expression} } | "
                            "{ ${unary-operator} ${} ${cast-expression} } | "
                            "{ ${PSH C1}   ${sizeof} ${POP C} ${} ${(} ${} ${unary-expression} ${} ${)} } | "
                            "{ ${PSH C1}   ${sizeof} ${POP C} ${} ${(} ${} ${type-name}        ${} ${)} } | "
                            "{ ${PSH C1} ${_Alignof} ${POP C} ${} ${(} ${} ${type-name}        ${} ${)} }");

    // Unary operator,
    updateRule    (&rdd, "unary-operator", "#{{&}{*}{+}{-}{~}{!} {&&}{++}{--} != {&&}{++}{--}}");

    // Cast expression,
    updateRule    (&rdd, "cast-expression",
                            "${unary-expression} | "
                            "{ ${(} ${} ${type-name} ${} ${)} ${} ${cast-expression} }");

    // Multiplicative expression,
    addPushingRule(&rdd, "multiplicative-expression",
                            "${cast-expression} {"
                            "   ${+ } ${*}|${/}|${%} ${+ } ${cast-expression}"
                            "}^*");

    // Additive expression,
    addPushingRule(&rdd, "additive-expression",
                            "${multiplicative-expression} {"
                            "   ${+ } ${+}|${-} ${+ } ${multiplicative-expression}"
                            "}^*");

    // Shift expression,
    addPushingRule(&rdd, "shift-expression",
                            "${additive-expression} {"
                            "   ${+ } ${<<}|${>>} ${+ } ${additive-expression}"
                            "}^*");

    // Relational expression,
    addPushingRule(&rdd, "relational-expression",
                            "${shift-expression} {"
                            "   ${+ } #{{<} {>} {<=} {>=}} ${+ } ${shift-expression}"
                            "}^*");

    // Equality expression,
    addPushingRule(&rdd, "equality-expression",
                            "${relational-expression} {"
                            "   ${+ } ${==}|${!=} ${+ } ${relational-expression}"
                            "}^*");

    // AND expression,
    addPushingRule(&rdd, "and-expression",
                            "${equality-expression} {"
                            "   ${+ } #{{&} {&&} != {&&}} ${+ } ${equality-expression}"
                            "}^*");

    // Exclusive OR expression,
    addPushingRule(&rdd, "xor-expression",
                            "${and-expression} {"
                            "   ${+ } ${^} ${+ } ${and-expression}"
                            "}^*");

    // Inclusive OR expression,
    addPushingRule(&rdd, "or-expression",
                            "${xor-expression} {"
                            "   ${+ } #{{|} {||} != {||}} ${+ } ${xor-expression}"
                            "}^*");

    // Logical AND expression,
    addPushingRule(&rdd, "logical-and-expression",
                            "${or-expression} {"
                            "   ${+ } ${&&} ${+ } ${or-expression}"
                            "}^*");

    // Logical OR expression,
    addPushingRule(&rdd, "logical-or-expression",
                            "${logical-and-expression} {"
                            "   ${+ } ${||} ${+ } ${logical-and-expression}"
                            "}^*");

    // Conditional expression,
    addPushingRule(&rdd, "conditional-expression", "STUB!");
    updateRule    (&rdd, "conditional-expression",
                            "${logical-or-expression} | "
                            "{${logical-or-expression} ${+ } ${?} ${+ } ${expression} ${+ } ${:} ${+ } ${conditional-expression}}");

    // Assignment expression,
    addRule       (&rdd, "assignment-operator", "STUB!");
    updateRule    (&rdd, "assignment-expression",
                            "${conditional-expression} | "
                            "{${unary-expression} ${+ } ${assignment-operator} ${+ } ${assignment-expression}}");

    // Assignment operator,
    updateRule    (&rdd, "assignment-operator", "#{{=} {*=} {/=} {%=} {+=} {-=} {<<=} {>>=} {&=} {^=} {|=}}");

    // Expression,
    updateRule    (&rdd, "expression",
                            "${assignment-expression} {"
                            "   ${} ${,} ${} ${assignment-expression}"
                            "}^*");

    addPushingRule(&rdd, "constant-expression", "${conditional-expression}");

    // -------------------------------------
    // Declarations,
    // -------------------------------------

    // Declaration,
    addPushingRule(&rdd, "declaration-specifiers", "STUB!");
    addPushingRule(&rdd, "init-declarator-list", "STUB!");
    addRule       (&rdd, "static_assert-declaration", "STUB!");
    addPushingRule(&rdd, "declaration",
                            "{${declaration-specifiers} {${+ } ${init-declarator-list}}|${ε} ${} ${;} } | "
                            "${static_assert-declaration}");

    // Declaration specifiers,
    addPushingRule(&rdd, "storage-class-specifier", "STUB!");
    addPushingRule(&rdd, "type-specifier", "STUB!");
    addRule       (&rdd, "type-qualifier", "STUB!");
    addRule       (&rdd, "function-specifier", "STUB!");
    addRule       (&rdd, "alignment-specifier", "STUB!");
    updateRule    (&rdd, "declaration-specifiers",
                            "${PSH C1} #{{storage-class-specifier} "
                            "            {type-specifier}"
                            "            {type-qualifier}"
                            "            {function-specifier}"
                            "            {alignment-specifier}}"
                            "${POP C} {${+ } ${declaration-specifiers}}|${ε}");

    // Init declarator list,
    addPushingRule(&rdd, "init-declarator", "STUB!");
    updateRule    (&rdd, "init-declarator-list",
                            "${init-declarator} { "
                            "   ${} ${,} ${+ } ${init-declarator}"
                            "}^*");

    // Init declarator,
    addPushingRule(&rdd, "declarator", "STUB!");
    addRule       (&rdd, "initializer", "STUB!");
    updateRule    (&rdd, "init-declarator",
                            "${declarator} {${+ } ${=} ${+ } ${initializer}}|${ε}");

    // Storage class specifier,
    updateRule    (&rdd, "storage-class-specifier",
                            "#{{typedef} {extern} {static} {_Thread_local} {auto} {register} {identifier} != {identifier}}");

    // Type specifier,
    addRule       (&rdd, "atomic-type-specifier", "STUB!");
    addPushingRule(&rdd, "struct-or-union-specifier", "STUB!");
    addRule       (&rdd, "enum-specifier", "STUB!");
    // TODO: use addRule instead of addSpecialRule?
    addSpecialRule(&rdd, "typedef-name", "STUB!");
    updateRule    (&rdd, "type-specifier",
                            "#{{void}     {char}            "
                            "  {short}    {int}      {long} "
                            "  {float}    {double}          "
                            "  {signed}   {unsigned}        "
                            "  {_Bool}    {_Complex}        "
                            "  {atomic-type-specifier}      "
                            "  {struct-or-union-specifier}  "
                            "  {enum-specifier}             "
                            "  {typedef-name}               "
                            "  {identifier} != {identifier}}");

    // Struct or union specifier,
    addRule       (&rdd, "struct-or-union", "STUB!");
    addRule       (&rdd, "struct-declaration-list", "STUB!");
    updateRule    (&rdd, "struct-or-union-specifier",
                            "${struct-or-union} ${+ }"
                            "{{${PSH C5} ${identifier} ${POP C}}|${ε} ${PSH C0} ${+ } ${OB} ${+\n} ${} ${struct-declaration-list} ${} ${CB} ${POP C}} | "
                            " {${PSH C5} ${identifier} ${POP C}}");

    // Struct or union,
    updateRule    (&rdd, "struct-or-union",
                            "#{{struct} {union}}");

    // Struct declaration list,
    addPushingRule(&rdd, "struct-declaration", "STUB!");
    updateRule    (&rdd, "struct-declaration-list",
                            "${struct-declaration} { "
                            "   ${} ${struct-declaration}"
                            "}^*");

    // Struct declaration,
    addRule       (&rdd, "specifier-qualifier-list", "STUB!");
    addRule       (&rdd, "struct-declarator-list", "STUB!");
    updateRule    (&rdd, "struct-declaration",
                            "{${specifier-qualifier-list} ${+ } ${struct-declarator-list}|${ε} ${} ${;} ${+\n}} | "
                            "${static_assert-declaration}");

    // Specifier qualifier list,
    updateRule    (&rdd, "specifier-qualifier-list",
                            "${PSH C1} #{{type-specifier} {type-qualifier}} ${POP C}"
                            "{${+ } ${specifier-qualifier-list}}|${ε}");

    // Struct declarator list,
    addRule       (&rdd, "struct-declarator", "STUB!");
    updateRule    (&rdd, "struct-declarator-list",
                            "${struct-declarator} { "
                            "   ${} ${,} ${+ } ${struct-declarator}"
                            "}^*");

    // Struct declarator,
    updateRule    (&rdd, "struct-declarator",
                            " {${PSH C6} ${declarator} ${POP C}} | "
                            "{{${PSH C6} ${declarator} ${POP C}}|${ε} ${} ${:} ${+ } ${constant-expression}}");

    // Enum specifier,
    addRule       (&rdd, "enumerator-list", "STUB!");
    updateRule    (&rdd, "enum-specifier",
                            "{ ${enum} ${} ${identifier}|${ε} ${} ${OB} ${enumerator-list} ${} ${,}|${ε} ${} ${CB} } | "
                            "{ ${enum} ${} ${identifier} }");

    // Enumerator list,
    addRule       (&rdd, "enumerator", "STUB!");
    updateRule    (&rdd, "enumerator-list",
                            "${enumerator} {"
                            "   ${} ${,} ${+ } ${enumerator}"
                            "}^*");

    // Enumerator,
    updateRule    (&rdd, "enumerator",
                            "${enumeration-constant} { ${} = ${} ${constant-expression} }|${ε}");

    // Atomic type specifier,
    updateRule    (&rdd, "atomic-type-specifier",
                            "${_Atomic} ${} ${(} ${} ${type-name} ${} ${)}");

    // Type qualifier,
    updateRule    (&rdd, "type-qualifier",
                            "#{{const} {restrict} {volatile} {_Atomic} {identifier} != {identifier}}");

    // Function specifier,
    updateRule    (&rdd, "function-specifier",
                            "#{{inline} {_Noreturn} {identifier} != {identifier}}");

    // Alignment specifier,
    updateRule    (&rdd, "alignment-specifier",
                            "${_Alineas} ${} ${(} ${} ${type-name}|${constant-expression} ${} ${)}");

    // Declarator,
    addRule       (&rdd, "pointer", "STUB!");
    addPushingRule(&rdd, "direct-declarator", "STUB!");
    updateRule    (&rdd, "declarator",
                            "${pointer}|${ε} ${} ${direct-declarator}");

    // Direct declarator,
    addRule       (&rdd, "type-qualifier-list", "STUB!");
    addPushingRule(&rdd, "parameter-type-list", "STUB!");
    addRule       (&rdd, "identifier-list", "STUB!");
    updateRule    (&rdd, "direct-declarator",
                            "{${identifier} | {(${} ${declarator} ${})}} {"
                            "   { ${} ${[} ${}               ${type-qualifier-list}|${ε} ${}               ${assignment-expression}|${ε} ${} ${]}} | "
                            "   { ${} ${[} ${} ${static} ${} ${type-qualifier-list}|${ε} ${}               ${assignment-expression}      ${} ${]}} | "
                            "   { ${} ${[} ${}               ${type-qualifier-list}      ${} ${static} ${} ${assignment-expression}      ${} ${]}} | "
                            "   { ${} ${[} ${}               ${type-qualifier-list}|${ε} ${} ${*}      ${}                                   ${]}} | "
                            "   { ${} ${(} ${} ${parameter-type-list}  ${} ${)}} | "
                            "   { ${} ${(} ${} ${identifier-list}|${ε} ${} ${)}}"
                            "}^*");

    // Pointer,
    updateRule    (&rdd, "pointer",
                            "${PSH C0} ${pointer*} ${POP C} ${} ${type-qualifier-list}|${ε} ${} ${pointer}|${ε}");

    // Type qualifier list,
    updateRule    (&rdd, "type-qualifier-list",
                            "${type-qualifier} {"
                            "   ${} ${type-qualifier}"
                            "}^*");

    // Parameter type list,
    addRule       (&rdd, "parameter-list", "STUB!");
    updateRule    (&rdd, "parameter-type-list",
                            "${parameter-list} {${} ${,} ${+ } ${...} }|${ε}");

    // Parameter list,
    addPushingRule(&rdd, "parameter-declaration", "STUB!");
    updateRule    (&rdd, "parameter-list",
                            "${parameter-declaration} {"
                            "   ${} ${,} ${+ } ${parameter-declaration}"
                            "}^*");

    // Parameter declaration,
    addRule       (&rdd, "abstract-declarator", "STUB!");
    updateRule    (&rdd, "parameter-declaration",
                            "${declaration-specifiers} ${} {${+ } ${declarator}}|${abstract-declarator}|${ε}");

    // Identifier list,
    updateRule    (&rdd, "identifier-list",
                            "${identifier} {"
                            "   ${} ${,} ${} ${identifier}"
                            "}^*");

    // Type name,
    updateRule    (&rdd, "type-name",
                            "${specifier-qualifier-list} ${} ${abstract-declarator}|${ε}");

    // Abstract declarator,
    addRule       (&rdd, "direct-abstract-declarator", "STUB!");
    updateRule    (&rdd, "abstract-declarator",
                            "${pointer} | "
                            "{ ${pointer}|${ε} ${} ${direct-abstract-declarator} }");

    // Direct abstract declarator,
    addRule       (&rdd, "direct-abstract-declarator-content",
                            "{${(} ${} ${abstract-declarator} ${} ${)} } | "
                            "{${[} ${}              ${type-qualifier-list}|${ε} ${}              ${assignment-expression}|${ε} ${} ${]} } | "
                            "{${[} ${} static ${}   ${type-qualifier-list}|${ε} ${}              ${assignment-expression}      ${} ${]} } | "
                            "{${[} ${}              ${type-qualifier-list}      ${} static ${}   ${assignment-expression}      ${} ${]} } | "
                            "{${[} ${} \\*    ${}                                                                                  ${]} } | "
                            "{${(} ${} ${parameter-type-list}|${ε} ${} ${)} }");
    updateRule    (&rdd, "direct-abstract-declarator",
                            "${direct-abstract-declarator-content} {"
                            "   ${} ${direct-abstract-declarator-content}"
                            "}^*");

    // Typedef name,
    // ...XXX
    // Note: typedef-name uses special rule,
    updateRule    (&rdd, "typedef-name", "${identifier}");

    // Initializer,
    updateRule    (&rdd, "initializer",
                            "${assignment-expression} | "
                            "{ ${OB} ${} ${initializer-list} ${} ${,}|${ε} ${} ${CB} }");

    // Initializer list,
    addRule       (&rdd, "designation", "STUB!");
    addRule       (&rdd, "initializer-list-content",
                            "${designation}|${ε} ${} ${initializer}");
    updateRule    (&rdd, "initializer-list",
                            "${initializer-list-content} {"
                            "   ${} ${,} ${} ${initializer-list-content}"
                            "}^*");

    // Designation,
    addRule       (&rdd, "designator-list", "STUB!");
    updateRule    (&rdd, "designation",
                            "${designator-list} ${} ${=}");

    // Designator list,
    addRule       (&rdd, "designator", "STUB!");
    updateRule    (&rdd, "designator-list",
                            "${designator} {"
                            "   ${} ${designator}"
                            "}^*");

    // Designator,
    updateRule    (&rdd, "designator",
                            "{ ${[} ${} ${constant-expression} ${} ${]} } | "
                            "{ ${.} ${} ${identifier}}");

    // static_assert declaration,
    updateRule    (&rdd, "static_assert-declaration",
                            "${_Static_assert} ${} ${(} ${} ${constant-expression} ${} ${,} ${} ${string-literal} ${} ${)} ${} ${;}");

    // -------------------------------------
    // Statements,
    // -------------------------------------

    // Statement,
    addPushingRule(&rdd,    "labeled-statement", "STUB!");
    addPushingRule(&rdd,   "compound-statement", "STUB!");
    addPushingRule(&rdd, "expression-statement", "STUB!");
    addPushingRule(&rdd,  "selection-statement", "STUB!");
    addPushingRule(&rdd,  "iteration-statement", "STUB!");
    addPushingRule(&rdd,       "jump-statement", "STUB!");
    addPushingRule(&rdd, "statement",
                            "#{   {labeled-statement}"
                            "    {compound-statement}"
                            "  {expression-statement}"
                            "   {selection-statement}"
                            "   {iteration-statement}"
                            "        {jump-statement}}");

    // Labeled statement,
    updateRule    (&rdd, "labeled-statement",
                            "{${identifier}                      ${} ${:} ${} ${statement}} | "
                            "{${case} ${} ${constant-expression} ${} ${:} ${} ${statement}} | "
                            "{${default}                         ${} ${:} ${} ${statement}}");

    // Compound statement,
    addRule       (&rdd, "block-item-list", "STUB!");
    updateRule    (&rdd, "compound-statement",
                            "${OB} ${} ${block-item-list}|${ε} ${} ${CB}");

    // Block item list,
    addRule       (&rdd, "block-item", "STUB!");
    updateRule    (&rdd, "block-item-list",
                            "${+\n} ${block-item} {{"
                            "   ${+\n} ${block-item}"
                            "}^*} ${+\n}");

    // Block item,
    updateRule    (&rdd, "block-item",
                            "#{{declaration} {statement}}");

    // Expression statement,
    updateRule    (&rdd, "expression-statement",
                            "${expression}|${ε} ${} ${;}");

    // Selection statement,
    updateRule    (&rdd, "selection-statement",
                            "{ ${PSH C1} ${if}     ${POP C} ${} ${(} ${} ${expression} ${} ${)} ${} ${statement} {${} ${else} ${} ${statement}}|${ε} } | "
                            "{ ${PSH C1} ${switch} ${POP C} ${} ${(} ${} ${expression} ${} ${)} ${} ${statement}                                     }");

    // Iteration statement,
    updateRule    (&rdd, "iteration-statement",
                            "{ ${PSH C1} ${while} ${POP C} ${+ }                           ${(} ${} ${expression} ${} ${)} ${} ${;}|{${+ } ${statement}} } | "
                            "{ ${PSH C1} ${do}    ${POP C} ${+ } ${statement} ${} ${while} ${(} ${} ${expression} ${} ${)} ${} ${;}                      } | "
                            "{ ${PSH C1} ${for}   ${POP C} ${+ } ${(} ${} ${expression}|${ε} ${} ${;} ${+ } ${expression}|${ε} ${} ${;} ${+ } ${expression}|${ε} ${} ${)} ${} ${;}|{${+ } ${statement}} } | "
                            "{ ${PSH C1} ${for}   ${POP C} ${+ } ${(} ${} ${declaration}              ${+ } ${expression}|${ε} ${} ${;} ${+ } ${expression}|${ε} ${} ${)} ${} ${;}|{${+ } ${statement}} }");

    // Jump statement,
    updateRule    (&rdd, "jump-statement",
                            "{ ${PSH C1} ${goto}     ${POP C} ${} ${identifier}      ${} ${;} } | "
                            "{ ${PSH C1} ${continue} ${POP C} ${}                        ${;} } | "
                            "{ ${PSH C1} ${break}    ${POP C} ${}                        ${;} } | "
                            "{ ${PSH C1} ${return}   ${POP C} ${} ${expression}|${ε} ${} ${;} }");

    // -------------------------------------
    // External definitions,
    // -------------------------------------

    // Translation unit,
    addRule       (&rdd, "external-declaration", "STUB!");
    addPushingRule(&rdd, "translation-unit",
                            "${} ${external-declaration} {{"
                            "   ${} ${+\ns} ${external-declaration}"
                            "}^*} ${}"); // Encapsulated the repeat in a sub-rule to avoid early termination. Can \
                                            we consider early termination a feature now?

    // External declaration,
    addPushingRule(&rdd, "function-definition", "STUB!");
    updateRule    (&rdd, "external-declaration",
                            "#{{function-definition} {declaration}}");

    // Function definition,
    addRule       (&rdd, "declaration-list", "STUB!");
    updateRule    (&rdd, "function-definition",
                            "${declaration-specifiers} ${+ } ${declarator} ${} ${declaration-list}|${ε} ${+ } ${compound-statement} ${+\n}");

    // Declaration list (for K&R function definition style. See: https://stackoverflow.com/a/18820829/1942069 ),
    //   Example: int foo(a,b) int a, b; {}
    updateRule    (&rdd, "declaration-list",
                            "${declaration} {"
                            "   ${} ${declaration}"
                            "}^*");

    // Test document,
    addPushingRule(&rdd, "TestDocument",
                            "#{                          "
                            "        {primary-expression}"
                            "        {postfix-expression}"
                            "          {unary-expression}"
                            "           {cast-expression}"
                            " {multiplicative-expression}"
                            "       {additive-expression}"
                            "          {shift-expression}"
                            "     {relational-expression}"
                            "       {equality-expression}"
                            "            {and-expression}"
                            "            {xor-expression}"
                            "             {or-expression}"
                            "    {logical-and-expression}"
                            "     {logical-or-expression}"
                            "    {conditional-expression}"
                            "     {assignment-expression}"
                            "                {expression}"
                            "       {constant-expression}"
                            "               {declaration}"
                            "          {translation-unit}"
                            "}                           ");

    // Cleanup,
    NCC_destroyRuleData(&rdd.  plainRuleData);
    NCC_destroyRuleData(&rdd.pushingRuleData);
    NCC_destroyRuleData(&rdd.  printRuleData);
    NCC_destroyRuleData(&rdd.specialRuleData);
}

NCC_Rule *getRootRule(struct NCC* ncc) {
    return NCC_getRule(ncc, "TestDocument");
}
