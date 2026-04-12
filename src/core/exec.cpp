// exec.cpp — Command execution, pipeline, and script interpreter
#include "globals.hpp"
#include "exec.hpp"
#include "expand.hpp"
#include "parser.hpp"
#include "vars.hpp"
#include "jobs.hpp"
#include "builtins.hpp"
#include "strutil.hpp"
#include "debug.hpp"
#include "stsc.hpp"
#include "dfs.hpp"
#include "stsc.hpp"
#include "dfs.hpp"
#include "introspect.hpp"

#include "color.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <iostream>
#include <set>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// =============================================================================
//  PATH search (cached)
// =============================================================================

std::string resolveInPath(const std::string& name) {
    auto it = g_pathCache.find(name);
    if (it != g_pathCache.end()) return it->second;
    const char* PATH = getenv("PATH");
    if (!PATH) return "";
    std::istringstream ss(PATH);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
            g_pathCache[name] = full;
            return full;
        }
    }
    return "";
}

// =============================================================================
//  "Did you mean?" fuzzy suggestion
// =============================================================================

static int editDistance(const std::string& a, const std::string& b) {
    size_t m = a.size(), n = b.size();
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (size_t i = 0; i <= m; ++i) dp[i][0] = (int)i;
    for (size_t j = 0; j <= n; ++j) dp[0][j] = (int)j;
    for (size_t i = 1; i <= m; ++i)
        for (size_t j = 1; j <= n; ++j)
            dp[i][j] = a[i-1] == b[j-1] ? dp[i-1][j-1]
                       : 1 + std::min({dp[i-1][j], dp[i][j-1], dp[i-1][j-1]});
    return dp[m][n];
}

std::string suggestCommand(const std::string& bad) {
    std::string best;
    int bestDist = 4;
    auto check = [&](const std::string& name) {
        int d = editDistance(bad, name);
        if (d < bestDist) { bestDist = d; best = name; }
    };
    static const std::vector<std::string> builtins = {
        "cd","exit","echo","export","alias","unalias","source","help",
        "jobs","fg","bg","tshell","retry","timeout","type","history","watch"
    };
    for (auto& b : builtins)   check(b);
    for (auto& c : g_commands) check(c.name);
    for (auto& [k,_] : g_cfg.aliases) check(k);
    return best;
}

// =============================================================================
//  Hook firing
// =============================================================================

void fireHook(TshEvent event, const std::string& data, int exitCode) {
    auto it = g_hooks.find(event);
    if (it == g_hooks.end()) return;
    TshHookContext ctx{event, data, exitCode, false};
    for (auto& fn : it->second) fn(ctx);
}

// =============================================================================
//  Middleware pipeline
// =============================================================================

void addMiddleware(MiddlewareFn fn) {
    g_middleware.push_back(std::move(fn));
}

// Runs all registered middleware in order.
// Returns false if any middleware set skip=true (execution should be aborted).
// result is set to the override exit code in that case.
bool runMiddleware(std::vector<std::string>& args, int& result) {
    for (auto& mw : g_middleware) {
        MiddlewareContext ctx{args, false, 0};
        mw(ctx);
        if (ctx.skip) { result = ctx.result; return false; }
    }
    return true;
}

// =============================================================================
//  Single command execution
// =============================================================================

