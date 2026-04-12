// hookdemo/mod.cpp — demonstrates every TShell hook event
#include "../../API/ModdingAPI.hpp"
#include <chrono>
#include <iostream>
#include <string>

class HookDemoMod : public Mod {
    bool verbose_ = false;
    std::chrono::steady_clock::time_point preExecTime_;

    void log(const std::string& tag, const std::string& msg) {
        if (!verbose_) return;
        shell->print("%bcyan[hookdemo:" + tag + "]%reset " + msg);
    }

public:
    HookDemoMod() {
        meta.id          = "com.example.hookdemo";
        meta.name        = "hookdemo";
        meta.version     = "1.0.0";
        meta.author      = "example";
        meta.description = "Demonstrates every TShell hook event";
    }

    ModSecurityPolicy securityPolicy() const override {
        ModSecurityPolicy p;
        p.allowTSHExpansion      = true;
        p.allowEnvironmentAccess = true;
        return p;
    }

    int Init() override {
        // ── PreParse: fires on raw input before tokenisation ──────────────
        shell->registerHook(TshEvent::PreParse, [this](TshHookContext& ctx) {
            log("PreParse", "raw input: \"" + ctx.data + "\"");
            // A hook can mutate ctx.data to rewrite input, or set
            // ctx.cancel = true to suppress execution.
        });

        // ── PreExec: fires after parse, before fork ───────────────────────
        shell->registerHook(TshEvent::PreExec, [this](TshHookContext& ctx) {
            preExecTime_ = std::chrono::steady_clock::now();
            log("PreExec", "running: \"" + ctx.data + "\"");
        });

        // ── PostExec: fires after every command finishes ──────────────────
        shell->registerHook(TshEvent::PostExec, [this](TshHookContext& ctx) {
            auto now = std::chrono::steady_clock::now();
            int ms = (int)std::chrono::duration<double, std::milli>(
                         now - preExecTime_).count();
            std::string status = ctx.exitCode == 0
                ? "%bgreen✓ exit 0%reset"
                : "%bred✗ exit " + std::to_string(ctx.exitCode) + "%reset";
            log("PostExec", status + "  (" + std::to_string(ms) + " ms)");
        });

        // ── DirChange: fires on cd / autocd ──────────────────────────────
        shell->registerHook(TshEvent::DirChange, [this](TshHookContext& ctx) {
            log("DirChange", "→ " + ctx.data);
        });

        // ── ModLoad: fires when mods are loaded at startup ────────────────
        shell->registerHook(TshEvent::ModLoad, [this](TshHookContext&) {
            log("ModLoad", "hookdemo loaded — use `tshell hookdemo verbose on`");
        });

        // ── ShellExit: fires on clean exit ────────────────────────────────
        shell->registerHook(TshEvent::ShellExit, [this](TshHookContext&) {
            log("ShellExit", "goodbye from hookdemo!");
        });

        // ── tshell hookdemo verbose on|off ────────────────────────────────
        // NOTE: RLAMBDA uses [] capture — use explicit lambda for `this`.
        TshCommand cmd;
        cmd.name     = "hookdemo";
        cmd.helpText = "hookdemo verbose on|off  — toggle hook event logging";
        cmd.run      = [this](int argc, char** argv) -> int {
            if (argc >= 3 && std::string(argv[1]) == "verbose") {
                verbose_ = (std::string(argv[2]) == "on");
                shell->print(std::string("hookdemo: verbose ") + (verbose_ ? "on" : "off"));
            } else {
                shell->print("usage: tshell hookdemo verbose on|off");
            }
            return 0;
        };
        shell->registerTCMD(cmd);
        return 0;
    }

    int Start()              override { return 0; }
    int Execute(int, char**) override { return 0; }
};

TSH_MOD_EXPORT(HookDemoMod)
