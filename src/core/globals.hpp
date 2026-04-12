#pragma once
// =============================================================================
//  globals.hpp — All shared state, structs, and forward declarations for tsh
// =============================================================================

#include <csignal>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../../Bin/API/ModdingAPI.hpp"
#include "../../Bin/Bin/Modloader.hpp"

namespace fs = std::filesystem;

// =============================================================================
//  Core structs
// =============================================================================

struct Theme { std::string name, description, promptFmt; };

struct ShellConfig {
    std::string activeTheme  = "FancyML-1";
    std::string promptFmt    = "";
    std::string modsPath     = "~/.tsh/Mods";
    std::string themesPath   = "/usr/share/tshell/themes";  // legacy; dual-load now in main.cpp
    std::string historyFile  = "~/.tsh/history";
    std::string varsFile     = "~/.tsh/vars";
    int         historyMax   = 500;
    bool        colorEnable  = true;
    bool        autocd       = true;
    bool        traceMode    = false;
    bool        verboseMode  = false;
    bool        debugJson    = false;
    std::map<std::string,std::string> aliases;
    std::map<std::string,std::string> envVars;
};

struct ShellVar {
    std::string              scalar;
    std::vector<std::string> array;
    bool                     isArray = false;
    bool                     persist = false;
    bool                     tagged  = false;
};

struct Job {
    int                  id;
    pid_t                pgid;
    std::string          cmdline;
    std::vector<pid_t>   pids;
    enum class Status { Running, Stopped, Done } status = Status::Running;
    std::vector<int>     exitCodes;
};

struct Redirect {
    enum class Type { In,Out,OutAppend,Err,ErrAppend,Both,BothAppend,HereStr,ProcSubIn,ProcSubOut } type;
    std::string target;
};
struct PipeStage {
    std::string           text;
    std::vector<Redirect> redirects;
    bool                  pipeStderr = false;
};
struct Section { std::string command, delimiter; };
struct Command { std::string name; Mod* executor=nullptr; fs::path filePath; };

// ---------------------------------------------------------------------------
//  Execution pipeline types
// ---------------------------------------------------------------------------
// NOTE: MiddlewareContext and MiddlewareFn are defined in ModdingAPI.hpp
//       (included above) so mod authors get them automatically.

// Per-command execution stats recorded after every fork/exec.
struct CmdStats {
    double      lastExecMs   = 0.0;  // wall-time of the last run (ms)
    uint64_t    runCount     = 0;    // times executed this session
    double      avgExecMs    = 0.0;  // running average (ms)
    std::string pluginOrigin;        // mod id that registered the cmd; "" = system
};

// =============================================================================
//  Global state (defined in globals.cpp, extern here)
// =============================================================================

extern std::vector<LoadedMod>   g_mods;
extern std::vector<Command>     g_commands;
extern ShellConfig              g_cfg;
extern int                      g_lastExit;
extern std::vector<int>         g_pipeStatus;
extern std::map<std::string,ShellVar> g_vars;
extern std::vector<Job>         g_jobs;
extern int                      g_nextJobId;
extern bool                     g_interactive;
extern bool                     g_safeMode;

extern std::map<std::string,Theme>                                 g_themes;
extern std::map<std::string,std::function<std::string()>>          g_engineTokens;
extern std::map<std::string,bool>                                  g_engineTokenEnabled;
extern std::map<std::string,std::map<std::string,std::vector<std::vector<std::string>>>> g_explanations;
extern std::vector<std::function<std::vector<HlSpan>(const std::string&)>> g_highlighters;
extern std::map<std::string,TshCommand>                            g_tshCommands;
extern std::vector<std::string>                                    g_dirStack;
extern std::map<std::string,std::string>                           g_cmdHashCache;
extern std::set<std::string>                                       g_readonlyVars;
extern volatile sig_atomic_t                                       g_ctrlCPressed;
extern std::map<std::string,std::function<std::string()>>          g_tokenProviders;
extern std::map<std::string,
    std::function<std::vector<CompletionEntry>(const std::string&,
                                               const std::vector<std::string>&)>> g_completionProviders;
