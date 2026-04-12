// builtins.cpp — All built-in command implementations
#include "globals.hpp"
#include "builtins.hpp"
#include "exec.hpp"
#include "expand.hpp"
#include "parser.hpp"
#include "prompt.hpp"
#include "vars.hpp"
#include "jobs.hpp"
#include "config.hpp"
#include "debug.hpp"
#include "color.hpp"
#include "strutil.hpp"
#include "introspect.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#include <readline/history.h>
#include <readline/readline.h>

namespace fs = std::filesystem;

// =============================================================================
//  Color helper
// =============================================================================

static inline const char* cc(const char* code) {
    return g_cfg.colorEnable ? code : "";
}

// =============================================================================
//  Built-in help
// =============================================================================

static void printHelp(const std::string& topic = "") {
    auto h  = [](const char* s) { return std::string(g_cfg.colorEnable ? Color::BOLD : "") + s + (g_cfg.colorEnable ? Color::RESET : ""); };
    auto kw = [](const char* s) { return std::string(g_cfg.colorEnable ? Color::BCYAN : "") + s + (g_cfg.colorEnable ? Color::RESET : ""); };

    if (topic == "builtins" || topic == "builtin") {
        std::cout << h("Built-in commands\n\n");
        std::cout << "  " << kw("cd") << " [-|dir]          change directory\n";
        std::cout << "  " << kw("echo") << " [-n] [args]     print text\n";
        std::cout << "  " << kw("export") << " VAR=val        export env variable\n";
        std::cout << "  " << kw("alias") << " [name=cmd]      define or list aliases\n";
        std::cout << "  " << kw("unalias") << " name          remove alias\n";
        std::cout << "  " << kw("type") << " name             is it builtin/external/alias/mod?\n";
        std::cout << "  " << kw("history") << " [n|clear|tag n] command history\n";
        std::cout << "  " << kw("source") << "/" << kw(".") << " file     run script in current shell\n";
        std::cout << "  " << kw("jobs") << " / " << kw("fg") << " / " << kw("bg") << "   job control\n";
        std::cout << "  " << kw("retry") << " N cmd            run cmd up to N times\n";
        std::cout << "  " << kw("timeout") << " N cmd          kill cmd after N seconds\n";
        std::cout << "  " << kw("watch") << " N cmd            run cmd every N seconds\n";
        std::cout << "  " << kw("exit") << " [n]              exit shell\n";
        return;
    }
    if (topic == "expansion") {
        std::cout << h("Expansion\n\n");
        std::cout << "  " << kw("{a,b,c}") << "  " << kw("{1..10}") << "       brace expansion\n";
        std::cout << "  " << kw("$(cmd)") << "  " << kw("`cmd`") << "        command substitution\n";
        std::cout << "  " << kw("$((1+2))") << "                arithmetic expansion\n";
        std::cout << "  " << kw("${var:-def}") << "              default value\n";
        std::cout << "  " << kw("${var#pat}") << "               strip leading prefix\n";
        std::cout << "  " << kw("${var%pat}") << "               strip trailing suffix\n";
        std::cout << "  " << kw("${var/pat/repl}") << "          pattern replace\n";
        std::cout << "  " << kw("* ? [abc] **") << "             glob patterns\n";
        std::cout << "  " << kw("*(.)") << " " << kw("*(/)") << "              files / dirs only\n";
        return;
    }
    if (topic == "redirection") {
        std::cout << h("Redirection\n\n");
        std::cout << "  " << kw(">") << "  " << kw(">>") << "  " << kw("<") << "  " << kw("2>") << "  " << kw("2>>") << "  " << kw("&>") << "  " << kw("&>>") << "\n";
        std::cout << "  " << kw("<< TERM") << "   here-document\n";
        std::cout << "  " << kw("<<< str") << "   here-string\n";
        std::cout << "  " << kw("<(cmd)") << "    process substitution (read)\n";
        std::cout << "  " << kw(">(cmd)") << "    process substitution (write)\n";
        return;
    }
    if (topic == "scripting") {
        std::cout << h("Scripting\n\n");
        std::cout << "  " << kw("if") << " / " << kw("elif") << " / " << kw("else") << " / " << kw("fi") << "\n";
        std::cout << "  " << kw("while") << " / " << kw("do") << " / " << kw("done") << "\n";
        std::cout << "  " << kw("for") << " VAR " << kw("in") << " list / " << kw("do") << " / " << kw("done") << "\n";
        std::cout << "  " << kw("case") << " WORD " << kw("in") << " pat) cmds ;; ... " << kw("esac") << "\n";
        std::cout << "  " << kw("{ cmd1; cmd2; }") << "   logical group\n";
        std::cout << "  " << kw("( cmd1; cmd2 )") << "    subshell\n";
        std::cout << "  " << kw("$PIPESTATUS") << "        exit codes of last pipeline\n";
        return;
    }
    if (topic == "themes") {
        std::cout << h("Themes\n\n");
        std::cout << "  " << kw("tshell theme list") << "  " << kw("tshell theme <n>") << "  " << kw("tshell theme preview <n>") << "\n";
        std::cout << "\n  Tokens: $USER $HOST $CWD $EXITSTR + mod tokens\n";
        std::cout << "  Colors: %reset %bold %dim %red %green %yellow %blue %magenta %cyan\n";
        std::cout << "  Config: " << kw("theme FancyML-1") << "  or  " << kw("prompt <fmt>") << "\n";
        return;
    }
    if (topic == "mods") {
        std::cout << h("Mods\n\n");
        std::cout << "  " << kw("tshell mods list") << "  " << kw("tshell mods info <n>") << "\n";
        std::cout << "\n  Formats: <n>.so | manifest.json | <n>.tmod | mod.py\n";
        std::cout << "  Build: " << kw("tshc build mymod/") << "  Install: " << kw("tshc install mymod/") << "\n";
        return;
    }
    if (topic == "debug") {
        std::cout << h("Debug system\n\n");
        std::cout << "  " << kw("tshell debug on|off") << "         master debug toggle\n";
        std::cout << "  " << kw("tshell debug level <0-4>") << "    0=trace 1=verbose 2=info 3=warn 4=error\n";
        std::cout << "  " << kw("tshell debug log <file>") << "     log to file\n";
        std::cout << "  " << kw("tshell debug log off") << "        close log file\n";
        std::cout << "  " << kw("tshell debug json on|off") << "    JSON exec records\n";
        std::cout << "  " << kw("tshell debug timestamps on|off") << "\n";
        std::cout << "  " << kw("tshell trace on|off") << "         set -x style trace (stderr)\n";
        std::cout << "  " << kw("tshell verbose on|off") << "       parsing step output\n";
        return;
    }
    // Default / tshell topic
    std::cout << cc(Color::BCYAN) << cc(Color::BOLD) << "tsh — Turtle Shell v4.0" << cc(Color::RESET) << "\n\n";
    std::cout << h("Help topics: ") << kw("builtins") << " " << kw("expansion") << " "
              << kw("redirection") << " " << kw("scripting") << " " << kw("themes")
              << " " << kw("mods") << " " << kw("debug") << "\n\n";
    std::cout << h("tshell subcommands:\n");
    std::cout << "  " << kw("tshell theme <n>|list|preview <n>") << "\n";
    std::cout << "  " << kw("tshell mods list|info <n>") << "\n";
    std::cout << "  " << kw("tshell explain <cmd|mod> [argpattern]") << "\n";
    std::cout << "  " << kw("tshell inspect <cmd>") << "          inspect type, path, origin, timing\n";
    std::cout << "  " << kw("tshell introspect") << "             show last pipeline breakdown\n";
    std::cout << "  " << kw("tshell jump <partial>") << "  — z-style directory jump\n";
    std::cout << "  " << kw("tshell persist VAR=val") << "  " << kw("tshell unpersist VAR") << "\n";
    std::cout << "  " << kw("tshell reload") << "  " << kw("tshell config check") << "\n";
    std::cout << "  " << kw("tshell trace on|off") << "  " << kw("tshell verbose on|off") << "\n";
    std::cout << "  " << kw("tshell debug on|off|level|log|json|timestamps") << "\n";
    std::cout << "  " << kw("tshell version") << "\n";
}

