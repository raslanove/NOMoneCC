#include <NSystemUtils.h>
#include <NError.h>

#include <NCC.h>

void NMain() {

    NSystemUtils.logI("sdf", "besm Allah :)");

    const char* rule = "besm Allah";
    struct NCC_Node* tree = NCC_constructRuleTree(rule);

    // TODO: delete ...

    NError.logAndTerminate();
}