int runCommand(const std::string& text, int inFd, int outFd, int errFd) {
    std::string t = trim(text);
    if (t.empty()) return 0;

    // Alias expansion (one level)
    {
        size_t sp = t.find(' ');
        std::string head = (sp == std::string::npos) ? t : t.substr(0, sp);
        auto it = g_cfg.aliases.find(head);
        if (it != g_cfg.aliases.end())
            t = it->second + (sp == std::string::npos ? "" : t.substr(sp));
    }

    std::vector<Redirect> reds;
    std::string cmdText = extractRedirects(t, reds);

    if (tryAssign(cmdText)) return 0;

    std::vector<std::string> args = parseArgs(cmdText);
    if (args.empty()) return 0;

    TSH_TRACE("exec", "runCommand: " + args[0]);

    // Interceptors
    auto iit = g_interceptors.find(args[0]);
    if (iit != g_interceptors.end()) {
        int r = iit->second(args);
        if (r != -1) return r;
    }

    // Mod executor
    const Command* found = nullptr;
    for (auto& c : g_commands) if (c.name == args[0]) { found = &c; break; }
    if (found && found->executor) {
        std::vector<char*> v;
        for (auto& a : args) v.push_back(a.data());
        v.push_back(nullptr);
        return found->executor->Execute((int)args.size(), v.data());
    }

    // ── Middleware pipeline ───────────────────────────────────────────────
    {
        int mwResult = 0;
        if (!runMiddleware(args, mwResult)) return mwResult;
    }

    // ── Record start time for stats ───────────────────────────────────────
    auto _execStart = std::chrono::steady_clock::now();

    // Fork + exec
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        if (inFd  != STDIN_FILENO)  { dup2(inFd,  STDIN_FILENO);  close(inFd);  }
        if (outFd != STDOUT_FILENO) { dup2(outFd, STDOUT_FILENO); close(outFd); }
        if (errFd != STDERR_FILENO) { dup2(errFd, STDERR_FILENO); close(errFd); }
        applyRedirects(reds);
        size_t na = countLeadingAssignments(args);
        applyInlineEnvChild(args, na);
        std::vector<std::string> execArgs(args.begin() + na, args.end());
        if (execArgs.empty()) _exit(0);
        std::vector<char*> argv;
        for (auto& a : execArgs) argv.push_back(a.data());
        argv.push_back(nullptr);
        if (execArgs[0].find('/') != std::string::npos) {
            execv(execArgs[0].c_str(), argv.data());
            perror(execArgs[0].c_str()); _exit(127);
        }
        if (found) { execv(found->filePath.c_str(), argv.data()); }
        std::string full = resolveInPath(execArgs[0]);
        if (!full.empty()) { execv(full.c_str(), argv.data()); }
        fprintf(stderr, "%s: command not found\n", execArgs[0].c_str());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    int cmdExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    {
        auto _execEnd = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(_execEnd - _execStart).count();
        CmdStats& cs = g_cmdStats[args[0]];
        cs.lastExecMs = ms;
        cs.runCount++;
        cs.avgExecMs = cs.avgExecMs + (ms - cs.avgExecMs) / (double)cs.runCount;
        if (found && !found->executor) {
            // system binary — pluginOrigin stays empty
        }
        // ── Introspect: record this single-command stage ──────────────────
        std::string cmdType = "external";
        if (found && found->executor) cmdType = "mod";
        introspectRecordStage(0, args[0], cmdType,
            isatty(inFd)  ? "tty" : (inFd  == STDIN_FILENO  ? "tty" : "pipe"),
            isatty(outFd) ? "tty" : (outFd == STDOUT_FILENO ? "tty" : "pipe"),
            ms, cmdExitCode);
    }
    return cmdExitCode;
}

// =============================================================================
//  Pipeline runner
// =============================================================================

