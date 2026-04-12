// config.cpp — Shell config loader and validator
#include "globals.hpp"
#include "config.hpp"
#include "strutil.hpp"
#include "expand.hpp"
#include "debug.hpp"

#include <fstream>
#include <iostream>
#include <set>

void loadConfig(const std::string& path, ShellConfig& cfg) {
    std::ifstream f(path);
    if (!f) {
        TSH_VERBOSE("config", "Config file not found: " + path);
        return;
    }
    std::string line;
    int lineNo = 0;
    TSH_VERBOSE("config", "Loading config: " + path);

    while (std::getline(f, line)) {
        ++lineNo;
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;

        size_t sp = line.find_first_of(" \t");
        std::string key   = (sp == std::string::npos) ? line : line.substr(0, sp);
        std::string value = (sp == std::string::npos) ? std::string{} : trim(line.substr(sp + 1));

        if      (key == "prompt")  { cfg.promptFmt = value; cfg.activeTheme = ""; }
        else if (key == "theme")   { cfg.activeTheme = value; cfg.promptFmt = ""; }
        else if (key == "color")   { cfg.colorEnable = (value == "on" || value == "1" || value == "true"); }
        else if (key == "mods")    { cfg.modsPath = value; }
        else if (key == "themes")  { cfg.themesPath = value; }
        else if (key == "history") { cfg.historyFile = value; }
        else if (key == "histmax") { cfg.historyMax = std::atoi(value.c_str()); }
        else if (key == "autocd")  { cfg.autocd = (value == "on" || value == "1" || value == "true"); }
        else if (key == "trace")   { cfg.traceMode = (value == "on" || value == "1" || value == "true"); }
        else if (key == "verbose") { cfg.verboseMode = (value == "on" || value == "1" || value == "true"); }
        else if (key == "alias") {
            auto eq = value.find('=');
            if (eq == std::string::npos) continue;
            cfg.aliases[value.substr(0, eq)] = value.substr(eq + 1);
        }
        else if (key == "set" || key == "export") {
            auto eq = value.find('=');
            if (eq == std::string::npos) continue;
            std::string var = value.substr(0, eq);
            std::string val = expandAll(value.substr(eq + 1));
            setenv(var.c_str(), val.c_str(), 1);
            cfg.envVars[var] = val;
        }
        else {
            std::cerr << path << ":" << lineNo << ": unknown directive '" << key << "'\n";
            TSH_WARN("config", path + ":" + std::to_string(lineNo) + ": unknown directive '" + key + "'");
        }
    }
    TSH_VERBOSE("config", "Config loaded OK");
}

bool validateConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cout << "Config not found: " << path << "\n"; return false; }

    static const std::set<std::string> known = {
        "prompt","theme","color","mods","themes","history",
        "histmax","autocd","trace","verbose","alias","set","export"
    };

    std::string line;
    int lineNo = 0;
    bool ok = true;

    while (std::getline(f, line)) {
        ++lineNo;
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        size_t sp = line.find_first_of(" \t");
        std::string key = (sp == std::string::npos) ? line : line.substr(0, sp);
        if (!known.count(key)) {
            std::cerr << "Config line " << lineNo << ": unknown key '" << key << "'\n";
            ok = false;
        }
    }
    if (ok) std::cout << "Config OK: " << path << "\n";
    return ok;
}
