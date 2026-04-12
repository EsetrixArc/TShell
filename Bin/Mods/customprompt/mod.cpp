// customprompt/mod.cpp
// Demonstrates registering multiple PromptFMT entries from a single mod.
//
// All formats below appear in `tshell theme list` and can be selected with:
//   tshell theme <name>
//
// You can also register formats dynamically from Init() via:
//   shell->registerPromptFormat({ "MyFmt", "<format string>", "description" });
//
// Format tokens: $USER $HOST $CWD $EXITSTR $GITBRANCH (or any $ENVVAR)
// Color tokens:  %reset %bold %dim %red %green %yellow %blue %magenta %cyan %white
//                %bred %bgreen %byellow %bblue %bmagenta %bcyan %bwhite

#include "../../API/ModdingAPI.hpp"
#include <string>
#include <vector>

class CustomPromptMod : public Mod {
public:
    CustomPromptMod() {
        meta.id          = "com.example.customprompt";
        meta.name        = "customprompt";
        meta.version     = "2.0.0";
        meta.author      = "you";
        meta.description = "Example mod registering multiple prompt formats";
    }

    // Declare what shell capabilities this mod uses.
    // The shell enforces this in --safe mode.
    ModSecurityPolicy securityPolicy() const override {
        ModSecurityPolicy pol;
        pol.allowRegisterTheme = true; // we register prompt formats
        return pol;
    }

    // ── Static formats ──────────────────────────────────────────────────────
    // Return all formats this mod provides. The loader auto-registers them
    // before Init() runs, so they're immediately selectable as themes.
    std::vector<PromptFMT> getPromptFormats() const override {
        return {
            {
                "CP-Classic",
                "%bgreen%bold$USER%reset@%bcyan$HOST%reset:%bblue$CWD%reset"
                "%byellow$EXITSTR%reset $ ",
                "Classic user@host:cwd prompt"
            },
            {
                "CP-Compact",
                "%bmagenta▶%reset %bblue$CWD%reset%byellow$EXITSTR%reset %bold›%reset ",
                "Compact single-token prompt with arrow"
            },
            {
                "CP-TwoLine",
                "┌─[%bgreen$USER%reset@%bcyan$HOST%reset]─[%bblue$CWD%reset]"
                "%byellow$EXITSTR%reset\n└─%bold$%reset ",
                "Two-line box-drawing prompt"
            },
        };
    }

    int Init() override {
        // You can also register formats dynamically here — useful when the
        // format string depends on runtime state (e.g. reading a config file).
        if (shell && shell->registerPromptFormat) {
            shell->registerPromptFormat({
                "CP-Dynamic",
                "%bcyan[dynamic]%reset %bblue$CWD%reset%byellow$EXITSTR%reset %bold$%reset ",
                "Dynamically registered from Init()"
            });
        }
        return 0;
    }

    int Start() override { return 0; }

    // This mod only registers formats; it doesn't handle any commands.
    int Execute(int /*argc*/, char* /*argv*/[]) override { return 0; }
    
};

TSH_MOD_EXPORT(CustomPromptMod)
