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
            "0x8.8p+2L";

    //float a = .2e-2f;
    //float a = 0x8.8p+2;
    //NLOGW("sdf", "A is %f", a);

    // Substitute,
    struct NCC ncc;
    NCC_initializeNCC(&ncc);

    // Common,
    NCC_addRule(&ncc, "ε", "", 0, False, False, False);
    NCC_addRule(&ncc, "digit", "0-9", 0, False, False, False);
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
    NCC_addRule(&ncc, "decimal-constant", "1-9 0-9^*", 0, False, False, False);
    NCC_addRule(&ncc, "octal-constant", "0 0-7^*", 0, False, False, False);
    NCC_addRule(&ncc, "hexadecimal-constant", "${hexadecimal-prefix} ${hexadecimal-digit} ${hexadecimal-digit}^*", 0, False, False, False);
    NCC_addRule(&ncc, "integer-suffix", "{ u|U l|L|{ll}|{LL}|${ε} } | { l|L|{ll}|{LL} u|U|${ε} }", 0, False, False, False);
    NCC_addRule(&ncc, "integer-constant", "${decimal-constant}|${octal-constant}|${hexadecimal-constant} ${integer-suffix}|${ε}", 0, False, True, False);

    // Decimal floating point,
    NCC_addRule(&ncc, "fractional-constant", "{0-9^* . 0-9 0-9^*} | {0-9 0-9^* . 0-9^*}", 0, False, False, False);
    NCC_addRule(&ncc, "exponent-part", "e|E +|\\-|${ε} 0-9 0-9^*", 0, False, False, False);
    NCC_addRule(&ncc, "floating-suffix", "f|l|F|L", 0, False, False, False);
    NCC_addRule(&ncc, "decimal-floating-constant",
            "{${fractional-constant} ${exponent-part}|${ε} ${floating-suffix}|${ε}} | "
            "{0-9 0-9^* ${exponent-part} ${floating-suffix}|${ε}}", 0, False, False, False);

    // Hexadecimal floating point,
    NCC_addRule(&ncc, "hexadecimal-fractional-constant",
            "{${hexadecimal-digit}^* . ${hexadecimal-digit} ${hexadecimal-digit}^*} | "
            "{${hexadecimal-digit} ${hexadecimal-digit}^* . ${hexadecimal-digit}^*}", 0, False, False, False);
    NCC_addRule(&ncc, "binary-exponent-part", "p|P +|\\-|${ε} 0-9 0-9^*", 0, False, False, False);
    NCC_addRule(&ncc, "hexadecimal-floating-constant",
                "${hexadecimal-prefix} ${hexadecimal-fractional-constant}|{${hexadecimal-digit}${hexadecimal-digit}^*} ${binary-exponent-part} ${floating-suffix}|${ε}", 0, False, False, False);

    // Floating point constant,
    NCC_addRule(&ncc, "floating-constant", "${decimal-floating-constant} | ${hexadecimal-floating-constant}", 0, False, True, False);

    // Document,
    NCC_addRule(&ncc, "testDocument", "${identifier} | ${integer-constant} | ${floating-constant}", printListener, True, False, False);

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
