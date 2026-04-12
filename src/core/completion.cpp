// completion.cpp — Tab completion and fish-style autosuggestion
#include "globals.hpp"
#include "completion.hpp"
#include "color.hpp"
#include "expand.hpp"
#include "strutil.hpp"
#include "debug.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>

#include <readline/readline.h>
#include <readline/history.h>

namespace fs = std::filesystem;

// =============================================================================
//  Completion state
// =============================================================================

static std::vector<std::string> g_completions;
static size_t                   g_completionIdx = 0;

// =============================================================================
//  Completion menu display
// =============================================================================

static void displayCompletionMenu() {
    if (g_completions.empty()) return;
    size_t termWidth = 80;
    size_t maxLen    = 0;
    for (auto& c : g_completions) {
        size_t len = Color::strip(c).size();
        if (len > maxLen) maxLen = len;
    }
    size_t colWidth = maxLen + 2;
    size_t cols     = std::max<size_t>(1, termWidth / colWidth);

    std::cout << "\n";
    for (size_t i = 0; i < g_completions.size(); i += cols) {
        for (size_t j = 0; j < cols && i + j < g_completions.size(); ++j) {
            std::string entry = g_completions[i + j];
            std::string plain = Color::strip(entry);
            std::cout << entry;
            for (size_t k = plain.size(); k < colWidth; ++k) std::cout << " ";
        }
        std::cout << "\n";
    }
    rl_forced_update_display();
}

// =============================================================================
//  Completion generator (readline callback)
// =============================================================================

char* completionGenerator(const char* text, int state) {
    if (state == 0) {
        g_completions.clear();
        g_completionIdx = 0;
        std::string prefix(text);
        std::string fullLine = rl_line_buffer ? std::string(rl_line_buffer) : "";
        auto args2  = parseArgs(fullLine);
        std::string cmd0 = args2.empty() ? "" : args2[0];
        bool dirsOnly    = (cmd0 == "cd");

        auto makeDirPfx = [](const std::string& pfx) -> std::string {
            auto sl = pfx.rfind('/');
            return sl == std::string::npos ? "" : pfx.substr(0, sl + 1);
        };

        // Path completion (contains / or starts with . or ~)
        if (prefix.find('/') != std::string::npos ||
            (!prefix.empty() && (prefix[0] == '.' || prefix[0] == '~'))) {
            std::string dirPfx = makeDirPfx(prefix);
            fs::path dir  = prefix.empty() ? fs::path(".") : fs::path(prefix).parent_path();
            std::string stem = fs::path(prefix).filename().string();
            if (dir.empty()) dir = ".";
            std::error_code ec;
            for (auto& e : fs::directory_iterator(dir, ec)) {
                std::string name = e.path().filename().string();
                if (name.rfind(stem, 0) != 0) continue;
                if (dirsOnly && !e.is_directory(ec)) continue;
                std::string full = dirPfx + name;
                if (e.is_directory(ec)) full += "/";
                g_completions.push_back(full);
            }
        } else if (!cmd0.empty() && cmd0 != prefix && !args2.empty()) {
            // Argument completion — check for a mod-registered provider first
            auto cit = g_completionProviders.find(cmd0);
            if (cit != g_completionProviders.end()) {
                auto entries = cit->second(prefix, args2);
                for (auto& e : entries) g_completions.push_back(e.text);
            } else {
                // Default: file/dir
                std::string dirPfx = makeDirPfx(prefix);
                fs::path dir  = prefix.empty() ? fs::path(".") : fs::path(prefix).parent_path();
                std::string stem = fs::path(prefix).filename().string();
                if (dir.empty()) dir = ".";
                std::error_code ec;
                for (auto& e : fs::directory_iterator(dir, ec)) {
                    std::string name = e.path().filename().string();
                    if (name.rfind(stem, 0) != 0) continue;
                    if (dirsOnly && !e.is_directory(ec)) continue;
                    std::string full = dirPfx + name;
                    if (e.is_directory(ec)) full += "/";
                    g_completions.push_back(full);
                }
            }
        } else {
            // Command completion
            static const std::vector<std::string> builtins = {
                "cd","exit","echo","export","alias","unalias","source","help",
                "jobs","fg","bg","tshell","retry","timeout","type","history",
                "watch","exec","which","command","builtin","pushd","popd","dirs",
                "realpath","readlink","readonly","read","test","[","shift",
                "wait","kill","time","hash"
            };
            for (auto& b : builtins)
                if (toLower(b).rfind(toLower(prefix), 0) == 0) g_completions.push_back(b);
            for (auto& c : g_commands)
                if (toLower(c.name).rfind(toLower(prefix), 0) == 0) g_completions.push_back(c.name);
            for (auto& [k, _] : g_cfg.aliases)
                if (toLower(k).rfind(toLower(prefix), 0) == 0) g_completions.push_back(k);

            // PATH search
            const char* PATH = getenv("PATH");
            if (PATH) {
                std::istringstream ss(PATH);
                std::string d;
                std::set<std::string> seen;
                while (std::getline(ss, d, ':')) {
                    std::error_code ec;
                    for (auto& e : fs::directory_iterator(d, ec)) {
                        std::string n = e.path().filename().string();
                        if (!seen.count(n) && toLower(n).rfind(toLower(prefix), 0) == 0) {
                            seen.insert(n);
                            g_completions.push_back(n);
                        }
                    }
                }
            }
            std::sort(g_completions.begin(), g_completions.end());
            g_completions.erase(std::unique(g_completions.begin(), g_completions.end()),
                                 g_completions.end());
        }

        if (g_completions.size() > 1)
            displayCompletionMenu();
    }

    if (g_completionIdx < g_completions.size())
        return strdup(g_completions[g_completionIdx++].c_str());
    return nullptr;
}

char** tshCompletion(const char* text, int /*start*/, int /*end*/) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, completionGenerator);
}

// =============================================================================
//  Autosuggestion
// =============================================================================

void updateSuggestion(const std::string& current) {
    g_suggestion.clear();
    if (current.empty()) return;
    for (int i = history_length - 1; i >= 0; --i) {
        HIST_ENTRY* he = history_get(i + history_base);
        if (!he) continue;
        std::string entry = he->line;
        if (entry.rfind(current, 0) == 0 && entry.size() > current.size()) {
            g_suggestion = entry.substr(current.size());
            return;
        }
    }
}

int acceptSuggestion(int /*count*/, int /*key*/) {
    if (!g_suggestion.empty()) {
        rl_insert_text(g_suggestion.c_str());
        g_suggestion.clear();
    }
    return 0;
}

// =============================================================================
//  Readline setup
// =============================================================================

static int rlDisplayHook() {
    if (rl_line_buffer)
        updateSuggestion(std::string(rl_line_buffer));
    return 0;
}

void setupReadline() {
    rl_attempted_completion_function = tshCompletion;
    rl_bind_key('\t', rl_menu_complete);
    rl_bind_keyseq("\033[Z", rl_backward_menu_complete); // Shift+Tab
    rl_bind_keyseq("\033[C", acceptSuggestion);           // → accepts suggestion
    rl_bind_key('\006', acceptSuggestion);                // Ctrl-F accepts suggestion
    rl_event_hook = rlDisplayHook;
}
