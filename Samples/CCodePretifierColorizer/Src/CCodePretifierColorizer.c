#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>

#include <NCC.h>

#define TEST_EXPRESSIONS  0
#define TEST_DECLARATIONS 0
#define TEST_STATEMENTS   1

void printListener(struct NCC_MatchingData* matchingData) {
    NLOGI("", "ruleName: %s, variablesCount: %d", NString.get(&matchingData->ruleData->ruleName), matchingData->variablesCount);
    struct NCC_Variable variable;
    while (NCC_popRuleVariable(matchingData->ruleData->ncc, &variable)) {
        NLOGI("", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), variable.name, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
}

void definePreprocessing(struct NCC* ncc) {

    // =====================================
    // Preprocessing directives,
    // =====================================

    // Header name,
    //NCC_addRule(ncc, "h-char", "\x01-\x09 | \x0b-\xff", 0, False, False, False); // All characters except new-line.
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

    struct NCC_RuleData plainRuleData, pushingRuleData, rootRuleData;
    NCC_initializeRuleData(&  plainRuleData, ncc, "", "", 0, 0,             0, False, False, False);
    NCC_initializeRuleData(&pushingRuleData, ncc, "", "", 0, 0,             0, False,  True, False);
    NCC_initializeRuleData(&   rootRuleData, ncc, "", "", 0, 0, printListener,  True, False, False);

    // =====================================
    // Lexical rules,
    // =====================================

    // Common,
    NCC_addRule(plainRuleData.set(&plainRuleData, "ε", ""));
    // TODO: Why do we have a \\\\n term?!
    NCC_addRule(plainRuleData.set(&plainRuleData, "white-space", "{\\ |\t|\r|\n|{\\\\\n}}^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "line-end", "\n|${ε}"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "line-comment", "// * ${line-end}|{\\\\${line-end}}"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "block-comment", "/\\* * \\*/"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "", "{${white-space}|${line-comment}|${block-comment}}^*"));

    NCC_addRule(plainRuleData.set(&plainRuleData, "digit", "0-9"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "non-zero-digit", "1-9"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "non-digit", "_|a-z|A-Z"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "hexadecimal-prefix", "0x|X"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "hexadecimal-digit", "0-9|a-f|A-F"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "hex-quad", "${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "universal-character-name", "{\\\\u ${hex-quad}} | {\\\\U ${hex-quad} ${hex-quad}}"));

    // Identifier,
    NCC_addRule(  plainRuleData.set(&  plainRuleData, "identifier-non-digit", "${non-digit} | ${universal-character-name}"));
    NCC_addRule(pushingRuleData.set(&pushingRuleData, "identifier", "${identifier-non-digit} {${digit} | ${identifier-non-digit}}^*"));

    // Constants,
    // Integer constant,
    NCC_addRule(plainRuleData.set(&plainRuleData, "decimal-constant", "${non-zero-digit} ${digit}^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "octal-constant", "0 0-7^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "hexadecimal-constant", "${hexadecimal-prefix} ${hexadecimal-digit} ${hexadecimal-digit}^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "integer-suffix", "{ u|U l|L|{ll}|{LL}|${ε} } | { l|L|{ll}|{LL} u|U|${ε} }"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "integer-constant", "${decimal-constant}|${octal-constant}|${hexadecimal-constant} ${integer-suffix}|${ε}"));

    // Decimal floating point,
    NCC_addRule(plainRuleData.set(&plainRuleData, "fractional-constant", "{${digit}^* . ${digit} ${digit}^*} | {${digit} ${digit}^* . }"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "exponent-part", "e|E +|\\-|${ε} ${digit} ${digit}^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "floating-suffix", "f|l|F|L"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "decimal-floating-constant",
                "{${fractional-constant} ${exponent-part}|${ε} ${floating-suffix}|${ε}} | "
                "{${digit} ${digit}^* ${exponent-part} ${floating-suffix}|${ε}}"));

    // Hexadecimal floating point,
    NCC_addRule(plainRuleData.set(&plainRuleData, "hexadecimal-fractional-constant",
                "{${hexadecimal-digit}^* . ${hexadecimal-digit} ${hexadecimal-digit}^*} | "
                "{${hexadecimal-digit} ${hexadecimal-digit}^* . }"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "binary-exponent-part", "p|P +|\\-|${ε} ${digit} ${digit}^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "hexadecimal-floating-constant",
                "${hexadecimal-prefix} ${hexadecimal-fractional-constant}|{${hexadecimal-digit}${hexadecimal-digit}^*} ${binary-exponent-part} ${floating-suffix}|${ε}"));

    // Floating point constant,
    NCC_addRule(plainRuleData.set(&plainRuleData, "floating-constant", "${decimal-floating-constant} | ${hexadecimal-floating-constant}"));

    // Enumeration constant,
    NCC_addRule(plainRuleData.set(&plainRuleData, "enumeration-constant", "${identifier}"));

    // Character constant (supporting unknown escape sequences which are implementation defined. We'll pass the escaped character like gcc and clang do),
    NCC_addRule(plainRuleData.set(&plainRuleData, "c-char", "\x01-\x09 | \x0b-\x5b | \x5d-\xff")); // All characters except new-line and backslash (\).
    NCC_addRule(plainRuleData.set(&plainRuleData, "c-char-with-backslash-without-uUxX", "\x01-\x09 | \x0b-\x54 | \x56-\x57| \x59-\x74 | \x76-\x77 | \x79-\xff")); // All characters except new-line, 'u', 'U', 'x' and 'X'.
    NCC_addRule(plainRuleData.set(&plainRuleData, "hexadecimal-escape-sequence", "\\\\x ${hexadecimal-digit} ${hexadecimal-digit}^*"));
    NCC_addRule(plainRuleData.set(&plainRuleData, "character-constant", "L|u|U|${ε} ' { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* '"));

    // Constant,
    NCC_addRule(pushingRuleData.set(&pushingRuleData, "constant", "${integer-constant} | ${floating-constant} | ${enumeration-constant} | ${character-constant}"));

    // String literal,
    // See: https://stackoverflow.com/a/13087264/1942069   and   https://stackoverflow.com/a/13445170/1942069
    NCC_addRule(  plainRuleData.set(&  plainRuleData, "string-literal-contents", "{u8}|u|U|L|${ε} \" { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* \""));
    NCC_addRule(pushingRuleData.set(&pushingRuleData, "string-literal", "${string-literal-contents} {${} ${string-literal-contents}}|${ε}"));

    // =====================================
    // Phrase structure,
    // =====================================

    // -------------------------------------
    // Expressions,
    // -------------------------------------

    // Primary expression,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "expression", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "generic-selection", "STUB!"));
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "primary-expression",
                "${identifier} | "
                "${constant} | "
                "${string-literal} | "
                "{ ( ${} ${expression} ${} ) } | "
                "${generic-selection}"));

    // Generic selection,
    // See: https://www.geeksforgeeks.org/_generic-keyword-c/
    //#define INC(x) _Generic((x), long double: INCl, default: INC, float: INCf)(x)
    //NLOGE("", "%d\n", _Generic(1, int: 7, float:1, double:2, long double:3, default:0));
    NCC_addRule   (plainRuleData.set(&plainRuleData, "assignment-expression", "STUB!"));
    NCC_addRule   (plainRuleData.set(&plainRuleData, "generic-assoc-list", "STUB!"));
    NCC_updateRule(plainRuleData.set(&plainRuleData, "generic-selection",
                "_Generic ${} ( ${} ${assignment-expression} ${} , ${} ${generic-assoc-list} ${} )"));

    // Generic assoc list,
    NCC_addRule   (plainRuleData.set(&plainRuleData, "generic-association", "STUB!"));
    NCC_updateRule(plainRuleData.set(&plainRuleData, "generic-assoc-list",
                "${generic-association} {"
                "   ${} , ${} ${generic-association}"
                "}^*"));

    // Generic association,
    NCC_addRule   (plainRuleData.set(&plainRuleData, "type-name", "STUB!"));
    NCC_updateRule(plainRuleData.set(&plainRuleData, "generic-association",
                "{${type-name} ${} : ${} ${assignment-expression}} |"
                "{default      ${} : ${} ${assignment-expression}}"));

    // Postfix expression,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "argument-expression-list", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "initializer-list", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "postfix-expression-contents",
                "${primary-expression} | "
                "{ ( ${} ${type-name} ${} ) ${} \\{ ${} ${initializer-list} ${} ,|${ε} ${} \\} }"));
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "postfix-expression",
                "${postfix-expression-contents} {"
                "   {${} [ ${} ${expression} ${} ]} | "
                "   {${} ( ${} ${argument-expression-list}|${ε} ${} )} | "
                "   {${} .     ${} ${identifier}} | "
                "   {${} \\->  ${} ${identifier}} | "
                "   {${} ++} | "
                "   {${} \\-\\-}"
                "}^*"));

    // Argument expression list,
    NCC_updateRule(plainRuleData.set(&plainRuleData, "argument-expression-list",
                "${assignment-expression} {"
                "   ${} , ${} ${assignment-expression}"
                "}^*"));

    // Unary expression,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "unary-expression", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "unary-operator", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "cast-expression", "STUB!"));
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "unary-expression",
                "${postfix-expression} | "
                "{ ++     ${} ${unary-expression} } | "
                "{ \\-\\- ${} ${unary-expression} } | "
                "{ ${unary-operator} ${} ${cast-expression} } | "
                "{   sizeof ${} ( ${} ${unary-expression} ${} ) } | "
                "{   sizeof ${} ( ${} ${type-name}        ${} ) } | "
                "{ _Alignof ${} ( ${} ${type-name}        ${} ) }"));

    // Unary operator,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "unary-operator", "& | \\* | + | \\- | ~ | !"));

    // Cast expression,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "cast-expression",
                "${unary-expression} | "
                "{ ( ${} ${type-name} ${} ) ${} ${cast-expression} }"));

    // Multiplicative expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "multiplicative-expression",
                "${cast-expression} {"
                "   { ${} \\* ${} ${cast-expression}} | "
                "   { ${}   / ${} ${cast-expression}} | "
                "   { ${}   % ${} ${cast-expression}}"
                "}^*"));

    // Additive expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "additive-expression",
                "${multiplicative-expression} {"
                "   { ${}   + ${} ${multiplicative-expression}} | "
                "   { ${} \\- ${} ${multiplicative-expression}}"
                "}^*"));

    // Shift expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "shift-expression",
                "${additive-expression} {"
                "   { ${} << ${} ${additive-expression}} | "
                "   { ${} >> ${} ${additive-expression}}"
                "}^*"));

    // Relational expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "relational-expression",
                "${shift-expression} {"
                "   { ${} <  ${} ${shift-expression}} | "
                "   { ${} >  ${} ${shift-expression}} | "
                "   { ${} <= ${} ${shift-expression}} | "
                "   { ${} >= ${} ${shift-expression}}"
                "}^*"));

    // Equality expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "equality-expression",
                "${relational-expression} {"
                "   { ${} == ${} ${relational-expression}} | "
                "   { ${} != ${} ${relational-expression}}"
                "}^*"));

    // AND expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "and-expression",
                "${equality-expression} {"
                "   ${} & ${} ${equality-expression}"
                "}^*"));

    // Exclusive OR expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "xor-expression",
                "${and-expression} {"
                "   ${} \\^ ${} ${and-expression}"
                "}^*"));

    // Inclusive OR expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "or-expression",
                "${xor-expression} {"
                "   ${} \\| ${} ${xor-expression}"
                "}^*"));

    // Logical AND expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "logical-and-expression",
                "${or-expression} {"
                "   ${} && ${} ${or-expression}"
                "}^*"));

    // Logical OR expression,
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "logical-or-expression",
                "${logical-and-expression} {"
                "   ${} \\|\\| ${} ${logical-and-expression}"
                "}^*"));

    // Conditional expression,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "conditional-expression", "STUB!"));
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "conditional-expression",
                "${logical-or-expression} | "
                "{${logical-or-expression} ${} ? ${} ${expression} ${} : ${} ${conditional-expression}}"));

    // Assignment expression,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "assignment-operator", "STUB!"));
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "assignment-expression",
                   "${conditional-expression} | "
                   "{${unary-expression} ${} ${assignment-operator} ${} ${assignment-expression}}"));

    // Assignment operator,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "assignment-operator", "= | {\\*=} | {/=} | {%=} | {+=} | {\\-=} | {<<=} | {>>=} | {&=} | {\\^=} | {\\|=}"));

    // Expression,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "expression",
                "${assignment-expression} {"
                "   ${} , ${} ${assignment-expression}"
                "}^*"));

    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "constant-expression", "${conditional-expression}"));

    // -------------------------------------
    // Declarations,
    // -------------------------------------

    // Declaration,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "declaration-specifiers", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "init-declarator-list", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "static_assert-declaration", "STUB!"));
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "declaration",
                   "{${declaration-specifiers} ${} ${init-declarator-list}|${ε} ${} ;} | "
                   "${static_assert-declaration}"));

    // Declaration specifiers,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "storage-class-specifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "type-specifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "type-qualifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "function-specifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "alignment-specifier", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "declaration-specifiers",
                   "${storage-class-specifier} | "
                   "${type-specifier} | "
                   "${type-qualifier} | "
                   "${function-specifier} | "
                   "${alignment-specifier} "
                   "${} ${declaration-specifiers}|${ε}"));

    // Init declarator list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "init-declarator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "init-declarator-list",
                   "${init-declarator} { "
                   "   ${} , ${} ${init-declarator}"
                   "}^*"));

    // Init declarator,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "declarator", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "initializer", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "init-declarator",
                   "${declarator} {${} = ${} ${initializer}}|${ε}"));

    // Storage class specifier,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "storage-class-specifier",
                   "{typedef} | {extern} | {static} | {_Thread_local} | {auto} | {register}"));

    // Type specifier,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "atomic-type-specifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "struct-or-union-specifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "enum-specifier", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "typedef-name", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "type-specifier",
                   // TODO: enable typedef when NCC is ready to handle it...
                   /*
                   "{void} | {char} | "
                   "{short} | {int} | {long} | "
                   "{float} | {double} | "
                   "{signed} | {unsigned} | "
                   "{_Bool} | {_Complex} | "
                   "${atomic-type-specifier} | "
                   "${struct-or-union-specifier} | "
                   "${enum-specifier} |"
                   "${typedef-name}"));
                    */
                   "{void} | {char} | "
                   "{short} | {int} | {long} | "
                   "{float} | {double} | "
                   "{signed} | {unsigned} | "
                   "{_Bool} | {_Complex} | "
                   "${atomic-type-specifier} | "
                   "${struct-or-union-specifier} | "
                   "${enum-specifier}"));

    // Struct or union specifier,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "struct-or-union", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "struct-declaration-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "struct-or-union-specifier",
                   "${struct-or-union} ${} "
                   "{${identifier}|${ε} ${} \\{ ${} ${struct-declaration-list} ${} \\}} | "
                   " ${identifier}"));

    // Struct or union,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "struct-or-union",
            "{struct} | {union}"));

    // Struct declaration list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "struct-declaration", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "struct-declaration-list",
                   "${struct-declaration} { "
                   "   ${} ${struct-declaration}"
                   "}^*"));

    // Struct declaration,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "specifier-qualifier-list", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "struct-declarator-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "struct-declaration",
                   "{${specifier-qualifier-list} ${} ${struct-declarator-list}|${ε} ${} ;} | "
                   "${static_assert-declaration}"));

    // Specifier qualifier list,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "specifier-qualifier-list",
                   "${type-specifier} | ${type-qualifier} { "
                   "   ${} ${type-specifier} | ${type-qualifier}"
                   "}^*"));

    // Struct declarator list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "struct-declarator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "struct-declarator-list",
                   "${struct-declarator} { "
                   "   ${} , ${} ${struct-declarator}"
                   "}^*"));

    // Struct declarator,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "struct-declarator",
                   " ${declarator} | "
                   "{${declarator}|${ε} ${} : ${} ${constant-expression}}"));

    // Enum specifier,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "enumerator-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "enum-specifier",
                   "{ enum ${} ${identifier}|${ε} ${} \\{ ${enumerator-list} ${} ,|${ε} ${} \\} } | "
                   "{ enum ${} ${identifier} }"));

    // Enumerator list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "enumerator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "enumerator-list",
                   "${enumerator} {"
                   "   ${} , ${} ${enumerator}"
                   "}^*"));

    // Enumerator,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "enumerator",
                   "${enumeration-constant} { ${} = ${} ${constant-expression} }|${ε}"));

    // Atomic type specifier,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "atomic-type-specifier",
                   "_Atomic ${} ( ${} ${type-name} ${} )"));

    // Type qualifier,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "type-qualifier",
                   "{const} | {restrict} | {volatile} | {_Atomic}"));

    // Function specifier,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "function-specifier",
                   "{inline} | {_Noreturn}"));

    // Alignment specifier,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "alignment-specifier",
                   "_Alineas ${} ( ${} ${type-name}|${constant-expression} ${} )"));

    // Declarator,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "pointer", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "direct-declarator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "declarator",
                   "${pointer}|${ε} ${} ${direct-declarator}"));

    // Direct declarator,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "type-qualifier-list", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "parameter-type-list", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "identifier-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "direct-declarator",
                   "{${identifier} | {(${} ${declarator} ${})}} {"
                   "   { ${} [ ${}              ${type-qualifier-list}|${ε} ${}              ${assignment-expression}|${ε} ${} ]} | "
                   "   { ${} [ ${} static ${}   ${type-qualifier-list}|${ε} ${}              ${assignment-expression}      ${} ]} | "
                   "   { ${} [ ${}              ${type-qualifier-list}      ${} static   ${} ${assignment-expression}      ${} ]} | "
                   "   { ${} [ ${}              ${type-qualifier-list}|${ε} ${} \\*      ${}                                   ]} | "
                   "   { ${} ( ${} ${parameter-type-list}  ${} )} | "
                   "   { ${} ( ${} ${identifier-list}|${ε} ${} )}"
                   "}^*"));

    // Pointer,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "pointer",
                   "\\* ${} ${type-qualifier-list}|${ε} ${} ${pointer}|${ε}"));

    // Type qualifier list,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "type-qualifier-list",
                   "${type-qualifier} {"
                   "   ${} ${type-qualifier}"
                   "}^*"));

    // Parameter type list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "parameter-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "parameter-type-list",
                   "${parameter-list} {${} , ${} ...}|${ε}"));

    // Parameter list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "parameter-declaration", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "parameter-list",
                   "${parameter-declaration} {"
                   "   ${} , ${} ${parameter-declaration}"
                   "}^*"));

    // Parameter declaration,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "abstract-declarator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "parameter-declaration",
                   "${declaration-specifiers} ${} ${declarator}|${abstract-declarator}|${ε}"));

    // Identifier list,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "identifier-list",
                   "${identifier} {"
                   "   ${} , ${} ${identifier}"
                   "}^*"));

    // Type name,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "type-name",
                   "${specifier-qualifier-list} ${} ${abstract-declarator}|${ε}"));

    // Abstract declarator,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "direct-abstract-declarator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "abstract-declarator",
                   "${pointer} | "
                   "{ ${pointer}|${ε} ${} ${direct-abstract-declarator} }"));

    // Direct abstract declarator,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "direct-abstract-declarator-content",
                   "{(${} ${abstract-declarator} ${})} | "
                   "{[${}              ${type-qualifier-list}|${ε} ${}              ${assignment-expression}|${ε} ${}]} | "
                   "{[${} static ${}   ${type-qualifier-list}|${ε} ${}              ${assignment-expression}      ${}]} | "
                   "{[${}              ${type-qualifier-list}      ${} static ${}   ${assignment-expression}      ${}]} | "
                   "{[${} \\*    ${}                                                                                 ]} | "
                   "{(${} ${parameter-type-list}|${ε} ${})}"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "direct-abstract-declarator",
                   "${direct-abstract-declarator-content} {"
                   "   ${} ${direct-abstract-declarator-content}"
                   "}^*"));

    // Typedef name,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "typedef-name",
                   "${identifier}"));

    // Initializer,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "initializer",
                   "${assignment-expression} | "
                   "{ \\{ ${} ${initializer-list} ${} ,|${ε} ${} \\} }"));

    // Initializer list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "designation", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "initializer-list-content",
                   "${designation}|${ε} ${} ${initializer}"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "initializer-list",
                   "${initializer-list-content} {"
                   "   ${} , ${} ${initializer-list-content}"
                   "}^*"));

    // Designation,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "designator-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "designation",
                   "${designator-list} ${} ="));

    // Designator list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "designator", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "designator-list",
                   "${designator} {"
                   "   ${} ${designator}"
                   "}^*"));

    // Designator,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "designator",
                   "{[${} ${constant-expression} ${}]} | "
                   "{ . ${} ${identifier}}"));

    // static_assert declaration,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "static_assert-declaration",
                   "_Static_assert ${} ( ${} ${constant-expression} ${} , ${} ${string-literal} ${} ) ${} ;"));

    // -------------------------------------
    // Statements,
    // -------------------------------------

    // Statement,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData,    "labeled-statement", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData,   "compound-statement", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "expression-statement", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData,  "selection-statement", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData,  "iteration-statement", "STUB!"));
    NCC_addRule   (  plainRuleData.set(&  plainRuleData,       "jump-statement", "STUB!"));
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "statement",
                   "   ${labeled-statement} | "
                   "  ${compound-statement} | "
                   "${expression-statement} | "
                   " ${selection-statement} | "
                   " ${iteration-statement} | "
                   "      ${jump-statement}"));

    // Labeled statement,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "labeled-statement",
                   "{${identifier}                   ${} : ${} ${statement}} | "
                   "{case ${} ${constant-expression} ${} : ${} ${statement}} | "
                   "{default                         ${} : ${} ${statement}}"));

    // Compound statement,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "block-item-list", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "compound-statement",
                   "\\{ ${} ${block-item-list}|${ε} ${} \\}"));

    // Block item list,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "block-item", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "block-item-list",
                   "${block-item} {"
                   "   ${} ${block-item}"
                   "}^*"));

    // Block item,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "block-item",
                   "${declaration} | ${statement}"));

    // Expression statement,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "expression-statement",
                   "${expression}|${ε} ${} ;"));

    // Selection statement,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "selection-statement",
                   "{ if     ${} ( ${} ${expression} ${} ) ${} ${statement} {${} else ${} ${statement}}|${ε} } | "
                   "{ switch ${} ( ${} ${expression} ${} ) ${} ${statement}                                  }"));

    // Iteration statement,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "iteration-statement",
                   "{ while ${}                        ( ${} ${expression} ${} ) ${} ${statement} } | "
                   "{ do    ${} ${statement} ${} while ( ${} ${expression} ${} ) ${} ;            } | "
                   "{ for   ${} ( ${}                    ${expression}|${ε} ${} ; ${} ${expression}|${ε} ${} ; ${} ${expression}|${ε} ${} ) ${} ${statement} } | "
                   "{ for   ${} ( ${} ${declaration} ${} ${expression}|${ε} ${} ;                              ${} ${expression}|${ε} ${} ) ${} ${statement} }"));

    // Jump statement,
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "jump-statement",
                   "{ goto     ${} ${identifier}      ${} ; } | "
                   "{ continue ${}                        ; } | "
                   "{ break    ${}                        ; } | "
                   "{ return   ${} ${expression}|${ε} ${} ; }"));

    // -------------------------------------
    // External definitions,
    // -------------------------------------

    // Translation unit,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "external-declaration", "STUB!"));
    NCC_addRule   (pushingRuleData.set(&pushingRuleData, "translation-unit",
                   "${} ${external-declaration} {{"
                   "   ${} ${external-declaration}"
                   "}^*} ${}")); // Encapsulated the repeat in a sub-rule to avoid early termination. Can
                                 // we consider early termination a feature now?

    // External declaration,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "function-definition", "STUB!"));
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "external-declaration",
                   "${function-definition} | ${declaration}"));

    // Function definition,
    NCC_addRule   (  plainRuleData.set(&  plainRuleData, "declaration-list", "STUB!"));
    NCC_updateRule(pushingRuleData.set(&pushingRuleData, "function-definition",
                   "${declaration-specifiers} ${} ${declarator} ${} ${declaration-list}|${ε} ${} ${compound-statement}"));

    // Declaration list,
    NCC_updateRule(  plainRuleData.set(&  plainRuleData, "declaration-list",
                   "${declaration} {"
                   "   ${} ${declaration}"
                   "}^*"));

    // Test document,
    NCC_addRule   (   rootRuleData.set(&   rootRuleData, "testDocument",
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
            "${constant-expression}       | "
            "${declaration}               | "
            "${translation-unit}"));

    // Cleanup,
    NCC_destroyRuleData(&  plainRuleData);
    NCC_destroyRuleData(&pushingRuleData);
    NCC_destroyRuleData(&   rootRuleData);
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

    // Clean up,
    NCC_destroyNCC(&ncc);
    NError.logAndTerminate();
}
