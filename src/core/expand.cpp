// expand.cpp — Brace, glob, parameter, arithmetic, and full expansion
#include "globals.hpp"
#include "expand.hpp"
#include "strutil.hpp"
#include "debug.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fnmatch.h>
#include <functional>
#include <glob.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// =============================================================================
//  Brace expansion: {a,b,c} and {1..10}
// =============================================================================

std::vector<std::string> expandBraces(const std::string& s) {
    size_t open = std::string::npos, depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') { if (depth++ == 0) open = i; }
        else if (s[i] == '}') {
            if (--depth == 0 && open != std::string::npos) {
                std::string pre   = s.substr(0, open);
                std::string post  = s.substr(i + 1);
                std::string inner = s.substr(open + 1, i - open - 1);

                // Range {N..M} or {a..z}
                auto dotdot = inner.find("..");
                if (dotdot != std::string::npos) {
                    std::string a = inner.substr(0, dotdot), b = inner.substr(dotdot + 2);
                    bool isNum = !a.empty() && !b.empty() &&
                        (std::isdigit((unsigned char)a[0]) || a[0] == '-') &&
                        (std::isdigit((unsigned char)b[0]) || b[0] == '-');
                    if (isNum) {
                        int lo = std::atoi(a.c_str()), hi = std::atoi(b.c_str());
                        int step = (lo <= hi) ? 1 : -1;
                        std::vector<std::string> res;
                        for (int v = lo; (step > 0 ? v <= hi : v >= hi); v += step) {
                            auto sub = expandBraces(pre + std::to_string(v) + post);
                            res.insert(res.end(), sub.begin(), sub.end());
                        }
                        return res;
                    }
                    if (a.size() == 1 && b.size() == 1) {
                        std::vector<std::string> res;
                        char lo2 = a[0], hi2 = b[0];
                        if (lo2 > hi2) std::swap(lo2, hi2);
                        for (char c = lo2; c <= hi2; ++c) {
                            auto sub = expandBraces(pre + c + post);
                            res.insert(res.end(), sub.begin(), sub.end());
                        }
                        return res;
                    }
                }
                // Comma-separated {a,b,c}
                std::vector<std::string> parts;
                std::string cur;
                int d2 = 0;
                for (char c : inner) {
                    if (c == '{') { ++d2; cur += c; }
                    else if (c == '}') { --d2; cur += c; }
                    else if (c == ',' && d2 == 0) { parts.push_back(cur); cur.clear(); }
                    else cur += c;
                }
                parts.push_back(cur);
                std::vector<std::string> res;
                for (auto& part : parts) {
                    auto sub = expandBraces(pre + part + post);
                    res.insert(res.end(), sub.begin(), sub.end());
                }
                return res;
            }
        }
    }
    return {s};
}

// =============================================================================
//  Glob expansion (including ** and *(.) *(/))
// =============================================================================

std::vector<std::string> expandGlob(const std::string& pattern) {
    bool filesOnly = false, dirsOnly = false;
    std::string pat = pattern;
    if (pat.size() >= 3 && pat.substr(pat.size() - 3) == "*(.") { filesOnly = true; pat = pat.substr(0, pat.size() - 3) + "*"; }
    if (pat.size() >= 4 && pat.substr(pat.size() - 4) == "*(/)"){ dirsOnly  = true; pat = pat.substr(0, pat.size() - 4) + "*"; }

    bool recursive = pat.find("**") != std::string::npos;
    if (recursive) {
        std::string base   = pat.substr(0, pat.find("**"));
        std::string suffix = pat.substr(pat.find("**") + 2);
        if (!suffix.empty() && suffix[0] == '/') suffix = suffix.substr(1);
        std::vector<std::string> res;
        std::error_code ec;
        fs::path startDir = base.empty() ? fs::path(".") : fs::path(base);
        if (fs::exists(startDir, ec)) {
            for (auto& e : fs::recursive_directory_iterator(startDir, ec)) {
                std::string ep = e.path().string();
                if (suffix.empty()) {
                    if (filesOnly && !e.is_regular_file(ec)) continue;
                    if (dirsOnly  && !e.is_directory(ec))    continue;
                    res.push_back(ep);
                } else {
                    if (ep.size() >= suffix.size() && ep.substr(ep.size() - suffix.size()) == suffix) {
                        if (filesOnly && !e.is_regular_file(ec)) continue;
                        if (dirsOnly  && !e.is_directory(ec))    continue;
                        res.push_back(ep);
                    }
                }
            }
        }
        return res.empty() ? std::vector<std::string>{pattern} : res;
    }

    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob(pat.c_str(), GLOB_TILDE | GLOB_BRACE, nullptr, &g) != 0) {
        globfree(&g);
        return {pattern};
    }
    std::vector<std::string> res;
    for (size_t i = 0; i < g.gl_pathc; ++i) {
        std::string p = g.gl_pathv[i];
        if (filesOnly) { struct stat st; if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue; }
        if (dirsOnly)  { struct stat st; if (stat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue; }
        res.push_back(p);
    }
    globfree(&g);
    return res.empty() ? std::vector<std::string>{pattern} : res;
}

