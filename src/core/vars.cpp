// vars.cpp — Shell variable management (set, persist, assign)
#include "globals.hpp"
#include "vars.hpp"
#include "strutil.hpp"
#include "expand.hpp"
#include "debug.hpp"

#include <fstream>
#include <iostream>

static std::string varsFilePath() { return expandHome(g_cfg.varsFile); }

void loadPersistVars() {
    std::ifstream f(varsFilePath());
    if (!f) return;
    std::string line;
    TSH_VERBOSE("vars", "Loading persistent vars from " + varsFilePath());
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        ShellVar sv;
        sv.scalar  = line.substr(eq + 1);
        sv.persist = true;
        g_vars[line.substr(0, eq)] = sv;
    }
}

void savePersistVars() {
    std::ofstream f(varsFilePath(), std::ios::trunc);
    if (!f) { TSH_WARN("vars", "Could not save persistent vars to " + varsFilePath()); return; }
    f << "# tsh persistent variables\n";
    for (auto& [k, v] : g_vars)
        if (v.persist) f << k << "=" << v.scalar << "\n";
}

bool tryAssign(const std::string& text) {
    auto args = parseArgs(text);
    if (args.empty()) return false;
    for (auto& a : args) {
        auto eq = a.find('=');
        if (eq == 0 || eq == std::string::npos) return false;
        bool valid = true;
        for (size_t k = 0; k < eq && valid; ++k)
            valid = (std::isalnum((unsigned char)a[k]) || a[k] == '_');
        if (!valid) return false;
    }
    // All tokens are NAME=val
    for (auto& a : args) {
        auto eq = a.find('=');
        std::string name   = a.substr(0, eq);
        std::string rawVal = a.substr(eq + 1);
        TSH_TRACE("vars", "assign " + name + "=" + rawVal);
        if (!rawVal.empty() && rawVal[0] == '(') {
            size_t close = rawVal.rfind(')');
            if (close != std::string::npos) {
                ShellVar sv;
                sv.isArray = true;
                sv.array   = parseArgs(rawVal.substr(1, close - 1));
                if (g_vars.count(name)) sv.persist = g_vars[name].persist;
                g_vars[name] = sv;
                if (sv.persist) savePersistVars();
                continue;
            }
        }
        ShellVar sv;
        sv.scalar  = expandAll(rawVal);
        sv.isArray = false;
        if (g_vars.count(name)) sv.persist = g_vars[name].persist;
        g_vars[name] = sv;
        if (sv.persist) savePersistVars();
    }
    return true;
}

size_t countLeadingAssignments(const std::vector<std::string>& args) {
    size_t i = 0;
    for (; i < args.size(); ++i) {
        const auto& a = args[i];
        auto eq = a.find('=');
        if (eq == 0 || eq == std::string::npos) break;
        bool valid = true;
        for (size_t k = 0; k < eq && valid; ++k)
            valid = (std::isalnum((unsigned char)a[k]) || a[k] == '_');
        if (!valid) break;
    }
    return i;
}

void applyInlineEnvChild(const std::vector<std::string>& args, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto& a = args[i];
        auto eq = a.find('=');
        setenv(a.substr(0, eq).c_str(), expandAll(a.substr(eq + 1)).c_str(), 1);
    }
}
