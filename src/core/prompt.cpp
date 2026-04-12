// prompt.cpp — Prompt building, theme loading, and frecency tracking
#include "globals.hpp"
#include "prompt.hpp"
#include "color.hpp"
#include "strutil.hpp"
#include "expand.hpp"
#include "debug.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <unistd.h>

// =============================================================================
//  Frecency
// =============================================================================

void loadFrecency() {
    if (g_frecencyFile.empty()) return;
    std::ifstream f(g_frecencyFile);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        g_dirFrecency[line.substr(0, tab)] = std::stod(line.substr(tab + 1));
    }
    TSH_VERBOSE("prompt", "Frecency loaded: " + std::to_string(g_dirFrecency.size()) + " entries");
}

void saveFrecency() {
    if (g_frecencyFile.empty()) return;
    std::ofstream f(g_frecencyFile, std::ios::trunc);
    if (!f) return;
    for (auto& [p, s] : g_dirFrecency) f << p << "\t" << s << "\n";
}

void trackDir(const std::string& path) {
    g_dirFrecency[path] = g_dirFrecency[path] * 0.9 + 1.0;
    saveFrecency();
}

// =============================================================================
//  Hostname / cwd
// =============================================================================

std::string getHostname() {
    if (const char* h = getenv("HOST"))     return h;
    if (const char* h = getenv("HOSTNAME")) return h;
    std::ifstream f("/etc/hostname");
    if (f) { std::string l; if (std::getline(f, l)) return trim(l); }
    return "localhost";
}

std::string getCwd() {
    const char* home = getenv("HOME");
    auto tilde = [&](const std::string& p) -> std::string {
        if (home && p.rfind(home, 0) == 0) return "~" + p.substr(std::strlen(home));
        return p;
    };
    if (const char* pwd = getenv("PWD")) return tilde(pwd);
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec) return tilde(cwd.string());
    return "?";
}

// =============================================================================
//  Theme file loading
// =============================================================================

static std::string unescapeNewlines(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            if (str[i + 1] == 'n') { out += '\n'; ++i; continue; }
            if (str[i + 1] == 't') { out += '\t'; ++i; continue; }
        }
        out += str[i];
    }
    return out;
}

void loadThemeFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    Theme t;
    std::string line;
    while (std::getline(f, line)) {
        auto h = line.find('#');
        if (h != std::string::npos) line = line.substr(0, h);
        line = trim(line);
        if (line.empty()) continue;
        size_t sp  = line.find_first_of(" \t");
        std::string key = (sp == std::string::npos) ? line : line.substr(0, sp);
        std::string val = (sp == std::string::npos) ? std::string{} : trim(line.substr(sp + 1));
        if      (key == "name")        t.name        = val;
        else if (key == "description") t.description = val;
        else if (key == "prompt")      t.promptFmt   = unescapeNewlines(val);
    }
    if (!t.name.empty() && !t.promptFmt.empty()) {
        g_themes[t.name] = t;
        TSH_VERBOSE("prompt", "Loaded theme: " + t.name);
    }
}

void loadThemesFromDir(const std::string& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".theme") loadThemeFile(e.path().string());
}

// =============================================================================
//  Prompt building
// =============================================================================

static std::string resolvePromptFmt() {
    if (!g_cfg.promptFmt.empty()) return g_cfg.promptFmt;
    auto it = g_themes.find(g_cfg.activeTheme);
    if (it != g_themes.end()) return it->second.promptFmt;
    return "%bgreen%bold$USER%reset@%bcyan$HOST%reset:%bblue$CWD%reset%byellow$EXITSTR%reset $ ";
}

std::string buildPrompt() {
    const char* user = getenv("USER");
    std::string p    = resolvePromptFmt();

    p = replaceAll(p, "$USER",    user ? user : "user");
    p = replaceAll(p, "$HOST",    getHostname());
    p = replaceAll(p, "$CWD",     getCwd());
    p = replaceAll(p, "$EXITSTR", (g_lastExit != 0) ? " [" + std::to_string(g_lastExit) + "]" : "");

    // Engine tokens
    for (auto& [token, prov] : g_engineTokens) {
        p = replaceAll(p, "$E_" + token, g_engineTokenEnabled[token] ? prov() : "");
    }
    // Mod-registered custom tokens
    for (auto& [token, prov] : g_tokenProviders)
        p = replaceAll(p, "$" + token, prov());

    // Expand remaining $VAR tokens
    {
        std::string res;
        size_t pos2 = 0;
        while (pos2 < p.size()) {
            if (p[pos2] == '$') { ++pos2; res += expandToken(p, pos2); }
            else res += p[pos2++];
        }
        p = res;
    }

    // Async extra (e.g. git status from background thread)
    {
        std::lock_guard<std::mutex> lk(g_asyncMtx);
        if (!g_asyncPromptExtra.empty()) p += g_asyncPromptExtra;
    }

    if (g_cfg.colorEnable) {
        p = Color::apply(p);
        // Wrap ANSI sequences with readline's \001 / \002 markers
        std::string rp;
        bool inEsc = false;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '\033') { rp += "\001\033"; inEsc = true; }
            else if (inEsc && p[i] == 'm') { rp += "m\002"; inEsc = false; }
            else rp += p[i];
        }
        return rp;
    }
    return Color::strip(p);
}
