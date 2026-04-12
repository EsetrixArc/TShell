// mods.cpp — ShellCallbacks factory and mod persistent storage
#include "globals.hpp"
#include "mods.hpp"
#include "exec.hpp"
#include "color.hpp"
#include "prompt.hpp"
#include "debug.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

// =============================================================================
//  Mod persistent storage (~/.tsh_moddata/<modId>/)
// =============================================================================

static std::string modStorePath(const std::string& modId) {
    const char* home = getenv("HOME");
    std::string dir = home ? std::string(home) + "/.tsh_moddata/" + modId
                           : "/tmp/tsh_" + modId;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

static void modStorePut(const std::string& modId, const std::string& key, const std::string& val) {
    std::string path = modStorePath(modId) + "/" + key;
    std::ofstream f(path, std::ios::trunc);
    if (f) f << val;
    else TSH_WARN("mods", "storePut failed for mod " + modId + " key " + key);
}

static std::string modStoreGet(const std::string& modId, const std::string& key, const std::string& def) {
    std::string path = modStorePath(modId) + "/" + key;
    std::ifstream f(path);
    if (!f) return def;
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static void modStoreDelete(const std::string& modId, const std::string& key) {
    std::error_code ec;
    fs::remove(modStorePath(modId) + "/" + key, ec);
}

// =============================================================================
//  ShellCallbacks factory
// =============================================================================

ShellCallbacks makeCallbacks(const std::string& modId) {
    ShellCallbacks cb;

    cb.registerTheme = [](const std::string& name, const std::string& fmt, const std::string& desc) {
        g_themes[name] = {name, desc, fmt};
    };

    cb.registerPromptFormat = [modId](const PromptFMT& pf) {
        PromptFMT tagged = pf;
        if (tagged.ownerId.empty()) tagged.ownerId = modId;
        std::string desc = tagged.description;
        if (!tagged.ownerId.empty()) desc += " [mod:" + tagged.ownerId + "]";
        g_themes[tagged.name] = {tagged.name, desc, tagged.fmt};
        TSH_VERBOSE("mods", "Registered prompt format: " + tagged.name + " from " + modId);
    };

    cb.registerEngineToken = [](const std::string& tok, std::function<std::string()> prov) {
        g_engineTokens[tok] = std::move(prov); 
        if (g_engineTokenEnabled.find(tok) == g_engineTokenEnabled.end())
            g_engineTokenEnabled[tok] = true;
    };
 
    cb.registerTCMD = [](const TshCommand& cmd) {
        g_tshCommands[cmd.name] = cmd;
        TSH_VERBOSE("mods", "Registered tshell command: " + cmd.name);
    };

    cb.registerHighlighter = [](std::function<std::vector<HlSpan>(const std::string&)> hl) {
        g_highlighters.push_back(std::move(hl));
    };

    cb.createExplanation = [](const std::string& cmd,
                               const std::vector<std::vector<std::string>>& expl,
                               const std::string& argPattern) {
        g_explanations[cmd][argPattern] = expl;
    };

    cb.registerToken = [](const std::string& tok, std::function<std::string()> prov) {
        g_tokenProviders[tok] = std::move(prov);
    };

    cb.registerCommand = [modId](const std::string& name) -> int {
        for (auto& c : g_commands) if (c.name == name) return 0;
        g_commands.push_back({name, nullptr, {}});
        // Tag the command with its mod origin so `tshell inspect` can report it
        g_cmdStats[name].pluginOrigin = modId;
        TSH_VERBOSE("mods", "Registered command: " + name + " [mod:" + modId + "]");
        return 0;
    };

    cb.registerCompletions = [](const std::string& cmd,
        std::function<std::vector<CompletionEntry>(const std::string&,
                                                   const std::vector<std::string>&)> prov) {
        g_completionProviders[cmd] = std::move(prov);
    };

    cb.registerHook = [](TshEvent ev, std::function<void(TshHookContext&)> fn) {
        g_hooks[ev].push_back(std::move(fn));
    };

    cb.registerAlias = [](const std::string& name, const std::string& expansion) {
        g_cfg.aliases[name] = expansion;
    };

    cb.registerKeybinding = [](const std::string& seq, std::function<void(std::string&)> fn) {
        g_keybindings.push_back({seq, std::move(fn)});
    };

    cb.interceptCommand = [](const std::string& name,
        std::function<int(const std::vector<std::string>&)> fn) {
        g_interceptors[name] = std::move(fn);
    };

    cb.getVar = [](const std::string& var) -> std::string {
        auto it = g_vars.find(var);
        if (it != g_vars.end()) return it->second.scalar;
        const char* e = getenv(var.c_str());
        return e ? e : "";
    };

    cb.setVar = [](const std::string& var, const std::string& val) {
        g_vars[var].scalar = val;
    };

    cb.storePut    = [modId](const std::string& key, const std::string& val) { modStorePut(modId, key, val); };
    cb.storeGet    = [modId](const std::string& key, const std::string& def) -> std::string { return modStoreGet(modId, key, def); };
    cb.storeDelete = [modId](const std::string& key) { modStoreDelete(modId, key); };

    // ── Middleware registration ───────────────────────────────────────────
    // Middleware runs between the parser and every fork/exec call.
    // It receives the resolved args vector and may rewrite or skip execution.
    cb.registerMiddleware = [](MiddlewareFn fn) {
        addMiddleware(std::move(fn));
    };

    // ── Pre-parse hook shortcut ───────────────────────────────────────────
    // Fires on raw input before tokenisation; ctx.data holds the input line
    // and may be mutated. Setting ctx.cancel skips execution entirely.
    cb.registerPreParseHook = [](std::function<void(TshHookContext&)> fn) {
        g_hooks[TshEvent::PreParse].push_back(std::move(fn));
    };

    cb.runCmd  = [](const std::string& cmd) -> int { return runPipeline(cmd); };
    cb.print   = [](const std::string& msg) { std::cout << (g_cfg.colorEnable ? Color::apply(msg) : Color::strip(msg)) << "\n"; };
    cb.printErr= [](const std::string& msg) { std::cerr << (g_cfg.colorEnable ? std::string(Color::BRED) + msg + Color::RESET : msg) << "\n"; };
    cb.lastExit= []() -> int { return g_lastExit; };
    cb.getCwd  = []() -> std::string { return getCwd(); };

    return cb;
}
