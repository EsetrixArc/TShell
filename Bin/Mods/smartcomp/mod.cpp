// smartcomp/mod.cpp — context-aware completions + syntax highlighting
#include "../../API/ModdingAPI.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> runLines(const std::string& cmd) {
    std::vector<std::string> out;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        std::string s(buf);
        while (!s.empty() && (s.back()=='\n'||s.back()=='\r'||s.back()==' '))
            s.pop_back();
        if (!s.empty()) out.push_back(s);
    }
    pclose(f);
    return out;
}

// ---------------------------------------------------------------------------
//  Completion providers
// ---------------------------------------------------------------------------

static const std::vector<std::string> kGitSubs = {
    "add","bisect","blame","branch","checkout","cherry-pick","clone",
    "commit","diff","fetch","grep","init","log","merge","mv","pull",
    "push","rebase","remote","reset","revert","rm","show","stash",
    "status","switch","tag","worktree"
};

static std::vector<CompletionEntry> gitCompletions(
    const std::string& partial, const std::vector<std::string>& argv)
{
    std::vector<CompletionEntry> out;
    if (argv.size() <= 1) {
        for (auto& s : kGitSubs)
            if (s.rfind(partial, 0) == 0) out.push_back({s, "git " + s});
        return out;
    }
    std::string sub = argv[1];
    static const std::vector<std::string> branchCmds =
        {"checkout","switch","merge","rebase","cherry-pick","branch","push","pull"};
    if (std::find(branchCmds.begin(), branchCmds.end(), sub) != branchCmds.end()) {
        for (auto& b : runLines("git branch --all --format='%(refname:short)' 2>/dev/null"))
            if (b.rfind(partial, 0) == 0) out.push_back({b, "branch"});
    }
    static const std::vector<std::string> fileCmds = {"add","diff","rm","mv","show"};
    if (std::find(fileCmds.begin(), fileCmds.end(), sub) != fileCmds.end()) {
        std::error_code ec;
        for (auto& e : fs::directory_iterator(".", ec)) {
            std::string name = e.path().filename().string();
            if (name.rfind(partial, 0) == 0)
                out.push_back({name, e.is_directory(ec) ? "dir" : "file"});
        }
    }
    return out;
}

static const std::vector<std::string> kDockerSubs = {
    "build","commit","container","cp","create","exec","image","images",
    "info","inspect","kill","logs","network","ps","pull","push","rm",
    "rmi","run","start","stop","system","tag","volume"
};

static std::vector<CompletionEntry> dockerCompletions(
    const std::string& partial, const std::vector<std::string>& argv)
{
    std::vector<CompletionEntry> out;
    if (argv.size() <= 1) {
        for (auto& s : kDockerSubs)
            if (s.rfind(partial, 0) == 0) out.push_back({s, "docker " + s});
        return out;
    }
    std::string sub = argv[1];
    if (sub=="stop"||sub=="rm"||sub=="exec"||sub=="logs"||sub=="kill"||sub=="start") {
        for (auto& c : runLines("docker ps --format '{{.Names}}' 2>/dev/null"))
            if (c.rfind(partial, 0) == 0) out.push_back({c, "running"});
    }
    if (sub == "rmi" || sub == "tag") {
        for (auto& i : runLines("docker images --format '{{.Repository}}:{{.Tag}}' 2>/dev/null"))
            if (i.rfind(partial, 0) == 0) out.push_back({i, "image"});
    }
    return out;
}

static std::vector<CompletionEntry> killCompletions(
    const std::string& partial, const std::vector<std::string>&)
{
    std::vector<CompletionEntry> out;
    for (auto& p : runLines("ps -eo comm= 2>/dev/null | sort -u"))
        if (p.rfind(partial, 0) == 0) out.push_back({p, "process"});
    return out;
}

// ---------------------------------------------------------------------------
//  Syntax highlighter
// ---------------------------------------------------------------------------

