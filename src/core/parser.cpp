// parser.cpp — Section, pipe, and redirect parsing
#include "globals.hpp"
#include "parser.hpp"
#include "expand.hpp"
#include "strutil.hpp"
#include "debug.hpp"

#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

// =============================================================================
//  Heredoc readers
// =============================================================================

static std::string readHeredoc(std::istream& stream, const std::string& term) {
    std::string content, line;
    while (std::getline(stream, line)) {
        if (trim(line) == term) break;
        content += line + "\n";
    }
    return content;
}

static std::string readHeredocInteractive(const std::string& term) {
    // readline.h not included here — fall back to plain getline
    std::string content;
    while (true) {
        std::cout << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (trim(line) == term) break;
        content += line + "\n";
    }
    return content;
}

// =============================================================================
//  Process substitution helper
// =============================================================================

static std::string procSubstitute(const std::string& cmd, bool forRead) {
    char tmpl[] = "/tmp/tsh_procsub_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return "";
    close(fd);
    unlink(tmpl);
    if (mkfifo(tmpl, 0600) != 0) return "";
    std::string path = tmpl;
    pid_t pid = fork();
    if (pid == 0) {
        if (forRead) {
            int wfd = open(path.c_str(), O_WRONLY);
            dup2(wfd, STDOUT_FILENO);
            close(wfd);
        } else {
            int rfd = open(path.c_str(), O_RDONLY);
            dup2(rfd, STDIN_FILENO);
            close(rfd);
        }
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }
    return path;
}

// =============================================================================
//  extractRedirects
// =============================================================================

std::string extractRedirects(const std::string& raw,
                              std::vector<Redirect>& reds,
                              std::istream* scriptStream) {
    std::string result;
    size_t pos = 0;
    auto skip   = [&]() { while (pos < raw.size() && std::isspace((unsigned char)raw[pos])) ++pos; };
    auto readTok = [&]() -> std::string {
        std::string tok; bool inS = false, inD = false;
        while (pos < raw.size()) {
            char c = raw[pos];
            if (inS) { if (c == '\'') inS = false; else tok += c; ++pos; continue; }
            if (inD) { if (c == '"')  inD = false; else tok += c; ++pos; continue; }
            if (c == '\'') { inS = true; ++pos; continue; }
            if (c == '"')  { inD = true; ++pos; continue; }
            if (std::isspace((unsigned char)c)) break;
            tok += c; ++pos;
        }
        return tok;
    };
    auto ahead = [&](const char* op) -> bool {
        size_t len = std::strlen(op);
        return raw.compare(pos, len, op) == 0;
    };

    while (pos < raw.size()) {
        skip();
        if (pos >= raw.size()) break;
        Redirect::Type rtype;
        bool isRedir = false, isHeredoc = false, isHereStr = false;

        if      (ahead("&>>")) { rtype = Redirect::Type::BothAppend;  pos += 3; isRedir = true; }
        else if (ahead("&>"))  { rtype = Redirect::Type::Both;        pos += 2; isRedir = true; }
        else if (ahead("2>>")) { rtype = Redirect::Type::ErrAppend;   pos += 3; isRedir = true; }
        else if (ahead("2>"))  { rtype = Redirect::Type::Err;         pos += 2; isRedir = true; }
        else if (ahead(">>>")) { rtype = Redirect::Type::HereStr;     pos += 3; isRedir = true; isHereStr = true; }
        else if (ahead("<<"))  { rtype = Redirect::Type::In;          pos += 2; isRedir = true; isHeredoc = true; }
        else if (ahead(">>"))  { rtype = Redirect::Type::OutAppend;   pos += 2; isRedir = true; }
        else if (ahead(">"))   { rtype = Redirect::Type::Out;         pos += 1; isRedir = true; }
        else if (ahead("<("))  {
            size_t depth2 = 1, start = pos + 2; pos = start;
            while (pos < raw.size() && depth2 > 0) { if (raw[pos] == '(') ++depth2; else if (raw[pos] == ')') --depth2; ++pos; }
            reds.push_back({Redirect::Type::ProcSubIn, procSubstitute(raw.substr(start, pos - start - 1), true)});
            continue;
        }
        else if (ahead(">("))  {
            size_t depth2 = 1, start = pos + 2; pos = start;
            while (pos < raw.size() && depth2 > 0) { if (raw[pos] == '(') ++depth2; else if (raw[pos] == ')') --depth2; ++pos; }
            reds.push_back({Redirect::Type::ProcSubOut, procSubstitute(raw.substr(start, pos - start - 1), false)});
            continue;
        }
        else if (ahead("<"))   { rtype = Redirect::Type::In;          pos += 1; isRedir = true; }

        if (isRedir) {
            skip();
            std::string target = readTok();
            if (isHereStr) {
                std::string content = expandAll(target) + "\n";
                reds.push_back({Redirect::Type::In, "heredoc:" + content});
            } else if (isHeredoc) {
                std::string hd;
                if (scriptStream) hd = readHeredoc(*scriptStream, target);
                else              hd = readHeredocInteractive(target);
                reds.push_back({Redirect::Type::In, "heredoc:" + hd});
            } else {
                reds.push_back({rtype, expandHome(expandAll(target))});
            }
            continue;
        }

        // Regular token
        if (!result.empty()) result += ' ';
        size_t tokStart = pos;
        bool inS = false, inD = false;
        while (pos < raw.size()) {
            char c = raw[pos];
            if (inS) { if (c == '\'') inS = false; ++pos; continue; }
            if (inD) { if (c == '"')  inD = false; ++pos; continue; }
            if (c == '\'') { inS = true; ++pos; continue; }
            if (c == '"')  { inD = true; ++pos; continue; }
            if (c == '\\' && pos + 1 < raw.size()) { pos += 2; continue; }
            if (std::isspace((unsigned char)c)) break;
            if (c == '>' || c == '<' ||
                (c == '2' && pos + 1 < raw.size() && raw[pos + 1] == '>') ||
                (c == '&' && pos + 1 < raw.size() && raw[pos + 1] == '>')) break;
            ++pos;
        }
        result += raw.substr(tokStart, pos - tokStart);
    }
    return trim(result);
}