// =============================================================================
//  handleBuiltin
// =============================================================================

bool handleBuiltin(const std::vector<std::string>& args, bool& shouldExit, int& rc) {
    if (args.empty()) return false;
    std::string cmd = toLower(args[0]);

    // ── exit ──
    if (cmd == "exit") {
        rc = (args.size() > 1) ? std::atoi(args[1].c_str()) : g_lastExit;
        shouldExit = true;
        return true;
    }

    // ── cd ──
    if (cmd == "cd") {
        std::string target;
        if (args.size() < 2) { const char* h = getenv("HOME"); target = h ? h : "/"; }
        else if (args[1] == "-") {
            const char* op = getenv("OLDPWD");
            target = op ? op : "";
            if (target.empty()) { std::cerr << "cd: OLDPWD not set\n"; rc = 1; return true; }
            std::cout << target << "\n";
        } else {
            target = expandHome(args[1]);
        }
        std::string prev = getCwd();
        if (::chdir(target.c_str()) != 0) {
            std::cerr << cc(Color::BRED) << "cd: " << target << ": " << std::strerror(errno) << cc(Color::RESET) << "\n";
            rc = 1;
        } else {
            setenv("OLDPWD", prev.c_str(), 1);
            std::error_code ec;
            fs::path cwd = fs::current_path(ec);
            if (!ec) { setenv("PWD", cwd.c_str(), 1); trackDir(cwd.string()); }
            fireHook(TshEvent::DirChange, getCwd());
            rc = 0;
        }
        return true;
    }

    // ── export ──
    if (cmd == "export") {
        rc = 0;
        for (size_t i = 1; i < args.size(); ++i) {
            auto eq = args[i].find('=');
            if (eq == std::string::npos) { setenv(args[i].c_str(), "", 1); continue; }
            setenv(args[i].substr(0, eq).c_str(), expandAll(args[i].substr(eq + 1)).c_str(), 1);
        }
        return true;
    }

    // ── alias ──
    if (cmd == "alias") {
        rc = 0;
        if (args.size() < 2) { for (auto& [k, v] : g_cfg.aliases) std::cout << "alias " << k << "='" << v << "'\n"; return true; }
        for (size_t i = 1; i < args.size(); ++i) {
            auto eq = args[i].find('=');
            if (eq == std::string::npos) {
                auto it = g_cfg.aliases.find(args[i]);
                if (it != g_cfg.aliases.end()) std::cout << "alias " << it->first << "='" << it->second << "'\n";
                else std::cerr << "alias: " << args[i] << ": not found\n";
            } else { g_cfg.aliases[args[i].substr(0, eq)] = args[i].substr(eq + 1); }
        }
        return true;
    }

    // ── unalias ──
    if (cmd == "unalias") { rc = 0; for (size_t i = 1; i < args.size(); ++i) g_cfg.aliases.erase(args[i]); return true; }

    // ── echo ──
    if (cmd == "echo") {
        rc = 0;
        bool newline = true; size_t start = 1;
        if (args.size() > 1 && args[1] == "-n") { newline = false; start = 2; }
        for (size_t i = start; i < args.size(); ++i) { if (i > start) std::cout << ' '; std::cout << args[i]; }
        if (newline) std::cout << '\n';
        return true;
    }

    // ── source / . ──
    if (cmd == "source" || cmd == ".") {
        if (args.size() < 2) { std::cerr << "source: filename required\n"; rc = 1; return true; }
        rc = runScript(expandHome(args[1]), std::vector<std::string>(args.begin() + 2, args.end()));
        return true;
    }

    // ── jobs ──
    if (cmd == "jobs") { rc = 0; reapJobs(); for (auto& j : g_jobs) printJob(j); return true; }

    // ── fg ──
    if (cmd == "fg") {
        rc = 1; reapJobs();
        Job* j = nullptr;
        if (args.size() < 2) { if (!g_jobs.empty()) j = &g_jobs.back(); }
        else { std::string spec = args[1]; if (!spec.empty() && spec[0] == '%') spec = spec.substr(1); j = findJob(std::atoi(spec.c_str())); }
        if (!j) { std::cerr << "fg: no such job\n"; return true; }
        std::cout << j->cmdline << "\n"; j->status = Job::Status::Running;
        killpg(j->pgid, SIGCONT); tcsetpgrp(STDIN_FILENO, j->pgid);
        rc = waitFg(*j); tcsetpgrp(STDIN_FILENO, getpgrp()); return true;
    }

    // ── bg ──
    if (cmd == "bg") {
        rc = 1; reapJobs();
        Job* j = nullptr;
        if (args.size() < 2) { if (!g_jobs.empty()) j = &g_jobs.back(); }
        else { std::string spec = args[1]; if (!spec.empty() && spec[0] == '%') spec = spec.substr(1); j = findJob(std::atoi(spec.c_str())); }
        if (!j || j->status != Job::Status::Stopped) { std::cerr << "bg: no stopped job\n"; return true; }
        j->status = Job::Status::Running; killpg(j->pgid, SIGCONT);
        std::cout << "[" << j->id << "] " << j->cmdline << " &\n"; rc = 0; return true;
    }

    // ── type ──
    if (cmd == "type") {
        rc = 0;
        static const std::set<std::string> builtins2 = {
            "cd","exit","echo","export","alias","unalias","source","help",
            "jobs","fg","bg","tshell","retry","timeout","type","history","watch"
        };
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& n = args[i];
            if (builtins2.count(n)) { std::cout << n << " is a shell builtin\n"; continue; }
            auto ait = g_cfg.aliases.find(n);
            if (ait != g_cfg.aliases.end()) { std::cout << n << " is aliased to '" << ait->second << "'\n"; continue; }
            bool isMod = false;
            for (auto& c : g_commands) if (c.name == n && c.executor) { isMod = true; std::cout << n << " is a mod command\n"; break; }
            if (isMod) continue;
            std::string full = resolveInPath(n);
            if (!full.empty()) { std::cout << n << " is " << full << "\n"; continue; }
            std::cerr << n << ": not found\n"; rc = 1;
        }
        return true;
    }

    // ── history ──
    if (cmd == "history") {
        rc = 0;
        if (args.size() < 2) {
            for (int i = 1; i <= history_length; ++i) {
                HIST_ENTRY* he = history_get(i);
                if (he) std::cout << "  " << i << "\t" << he->line << "\n";
            }
            return true;
        }
        std::string sub = toLower(args[1]);
        if (sub == "clear") { clear_history(); std::cout << "History cleared.\n"; return true; }
        if (sub == "tag" && args.size() >= 3) {
            int n = std::atoi(args[2].c_str());
            HIST_ENTRY* he = history_get(n);
            if (he) std::cout << "Tagged: " << he->line << "\n";
            return true;
        }
        int n2 = std::atoi(args[1].c_str());
        int start2 = std::max(1, history_length - n2 + 1);
        for (int i = start2; i <= history_length; ++i) {
            HIST_ENTRY* he = history_get(i);
            if (he) std::cout << "  " << i << "\t" << he->line << "\n";
        }
        return true;
    }

    // ── retry ──
    if (cmd == "retry") {
        rc = 1;
        if (args.size() < 3) { std::cerr << "retry: usage: retry N cmd [args...]\n"; return true; }
        int n3 = std::atoi(args[1].c_str());
        std::string subcmd; for (size_t i = 2; i < args.size(); ++i) { if (i > 2) subcmd += " "; subcmd += args[i]; }
        for (int attempt = 0; attempt < n3; ++attempt) {
            rc = runPipeline(subcmd);
            if (rc == 0) return true;
            if (attempt < n3 - 1) std::cerr << "[retry " << (attempt + 1) << "/" << n3 << "] exit " << rc << "\n";
        }
        std::cerr << "[retry] all " << n3 << " attempts failed\n"; return true;
    }

    // ── timeout ──
    if (cmd == "timeout") {
        rc = 1;
        if (args.size() < 3) { std::cerr << "timeout: usage: timeout N cmd [args...]\n"; return true; }
        int secs = std::atoi(args[1].c_str());
        std::string subcmd; for (size_t i = 2; i < args.size(); ++i) { if (i > 2) subcmd += " "; subcmd += args[i]; }
        pid_t pid = fork();
        if (pid == 0) { signal(SIGINT, SIG_DFL); signal(SIGTSTP, SIG_DFL); int r = runPipeline(subcmd); _exit(r); }
        std::thread timer([pid, secs]() { std::this_thread::sleep_for(std::chrono::seconds(secs)); kill(pid, SIGTERM); });
        int st = 0; waitpid(pid, &st, 0);
        timer.detach();
        rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        if (WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM)
            std::cerr << cc(Color::BRED) << "[timeout] killed after " << secs << "s" << cc(Color::RESET) << "\n";
        return true;
    }

    // ── watch ──
    if (cmd == "watch") {
        rc = 0;
        if (args.size() < 3) { std::cerr << "watch: usage: watch N cmd [args...]\n"; return true; }
        int secs2 = std::atoi(args[1].c_str());
        std::string subcmd; for (size_t i = 2; i < args.size(); ++i) { if (i > 2) subcmd += " "; subcmd += args[i]; }
        std::cout << "Watching '" << subcmd << "' every " << secs2 << "s. Ctrl-C to stop.\n";
        while (true) { std::cout << "\033[2J\033[H"; runPipeline(subcmd); std::this_thread::sleep_for(std::chrono::seconds(secs2)); }
        return true;
    }

    // ── exec ──
    if (cmd == "exec") {
        if (args.size() < 2) { std::cerr << "exec: command required\n"; rc = 1; return true; }
        std::vector<const char*> argv;
        for (size_t i = 1; i < args.size(); ++i) argv.push_back(args[i].c_str());
        argv.push_back(nullptr);
        execvp(args[1].c_str(), (char* const*)argv.data());
        perror("exec"); _exit(127);
    }

    // ── which ──
    if (cmd == "which") {
        rc = 0;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string full = resolveInPath(args[i]);
            if (!full.empty()) std::cout << full << "\n";
            else { std::cerr << args[i] << " not found\n"; rc = 1; }
        }
        return true;
    }

    // ── command ──
    if (cmd == "command") {
        if (args.size() < 2) { rc = 0; return true; }
        std::string subcmd; for (size_t i = 1; i < args.size(); ++i) { if (i > 1) subcmd += " "; subcmd += args[i]; }
        auto savedAliases = g_cfg.aliases; g_cfg.aliases.clear();
        rc = execLine(subcmd);
        g_cfg.aliases = savedAliases; return true;
    }

    // ── builtin ──
    if (cmd == "builtin") {
        if (args.size() < 2) { rc = 0; return true; }
        std::vector<std::string> sub(args.begin() + 1, args.end());
        bool se = false; handleBuiltin(sub, se, rc); return true;
    }

    // ── pushd ──
    if (cmd == "pushd") {
        std::string target;
        if (args.size() < 2) {
            if (g_dirStack.empty()) { std::cerr << "pushd: no other directory\n"; rc = 1; return true; }
            target = g_dirStack.back(); g_dirStack.pop_back();
        } else { target = expandHome(args[1]); }
        std::string cwd = getCwd();
        if (::chdir(target.c_str()) != 0) { std::cerr << "pushd: " << target << ": " << std::strerror(errno) << "\n"; rc = 1; }
        else {
            g_dirStack.push_back(cwd);
            std::error_code ec; fs::path newCwd = fs::current_path(ec);
            if (!ec) setenv("PWD", newCwd.c_str(), 1);
            for (auto& d : g_dirStack) std::cout << d << " ";
            std::cout << (ec ? target : newCwd.string()) << "\n"; rc = 0;
        }
        return true;
    }

    // ── popd ──
    if (cmd == "popd") {
        if (g_dirStack.empty()) { std::cerr << "popd: directory stack empty\n"; rc = 1; return true; }
        std::string target = g_dirStack.back(); g_dirStack.pop_back();
        if (::chdir(target.c_str()) != 0) { std::cerr << "popd: " << target << ": " << std::strerror(errno) << "\n"; rc = 1; }
        else {
            std::error_code ec; fs::path cwd = fs::current_path(ec);
            if (!ec) setenv("PWD", cwd.c_str(), 1);
            for (auto& d : g_dirStack) std::cout << d << " ";
            std::cout << (ec ? target : cwd.string()) << "\n"; rc = 0;
        }
        return true;
    }

    // ── dirs ──
    if (cmd == "dirs") { for (auto& d : g_dirStack) std::cout << d << " "; std::cout << getCwd() << "\n"; rc = 0; return true; }

    // ── realpath ──
    if (cmd == "realpath") {
        rc = 0;
        for (size_t i = 1; i < args.size(); ++i) {
            std::error_code ec; fs::path abs = fs::canonical(args[i], ec);
            if (ec) { std::cerr << "realpath: " << args[i] << ": " << ec.message() << "\n"; rc = 1; }
            else std::cout << abs.string() << "\n";
        }
        return true;
    }

    // ── readlink ──
    if (cmd == "readlink") {
        rc = 0;
        for (size_t i = 1; i < args.size(); ++i) {
            std::error_code ec; fs::path target = fs::read_symlink(args[i], ec);
            if (ec) { std::cerr << "readlink: " << args[i] << ": " << ec.message() << "\n"; rc = 1; }
            else std::cout << target.string() << "\n";
        }
        return true;
    }

    // ── readonly ──
    if (cmd == "readonly") {
        if (args.size() < 2) {
            for (auto& v : g_readonlyVars) {
                auto it = g_vars.find(v);
                if (it != g_vars.end()) std::cout << "readonly " << v << "=" << it->second.scalar << "\n";
            }
            rc = 0; return true;
        }
        for (size_t i = 1; i < args.size(); ++i) {
            auto eq = args[i].find('=');
            std::string name = (eq == std::string::npos) ? args[i] : args[i].substr(0, eq);
            g_readonlyVars.insert(name);
            if (eq != std::string::npos) g_vars[name].scalar = expandAll(args[i].substr(eq + 1));
        }
        rc = 0; return true;
    }

    // ── read ──
    if (cmd == "read") {
        std::string prompt, varName;
        if (args.size() == 2) { varName = args[1]; }
        else if (args.size() == 4 && args[1] == "-p") { prompt = args[2]; varName = args[3]; }
        else { std::cerr << "read: usage: read [-p prompt] var\n"; rc = 1; return true; }
        if (!prompt.empty()) std::cout << prompt;
        std::string line; std::getline(std::cin, line);
        g_vars[varName].scalar = line; rc = 0; return true;
    }

    // ── test / [ ──
    if (cmd == "test" || cmd == "[") {
        rc = 1;
        if (args.size() < 2) return true;
        std::string op = args[1];
        if (op == "-e" && args.size() == 3) { std::error_code ec; rc = fs::exists(args[2], ec) ? 0 : 1; }
        else if (op == "-d" && args.size() == 3) { std::error_code ec; rc = fs::is_directory(args[2], ec) ? 0 : 1; }
        else if (op == "-f" && args.size() == 3) { std::error_code ec; rc = fs::is_regular_file(args[2], ec) ? 0 : 1; }
        else if (op == "-z" && args.size() == 3) { rc = args[2].empty() ? 0 : 1; }
        else if (op == "-n" && args.size() == 3) { rc = args[2].empty() ? 1 : 0; }
        else if (args.size() == 4 && args[2] == "=") { rc = (args[1] == args[3]) ? 0 : 1; }
        return true;
    }

    // ── shift ──
    if (cmd == "shift") { rc = 0; return true; }

    // ── wait ──
    if (cmd == "wait") {
        rc = 0; reapJobs();
        for (auto& j : g_jobs) if (j.status == Job::Status::Running) for (auto pid : j.pids) { int st; waitpid(pid, &st, 0); }
        g_jobs.clear(); return true;
    }

    // ── kill ──
    if (cmd == "kill") {
        rc = 0;
        for (size_t i = 1; i < args.size(); ++i) {
            int sig = SIGTERM; std::string target = args[i];
            if (target[0] == '-') { sig = std::atoi(target.substr(1).c_str()); if (++i >= args.size()) break; target = args[i]; }
            pid_t pid = std::atoi(target.c_str());
            if (kill(pid, sig) != 0) { perror("kill"); rc = 1; }
        }
        return true;
    }

    // ── time ──
    if (cmd == "time") {
        if (args.size() < 2) { rc = 0; return true; }
        std::string subcmd; for (size_t i = 1; i < args.size(); ++i) { if (i > 1) subcmd += " "; subcmd += args[i]; }
        auto t0 = std::chrono::steady_clock::now();
        rc = execLine(subcmd);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::cerr << "\nreal\t" << ms << "ms\n"; return true;
    }

    // ── hash ──
    if (cmd == "hash") {
        if (args.size() > 1 && args[1] == "-r") {
            g_cmdHashCache.clear(); g_pathCache.clear();
            std::cout << "Command hash table cleared.\n";
        } else {
            for (auto& [c, p] : g_cmdHashCache) std::cout << c << "\t" << p << "\n";
        }
        rc = 0; return true;
    }

    // ── help ──
    if (cmd == "help") { printHelp(args.size() > 1 ? args[1] : ""); rc = 0; return true; }

    // ── tshell ──
    if (cmd == "tshell") {
        rc = 0;
        if (args.size() < 2) { printHelp("tshell"); return true; }
        std::string sub = toLower(args[1]);

        if (sub == "version") {
            std::cout << cc(Color::BCYAN) << "tsh v4.0" << cc(Color::RESET) << "  (API v" << TSH_API_VERSION << ")\n";
            return true;
        }

        if (sub == "introspect") {
            tshIntrospectPrint(g_lastPipeline);
            return true;
        }

        if (sub == "reload") {
            const char* home = getenv("HOME");
            if (home) loadConfig(std::string(home) + "/.tshcfg", g_cfg);
            std::cout << "Config reloaded.\n"; return true;
        }

        if (sub == "config") {
            std::string sub2 = (args.size() > 2) ? toLower(args[2]) : "";
            if (sub2 == "check") {
                const char* home = getenv("HOME");
                validateConfig(home ? std::string(home) + "/.tshcfg" : ".tshcfg");
            }
            return true;
        }

        if (sub == "trace") {
            if (args.size() > 2) g_cfg.traceMode = (toLower(args[2]) == "on" || args[2] == "1");
            std::cout << "Trace: " << (g_cfg.traceMode ? "on" : "off") << "\n"; return true;
        }

        if (sub == "verbose") {
            if (args.size() > 2) g_cfg.verboseMode = (toLower(args[2]) == "on" || args[2] == "1");
            std::cout << "Verbose: " << (g_cfg.verboseMode ? "on" : "off") << "\n"; return true;
        }

        // ── tshell debug ── (fully expanded)
        if (sub == "debug") {
            if (args.size() < 3) { printHelp("debug"); return true; }
            std::string sub2 = toLower(args[2]);

            if (sub2 == "on")  { g_debugCfg.enabled = true;  std::cout << "Debug: on\n";  return true; }
            if (sub2 == "off") { g_debugCfg.enabled = false; std::cout << "Debug: off\n"; return true; }

            if (sub2 == "level" && args.size() >= 4) {
                int lv = std::atoi(args[3].c_str());
                lv = std::max(0, std::min(4, lv));
                g_debugCfg.minLevel = static_cast<DebugLevel>(lv);
                static const char* names[] = {"trace","verbose","info","warn","error"};
                std::cout << "Debug level: " << names[lv] << "\n";
                return true;
            }

            if (sub2 == "log" && args.size() >= 4) {
                if (toLower(args[3]) == "off") { tshDebugCloseLog(); std::cout << "Debug log closed.\n"; }
                else { tshDebugOpenLog(args[3]); std::cout << "Debug log: " << args[3] << "\n"; }
                return true;
            }

            if (sub2 == "json" && args.size() >= 4) {
                g_cfg.debugJson = (toLower(args[3]) == "on" || args[3] == "1");
                std::cout << "Debug JSON: " << (g_cfg.debugJson ? "on" : "off") << "\n";
                return true;
            }

            if (sub2 == "timestamps" && args.size() >= 4) {
                g_debugCfg.timestamps = (toLower(args[3]) == "on" || args[3] == "1");
                std::cout << "Debug timestamps: " << (g_debugCfg.timestamps ? "on" : "off") << "\n";
                return true;
            }

            if (sub2 == "status") {
                std::cout << "Debug enabled:    " << (g_debugCfg.enabled ? "yes" : "no") << "\n";
                std::cout << "Min level:        " << static_cast<int>(g_debugCfg.minLevel) << "\n";
                std::cout << "Timestamps:       " << (g_debugCfg.timestamps ? "on" : "off") << "\n";
                std::cout << "JSON mode:        " << (g_cfg.debugJson ? "on" : "off") << "\n";
                std::cout << "Log file:         " << (g_debugCfg.logFile.empty() ? "(none)" : g_debugCfg.logFile) << "\n";
                return true;
            }

            std::cerr << "tshell debug: unknown option '" << sub2 << "'\n";
            printHelp("debug"); rc = 1; return true;
        }

        if (sub == "theme") {
            if (args.size() < 3 || toLower(args[2]) == "list") {
                std::cout << cc(Color::BOLD) << "Available themes:" << cc(Color::RESET) << "\n";
                for (auto& [name, t] : g_themes) {
                    bool active = (name == g_cfg.activeTheme);
                    std::cout << (active ? std::string(cc(Color::BGREEN)) + "* " : "  ");
                    std::cout << cc(Color::BOLD) << name << cc(Color::RESET);
                    if (!t.description.empty()) std::cout << "  —  " << t.description;
                    std::cout << cc(Color::RESET) << "\n";
                }
                return true;
            }
            if (toLower(args[2]) == "preview" && args.size() >= 4) {
                auto it = g_themes.find(args[3]);
                if (it == g_themes.end()) { std::cerr << "Unknown theme '" << args[3] << "'\n"; rc = 1; return true; }
                std::string saved = g_cfg.activeTheme, savedFmt = g_cfg.promptFmt;
                g_cfg.activeTheme = args[3]; g_cfg.promptFmt = "";
                std::cout << "Preview [" << args[3] << "]: " << buildPrompt() << "\n";
                g_cfg.activeTheme = saved; g_cfg.promptFmt = savedFmt; return true;
            }
            auto it = g_themes.find(args[2]);
            if (it == g_themes.end()) {
                std::cerr << cc(Color::BRED) << "tshell theme: unknown theme '" << args[2] << "'\n" << cc(Color::RESET);
                std::cerr << "Run 'tshell theme list'\n"; rc = 1; return true;
            }
            g_cfg.activeTheme = args[2]; g_cfg.promptFmt = "";
            std::cout << cc(Color::BGREEN) << "Theme: " << cc(Color::BOLD) << args[2] << cc(Color::RESET) << "\n";
            return true;
        }

        if (sub == "mods") {
            std::string sub2 = (args.size() > 2) ? toLower(args[2]) : "list";
            if (sub2 == "list") {
                if (g_mods.empty()) { std::cout << "No mods loaded.\n"; return true; }
                std::cout << cc(Color::BOLD) << "Loaded mods:" << cc(Color::RESET) << "\n";
                for (auto& lm : g_mods) {
                    auto& m = lm.mod->meta;
                    std::cout << "  " << cc(Color::BBLUE) << m.name << cc(Color::RESET);
                    if (!m.version.empty()) std::cout << " v" << m.version;
                    if (!m.author.empty())  std::cout << " by " << m.author;
                    std::cout << "\n";
                    if (!m.description.empty()) std::cout << "    " << m.description << "\n";
                }
                return true;
            }
            if (sub2 == "info" && args.size() >= 4) {
                for (auto& lm : g_mods) {
                    if (lm.mod->meta.name == args[3] || lm.mod->meta.id == args[3]) {
                        auto& m = lm.mod->meta;
                        std::cout << cc(Color::BOLD) << m.name << cc(Color::RESET) << "\n";
                        std::cout << "  ID:          " << m.id          << "\n";
                        std::cout << "  Version:     " << m.version     << "\n";
                        std::cout << "  Author:      " << m.author      << "\n";
                        std::cout << "  Description: " << m.description << "\n";
                        std::cout << "  API version: " << m.apiVersion  << "\n";
                        std::cout << "  Source:      " << lm.sourcePath << "\n";
                        return true;
                    }
                }
                std::cerr << "Mod '" << args[3] << "' not found\n"; rc = 1; return true;
            }
            return true;
        }

        if (sub == "jump") {
            if (args.size() < 3) { std::cerr << "usage: tshell jump <partial>\n"; rc = 1; return true; }
            std::string partial = args[2];
            std::string best; double bestScore = -1;
            for (auto& [path, score] : g_dirFrecency)
                if (path.find(partial) != std::string::npos && score > bestScore) { bestScore = score; best = path; }
            if (best.empty()) { std::cerr << "tshell jump: no match for '" << partial << "'\n"; rc = 1; }
            else {
                std::cout << best << "\n";
                if (::chdir(best.c_str()) == 0) { setenv("PWD", best.c_str(), 1); fireHook(TshEvent::DirChange, best); }
            }
            return true;
        }

        if (sub == "persist") {
            for (size_t i = 2; i < args.size(); ++i) {
                auto eq = args[i].find('=');
                if (eq == std::string::npos) {
                    auto it = g_vars.find(args[i]);
                    if (it != g_vars.end()) std::cout << args[i] << "=" << it->second.scalar << " (persist=" << it->second.persist << ")\n";
                    else std::cerr << "tshell persist: " << args[i] << " not set\n";
                } else {
                    std::string key = args[i].substr(0, eq), val = expandAll(args[i].substr(eq + 1));
                    ShellVar& sv = g_vars[key]; sv.scalar = val; sv.persist = true; sv.isArray = false;
                    savePersistVars(); std::cout << key << " persisted.\n";
                }
            }
            return true;
        }

        if (sub == "unpersist") {
            for (size_t i = 2; i < args.size(); ++i) {
                auto it = g_vars.find(args[i]); if (it != g_vars.end()) it->second.persist = false;
            }
            savePersistVars(); return true;
        }

        if (sub == "engine") {
            if (args.size() < 3) {
                std::cout << "Prompt engine tokens:\n";
                for (auto& [name, enabled] : g_engineTokenEnabled)
                    std::cout << "  $E_" << name << "\t" << (enabled ? "on" : "off") << "\n";
                return true;
            }
            std::string token = args[2];
            if (args.size() >= 4) {
                bool enable = (toLower(args[3]) == "on" || args[3] == "1");
                g_engineTokenEnabled[token] = enable;
                std::cout << "$E_" << token << " " << (enable ? "enabled" : "disabled") << "\n";
            }
            return true;
        }

        if (sub == "explain") {
            // Usage: tshell explain <cmd|modname> [argpattern]
            if (args.size() < 3) {
                std::cerr << "usage: tshell explain <cmd|mod> [argpattern]\n";
                rc = 1; return true;
            }
            std::string target     = args[2];
            std::string argPattern = (args.size() > 3) ? args[3] : "";

            // ── Check for a loaded mod by that name / id first ────────────
            for (auto& lm : g_mods) {
                const auto& m = lm.mod->meta;
                if (m.id == target || m.name == target) {
                    std::cout << cc(Color::BOLD) << "Mod: " << m.name
                              << cc(Color::RESET) << "  (id: " << m.id << ")"
                              << "  Version : " << m.version << ""
                              << "  Author  : " << m.author  << ""
                              << "  About   : " << m.description << "";
                    // List commands and tshell sub-commands this mod provides
                    for (auto& c : g_commands)
                        if (!c.name.empty() && g_cmdStats.count(c.name) &&
                            g_cmdStats.at(c.name).pluginOrigin == m.id)
                            std::cout << "  command : " << c.name << "\n";
                    for (auto& [cname, tc] : g_tshCommands)
                        if (tc.owner && (tc.owner->meta.id == m.id || tc.owner->meta.name == m.name))
                            std::cout << "  tshell  : tshell " << cname
                                      << (tc.helpText.empty() ? "" : "  — " + tc.helpText) << "\n";
                    return true;
                }
            }

            // ── Fall back to command explanation table ─────────────────────
            auto it = g_explanations.find(target);
            if (it == g_explanations.end()) {
                std::cerr << "No explanation registered for: " << target << "\n"
                          << "  (Use a mod to call shell->createExplanation() to add one)\n";
                rc = 1; return true;
            }
            auto& patterns = it->second;
            auto pit = patterns.find(argPattern);
            if (pit == patterns.end()) pit = patterns.find("");
            if (pit == patterns.end()) {
                std::cerr << "No explanation for: " << target << " " << argPattern << "\n";
                rc = 1; return true;
            }
            std::cout << cc(Color::BOLD) << target;
            if (!argPattern.empty()) std::cout << " " << argPattern;
            std::cout << cc(Color::RESET) << "\n\n";
            // Column-aligned tabular output
            for (auto& row : pit->second) {
                for (size_t i = 0; i < row.size(); ++i) {
                    if (i) std::cout << "\t";
                    std::cout << row[i];
                }
                std::cout << "\n";
            }
            return true;
        }

        if (sub == "inspect") {
            // Usage: tshell inspect <cmd>
            // Returns: alias?, builtin?, resolved path, plugin origin,
            //          last exec time, performance stats
            if (args.size() < 3) {
                std::cerr << "usage: tshell inspect <cmd>\n";
                rc = 1; return true;
            }
            std::string name = args[2];
            auto col  = [&](const char* c) { return g_cfg.colorEnable ? c : ""; };
            auto bold = [&]() { return col(Color::BOLD);  };
            auto dim  = [&]() { return col(Color::DIM);   };
            auto rst  = [&]() { return col(Color::RESET); };
            auto cyan = [&]() { return col(Color::BCYAN); };
            auto grn  = [&]() { return col(Color::BGREEN);};

            std::cout << bold() << "inspect: " << cyan() << name << rst() << "\n\n";

            // is alias?
            auto ait = g_cfg.aliases.find(name);
            if (ait != g_cfg.aliases.end())
                std::cout << "  " << bold() << "alias     " << rst()
                          << ": yes → " << grn() << ait->second << rst() << "\n";
            else
                std::cout << "  " << bold() << "alias     " << rst()
                          << ": " << dim() << "no" << rst() << "\n";

            // is builtin?
            static const std::vector<std::string> builtinNames = {
                "cd","exit","echo","export","alias","unalias","source","help",
                "jobs","fg","bg","tshell","retry","timeout","type","history","watch"
            };
            bool isBuiltin = std::find(builtinNames.begin(), builtinNames.end(), name) != builtinNames.end();
            std::cout << "  " << bold() << "builtin   " << rst()
                      << ": " << (isBuiltin ? grn() + std::string("yes") : dim() + std::string("no")) << rst() << "\n";

            // resolved path
            std::string resolved;
            // check mod commands first
            std::string modOrigin;
            for (auto& c : g_commands) {
                if (c.name == name) {
                    resolved = c.filePath.string();
                    auto sit = g_cmdStats.find(name);
                    if (sit != g_cmdStats.end()) modOrigin = sit->second.pluginOrigin;
                    break;
                }
            }
            if (resolved.empty()) resolved = resolveInPath(name);
            std::cout << "  " << bold() << "path      " << rst()
                      << ": " << (resolved.empty() ? dim() + std::string("(not found)") + rst()
                                                   : grn() + resolved + rst()) << "\n";

            // plugin origin
            std::cout << "  " << bold() << "origin    " << rst() << ": ";
            if (!modOrigin.empty())
                std::cout << grn() << "mod:" << modOrigin << rst() << "\n";
            else if (isBuiltin)
                std::cout << grn() << "builtin" << rst() << "\n";
            else if (!resolved.empty())
                std::cout << "system" << "\n";
            else
                std::cout << dim() << "unknown" << rst() << "\n";

            // exec stats
            auto sit = g_cmdStats.find(name);
            if (sit != g_cmdStats.end() && sit->second.runCount > 0) {
                auto& cs = sit->second;
                std::cout << "  " << bold() << "last exec " << rst()
                          << ": " << cs.lastExecMs << " ms\n";
                std::cout << "  " << bold() << "run count " << rst()
                          << ": " << cs.runCount << "\n";
                std::cout << "  " << bold() << "avg exec  " << rst()
                          << ": " << cs.avgExecMs << " ms\n";
            } else {
                std::cout << "  " << bold() << "last exec " << rst()
                          << ": " << dim() << "(no data this session)" << rst() << "\n";
            }
            return true;
        }

        if (sub == "mod") {
            if (args.size() < 4) { std::cerr << "usage: tshell mod <modname> <command> [args...]\n"; rc = 1; return true; }
            std::string modName = args[2], cmdName = args[3];
            auto it = g_tshCommands.find(cmdName);
            if (it == g_tshCommands.end()) { std::cerr << "tshell mod: unknown command '" << cmdName << "'\n"; rc = 1; return true; }
            if (it->second.owner && it->second.owner->meta.id != modName && it->second.owner->meta.name != modName) {
                std::cerr << "tshell mod: '" << cmdName << "' not owned by '" << modName << "'\n"; rc = 1; return true;
            }
            std::vector<char*> argv;
            for (size_t i = 3; i < args.size(); ++i) argv.push_back(const_cast<char*>(args[i].c_str()));
            argv.push_back(nullptr);
            rc = it->second.run((int)argv.size() - 1, argv.data());
            return true;
        }

        if (sub == "help") { printHelp(args.size() > 2 ? args[2] : ""); return true; }

        // Check mod-registered tshell subcommands
        auto tcit = g_tshCommands.find(sub);
        if (tcit != g_tshCommands.end()) {
            std::vector<char*> argv;
            for (size_t i = 1; i < args.size(); ++i) argv.push_back(const_cast<char*>(args[i].c_str()));
            argv.push_back(nullptr);
            rc = tcit->second.run((int)argv.size() - 1, argv.data());
            return true;
        }

        std::cerr << "tshell: unknown subcommand '" << args[1] << "'\n";
        std::cerr << "Run 'tshell help' for usage.\n"; rc = 1;
        return true;
    }

    return false;
}
