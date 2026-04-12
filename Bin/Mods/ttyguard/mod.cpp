// ttyguard/mod.cpp — Unload all other mods when not running in a TTY
//
// Detects non-interactive sessions (pipe, script, cron …) via isatty() and
// tears down every other loaded mod before their Init/Start runs, leaving the
// shell in a bare, fast state for scripting.
//
// Install in the SYSTEM mod directory (/usr/lib/tshell/mods/) so it is loaded
// before user mods from ~/.tsh/Mods and can gate them first.
//
// Build:
//   g++ -std=c++17 -O2 -shared -fPIC -I. -IBin/API \
//       Bin/Mods/ttyguard/mod.cpp -o Bin/Mods/ttyguard/main.so -ldl

#include "../../API/ModdingAPI.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>   // isatty, STDIN_FILENO

// ---------------------------------------------------------------------------
//  g_mods_ptr
//
//  The shell binary exports this symbol (file-scope extern "C" in main.cpp).
//  It points at the std::vector<LoadedMod> that holds all loaded mods.
//  We declare it extern here so the dynamic linker resolves it at dlopen time
//  without any dlsym call — no mangling issues, no g_interactive dependency.
// ---------------------------------------------------------------------------
extern "C" void* g_mods_ptr;

// ---------------------------------------------------------------------------
//  Minimal mirror of LoadedMod
//
//  Must match the layout of LoadedMod in Modloader.hpp exactly:
//      std::unique_ptr<Mod>  mod;
//      void*                 handle;
//      ModManifest           manifest;   ← we don't touch this
//      std::string           sourcePath; ← we don't touch this
//
//  We only need the first two fields; the destructor of the erased element
//  will clean up manifest and sourcePath normally.
// ---------------------------------------------------------------------------
struct LoadedModMirror {
    std::unique_ptr<Mod> mod;
    void*                handle = nullptr;
    // remaining fields (ModManifest + std::string) follow in memory;
    // we never access them directly — their dtors run on erase().
};

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
        // Use isatty() directly — no dependency on any shell-internal symbol.
        if (isatty(STDIN_FILENO)) return 0;

        std::cerr << "[ttyguard] Non-TTY session — unloading all other mods\n";

        if (!g_mods_ptr) {
            std::cerr << "[ttyguard] warning: g_mods_ptr is null, skipping unload\n";
            return 0;
        }

        auto* mods = static_cast<std::vector<LoadedModMirror>*>(g_mods_ptr);

        // Cleanly destroy every mod that isn't ttyguard itself.
        for (auto& lm : *mods) {
            if (!lm.mod) continue;
            if (lm.mod->meta.id == meta.id) continue;
            try { lm.mod.reset(); } catch (...) {}
            if (lm.handle) { dlclose(lm.handle); lm.handle = nullptr; }
        }

        // Remove the now-dead entries from the vector.
        // main()'s Init/Start loops will then only see ttyguard.
        mods->erase(
            std::remove_if(mods->begin(), mods->end(),
                [&](const LoadedModMirror& lm) {
                    return !lm.mod || lm.mod->meta.id != meta.id;
                }),
            mods->end());

        return 0;
    }

    int Start()              override { return 0; }
    int Execute(int, char**) override { return 0; }
};

TSH_MOD_EXPORT(TtyGuardMod)