// =============================================================================
//  Section parser (; && ||)
// =============================================================================

std::vector<Section> parseSections(const std::string& input) {
    std::vector<Section> result;
    std::string current;
    size_t pos = 0;
    bool inS = false, inD = false;

    auto push = [&](const std::string& delim) {
        std::string t = trim(current);
        if (!t.empty() || !result.empty()) result.push_back({t, delim});
        current.clear();
    };

    while (pos < input.size()) {
        char c = input[pos];
        if (!inS && !inD) {
            if (c == '\'') { inS = true;  current += c; ++pos; continue; }
            if (c == '"')  { inD = true;  current += c; ++pos; continue; }
            if (pos + 1 < input.size()) {
                std::string two = input.substr(pos, 2);
                if (two == "&&" || two == "||") { push(two); pos += 2; continue; }
            }
            if (c == ';') { push(";"); ++pos; continue; }
        } else {
            if (inS && c == '\'') { inS = false; current += c; ++pos; continue; }
            if (inD && c == '"')  { inD = false; current += c; ++pos; continue; }
        }
        current += c; ++pos;
    }
    std::string t = trim(current);
    if (!t.empty()) result.push_back({t, ""});
    return result;
}

// =============================================================================
//  Pipe parser
// =============================================================================

std::vector<PipeStage> parsePipes(const std::string& input) {
    std::vector<PipeStage> stages;
    std::string current;
    size_t pos = 0;
    bool inS = false, inD = false;

    auto push = [&](bool pipeStderr) {
        std::string t = trim(current);
        if (!stages.empty()) stages.back().pipeStderr = pipeStderr;
        stages.push_back({t, {}, false});
        current.clear();
    };

    while (pos < input.size()) {
        char c = input[pos];
        if (!inS && !inD) {
            if (c == '\'') { inS = true;  current += c; ++pos; continue; }
            if (c == '"')  { inD = true;  current += c; ++pos; continue; }
            if (c == '|' && pos + 1 < input.size() && input[pos + 1] == '&') { push(true);  pos += 2; continue; }
            if (c == '|' && (pos + 1 >= input.size() || input[pos + 1] != '|'))  { push(false); pos += 1; continue; }
        } else {
            if (inS && c == '\'') { inS = false; current += c; ++pos; continue; }
            if (inD && c == '"')  { inD = false; current += c; ++pos; continue; }
        }
        current += c; ++pos;
    }
    std::string t = trim(current);
    if (!t.empty()) stages.push_back({t, {}, false});
    return stages;
}

// =============================================================================
//  Misc parser helpers
// =============================================================================

bool stripBackground(std::string& cmd) {
    size_t pos = cmd.size();
    while (pos > 0 && std::isspace((unsigned char)cmd[pos - 1])) --pos;
    if (pos >= 1 && cmd[pos - 1] == '&' && (pos < 2 || cmd[pos - 2] != '&')) {
        cmd = trim(cmd.substr(0, pos - 1));
        return true;
    }
    return false;
}

bool isGroup(const std::string& t, bool& isSubshell) {
    std::string s = trim(t);
    if (s.empty()) return false;
    if (s[0] == '{' && s.back() == '}') { isSubshell = false; return true; }
    if (s[0] == '(' && s.back() == ')') { isSubshell = true;  return true; }
    return false;
}