// =============================================================================
//  Command substitution helper: run cmd, capture stdout
// =============================================================================

std::string captureCommand(const std::string& cmd) {
    TSH_TRACE("expand", "captureCommand: " + cmd);
    int pfd[2];
    if (pipe(pfd) < 0) return "";
    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }
    close(pfd[1]);
    std::string out;
    char buf[256];
    ssize_t n;
    while ((n = read(pfd[0], buf, sizeof(buf))) > 0) out.append(buf, n);
    close(pfd[0]);
    int st;
    waitpid(pid, &st, 0);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// =============================================================================
//  Arithmetic evaluation $((expr))
// =============================================================================

static long long evalArith(const std::string& s0) {
    // Expand shell vars first
    std::string expanded;
    for (size_t ii = 0; ii < s0.size(); ++ii) {
        if (s0[ii] == '$') {
            ++ii;
            std::string name;
            while (ii < s0.size() && (std::isalnum((unsigned char)s0[ii]) || s0[ii] == '_'))
                name += s0[ii++];
            --ii;
            auto vit = g_vars.find(name);
            if (vit != g_vars.end()) expanded += vit->second.scalar;
            else { const char* ev = getenv(name.c_str()); if (ev) expanded += ev; }
        } else {
            expanded += s0[ii];
        }
    }
    const std::string& expr = expanded;
    size_t pos = 0;
    std::function<long long()> parseExpr, parseTerm, parsePrimary;

    auto skip = [&]() { while (pos < expr.size() && std::isspace((unsigned char)expr[pos])) ++pos; };

    parsePrimary = [&]() -> long long {
        skip();
        if (pos < expr.size() && expr[pos] == '(') {
            ++pos;
            long long v = parseExpr();
            skip();
            if (pos < expr.size() && expr[pos] == ')') ++pos;
            return v;
        }
        if (pos < expr.size() && expr[pos] == '-') { ++pos; return -parsePrimary(); }
        long long v = 0;
        while (pos < expr.size() && std::isdigit((unsigned char)expr[pos]))
            v = v * 10 + (expr[pos++] - '0');
        return v;
    };
    parseTerm = [&]() -> long long {
        long long v = parsePrimary();
        skip();
        while (pos < expr.size() && (expr[pos] == '*' || expr[pos] == '/' || expr[pos] == '%')) {
            char op = expr[pos++];
            long long r = parsePrimary();
            if (op == '*') v *= r;
            else if (op == '/') v = (r ? v / r : 0);
            else v = (r ? v % r : 0);
            skip();
        }
        return v;
    };
    parseExpr = [&]() -> long long {
        long long v = parseTerm();
        skip();
        while (pos < expr.size() && (expr[pos] == '+' || expr[pos] == '-')) {
            char op = expr[pos++];
            long long r = parseTerm();
            if (op == '+') v += r; else v -= r;
            skip();
        }
        return v;
    };
    return parseExpr();
}

// =============================================================================
//  Parameter expansion: ${var:-def} ${var#pat} ${var%pat} ${var/p/r}
// =============================================================================

