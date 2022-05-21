#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>

#include <NCC.h>

#define TEST_EXPRESSIONS  0
#define TEST_DECLARATIONS 1

void printListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("", "ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NCC_Variable variable;
    while (NCC_popRuleVariable(ncc, &variable)) {
        NLOGI("", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), variable.name, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
}

void definePreprocessing(struct NCC* ncc) {

    // Header name,
    NCC_addRule(ncc, "h-char", "\x01-\x09 | \x0b-\xff", 0, False, False, False); // All characters except new-line.
    NCC_addRule(ncc, "header-name", "{<${h-char}^*>} | {\"${h-char}^*\"}", 0, False, False, False);

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

    // =====================================
    // Lexical rules,
    // =====================================

    // Common,
    NCC_addRule(ncc, "ε", "", 0, False, False, False);
    NCC_addRule(ncc, "white-space", "{\\ |\t|\r|\n|{\\\\\n}}^*", 0, False, False, False);
    NCC_addRule(ncc, "line-end", "\n|${ε}", 0, False, False, False);
    NCC_addRule(ncc, "line-comment", "// * ${line-end}|{\\\\${line-end}}", 0, False, False, False);
    NCC_addRule(ncc, "block-comment", "/\\* * \\*/", 0, False, False, False);
    NCC_addRule(ncc, "", "{${white-space}|${line-comment}|${block-comment}}^*", 0, False, False, False);

    NCC_addRule(ncc, "digit", "0-9", 0, False, False, False);
    NCC_addRule(ncc, "non-zero-digit", "1-9", 0, False, False, False);
    NCC_addRule(ncc, "non-digit", "_|a-z|A-Z", 0, False, False, False);
    NCC_addRule(ncc, "hexadecimal-prefix", "0x|X", 0, False, False, False);
    NCC_addRule(ncc, "hexadecimal-digit", "0-9|a-f|A-F", 0, False, False, False);
    NCC_addRule(ncc, "hex-quad", "${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}", 0, False, False, False);
    NCC_addRule(ncc, "universal-character-name", "{\\\\u ${hex-quad}} | {\\\\U ${hex-quad} ${hex-quad}}", 0, False, False, False);

    // Identifier,
    NCC_addRule(ncc, "identifier-non-digit", "${non-digit} | ${universal-character-name}", 0, False, False, False);
    NCC_addRule(ncc, "identifier", "${identifier-non-digit} {${digit} | ${identifier-non-digit}}^*", 0, False, True, False);

    // Constants,
    // Integer constant,
    NCC_addRule(ncc, "decimal-constant", "${non-zero-digit} ${digit}^*", 0, False, False, False);
    NCC_addRule(ncc, "octal-constant", "0 0-7^*", 0, False, False, False);
    NCC_addRule(ncc, "hexadecimal-constant", "${hexadecimal-prefix} ${hexadecimal-digit} ${hexadecimal-digit}^*", 0, False, False, False);
    NCC_addRule(ncc, "integer-suffix", "{ u|U l|L|{ll}|{LL}|${ε} } | { l|L|{ll}|{LL} u|U|${ε} }", 0, False, False, False);
    NCC_addRule(ncc, "integer-constant", "${decimal-constant}|${octal-constant}|${hexadecimal-constant} ${integer-suffix}|${ε}", 0, False, False, False);

    // Decimal floating point,
    NCC_addRule(ncc, "fractional-constant", "{${digit}^* . ${digit} ${digit}^*} | {${digit} ${digit}^* . }", 0, False, False, False);
    NCC_addRule(ncc, "exponent-part", "e|E +|\\-|${ε} ${digit} ${digit}^*", 0, False, False, False);
    NCC_addRule(ncc, "floating-suffix", "f|l|F|L", 0, False, False, False);
    NCC_addRule(ncc, "decimal-floating-constant",
                "{${fractional-constant} ${exponent-part}|${ε} ${floating-suffix}|${ε}} | "
                "{${digit} ${digit}^* ${exponent-part} ${floating-suffix}|${ε}}", 0, False, False, False);

    // Hexadecimal floating point,
    NCC_addRule(ncc, "hexadecimal-fractional-constant",
                "{${hexadecimal-digit}^* . ${hexadecimal-digit} ${hexadecimal-digit}^*} | "
                "{${hexadecimal-digit} ${hexadecimal-digit}^* . }", 0, False, False, False);
    NCC_addRule(ncc, "binary-exponent-part", "p|P +|\\-|${ε} ${digit} ${digit}^*", 0, False, False, False);
    NCC_addRule(ncc, "hexadecimal-floating-constant",
                "${hexadecimal-prefix} ${hexadecimal-fractional-constant}|{${hexadecimal-digit}${hexadecimal-digit}^*} ${binary-exponent-part} ${floating-suffix}|${ε}", 0, False, False, False);

    // Floating point constant,
    NCC_addRule(ncc, "floating-constant", "${decimal-floating-constant} | ${hexadecimal-floating-constant}", 0, False, False, False);

    // Enumeration constant,
    NCC_addRule(ncc, "enumeration-constant", "${identifier}", 0, False, False, False);

    // Character constant (supporting unknown escape sequences which are implementation defined. We'll pass the escaped character like gcc and clang do),
    NCC_addRule(ncc, "c-char", "\x01-\x09 | \x0b-\x5b | \x5d-\xff", 0, False, False, False); // All characters except new-line and backslash (\).
    NCC_addRule(ncc, "c-char-with-backslash-without-uUxX", "\x01-\x09 | \x0b-\x54 | \x56-\x57| \x59-\x74 | \x76-\x77 | \x79-\xff", 0, False, False, False); // All characters except new-line, 'u', 'U', 'x' and 'X'.
    NCC_addRule(ncc, "hexadecimal-escape-sequence", "\\\\x ${hexadecimal-digit} ${hexadecimal-digit}^*", 0, False, False, False);
    NCC_addRule(ncc, "character-constant", "L|u|U|${ε} ' { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* '", 0, False, False, False);

    // Constant,
    NCC_addRule(ncc, "constant", "${integer-constant} | ${floating-constant} | ${enumeration-constant} | ${character-constant}", 0, False, True, False);

    // String literal,
    // See: https://stackoverflow.com/a/13087264/1942069   and   https://stackoverflow.com/a/13445170/1942069
    NCC_addRule(ncc, "string-literal-contents", "{u8}|u|U|L|${ε} \" { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* \"", 0, False, False, False);
    NCC_addRule(ncc, "string-literal", "${string-literal-contents} {${} ${string-literal-contents}}|${ε}", 0, False, True, False);

    // =====================================
    // Phrase Structure,
    // =====================================

    // -------------------------------------
    // Expressions,
    // -------------------------------------

    // TODO: add ${} where needed...

    // Primary expression,
    NCC_addRule(ncc, "expression", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "generic-selection", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "primary-expression",
            "${identifier} | "
            "${constant} | "
            "${string-literal} | "
            "{ (${expression}) } | "
            "${generic-selection}", 0, False, True, False);

    // Generic selection,
    // See: https://www.geeksforgeeks.org/_generic-keyword-c/
    //#define INC(x) _Generic((x), long double: INCl, default: INC, float: INCf)(x)
    //NLOGE("", "%d\n", _Generic(1, int: 7, float:1, double:2, long double:3, default:0));
    NCC_addRule(ncc, "assignment-expression", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "generic-assoc-list", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "generic-selection", "_Generic ( ${assignment-expression}, ${generic-assoc-list} )", 0, False, False, False);

    // Generic assoc list,
    NCC_addRule(ncc, "generic-association", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "generic-assoc-list", "${generic-association} {, ${generic-association}}^*", 0, False, False, False);

    // Generic association,
    NCC_addRule(ncc, "type-name", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "generic-association",
            "{${type-name} : ${assignment-expression}} |"
            "{default : ${assignment-expression}}", 0, False, False, False);

    // Postfix expression,
    NCC_addRule(ncc, "argument-expression-list", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "initializer-list", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "postfix-expression-contents",
            "${primary-expression} | "
            "{ ( ${type-name} ) \\{ ${initializer-list}   \\} } | "
            "{ ( ${type-name} ) \\{ ${initializer-list} , \\} }", 0, False, False, False);
    NCC_addRule(ncc, "postfix-expression",
            "${postfix-expression-contents} {"
            "   {[${expression}]} | "
            "   {(${argument-expression-list}|${ε})} | "
            "   {.${identifier}} | "
            "   {\\-> ${identifier}} | "
            "   {++} | "
            "   {\\-\\-}"
            "}^*", 0, False, True, False);

    // Argument expression list,
    NCC_updateRule(ncc, "argument-expression-list", "${assignment-expression} {, ${assignment-expression}}^*", 0, False, False, False);

    // Unary expression,
    NCC_addRule(ncc, "unary-expression", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "unary-operator", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "cast-expression", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "unary-expression",
                "${postfix-expression} | "
                "{ ++     ${unary-expression} } | "
                "{ \\-\\- ${unary-expression} } | "
                "{ ${unary-operator} ${cast-expression} } | "
                "{   sizeof(${unary-expression}) } | "
                "{   sizeof(${type-name}) } | "
                "{ _Alignof(${type-name}) }", 0, False, True, False);

    // Unary operator,
    NCC_updateRule(ncc, "unary-operator", "& | \\* | + | \\- | ~ | !", 0, False, False, False);

    // Cast expression,
    NCC_updateRule(ncc, "cast-expression",
            "${unary-expression} | "
            "{ (${type-name}) ${} ${cast-expression} }", 0, False, True, False);

    // Multiplicative expression,
    NCC_addRule(ncc, "multiplicative-expression",
            "${cast-expression} {"
            "   { ${} \\* ${} ${cast-expression}} | "
            "   { ${}   / ${} ${cast-expression}} | "
            "   { ${}   % ${} ${cast-expression}}"
            "}^*", 0, False, True, False);

    // Additive expression,
    NCC_addRule(ncc, "additive-expression",
                "${multiplicative-expression} {"
                "   { ${}   + ${} ${multiplicative-expression}} | "
                "   { ${} \\- ${} ${multiplicative-expression}}"
                "}^*", 0, False, True, False);

    // Shift expression,
    NCC_addRule(ncc, "shift-expression",
                "${additive-expression} {"
                "   { ${} << ${} ${additive-expression}} | "
                "   { ${} >> ${} ${additive-expression}}"
                "}^*", 0, False, True, False);

    // Relational expression,
    NCC_addRule(ncc, "relational-expression",
                "${shift-expression} {"
                "   { ${} <  ${} ${shift-expression}} | "
                "   { ${} >  ${} ${shift-expression}} | "
                "   { ${} <= ${} ${shift-expression}} | "
                "   { ${} >= ${} ${shift-expression}}"
                "}^*", 0, False, True, False);

    // Equality expression,
    NCC_addRule(ncc, "equality-expression",
                "${relational-expression} {"
                "   { ${} == ${} ${relational-expression}} | "
                "   { ${} != ${} ${relational-expression}}"
                "}^*", 0, False, True, False);

    // AND expression,
    NCC_addRule(ncc, "and-expression",
                "${equality-expression} {"
                "   ${} & ${} ${equality-expression}"
                "}^*", 0, False, True, False);

    // Exclusive OR expression,
    NCC_addRule(ncc, "xor-expression",
                "${and-expression} {"
                "   ${} \\^ ${} ${and-expression}"
                "}^*", 0, False, True, False);

    // Inclusive OR expression,
    NCC_addRule(ncc, "or-expression",
                "${xor-expression} {"
                "   ${} \\| ${} ${xor-expression}"
                "}^*", 0, False, True, False);

    // Logical AND expression,
    NCC_addRule(ncc, "logical-and-expression",
                "${or-expression} {"
                "   ${} && ${} ${or-expression}"
                "}^*", 0, False, True, False);

    // Logical OR expression,
    NCC_addRule(ncc, "logical-or-expression",
                "${logical-and-expression} {"
                "   ${} \\|\\| ${} ${logical-and-expression}"
                "}^*", 0, False, True, False);

    // Conditional expression,
    NCC_addRule(ncc, "conditional-expression", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "conditional-expression",
                "${logical-or-expression} | "
                "{${logical-or-expression} ${} ? ${} ${expression} ${} : ${} ${conditional-expression}}", 0, False, True, False);

    // Assignment expression,
    NCC_addRule(ncc, "assignment-operator", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "assignment-expression",
                   "${conditional-expression} | "
                   "{${unary-expression} ${} ${assignment-operator} ${} ${assignment-expression}}", 0, False, True, False);

    // Assignment operator,
    NCC_updateRule(ncc, "assignment-operator", "= | {\\*=} | {/=} | {%=} | {+=} | {\\-=} | {<<=} | {>>=} | {&=} | {\\^=} | {\\|=}", 0, False, False, False);

    // Expression,
    NCC_updateRule(ncc, "expression",
                "${assignment-expression} {"
                "   ${} , ${} ${assignment-expression}"
                "}^*", 0, False, True, False);

    NCC_addRule(ncc, "constant-expression", "${conditional-expression}", 0, False, True, False);

    // -------------------------------------
    // Declarations,
    // -------------------------------------

    // Declaration,
    NCC_addRule(ncc, "declaration-specifiers", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "init-declarator-list", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "static_assert-declaration", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "declaration",
            "{${declaration-specifiers} ${} ${init-declarator-list}|${ε} ${} ;} | "
            "${static_assert-declaration}", 0, False, True, False);

    // Declaration specifiers,
    NCC_addRule(ncc, "storage-class-specifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "type-specifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "type-qualifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "function-specifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "alignment-specifier", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "declaration-specifiers",
            "${storage-class-specifier} | "
            "${type-specifier} | "
            "${type-qualifier} | "
            "${function-specifier} | "
            "${alignment-specifier} "
            "${} ${declaration-specifiers}|${ε}", 0, False, False, False);

    // Init declarator list,
    NCC_addRule(ncc, "init-declarator", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "init-declarator-list",
                   "${init-declarator} { "
                   "   ${} , ${} ${init-declarator}"
                   "}^*", 0, False, False, False);

    // Init declarator,
    NCC_addRule(ncc, "declarator", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "initializer", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "init-declarator-list",
                   "${declarator} ${} = ${} ${initializer}", 0, False, False, False);

    // Storage class specifier,
    NCC_updateRule(ncc, "storage-class-specifier",
                   "{typedef} | {extern} | {static} | {_Thread_local} | {auto} | {register}", 0, False, False, False);

    // Type specifier,
    NCC_addRule(ncc, "atomic-type-specifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "struct-or-union-specifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "enum-specifier", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "typedef-name", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "type-specifier",
                   "{void} | {char} | "
                   "{short} | {int} | {long} | "
                   "{float} | {double} | "
                   "{signed} | {unsigned} | "
                   "{_Bool} | {_Complex} | "
                   "${atomic-type-specifier} | "
                   "${struct-or-union-specifier} | "
                   "${enum-specifier} |"
                   "${typedef-name}", 0, False, False, False);

    // Struct or union specifier,
    NCC_addRule(ncc, "struct-or-union", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "struct-declaration-list", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "struct-or-union-specifier",
            "${struct-or-union} ${} "
            "{${identifier}|${ε} ${} ${struct-declaration-list}} | "
            " ${identifier}", 0, False, False, False);

    // Struct or union,
    NCC_updateRule(ncc, "struct-or-union",
            "{struct} | {union}", 0, False, False, False);

    // Struct declaration list,
    NCC_addRule(ncc, "struct-declaration", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "struct-declaration-list",
                   "${struct-declaration} { "
                   "   ${} ${struct-declaration}"
                   "}^*", 0, False, False, False);

    // Struct declaration,
    NCC_addRule(ncc, "specifier-qualifier-list", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "struct-declarator-list", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "struct-declaration",
                   "{${specifier-qualifier-list} ${} ${struct-declarator-list}|${ε} ;} | "
                   "${static_assert-declaration}", 0, False, False, False);

    // Specifier qualifier list,
    NCC_updateRule(ncc, "specifier-qualifier-list",
                   "${type-specifier} | ${type-qualifier} { "
                   "   ${} ${type-specifier} | ${type-qualifier}"
                   "}^*", 0, False, False, False);

    // Struct declarator list,
    NCC_addRule(ncc, "struct-declarator", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "struct-declarator-list",
                   "${struct-declarator} { "
                   "   ${} , ${} ${struct-declarator}"
                   "}^*", 0, False, False, False);

    // Struct declarator,
    NCC_updateRule(ncc, "struct-declarator",
                   " ${declarator} | "
                   "{${declarator}|${ε} ${} : ${} ${constant-expression}}", 0, False, False, False);

    // Enum specifier,
    NCC_addRule(ncc, "enumerator-list", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "enum-specifier",
                   "{ {enum} ${} ${identifier}|${ε} ${} \\{ ${enumerator-list} ${} ,|${ε} ${} \\} } | "
                   "{ {enum} ${} ${identifier} }", 0, False, False, False);

    // Enumerator list,
    NCC_addRule(ncc, "enumerator", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "enumerator-list",
                   "${enumerator} {"
                   "   ${} , ${} ${enumerator}"
                   "}^*", 0, False, False, False);

    // Enumerator,
    NCC_updateRule(ncc, "enumerator",
                   "${enumeration-constant} { ${} = ${} ${constant-expression} }|${ε}", 0, False, False, False);

    // Atomic type specifier,
    NCC_updateRule(ncc, "atomic-type-specifier",
                   "{_Atomic} ${} ( ${} ${type-name} ${} )", 0, False, False, False);

    // Type qualifier,
    NCC_updateRule(ncc, "type-qualifier",
                   "{const} | {restrict} | {volatile} | {_Atomic}", 0, False, False, False);

    // Function specifier,
    NCC_updateRule(ncc, "function-specifier",
                   "{inline} | {_Noreturn}", 0, False, False, False);

    // Alignment specifier,
    NCC_updateRule(ncc, "alignment-specifier",
                   "{_Alineas} ${} ( ${} ${type-name}|${constant-expression} ${} )", 0, False, False, False);

    // Declarator,
    NCC_addRule(ncc, "pointer", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "direct-declarator", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "declarator",
                   "${pointer}|${ε} ${} ${direct-declarator}", 0, False, False, False);

    // Direct declarator,
    NCC_addRule(ncc, "type-qualifier-list", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "parameter-type-list", "STUB!", 0, False, False, False);
    NCC_addRule(ncc, "identifier-list", "STUB!", 0, False, False, False);
    NCC_updateRule(ncc, "direct-declarator",
                   "{${identifier} | {(${} ${declarator} ${})}} {"
                   "   { ${} [ ${}              ${type-qualifier-list}|${ε} ${}              ${assignment-expression}|${ε} ${} ]} | "
                   "   { ${} [ ${} {static} ${} ${type-qualifier-list}|${ε} ${}              ${assignment-expression}      ${} ]} | "
                   "   { ${} [ ${}              ${type-qualifier-list}      ${} {static} ${} ${assignment-expression}      ${} ]} | "
                   "   { ${} [ ${}              ${type-qualifier-list}|${ε} ${}      \\* ${}                                   ]} | "
                   "   { ${} ( ${} ${parameter-type-list}  ${} )} | "
                   "   { ${} ( ${} ${identifier-list}|${ε} ${} )}"
                   "}^*", 0, False, False, False);

    // Pointer,
    // ...xxx

    // Document,
    NCC_addRule(ncc, "testDocument",
            "${primary-expression}        | "
            "${postfix-expression}        | "
            "${unary-expression}          | "
            "${cast-expression}           | "
            "${multiplicative-expression} | "
            "${additive-expression}       | "
            "${shift-expression}          | "
            "${relational-expression}     | "
            "${equality-expression}       | "
            "${and-expression}            | "
            "${xor-expression}            | "
            "${or-expression}             | "
            "${logical-and-expression}    | "
            "${logical-or-expression}     | "
            "${conditional-expression}    | "
            "${assignment-expression}     | "
            "${expression}                | "
            "${constant-expression}"
            , printListener, True, False, False);

}

static void test(struct NCC* ncc, const char* code) {

    NLOGI("", "%sTesting: %s%s", NTCOLOR(GREEN_BRIGHT), NTCOLOR(HIGHLIGHT), code);
    int32_t matchLength = NCC_match(ncc, code);
    int32_t codeLength = NCString.length(code);
    if (matchLength == codeLength) {
        NLOGI("test()", "Success!");
    } else {
        NERROR("test()", "Failed! MatchLength: %d", matchLength);
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

    #endif

    // Clean up,
    NCC_destroyNCC(&ncc);
    NError.logAndTerminate();
}