extern std::map<TshEvent,std::vector<std::function<void(TshHookContext&)>>> g_hooks;
extern std::map<std::string,std::function<int(const std::vector<std::string>&)>> g_interceptors;
extern std::vector<std::pair<std::string,std::function<void(std::string&)>>> g_keybindings;
extern std::map<std::string,double> g_dirFrecency;
extern std::string                  g_frecencyFile;
extern std::string                  g_asyncPromptExtra;
extern std::mutex                   g_asyncMtx;
extern std::string                  g_suggestion;
extern std::map<std::string,std::string> g_pathCache;
extern std::vector<MiddlewareFn>          g_middleware;
extern std::map<std::string,CmdStats>     g_cmdStats;
// Pipeline introspector — updated after every runPipeline() call
// (defined in debug.cpp alongside the other debug globals)
// g_lastPipeline is already declared in debug.hpp which is included transitively

// =============================================================================
//  Forward declarations for cross-module functions
// =============================================================================

// exec.cpp
int  runPipeline(const std::string& text);
int  runPipelineTop(const std::string& text);
int  execLine(const std::string& text);
int  runCommand(const std::string& text, int inFd, int outFd, int errFd);
int  runScript(const std::string& path, const std::vector<std::string>& args = {});
int  runScriptLines(const std::vector<std::string>& lines, size_t& idx);

// expand.cpp
std::string              expandAll(const std::string& s);
std::string              expandToken(const std::string& s, size_t& pos);
std::vector<std::string> parseArgs(const std::string& input);
std::vector<std::string> expandBraces(const std::string& s);
std::vector<std::string> expandGlob(const std::string& pattern);
std::string              captureCommand(const std::string& cmd);

// parser.cpp
std::vector<Section>   parseSections(const std::string& input);
std::vector<PipeStage> parsePipes(const std::string& input);
std::string            extractRedirects(const std::string& raw,
                                        std::vector<Redirect>& reds,
                                        std::istream* scriptStream = nullptr);
bool                   stripBackground(std::string& cmd);
bool                   isGroup(const std::string& t, bool& isSubshell);

// vars.cpp
bool     tryAssign(const std::string& text);
size_t   countLeadingAssignments(const std::vector<std::string>& args);
void     applyInlineEnvChild(const std::vector<std::string>& args, size_t count);
void     loadPersistVars();
void     savePersistVars();

// jobs.cpp
void   printJob(const Job& j);
Job*   findJob(int id);
void   reapJobs();
int    waitFg(Job& j);
void   applyRedirects(const std::vector<Redirect>& reds);

// prompt.cpp
std::string buildPrompt();
std::string getCwd();
std::string getHostname();
void        loadThemeFile(const std::string& path);
void        loadThemesFromDir(const std::string& dir);

// frecency.cpp (part of prompt.cpp)
void loadFrecency();
void saveFrecency();
void trackDir(const std::string& path);

// config.cpp
void loadConfig(const std::string& path, ShellConfig& cfg);
bool validateConfig(const std::string& path);

// mods.cpp
ShellCallbacks makeCallbacks(const std::string& modId);

// hooks.cpp
void fireHook(TshEvent event, const std::string& data, int exitCode = 0);

// pipeline.cpp (part of exec)
void  addMiddleware(MiddlewareFn fn);
bool  runMiddleware(std::vector<std::string>& args, int& result);

// builtins.cpp
bool handleBuiltin(const std::vector<std::string>& args, bool& shouldExit, int& rc);

// debug.cpp
void tshDebugJson(const std::string& command, int exitCode, const std::vector<std::string>& logs);

// suggest.cpp (part of exec)
std::string suggestCommand(const std::string& bad);

// path.cpp (part of exec)
std::string resolveInPath(const std::string& name);