static std::string expandParam(const std::string& inner) {
    auto cdPos = inner.find(":-");
    if (cdPos != std::string::npos) {
        std::string var = inner.substr(0, cdPos), def = inner.substr(cdPos + 2);
        auto it = g_vars.find(var);
        if (it != g_vars.end() && !it->second.scalar.empty()) return it->second.scalar;
        const char* e = getenv(var.c_str());
        if (e && *e) return e;
        return def;
    }
    auto slPos = inner.find('/');
    if (slPos != std::string::npos) {
        std::string var  = inner.substr(0, slPos);
        std::string rest = inner.substr(slPos + 1);
        auto sl2 = rest.find('/');
        std::string pat  = rest.substr(0, sl2);
        std::string repl = (sl2 != std::string::npos) ? rest.substr(sl2 + 1) : "";
        std::string val;
        auto it = g_vars.find(var);
        if (it != g_vars.end()) val = it->second.scalar;
        else { const char* e = getenv(var.c_str()); if (e) val = e; }
        size_t p = val.find(pat);
        if (p != std::string::npos) val.replace(p, pat.size(), repl);
        return val;
    }
    auto hashPos = inner.find('#');
    if (hashPos != std::string::npos) {
        std::string var = inner.substr(0, hashPos), pat = inner.substr(hashPos + 1);
        std::string val;
        auto it = g_vars.find(var);
        if (it != g_vars.end()) val = it->second.scalar;
        else { const char* e = getenv(var.c_str()); if (e) val = e; }
        if (val.rfind(pat, 0) == 0) val = val.substr(pat.size());
        return val;
    }
    auto pctPos = inner.find('%');
    if (pctPos != std::string::npos) {
        std::string var = inner.substr(0, pctPos), pat = inner.substr(pctPos + 1);
        std::string val;
        auto it = g_vars.find(var);
        if (it != g_vars.end()) val = it->second.scalar;
        else { const char* e = getenv(var.c_str()); if (e) val = e; }
        if (!pat.empty() && val.size() >= pat.size() && val.substr(val.size() - pat.size()) == pat)
            val = val.substr(0, val.size() - pat.size());
        return val;
    }
    // Plain ${var}
    auto it = g_vars.find(inner);
    if (it != g_vars.end()) return it->second.scalar;
    const char* e = getenv(inner.c_str());
    return e ? e : "";
}

// =============================================================================
//  Variable expansion token ($VAR ${VAR} $((expr)) $(cmd) `cmd`)
// =============================================================================

std::string expandToken(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return "$";
    if (s[pos] == '?') { ++pos; return std::to_string(g_lastExit); }
    // $((arith))
    if (pos + 1 < s.size() && s[pos] == '(' && s[pos + 1] == '(') {
        size_t end = s.find("))", pos + 2);
        if (end == std::string::npos) return "$(";
        std::string expr = s.substr(pos + 2, end - pos - 2);
        pos = end + 2;
        return std::to_string(evalArith(expr));
    }
    // $(cmd)
    if (s[pos] == '(') {
        int depth = 1;
        size_t start = pos + 1, i = start;
        while (i < s.size() && depth > 0) {
            if (s[i] == '(') ++depth;
            else if (s[i] == ')') --depth;
            ++i;
        }
        std::string cmd = s.substr(start, i - start - 1);
        pos = i;
        return captureCommand(cmd);
    }
    // ${...}
    if (s[pos] == '{') {
        size_t end = s.find('}', pos + 1);
        if (end == std::string::npos) return "${";
        std::string inner = s.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        // Array indexing ${arr[n]}
        auto bracket = inner.find('[');
        if (bracket != std::string::npos) {
            std::string varName = inner.substr(0, bracket);
            std::string idx     = inner.substr(bracket + 1, inner.size() - bracket - 2);
            auto it = g_vars.find(varName);
            if (it != g_vars.end() && it->second.isArray) {
                auto& arr = it->second.array;
                if (idx == "@" || idx == "*") {
                    std::string r;
                    for (size_t i2 = 0; i2 < arr.size(); ++i2) { if (i2) r += ' '; r += arr[i2]; }
                    return r;
                }
                size_t ii = static_cast<size_t>(std::atoi(idx.c_str()));
                return (ii < arr.size()) ? arr[ii] : "";
            }
            return "";
        }
        return expandParam(inner);
    }
    // $name
    size_t start = pos;
    while (pos < s.size() && (std::isalnum((unsigned char)s[pos]) || s[pos] == '_')) ++pos;
    if (pos == start) return "$";
    std::string name = s.substr(start, pos - start);
    auto it = g_vars.find(name);
    if (it != g_vars.end()) return it->second.scalar;
    const char* e = getenv(name.c_str());
    return e ? e : "";
}

