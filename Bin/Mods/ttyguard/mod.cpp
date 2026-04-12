// ttyguard/mod.cpp — Unload all other mods when not running in a TTY
//
// When TShell is invoked non-interactively (pipe, script, cron …) there is
// usually no reason to run UI-oriented mods.  This mod detects the situation
// in Init() and tears down every other loaded mod before the main startup
// sequence calls their Init/Start.
//
// For this to work correctly, ttyguard MUST be the first mod loaded.
// Install it in the system mod directory (/usr/lib/tshell/mods/) so it is
// picked up before user mods from ~/.tsh/Mods.
//
// Usage:   #include <TSh/ModdingAPI.hpp>

#include "../../API/ModdingAPI.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <iostream>
#include <unistd.h>  // isatty, STDIN_FILENO

// g_mods and g_interactive are defined in the shell binary and exported.
// We access them via the linker (RTLD_DEFAULT) rather than a direct extern
// so that this .so works whether built against the header-only API or the
// full source tree.

// Forward-declare the LoadedMod struct minimally so we can manipulate g_mods.
// The real definition is in Modloader.hpp / globals.hpp — we only need the
// fields we use here.
struct LoadedMod;   // opaque forward; resolved by the linker at load time

// Pull in the real vector type from the shell binary via dlsym.
// We use a type-punned pointer because we can't include the full headers in
// a standalone mod .so.  The layout of std::vector is stable within the same
// ABI / compiler.
#include <vector>
#include <memory>

// The shell binary exports these symbols:
extern "C" {
    // We read g_interactive directly — it's a plain bool.
    extern bool g_interactive;
}

// g_mods is std::vector<LoadedMod>.  We need just enough of LoadedMod to
// call reset() on the mod unique_ptr and dlclose the handle.  We use a
// local mirror struct that matches the binary layout (unique_ptr + void* +
// two strings + a bool).
//
// IMPORTANT: this layout MUST match LoadedMod in Modloader.hpp exactly.
//            If you change that struct, update this mirror too.
struct LoadedModMirror {
    std::unique_ptr<Mod> mod;
    void*                handle     = nullptr;
    // manifest and sourcePath follow — we don't touch them
    // (their destructors run normally when the vector element is erased)
};

// Access g_mods via dlsym so the .so doesn't need to link against the shell.
static std::vector<LoadedModMirror>* getGMods() {
    static auto* ptr = reinterpret_cast<std::vector<LoadedModMirror>*>(
        dlsym(RTLD_DEFAULT, "g_mods_ptr"));  // exported by main binary
    // Fallback: try the mangled vector symbol directly (implementation detail,
    // fragile — the recommended path is for the shell to export g_mods_ptr).
    return ptr;
}

class TtyGuardMod : public Mod {
public:
    TtyGuardMod() {
        meta.id          = "com.tshell.ttyguard";
        meta.name        = "ttyguard";
        meta.version     = "1.0.0";
        meta.author      = "tshell";
        meta.description = "Unloads all mods when TShell is not running in a TTY";
        meta.apiVersion  = TSH_API_VERSION;
    }

    ModSecurityPolicy securityPolicy() const override {
        ModSecurityPolicy pol;
        pol.filesystemAccess = "none";
        return pol;
    }

    int Init() override {
        if (g_interactive) return 0;   // TTY — nothing to do

        std::cerr << "[ttyguard] Non-TTY session — unloading all other mods\n";

        auto* mods = getGMods();
        if (!mods) {
            // Shell didn't export g_mods_ptr; degrade gracefully.
            std::cerr << "[ttyguard] warning: could not locate g_mods — "
                         "mod unloading skipped\n";
            return 0;
        }

        for (auto& lm : *mods) {
            if (!lm.mod) continue;
            if (lm.mod->meta.id == meta.id) continue;  // keep ourselves
            try { lm.mod.reset(); } catch (...) {}
            if (lm.handle) { dlclose(lm.handle); lm.handle = nullptr; }
        }

        mods->erase(
            std::remove_if(mods->begin(), mods->end(),
                [&](const LoadedModMirror& lm) {
                    return !lm.mod || lm.mod->meta.id != meta.id;
                }),
            mods->end());

        return 0;
    }

    int Start()   override { return 0; }
    int Execute(int, char**) override { return 0; }
};

TSH_MOD_EXPORT(TtyGuardMod)