int runPipeline(const std::string& text) {
    std::string t = text;

    // Logical group { cmd; } or subshell ( cmd )
    bool isSub = false;
    if (isGroup(t, isSub)) {
        std::string inner = trim(t.substr(1, t.size() - 2));
        if (isSub) {
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);
                std::vector<std::string> lines;
                std::istringstream ss(inner);
                std::string ln;
                while (std::getline(ss, ln, ';')) lines.push_back(trim(ln));
                size_t idx = 0;
                int rc = runScriptLines(lines, idx);
                _exit(rc < 0 ? g_lastExit : rc);
            }
            int st = 0; waitpid(pid, &st, 0);
            return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        } else {
            std::vector<std::string> lines;
            std::istringstream ss(inner);
            std::string ln;
            while (std::getline(ss, ln, ';')) lines.push_back(trim(ln));
            size_t idx = 0;
            return runScriptLines(lines, idx);
        }
    }

    bool background = stripBackground(t);
    std::vector<PipeStage> stages = parsePipes(t);
    if (stages.empty()) return 0;

    // ── Introspect: begin pipeline record ─────────────────────────────────
    introspectBeginPipeline(t);

    // Single stage, no background — handled by execLine for builtin routing
    if (stages.size() == 1 && !background) {
        std::vector<Redirect> reds;
        std::string cmdText = extractRedirects(stages[0].text, reds);
        if (tryAssign(cmdText)) return 0;
        std::vector<std::string> args = parseArgs(cmdText);
        if (args.empty()) return 0;
        return runCommand(stages[0].text, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
    }

    // Multi-stage pipeline
    std::vector<pid_t> pids;
    std::vector<int>   exitCodes;
    int prevRead = STDIN_FILENO;
    pid_t pgid = 0;

    for (size_t i = 0; i < stages.size(); ++i) {
        bool isLast = (i == stages.size() - 1);
        int pipefd[2] = {-1, -1};
        if (!isLast && pipe(pipefd) < 0) { perror("pipe"); return -1; }
        int outFd = isLast ? STDOUT_FILENO : pipefd[1];

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return -1; }
        if (pid == 0) {
            if (pgid == 0) pgid = getpid();
            setpgid(0, pgid);
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            if (prevRead != STDIN_FILENO) { dup2(prevRead, STDIN_FILENO); close(prevRead); }
            if (outFd != STDOUT_FILENO)   { dup2(outFd, STDOUT_FILENO);  close(outFd); }
            if (!isLast && stages[i].pipeStderr) dup2(STDOUT_FILENO, STDERR_FILENO);
            if (!isLast) close(pipefd[0]);
            std::vector<Redirect> reds;
            std::string cmdText = extractRedirects(stages[i].text, reds);
            applyRedirects(reds);
            std::vector<std::string> args = parseArgs(cmdText);
            if (!args.empty()) {
                size_t na = countLeadingAssignments(args);
                applyInlineEnvChild(args, na);
                args.erase(args.begin(), args.begin() + na);
            }
            if (!args.empty()) {
                std::vector<char*> argv;
                for (auto& a : args) argv.push_back(a.data());
                argv.push_back(nullptr);
                if (args[0].find('/') != std::string::npos) { execv(args[0].c_str(), argv.data()); _exit(127); }
                for (auto& c : g_commands) {
                    if (c.name == args[0]) { execv(c.filePath.c_str(), argv.data()); break; }
                }
                std::string full = resolveInPath(args[0]);
                if (!full.empty()) { execv(full.c_str(), argv.data()); }
                fprintf(stderr, "%s: command not found\n", args[0].c_str());
            }
            _exit(127);
        }
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);
        if (prevRead != STDIN_FILENO) close(prevRead);
        if (!isLast) { close(pipefd[1]); prevRead = pipefd[0]; }
        pids.push_back(pid);
        exitCodes.push_back(0);
    }

    if (background) {
        Job j;
        j.id = g_nextJobId++; j.pgid = pgid; j.cmdline = t;
        j.pids = pids; j.status = Job::Status::Running; j.exitCodes = exitCodes;
        g_jobs.push_back(j);
        std::cout << "[" << j.id << "] " << pgid << "\n";
        return 0;
    }

    if (g_interactive) tcsetpgrp(STDIN_FILENO, pgid);
    int lastRc = 0;
    for (size_t i = 0; i < pids.size(); ++i) {
        int status = 0;
        waitpid(pids[i], &status, WUNTRACED);
        if (WIFSTOPPED(status)) {
            Job j;
            j.id = g_nextJobId++; j.pgid = pgid; j.cmdline = t;
            j.pids = pids; j.status = Job::Status::Stopped; j.exitCodes = exitCodes;
            g_jobs.push_back(j);
            std::cout << "\n[" << j.id << "] Stopped\t" << t << "\n";
            if (g_interactive) tcsetpgrp(STDIN_FILENO, getpgrp());
            return 128 + SIGTSTP;
        }
        int ec = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        exitCodes[i] = ec;
        if (i == pids.size() - 1) lastRc = ec;
    }
    if (g_interactive) tcsetpgrp(STDIN_FILENO, getpgrp());
    g_pipeStatus = exitCodes;
    {
        std::string ps;
        for (size_t i = 0; i < exitCodes.size(); ++i) { if (i) ps += " "; ps += std::to_string(exitCodes[i]); }
        g_vars["PIPESTATUS"].scalar = ps;
    }
    // ── Introspect: record each stage of multi-pipe and close ─────────────
    for (size_t i = 0; i < stages.size(); ++i) {
        introspectRecordStage(i, stages[i].text,
            "" /* type detected inside */,
            i == 0 ? "tty" : "pipe",
            i == stages.size()-1 ? "tty" : "pipe",
            0.0, exitCodes[i]);
    }
    introspectEndPipeline(lastRc);
    return lastRc;
}