// =============================================================================
//  Full expansion (vars, command substitution, backticks)
// =============================================================================

std::string expandAll(const std::string& s) {
    std::string result;
    size_t pos = 0;
    bool inSingle = false, inDouble = false;
    while (pos < s.size()) {
        char c = s[pos];
        if (inSingle) { if (c == '\'') inSingle = false; else result += c; ++pos; continue; }
        if (c == '\'' && !inDouble) { inSingle = true; ++pos; continue; }
        if (c == '"') { inDouble = !inDouble; ++pos; continue; }
        if (c == '\\' && !inSingle && pos + 1 < s.size()) { result += s[pos + 1]; pos += 2; continue; }
        if (c == '`' && !inSingle) {
            size_t end = s.find('`', pos + 1);
            if (end != std::string::npos) { result += captureCommand(s.substr(pos + 1, end - pos - 1)); pos = end + 1; continue; }
        }
        if (c == '$') { ++pos; result += expandToken(s, pos); continue; }
        result += c; ++pos;
    }
    return result;
}

// =============================================================================
//  Argument parser + glob + brace expansion
// =============================================================================

std::vector<std::string> parseArgs(const std::string& input) {
    std::vector<std::string> result;
    std::string current;
    size_t pos = 0;
    bool inDouble = false, inSingle = false;
    bool forceKeep = false;

    auto flush = [&]() {
        if (!current.empty() || forceKeep) {
            if (!forceKeep) {
                auto braced = expandBraces(current);
                for (auto& b : braced) {
                    auto globs = expandGlob(b);
                    result.insert(result.end(), globs.begin(), globs.end());
                }
            } else {
                result.push_back(current);
            }
            current.clear();
            forceKeep = false;
        }
    };

    while (pos < input.size()) {
        char c = input[pos];
        if (inSingle) {
            if (c == '\'') inSingle = false;
            else { current += c; forceKeep = true; }
            ++pos; continue;
        }
        if (inDouble) {
            if (c == '"') { inDouble = false; ++pos; forceKeep = true; continue; }
            if (c == '\\' && pos + 1 < input.size()) {
                char n = input[pos + 1];
                if (n == '"' || n == '\\' || n == '$' || n == '`') { current += n; pos += 2; forceKeep = true; continue; }
            }
            if (c == '`') {
                size_t end = input.find('`', pos + 1);
                if (end != std::string::npos) { current += captureCommand(input.substr(pos + 1, end - pos - 1)); pos = end + 1; forceKeep = true; continue; }
            }
            if (c == '$') { ++pos; current += expandToken(input, pos); forceKeep = true; continue; }
            current += c; ++pos; forceKeep = true; continue;
        }
        if (c == '\'') { inSingle = true; ++pos; continue; }
        if (c == '"')  { inDouble = true; ++pos; continue; }
        if (c == '\\' && pos + 1 < input.size()) { current += input[pos + 1]; pos += 2; forceKeep = true; continue; }
        if (c == '`') {
            size_t end = input.find('`', pos + 1);
            if (end != std::string::npos) { current += captureCommand(input.substr(pos + 1, end - pos - 1)); pos = end + 1; continue; }
        }
        if (c == '$') { ++pos; current += expandToken(input, pos); continue; }
        if (std::isspace((unsigned char)c)) { flush(); ++pos; continue; }
        current += c; ++pos;
    }
    flush();
    return result;
}