static std::vector<HlSpan> highlight(const std::string& line) {
    std::vector<HlSpan> spans;
    if (line.empty()) return spans;

    // Full-line comment
    size_t nonws = line.find_first_not_of(" \t");
    if (nonws != std::string::npos && line[nonws] == '#') {
        spans.push_back({nonws, line.size()-nonws, HlTokenType::Comment, ""});
        return spans;
    }

    size_t i = 0;
    bool firstToken = true;
    while (i < line.size()) {
        if (std::isspace((unsigned char)line[i])) { ++i; continue; }

        // String literal
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i]; size_t start = i++;
            while (i < line.size() && line[i] != q) { if (line[i]=='\\') ++i; ++i; }
            if (i < line.size()) ++i;
            spans.push_back({start, i-start, HlTokenType::String, ""}); continue;
        }

        // Variable $VAR
        if (line[i] == '$') {
            size_t start = i++;
            while (i < line.size() && (std::isalnum((unsigned char)line[i])||line[i]=='_')) ++i;
            spans.push_back({start, i-start, HlTokenType::Variable, ""}); continue;
        }

        // Operators: | || & && ;
        if (line[i]=='|' || line[i]==';' || line[i]=='&') {
            size_t len = (i+1<line.size() && (line[i+1]=='|'||line[i+1]=='&')) ? 2 : 1;
            spans.push_back({i, len, HlTokenType::Operator, ""});
            i += len; firstToken = true; continue;
        }

        // Word token
        size_t start = i;
        while (i < line.size() && !std::isspace((unsigned char)line[i]) &&
               line[i]!='|' && line[i]!=';' && line[i]!='"' &&
               line[i]!='\'' && line[i]!='$' && line[i]!='&') ++i;
        if (i == start) { ++i; continue; }
        std::string word = line.substr(start, i-start);

        if (firstToken) {
            // Check if command exists: try PATH lookup via popen
            bool exists = false;
            FILE* f = popen(("command -v " + word + " >/dev/null 2>&1 && echo 1").c_str(), "r");
            if (f) { char buf[4]; exists = (fgets(buf, sizeof(buf), f) != nullptr); pclose(f); }
            spans.push_back({start, i-start,
                exists ? HlTokenType::Command : HlTokenType::Error, ""});
            firstToken = false;
        } else {
            bool isNum = !word.empty() &&
                (std::isdigit((unsigned char)word[0]) ||
                 (word[0]=='-' && word.size()>1 && std::isdigit((unsigned char)word[1])));
            spans.push_back({start, i-start,
                isNum ? HlTokenType::Number : HlTokenType::Argument, ""});
        }
    }
    return spans;
}

// ---------------------------------------------------------------------------
//  Mod
// ---------------------------------------------------------------------------

class SmartCompMod : public Mod {
    int providerCount_ = 0;

public:
    SmartCompMod() {
        meta.id          = "com.example.smartcomp";
        meta.name        = "smartcomp";
        meta.version     = "1.0.0";
        meta.author      = "example";
        meta.description = "Context-aware completions for git/docker/kill + syntax highlighting";
    }

    ModSecurityPolicy securityPolicy() const override {
        ModSecurityPolicy p;
        p.allowTSHExpansion = true;
        p.allowHighlighter  = true;
        p.filesystemAccess  = "full";
        return p;
    }

    int Init() override {
        shell->registerCompletions("git",     gitCompletions);    ++providerCount_;
        shell->registerCompletions("docker",  dockerCompletions); ++providerCount_;
        shell->registerCompletions("kill",    killCompletions);   ++providerCount_;
        shell->registerCompletions("killall", killCompletions);   ++providerCount_;
        shell->registerHighlighter(highlight);

        TshCommand cmd;
        cmd.name     = "smartcomp";
        cmd.helpText = "smartcomp status  — show registered completion providers";
        cmd.run = [this](int, char**) -> int {
            shell->print("%bblue=== smartcomp ===%reset");
            shell->print("Completion providers: %bgreen" +
                         std::to_string(providerCount_) + "%reset");
            shell->print("Commands: git, docker, kill, killall");
            shell->print("Syntax highlighter: active");
            return 0;
        };
        shell->registerTCMD(cmd);
        return 0;
    }

    int Start()              override { return 0; }
    int Execute(int, char**) override { return 0; }
};

TSH_MOD_EXPORT(SmartCompMod)
