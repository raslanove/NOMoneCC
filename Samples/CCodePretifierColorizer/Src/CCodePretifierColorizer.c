#include <NSystemUtils.h>
#include <NError.h>
#include <NCString.h>

#include <NCC.h>

void printListener(struct NCC* ncc, struct NString* ruleName, int32_t variablesCount) {
    NLOGI("C", "ruleName: %s, variablesCount: %d", NString.get(ruleName), variablesCount);
    struct NCC_Variable variable;
    while (NCC_popRuleVariable(ncc, &variable)) {
        NLOGI("C", "            Name: %s%s%s, Value: %s%s%s", NTCOLOR(HIGHLIGHT), variable.name, NTCOLOR(STREAM_DEFAULT), NTCOLOR(HIGHLIGHT), NString.get(&variable.value), NTCOLOR(STREAM_DEFAULT));
        NCC_destroyVariable(&variable);
    }
}

void NMain() {

    NSystemUtils.logI("C", "besm Allah :)\n");

    const char* code =
            "\"abcdef\\xf\\\"f\\\\ghi\\\"\"";

    // Substitute,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // =====================================
    // Lexical rules,
    // =====================================

    // Common,
    NCC_addRule(&ncc, "ε", "", 0, False, False, False);
    NCC_addRule(&ncc, "digit", "0-9", 0, False, False, False);
    NCC_addRule(&ncc, "non-zero-digit", "1-9", 0, False, False, False);
    NCC_addRule(&ncc, "non-digit", "_|a-z|A-Z", 0, False, False, False);
    NCC_addRule(&ncc, "hexadecimal-prefix", "0x|X", 0, False, False, False);
    NCC_addRule(&ncc, "hexadecimal-digit", "0-9|a-f|A-F", 0, False, False, False);
    NCC_addRule(&ncc, "hex-quad", "${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}${hexadecimal-digit}", 0, False, False, False);
    NCC_addRule(&ncc, "universal-character-name", "{\\\\u ${hex-quad}} | {\\\\U ${hex-quad} ${hex-quad}}", 0, False, False, False);

    // Identifier,
    NCC_addRule(&ncc, "identifier-non-digit", "${non-digit} | ${universal-character-name}", 0, False, False, False);
    NCC_addRule(&ncc, "identifier", "${identifier-non-digit} {${digit} | ${identifier-non-digit}}^*", 0, False, True, False);

    // Constants,
    // Integer constant,
    NCC_addRule(&ncc, "decimal-constant", "${non-zero-digit} ${digit}^*", 0, False, False, False);
    NCC_addRule(&ncc, "octal-constant", "0 0-7^*", 0, False, False, False);
    NCC_addRule(&ncc, "hexadecimal-constant", "${hexadecimal-prefix} ${hexadecimal-digit} ${hexadecimal-digit}^*", 0, False, False, False);
    NCC_addRule(&ncc, "integer-suffix", "{ u|U l|L|{ll}|{LL}|${ε} } | { l|L|{ll}|{LL} u|U|${ε} }", 0, False, False, False);
    NCC_addRule(&ncc, "integer-constant", "${decimal-constant}|${octal-constant}|${hexadecimal-constant} ${integer-suffix}|${ε}", 0, False, True, False);

    // Decimal floating point,
    NCC_addRule(&ncc, "fractional-constant", "{${digit}^* . ${digit} ${digit}^*} | {${digit} ${digit}^* . ${digit}^*}", 0, False, False, False);
    NCC_addRule(&ncc, "exponent-part", "e|E +|\\-|${ε} ${digit} ${digit}^*", 0, False, False, False);
    NCC_addRule(&ncc, "floating-suffix", "f|l|F|L", 0, False, False, False);
    NCC_addRule(&ncc, "decimal-floating-constant",
            "{${fractional-constant} ${exponent-part}|${ε} ${floating-suffix}|${ε}} | "
            "{${digit} ${digit}^* ${exponent-part} ${floating-suffix}|${ε}}", 0, False, False, False);

    // Hexadecimal floating point,
    NCC_addRule(&ncc, "hexadecimal-fractional-constant",
            "{${hexadecimal-digit}^* . ${hexadecimal-digit} ${hexadecimal-digit}^*} | "
            "{${hexadecimal-digit} ${hexadecimal-digit}^* . ${hexadecimal-digit}^*}", 0, False, False, False);
    NCC_addRule(&ncc, "binary-exponent-part", "p|P +|\\-|${ε} ${digit} ${digit}^*", 0, False, False, False);
    NCC_addRule(&ncc, "hexadecimal-floating-constant",
                "${hexadecimal-prefix} ${hexadecimal-fractional-constant}|{${hexadecimal-digit}${hexadecimal-digit}^*} ${binary-exponent-part} ${floating-suffix}|${ε}", 0, False, False, False);

    // Floating point constant,
    NCC_addRule(&ncc, "floating-constant", "${decimal-floating-constant} | ${hexadecimal-floating-constant}", 0, False, True, False);

    // Character constant (supporting unknown escape sequences which are implementation defined. We'll pass the escaped character like gcc and clang do),
    NCC_addRule(&ncc, "c-char", "\x01-\x09 | \x0b-\x5b | \x5d-\xff", 0, False, False, False); // All characters except new-line and backslash (\).
    NCC_addRule(&ncc, "c-char-with-backslash-without-uUxX", "\x01-\x09 | \x0b-\x54 | \x56-\x57| \x59-\x74 | \x76-\x77 | \x79-\xff", 0, False, False, False); // All characters except new-line, 'u', 'U', 'x' and 'X'.
    NCC_addRule(&ncc, "hexadecimal-escape-sequence", "\\\\x ${hexadecimal-digit} ${hexadecimal-digit}^*", 0, False, False, False);
    NCC_addRule(&ncc, "character-constant", "L|u|U|${ε} ' { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* '", 0, False, True, False); // TODO: add hex escape....

    // String literal,
    // See: https://stackoverflow.com/a/13087264/1942069   and   https://stackoverflow.com/a/13445170/1942069
    NCC_addRule(&ncc, "string-literal", "{u8}|u|U|L|${ε} \" { ${c-char}|${hexadecimal-escape-sequence}|${universal-character-name}|{\\\\${c-char-with-backslash-without-uUxX}} }^* \"", 0, False, True, False); // TODO: add hex escape...

    // Document,
    NCC_addRule(&ncc, "testDocument", "${identifier} | ${integer-constant} | ${floating-constant} | ${character-constant} | ${string-literal}", printListener, True, False, False);

    // Match and cleanup,
    int32_t matchLength = NCC_match(&ncc, code);
    int32_t codeLength = NCString.length(code);
    NCC_destroyNCC(&ncc);

    NLOGI("", "");

    if (matchLength == codeLength) {
        NLOGI("C", "Success!");
    } else {
        NLOGE("C", "Failed! MatchLength: %d", matchLength);
    }

    NLOGI("", "");
    NError.logAndTerminate();
}
