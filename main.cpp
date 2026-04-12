// tsh — Turtle Shell v4.0
// main.cpp — entry point and REPL
// =============================================================================

#include "src/core/globals.hpp"
#include "src/core/color.hpp"
#include "src/core/strutil.hpp"
#include "src/core/debug.hpp"
#include "src/core/config.hpp"
#include "src/core/vars.hpp"
#include "src/core/jobs.hpp"
#include "src/core/prompt.hpp"
#include "src/core/expand.hpp"
#include "src/core/parser.hpp"
#include "src/core/exec.hpp"
#include "src/core/builtins.hpp"
#include "src/core/completion.hpp"
#include "src/core/mods.hpp"
#include "src/core/introspect.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <signal.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

namespace fs = std::filesystem;
using std::cout;
using std::endl;

// =============================================================================
//  Signal handler
// =============================================================================

static void handleSigInt(int) {
    g_ctrlCPressed = 1;
    rl_replace_line("", 0);
    rl_crlf();
    rl_on_new_line();
    rl_redisplay();
}

// =============================================================================
//  main
// =============================================================================

int main(int argc, char** argv) {
    std::cout << Color::BGREEN << "Welcome to TShell V1." << Color::RESET << endl;
    // ── Parse flags ──────────────────────────────────────────────────────────
    bool startSafe = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--safe") startSafe = true;
    g_safeMode = startSafe;

    // ── Signals ──────────────────────────────────────────────────────────────
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGINT,  handleSigInt);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    g_interactive = isatty(STDIN_FILENO);

    // ── Config ───────────────────────────────────────────────────────────────
    const char* home = getenv("HOME");
    if (home) {
        loadConfig(std::string(home) + "/.tshcfg", g_cfg);
        g_frecencyFile = std::string(home) + "/.tsh_frecency";
        loadFrecency();
    }

    if (startSafe && g_interactive)
        std::cout << Color::BYELLOW << "[safe mode]" << Color::RESET << "\n";

    // ── Themes ───────────────────────────────────────────────────────────────
    loadThemesFromDir(expandHome(g_cfg.themesPath));
    if (home) loadThemesFromDir(std::string(home) + "/.tsh_themes");

    loadPersistVars();

    // ── Mods ─────────────────────────────────────────────────────────────────
    if (g_interactive) {
        std::cout << Color::BBLUE << "Loading mods..." << Color::RESET << "\n";
    }

    static ShellCallbacks baseCb = makeCallbacks("__base__");
    Modloader loader;
    try {
        g_mods = loader.loadMods(expandHome(g_cfg.modsPath), &baseCb, g_safeMode);
    } catch (...) {
        std::cerr << "tsh: failed to load mods directory\n";
        // Non-fatal — continue without mods
    }

    // Re-inject per-mod callbacks with correct IDs
    static std::map<std::string, ShellCallbacks> modCbMap;
    for (auto& lm : g_mods) {
        std::string mid = lm.mod->meta.id.empty() ? lm.mod->meta.name : lm.mod->meta.id;
        modCbMap[mid]    = makeCallbacks(mid);
        lm.mod->shell    = &modCbMap[mid];
        TSH_VERBOSE("main", "Injected callbacks for mod: " + mid);
    }

    // ── Register system binaries ─────────────────────────────────────────────
    for (const char* dir : {"/usr/bin", "/usr/local/bin"}) {
        std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (e.is_directory(ec)) continue;
            g_commands.push_back({e.path().filename().string(), nullptr, e.path()});
        }
    }

    // ── Safe-mode capability enforcement ────────────────────────────────────
    if (g_safeMode) {
        for (auto& lm : g_mods) {
            ModSecurityPolicy pol = lm.mod->securityPolicy();
            std::string mname     = lm.mod->meta.name;
            ShellCallbacks& cb    = *lm.mod->shell;
            auto guard = [&](bool allowed, const std::string& cap, auto& fn) {
                if (!allowed) {
                    fn = nullptr;
                    std::cerr << "[safe-mode] " << mname << ": blocked undeclared capability '" << cap << "'\n";
                }
            };
            guard(pol.allowRunCmd,           "runCmd",             cb.runCmd);
            guard(pol.allowSetVar,           "setVar",             cb.setVar);
            guard(pol.allowStoreWrite,       "storePut",           cb.storePut);
            guard(pol.allowStoreWrite,       "storeDelete",        cb.storeDelete);
            guard(pol.allowRegisterAlias,    "registerAlias",      cb.registerAlias);
            guard(pol.allowInterceptCmd,     "interceptCommand",   cb.interceptCommand);
            guard(pol.allowRegisterKeybind,  "registerKeybinding", cb.registerKeybinding);
            guard(pol.allowRegisterTheme,    "registerTheme",      cb.registerTheme);
            guard(pol.allowRegisterTheme,    "registerPromptFormat", cb.registerPromptFormat);
            guard(pol.allowEnvRead,          "getVar",             cb.getVar);

            // ── New sandbox axes ──────────────────────────────────────────
            // Command injection: shell->runCmd() injects arbitrary command
            // strings; block it unless the policy explicitly permits it.
            if (!pol.allowCommandInjection) {
                cb.runCmd = nullptr;
                if (pol.allowRunCmd) {
                    std::cerr << "[safe-mode] " << mname
                              << ": runCmd granted only if allowCommandInjection=true\n";
                }
            }

            // Environment access: if not permitted, replace getVar with a
            // store-scoped stub that only reads from the mod's own store.
            if (!pol.allowEnvironmentAccess) {
                std::string mid2 = lm.mod->meta.id.empty()
                                   ? lm.mod->meta.name : lm.mod->meta.id;
                cb.getVar = [mid2](const std::string& var) -> std::string {
                    // Only expose the mod's own persisted vars
                    std::string path = std::string(getenv("HOME") ? getenv("HOME") : "/tmp")
                                       + "/.tsh_moddata/" + mid2 + "/" + var;
                    std::ifstream f(path);
                    if (!f) return "";
                    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
                };
            }

            // Network access: we can't syscall-filter here without seccomp,
            // so we log the policy for auditing. A future seccomp layer can
            // use this flag to install the appropriate filter per-mod process.
            if (!pol.allowNetworkAccess) {
                TSH_VERBOSE("safe-mode", mname + ": network access DENIED (policy)");
            }

            // Filesystem access: enforce via store-only write gate.
            // "none"  → block storePut/storeDelete as well
            // "store" → only ~/.tsh_moddata/<id>/  (default, already enforced
            //           by the store API implementation)
            // "home"/"full" → allow (trust the mod)
            if (pol.filesystemAccess == "none") {
                cb.storePut    = nullptr;
                cb.storeDelete = nullptr;
                std::cerr << "[safe-mode] " << mname
                          << ": filesystem access=none — store API disabled\n";
            }
        }
    }

    // ── Init / Start mods ────────────────────────────────────────────────────
    for (auto& lm : g_mods) {
        try { lm.mod->Init(); }
        catch (...) { std::cerr << "[mod] " << lm.mod->meta.name << ": Init() threw, disabling\n"; }
    }
    for (auto& lm : g_mods) {
        try { lm.mod->Start(); }
        catch (...) { std::cerr << "[mod] " << lm.mod->meta.name << ": Start() threw, disabling\n"; }
    }

    fireHook(TshEvent::ModLoad, "");

    // ── Script mode ──────────────────────────────────────────────────────────
    {
        int scriptArgStart = 1;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) != "--safe") { scriptArgStart = i; break; }
        if (argc >= 2 && std::string(argv[1]) != "--safe") {
            std::vector<std::string> scriptArgs(argv + scriptArgStart + 1, argv + argc);
            int rc = runScript(argv[scriptArgStart], scriptArgs);
            for (auto& lm : g_mods) { try { lm.mod.reset(); } catch (...) {} if (lm.handle) dlclose(lm.handle); }
            return rc;
        }
    }

    // ── Pipeline introspector ────────────────────────────────────────────────
    introspectInit();

    // ── Interactive REPL ─────────────────────────────────────────────────────
    std::string histPath = expandHome(g_cfg.historyFile);
    using_history();
    read_history(histPath.c_str());
    stifle_history(g_cfg.historyMax);
    setupReadline();

    bool running = true;
    while (running) {
        reapJobs();
        std::string prompt = buildPrompt();
        char* raw = readline(prompt.c_str());

        if (g_ctrlCPressed) { g_ctrlCPressed = 0; continue; }
        if (!raw) { std::cout << "\n"; break; }

        std::string line = trim(raw);
        free(raw);
        if (line.empty()) continue;

        // History deduplication
        HIST_ENTRY* last = history_length > 0 ? history_get(history_base + history_length - 1) : nullptr;
        if (!last || std::string(last->line) != line)
            add_history(line.c_str());

        // Clear path cache between commands
        g_pathCache.clear();

        // ── Stage 1: Pre-parse hook ──────────────────────────────────────────
        // Fires on the raw input string before any tokenisation.
        // A hook may mutate ctx.data to rewrite the input, or set
        // ctx.cancel = true to suppress execution entirely.
        {
            TshHookContext ctx{TshEvent::PreParse, line, 0, false};
            auto it = g_hooks.find(TshEvent::PreParse);
            if (it != g_hooks.end()) for (auto& fn : it->second) fn(ctx);
            if (ctx.cancel) { g_lastExit = 0; continue; }
            line = ctx.data; // allow hooks to rewrite the input
        }

        // ── Stage 2: Parser → AST (Section list) ─────────────────────────
        std::vector<Section> sections = parseSections(line);

        // ── Stage 3: Pre-exec hook (post-parse, pre-fork) ─────────────────
        // Fires with the fully-parsed command string.
        {
            TshHookContext ctx{TshEvent::PreExec, line, 0, false};
            auto it = g_hooks.find(TshEvent::PreExec);
            if (it != g_hooks.end()) for (auto& fn : it->second) fn(ctx);
            if (ctx.cancel) { g_lastExit = 0; continue; }
        }

        // (Middleware pipeline runs inside runCommand, between parse and fork)
        bool chainBroken = false;

        int sectionIdx = 0;
        for (auto& section : sections) {
            if (chainBroken) break;
            std::string cmd = trim(section.command);
            if (cmd.empty()) continue;

            // ── Timing + execution ────────────────────────────────────────
            auto _t0 = std::chrono::steady_clock::now();
            g_lastExit = runPipelineTop(cmd);
            auto _t1 = std::chrono::steady_clock::now();
            double execMs = std::chrono::duration<double, std::milli>(_t1 - _t0).count();

            // Command-not-found suggestion
            if (g_lastExit == 127) {
                std::vector<std::string> wa = parseArgs(cmd);
                if (!wa.empty()) {
                    std::string sug = suggestCommand(wa[0]);
                    if (!sug.empty())
                        std::cerr << Color::BRED
                                  << "error: command '" << wa[0] << "' not found"
                                  << Color::RESET << "\n"
                                  << "  Did you mean: "
                                  << Color::BGREEN << sug
                                  << Color::RESET << "?\n";
                }
            }

            // ── Rich debug JSON record ────────────────────────────────────
            if (g_cfg.debugJson) {
                static const std::vector<std::string> kBuiltins = {
                    "cd","exit","echo","export","alias","unalias","source","help",
                    "jobs","fg","bg","tshell","retry","timeout","type","history","watch"
                };
                std::vector<std::string> wa = parseArgs(cmd);
                std::string head = wa.empty() ? cmd : wa[0];

                CmdType ctype = CmdType::External;
                for (auto& b : kBuiltins) if (b == head) { ctype = CmdType::Builtin; break; }
                if (ctype == CmdType::External) {
                    for (auto& [k,_] : g_tshCommands)
                        if (k == head) { ctype = CmdType::Mod; break; }
                }

                ExecRecord rec;
                rec.command      = cmd;
                rec.expanded     = cmd;   // post-alias expansion already done inside runPipelineTop
                rec.args         = wa;
                rec.type         = ctype;
                rec.pipelineIdx  = sectionIdx;
                rec.exitCode     = g_lastExit;
                rec.startTime    = _t0;
                rec.durationMs   = execMs;
                rec.stdinDesc    = isatty(STDIN_FILENO)  ? "tty" : "pipe";
                rec.stdoutDesc   = isatty(STDOUT_FILENO) ? "tty" : "pipe";
                tshDebugJson(rec);
            }

            // Post-exec hook
            fireHook(TshEvent::PostExec, cmd, g_lastExit);
            ++sectionIdx;

            if (g_lastExit == -127) { running = false; break; }
            if (section.delimiter == "&&" && g_lastExit != 0) chainBroken = true;
            if (section.delimiter == "||" && g_lastExit == 0) chainBroken = true;
        }
    }

    // ── Shutdown ─────────────────────────────────────────────────────────────
    fireHook(TshEvent::ShellExit, "");
    write_history(histPath.c_str());
    saveFrecency();
    tshDebugCloseLog();
    for (auto& lm : g_mods) { try { lm.mod.reset(); } catch (...) {} if (lm.handle) dlclose(lm.handle); }
    return g_lastExit;
}
