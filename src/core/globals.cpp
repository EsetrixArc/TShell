// globals.cpp — definitions of all shared global state
#include "globals.hpp"

std::vector<LoadedMod>   g_mods;
std::vector<Command>     g_commands;
ShellConfig              g_cfg;
int                      g_lastExit     = 0;
std::vector<int>         g_pipeStatus;
std::map<std::string,ShellVar> g_vars;
std::vector<Job>         g_jobs;
int                      g_nextJobId    = 1;
bool                     g_interactive  = true;
bool                     g_safeMode     = false;

std::map<std::string,Theme> g_themes = {
    {"FancyML-1",{"FancyML-1","Default two-line prompt",
        "┌ %bold[%bgreen$USER%reset%bold@%bcyan$HOST%reset%bold] ~ [%bblue$CWD%reset%bold]%byellow$EXITSTR%reset\n└ %bold$%reset "}},
    {"Classic",{"Classic","Old-school user@host:cwd $",
        "%bgreen%bold$USER%reset@%bcyan$HOST%reset:%bblue$CWD%reset%byellow$EXITSTR%reset $ "}},
    {"Minimal",{"Minimal","No color",
        "$USER@$HOST:$CWD$ "}},
    {"Arrow",{"Arrow","Powerline-style ❯",
        "%bblue%bold[$CWD]%reset %byellow$EXITSTR%reset %bmagenta❯%reset "}},
    {"Git",{"Git","With $GITBRANCH token",
        "%bgreen$USER%reset@%bcyan$HOST%reset:%bblue$CWD%reset%bmagenta$GITBRANCH%reset%byellow$EXITSTR%reset $ "}},
    {"Pastel",{"Pastel","Soft pastel ➜",
        "%bcyan$USER%reset %bwhite~%reset %bmagenta$CWD%reset%byellow$EXITSTR%reset %bgreen➜%reset  "}},
    {"Root",{"Root","Red for root sessions",
        "%bred%bold[$USER@$HOST]%reset:%bblue$CWD%reset%byellow$EXITSTR%reset # "}},
};

std::map<std::string,std::function<std::string()>>          g_engineTokens;
std::map<std::string,bool>                                  g_engineTokenEnabled;
std::map<std::string,std::map<std::string,std::vector<std::vector<std::string>>>> g_explanations;
std::vector<std::function<std::vector<HlSpan>(const std::string&)>> g_highlighters;
std::map<std::string,TshCommand>                            g_tshCommands;
std::vector<std::string>                                    g_dirStack;
std::map<std::string,std::string>                           g_cmdHashCache;
std::set<std::string>                                       g_readonlyVars;
volatile sig_atomic_t                                       g_ctrlCPressed = 0;
std::map<std::string,std::function<std::string()>>          g_tokenProviders;
std::map<std::string,
    std::function<std::vector<CompletionEntry>(const std::string&,
                                               const std::vector<std::string>&)>> g_completionProviders;
std::map<TshEvent,std::vector<std::function<void(TshHookContext&)>>> g_hooks;
std::map<std::string,std::function<int(const std::vector<std::string>&)>> g_interceptors;
std::vector<std::pair<std::string,std::function<void(std::string&)>>> g_keybindings;
std::map<std::string,double>    g_dirFrecency;
std::string                     g_frecencyFile;
std::string                     g_asyncPromptExtra;
std::mutex                      g_asyncMtx;
std::string                     g_suggestion;
std::map<std::string,std::string> g_pathCache;
std::vector<MiddlewareFn>          g_middleware;
std::map<std::string,CmdStats>     g_cmdStats;
