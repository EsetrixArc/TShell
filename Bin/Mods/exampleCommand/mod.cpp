// customprompt/mod.cpp
// Demonstrates registering multiple PromptFMT entries from a single mod.
//
// Commands below appear in `tshell *` and can be executed via `tshell <name>`
//
// Format tokens: $USER $HOST $CWD $EXITSTR $GITBRANCH (or any $ENVVAR)
// Color tokens:  %reset %bold %dim %red %green %yellow %blue %magenta %cyan %white
//                %bred %bgreen %byellow %bblue %bmagenta %bcyan %bwhite

#include "../../API/ModdingAPI.hpp"
#include <string>
#include <vector>
#include <iostream>


class ExampleCommand : public Mod {
public:
    ExampleCommand() {
        meta.id          = "com.example.customcommand";
        meta.name        = "customcommand";
        meta.version     = "1.0.0";
        meta.author      = "you";
        meta.description = "Example mod registering a command to tshell";
    }

    ModSecurityPolicy securityPolicy() const override {
        ModSecurityPolicy pol;
        pol.allowCmdCreation = true; // adding a new command (is not required for just expanding `tshell`)
        pol.allowTSHExpansion = true; // expanding the tshell command
        return pol;
    }
    int Start() override { return 0; };
    int Execute(int, char**) override { return 0; };
    int Init() override {
        if (shell) {                
            TshCommand c;
            c.name = "example";
            c.owner = this;
            c.run = RLAMBDA(int, (int argc, char** argv), { 
                /*
                RLambdas, "Returning Lambdas" is just a fancy macro:
                #define RLAMBDA(returnType, params, body) [] params -> returnType body

                All it does is compress:
                c.run = [] (int argc, char** argv) -> int {};
                to:
                c.run = RLAMBDA(int, (int argc, char** argv), {});
                Just a bit of QoL
                */
                for (int i=0; i < argc; i++) {
                    std::cout << argv[i];
                }
                std::cout << std::endl;
            }); // Registers the command `tshell example` which just prints any following text. 
            shell->registerTCMD(c);
        }
    return 0;
    }
};

TSH_MOD_EXPORT(ExampleCommand)

/*
Also note that:
extern "C" ExampleCommand* tsh_mod_init() {
    return new ExampleCommand();
}
Also works. (manual expansion of macro)
For dependencies. Check exampleDependant.
*/