// =============================================================================
//  runPipelineTop — routes single-stage through execLine for builtin handling
// =============================================================================

int runPipelineTop(const std::string& text) {
    std::string t = text;
    bool isSub = false;
    if (isGroup(t, isSub)) return runPipeline(t);
    bool background = stripBackground(t);
    std::vector<PipeStage> stages = parsePipes(t);
    if (stages.empty()) return 0;
    if (stages.size() == 1 && !background) return execLine(stages[0].text);
    return runPipeline(text);
}

// =============================================================================
//  Main execution entry point
// =============================================================================

int execLine(const std::string& text) {
    std::string t = trim(text);
    if (t.empty()) return 0;

    if (g_cfg.traceMode)
        std::cerr << (g_cfg.colorEnable ? Color::BMAGENTA : "")
                  << "> " << t
                  << (g_cfg.colorEnable ? Color::RESET : "") << "\n";

    TSH_TRACE("exec", "execLine: " + t);

    // Alias expansion (one level)
    {
        size_t sp = t.find(' ');
        std::string head = (sp == std::string::npos) ? t : t.substr(0, sp);
        auto it = g_cfg.aliases.find(head);
        if (it != g_cfg.aliases.end())
            t = it->second + (sp == std::string::npos ? "" : t.substr(sp));
    }

    std::vector<Redirect> reds;
    std::string cmdText = extractRedirects(t, reds);
    if (tryAssign(cmdText)) return 0;

    std::vector<std::string> args = parseArgs(cmdText);
    if (args.empty()) return 0;

    // Auto-cd: single token that is an existing directory
    if (g_cfg.autocd && args.size() == 1) {
        std::error_code ec;
        std::string maybe = expandHome(args[0]);
        if (fs::is_directory(maybe, ec)) {
            std::vector<std::string> cda = {"cd", maybe};
            bool se = false; int r = 0;
            handleBuiltin(cda, se, r);
            return r;
        }
    }

    bool shouldExit = false;
    int rc = 0;
    if (handleBuiltin(args, shouldExit, rc)) {
        if (shouldExit) return -127;
        return rc;
    }

    return runCommand(t, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
}

// =============================================================================
//  Script interpreter
// =============================================================================

static std::string collectBlock(const std::vector<std::string>& lines, size_t& idx,
                                 const std::vector<std::string>& stops,
                                 std::vector<std::string>& block) {
    block.clear();
    int depth = 0;
    while (idx < lines.size()) {
        std::string t = trim(lines[idx]); idx++;
        if (t == "if"    || t.rfind("if ",    0) == 0) depth++;
        if (t == "while" || t.rfind("while ", 0) == 0) depth++;
        if (t == "for"   || t.rfind("for ",   0) == 0) depth++;
        if (t == "case"  || t.rfind("case ",  0) == 0) depth++;
        for (auto& sw : stops)
            if (depth == 0 && (t == sw || t.rfind(sw + " ", 0) == 0 || t.rfind(sw + ";", 0) == 0))
                return sw;
        if (t == "fi" || t == "done" || t == "esac") { if (depth > 0) --depth; }
        block.push_back(lines[idx - 1]);
    }
    return "";
}

int runScriptLines(const std::vector<std::string>& lines, size_t& idx) {
    int rc = 0;
    while (idx < lines.size()) {
        std::string raw = lines[idx++];
        { auto h = raw.find('#'); if (h != std::string::npos) raw = raw.substr(0, h); }
        std::string t = trim(raw);
        if (t.empty()) continue;

        if (g_cfg.traceMode)
            std::cerr << (g_cfg.colorEnable ? Color::BMAGENTA : "")
                      << "+ " << t
                      << (g_cfg.colorEnable ? Color::RESET : "") << "\n";

        TSH_TRACE("script", "line: " + t);

        // ── if ──
        if (t.rfind("if ", 0) == 0 || t == "if") {
            std::string cond = trim(t.substr(2));
            auto semi = cond.rfind(';'); if (semi != std::string::npos) cond = trim(cond.substr(0, semi));
            auto tp   = cond.rfind(" then"); if (tp != std::string::npos) cond = trim(cond.substr(0, tp));
            if (idx < lines.size() && trim(lines[idx]) == "then") idx++;
            bool condMet = false;
            std::vector<std::string> thenBlock, elseBlock;
            std::string stop = collectBlock(lines, idx, {"elif","else","fi"}, thenBlock);
            condMet = (runPipeline(cond) == 0);
            while (stop == "elif") {
                std::string elifLine = trim(lines[idx - 1]);
                std::string ec2 = trim(elifLine.substr(4));
                auto s2 = ec2.rfind(';'); if (s2 != std::string::npos) ec2 = trim(ec2.substr(0, s2));
                auto t2 = ec2.rfind(" then"); if (t2 != std::string::npos) ec2 = trim(ec2.substr(0, t2));
                if (idx < lines.size() && trim(lines[idx]) == "then") idx++;
                std::vector<std::string> elifBlock;
                stop = collectBlock(lines, idx, {"elif","else","fi"}, elifBlock);
                if (!condMet && runPipeline(ec2) == 0) { condMet = true; size_t si = 0; rc = runScriptLines(elifBlock, si); }
            }
            if (stop == "else") {
                if (idx < lines.size() && trim(lines[idx]) == "then") idx++;
                collectBlock(lines, idx, {"fi"}, elseBlock);
            }
            if (condMet) { size_t si = 0; rc = runScriptLines(thenBlock, si); }
            else         { size_t si = 0; rc = runScriptLines(elseBlock, si); }
            continue;
        }
        // ── while ──
        if (t.rfind("while ", 0) == 0 || t == "while") {
            std::string cond = trim(t.substr(5));
            auto semi = cond.rfind(';'); if (semi != std::string::npos) cond = trim(cond.substr(0, semi));
            if (idx < lines.size() && trim(lines[idx]) == "do") idx++;
            std::vector<std::string> body;
            collectBlock(lines, idx, {"done"}, body);
            rc = 0;
            while (runPipeline(cond) == 0) { size_t si = 0; rc = runScriptLines(body, si); if (rc == -127) return rc; }
            continue;
        }
        // ── for ──
        if (t.rfind("for ", 0) == 0) {
            std::string rest = trim(t.substr(3));
            size_t inPos = rest.find(" in ");
            if (inPos == std::string::npos) { std::cerr << "for: syntax error\n"; continue; }
            std::string var  = trim(rest.substr(0, inPos));
            std::string list = rest.substr(inPos + 4);
            auto semi = list.rfind(';'); if (semi != std::string::npos) list = trim(list.substr(0, semi));
            if (idx < lines.size() && trim(lines[idx]) == "do") idx++;
            std::vector<std::string> items = parseArgs(list);
            std::vector<std::string> body;
            collectBlock(lines, idx, {"done"}, body);
            rc = 0;
            for (auto& item : items) {
                g_vars[var].scalar = item;
                size_t si = 0; rc = runScriptLines(body, si); if (rc == -127) return rc;
            }
            continue;
        }
        // ── case ──
        if (t.rfind("case ", 0) == 0) {
            std::string word = trim(t.substr(5));
            auto inPos2 = word.rfind(" in");
            if (inPos2 != std::string::npos) word = trim(word.substr(0, inPos2));
            word = expandAll(word);
            if (idx < lines.size() && trim(lines[idx]) == "in") idx++;
            std::vector<std::string> caseLines;
            collectBlock(lines, idx, {"esac"}, caseLines);
            size_t ci = 0; bool matched = false;
            while (ci < caseLines.size() && !matched) {
                std::string pat = trim(caseLines[ci++]);
                if (pat.empty() || pat.back() != ')') continue;
                pat = pat.substr(0, pat.size() - 1);
                std::vector<std::string> cmds;
                while (ci < caseLines.size() && trim(caseLines[ci]) != ";;") cmds.push_back(caseLines[ci++]);
                if (ci < caseLines.size()) ci++;
                if (fnmatch(pat.c_str(), word.c_str(), 0) == 0) { matched = true; size_t si = 0; rc = runScriptLines(cmds, si); }
            }
            continue;
        }
        // Regular command / chain
        std::vector<Section> sections = parseSections(t);
        bool chainBroken = false;
        for (auto& s : sections) {
            if (chainBroken) break;
            std::string cmd = trim(s.command);
            if (cmd.empty()) continue;
            rc = runPipeline(cmd);
            if (rc == -127) return rc;
            if (s.delimiter == "&&" && rc != 0) chainBroken = true;
            if (s.delimiter == "||" && rc == 0) chainBroken = true;
        }
    }
    return rc;
}

int runScript(const std::string& path, const std::vector<std::string>& args) {
    // ── Extension-based dispatch ──────────────────────────────────────────
    {
        fs::path ext = fs::path(path).extension();
        if (ext == ".stsc") return runStsc(path, args);
        if (ext == ".dfs")  return runDfs(path, args);
    }
    std::ifstream f(path);
    if (!f) { std::cerr << path << ": " << std::strerror(errno) << "\n"; return 1; }
    std::string firstLine;
    std::getline(f, firstLine);
    if (firstLine.size() >= 2 && firstLine[0] == '#' && firstLine[1] == '!') {
        if (firstLine.find("tsh") == std::string::npos) {
            f.close();
            std::vector<const char*> argv = {path.c_str()};
            for (auto& a : args) argv.push_back(a.c_str());
            argv.push_back(nullptr);
            execv(path.c_str(), (char* const*)argv.data());
            perror(path.c_str()); return 127;
        }
    }
    f.seekg(0);
    for (size_t i = 0; i <= 9; ++i) {
        std::string v = (i == 0) ? path : (i <= args.size() ? args[i-1] : "");
        setenv(std::to_string(i).c_str(), v.c_str(), 1);
    }
    std::vector<std::string> lines;
    std::string line, accumulated;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\\') { accumulated += line.substr(0, line.size() - 1) + " "; }
        else { accumulated += line; lines.push_back(accumulated); accumulated.clear(); }
    }
    if (!accumulated.empty()) lines.push_back(accumulated);
    size_t idx = 0;
    int rc = runScriptLines(lines, idx);
    return rc == -127 ? g_lastExit : rc;
}
