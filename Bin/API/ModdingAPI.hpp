#pragma once
#include <string>
#include <functional>
#include <vector>
#include <map>

// =============================================================================
//  TShell Modding API  —  v5.0
//
//  Entry point (C linkage required):
//      extern "C" Mod* tsh_mod_init()
//
//  Use the TSH_MOD_EXPORT macro at the bottom of your mod file.
//
//  Prompt format tokens:
//      $USER  $HOST  $CWD  $EXITSTR  $ANY_ENV_VAR  $CUSTOM_TOKEN
//  Engine tokens (prompt engine, configure with `tshell engine`):
//      $E_GIT   $E_TIME  $E_JOBS  $E_<registered name>
//  Color tokens:
//      %reset %bold %dim
//      %black %red %green %yellow %blue %magenta %cyan %white
//      %bred  %bgreen %byellow %bblue %bmagenta %bcyan %bwhite
//
//  v5 additions:
//    - TshCommand + shell->registerTCMD()  (`tshell mod <id> <cmd>`)
//    - RLAMBDA macro
//    - allowCmdCreation / allowTSHExpansion in ModSecurityPolicy
//    - Explain system: shell->createExplanation()
//    - Syntax highlight hooks: shell->registerHighlighter()
//    - Prompt engine tokens: shell->registerEngineToken()
//    - getTshCommands() virtual on Mod (auto-registered, like getPromptFormats)
// =============================================================================

#define TSH_API_VERSION 5

// ---------------------------------------------------------------------------
//  RLAMBDA — "Returning Lambda" convenience macro
//
//  Compresses:  [] (int argc, char** argv) -> int { ... }
//  Into:        RLAMBDA(int, (int argc, char** argv), { ... })
// ---------------------------------------------------------------------------
#define RLAMBDA(returnType, params, body) [] params -> returnType body

// ---------------------------------------------------------------------------
//  Event types
// ---------------------------------------------------------------------------
enum class TshEvent {
    PreParse,   // fired on raw input before tokenisation; ctx.cancel skips execution
    PreExec,    // fired after parse, before fork/exec
    PostExec,   // fired after every command finishes
    DirChange,
    ModLoad,
    ShellExit,
};

struct TshHookContext {
    TshEvent    event;
    std::string data;
    int         exitCode = 0;
    bool        cancel   = false;
};

// ---------------------------------------------------------------------------
//  Completion
// ---------------------------------------------------------------------------
struct CompletionEntry {
    std::string text;
    std::string description;
};

// ---------------------------------------------------------------------------
//  Prompt format
// ---------------------------------------------------------------------------
struct PromptFMT {
    std::string name;
    std::string fmt;
    std::string description;
    std::string ownerId;
};

// ---------------------------------------------------------------------------
//  Syntax highlight token types
// ---------------------------------------------------------------------------
enum class HlTokenType {
    Command,
    Argument,
    String,
    Variable,
    Operator,
    Error,
    Number,
    Comment,
    Keyword,
};

struct HlSpan {
    size_t      start;
    size_t      len;
    HlTokenType type;
    std::string ansiCode; // empty = use theme default for this type
};

// ---------------------------------------------------------------------------
//  TshCommand — expands `tshell mod <modname> <cmd>` (and `tshell <cmd>`)
// ---------------------------------------------------------------------------
class Mod;
struct TshCommand {
    std::string name;
    Mod*        owner    = nullptr;
    std::string helpText;
    std::function<int(int argc, char** argv)> run;
    std::function<std::vector<CompletionEntry>(
        const std::string& partial,
        const std::vector<std::string>& argv)> complete;
};

// ---------------------------------------------------------------------------
//  Security policy
// ---------------------------------------------------------------------------
struct ModSecurityPolicy {
    // ── Legacy fine-grained caps (kept for back-compat) ──────────────────
    bool allowRunCmd          = false;
    bool allowSetVar          = false;
    bool allowStoreWrite      = false;
    bool allowRegisterAlias   = false;
    bool allowInterceptCmd    = false;
    bool allowRegisterKeybind = false;
    bool allowRegisterTheme   = false;
    bool allowEnvRead         = false;
    bool allowCmdCreation     = false;
    bool allowTSHExpansion    = false;
    bool allowHighlighter     = false;
    bool allowEngineToken     = false;

    // ── Sandbox axes (enforced in main.cpp safe-mode gate) ───────────────

    // Filesystem: may the mod open/read/write paths outside its store dir?
    // "none"  — blocked entirely (store API still works)
    // "store" — only ~/.tsh_moddata/<id>/  (default)
    // "home"  — HOME subtree
    // "full"  — unrestricted
    std::string filesystemAccess = "store"; // "none"|"store"|"home"|"full"

    // Network: may the mod create sockets / make outbound connections?
    bool allowNetworkAccess   = false;

    // Command injection: may the mod call shell->runCmd() to inject
    // arbitrary command strings into the shell pipeline?
    // (distinct from allowRunCmd which controls callback presence)
    bool allowCommandInjection = false;

    // Environment: may the mod read the full process environment via
    // shell->getVar() and the system getenv()?
    // false = getVar() is restricted to the mod's own stored vars only.
    bool allowEnvironmentAccess = false;
};

// ---------------------------------------------------------------------------
//  AST node (for mods that inspect parse structure)
// ---------------------------------------------------------------------------
enum class AstNodeType { Command, Pipeline, Sequence, Assign };
struct AstNode {
    AstNodeType              type;
    std::vector<std::string> argv;
    std::vector<AstNode>     children;
    std::string              op;
};

// ---------------------------------------------------------------------------
//  Middleware (input → pre-parse → parser → middleware → executor → post-exec)
// ---------------------------------------------------------------------------
struct MiddlewareContext {
    std::vector<std::string>& args;   // writable — middleware may rewrite argv
    bool                      skip   = false; // set true to abort execution
    int                       result = 0;     // exit-code override when skip=true
};
using MiddlewareFn = std::function<void(MiddlewareContext&)>;

// ---------------------------------------------------------------------------
//  Shell callbacks
// ---------------------------------------------------------------------------
struct ShellCallbacks {
    // ── Themes & prompt ───────────────────────────────────────────────────
    std::function<void(const std::string& name,
                       const std::string& promptFmt,
                       const std::string& description)>           registerTheme;

    std::function<void(const PromptFMT& fmt)>                     registerPromptFormat;

    std::function<void(const std::string& tokenName,
                       std::function<std::string()> provider)>    registerToken;

    // Prompt engine tokens — accessed as $E_<name> in format strings.
    // Configure which are shown with `tshell engine <token> on|off`.
    std::function<void(const std::string& tokenName,
                       std::function<std::string()> provider)>    registerEngineToken;

    // ── Commands ──────────────────────────────────────────────────────────
    std::function<int(const std::string& name)>                   registerCommand;

    std::function<void(
        const std::string& command,
        std::function<std::vector<CompletionEntry>(
            const std::string& partial,
            const std::vector<std::string>& lineArgv)> provider)> registerCompletions;

    std::function<void(
        const std::string& commandName,
        std::function<int(const std::vector<std::string>& args)> interceptor)> interceptCommand;

    std::function<void(const std::string& name,
                       const std::string& expansion)>             registerAlias;

    // ── tshell sub-command expansion ──────────────────────────────────────
    // Registers cmd under `tshell mod <modId> <cmd.name>`.
    // Also callable as `tshell <cmd.name>` if no builtin of that name exists.
    std::function<void(const TshCommand& cmd)>                    registerTCMD;

    // ── Hooks & keys ──────────────────────────────────────────────────────
    std::function<void(TshEvent event,
                       std::function<void(TshHookContext&)> handler)> registerHook;

    std::function<void(const std::string& keyseq,
                       std::function<void(std::string& buffer)> handler)> registerKeybinding;

    // ── Syntax highlighting ───────────────────────────────────────────────
    // Highlighter receives the current input line; returns coloured spans.
    // Multiple highlighters merge (last wins on overlapping spans).
    std::function<void(
        std::function<std::vector<HlSpan>(const std::string& line)> highlighter)> registerHighlighter;

    // ── Explain system ────────────────────────────────────────────────────
    // createExplanation(cmd, expl[line][col], argPattern)
    //   cmd        — "ls", "git", etc.
    //   expl       — table displayed by `tshell explain`; outer=lines, inner=columns
    //   argPattern — argument this applies to, "" = base command summary
    std::function<void(
        const std::string& cmd,
        const std::vector<std::vector<std::string>>& expl,
        const std::string& argPattern)>               createExplanation;

    // ── Variables ─────────────────────────────────────────────────────────
    std::function<std::string(const std::string& var)>            getVar;
    std::function<void(const std::string& var,
                       const std::string& val)>                   setVar;

    // ── Persistent store (~/.tsh_moddata/<modId>/) ────────────────────────
    std::function<void(const std::string& key,
                       const std::string& value)>                 storePut;
    std::function<std::string(const std::string& key,
                               const std::string& defaultVal)>    storeGet;
    std::function<void(const std::string& key)>                   storeDelete;

    // ── Pipeline hooks & middleware ───────────────────────────────────────
    // Register a pre-parse hook: fires on raw input before tokenisation.
    std::function<void(std::function<void(TshHookContext&)>)>     registerPreParseHook;

    // Register a middleware step: runs between parser and executor for
    // every single command.  Middleware may rewrite args or skip execution.
    std::function<void(MiddlewareFn fn)>                          registerMiddleware;

    // ── Shell integration ─────────────────────────────────────────────────
    std::function<int(const std::string& cmd)>                    runCmd;
    std::function<void(const std::string& msg)>                   print;
    std::function<void(const std::string& msg)>                   printErr;
    std::function<int()>                                          lastExit;
    std::function<std::string()>                                  getCwd;
};

// ---------------------------------------------------------------------------
//  Mod metadata
// ---------------------------------------------------------------------------
struct ModMeta {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    int         apiVersion = TSH_API_VERSION;
};

// ---------------------------------------------------------------------------
//  Mod base class
// ---------------------------------------------------------------------------
class Mod {
public:
    ModMeta         meta;
    ShellCallbacks* shell = nullptr;

    virtual ~Mod() = default;

    virtual ModSecurityPolicy  securityPolicy()    const { return {}; }
    virtual int                Init()                    = 0;
    virtual int                Start()                   = 0;
    virtual int                Execute(int argc, char* argv[]) = 0;

    // Auto-registered on load before Init() runs.
    virtual std::vector<PromptFMT>   getPromptFormats() const { return {}; }
    virtual const std::vector<TshCommand>& getTshCommands() const {
        static std::vector<TshCommand> empty;
        return empty;
    }
};

// ---------------------------------------------------------------------------
//  Export macro
// ---------------------------------------------------------------------------
#define TSH_MOD_EXPORT(ClassName)                   \
    extern "C" ClassName* tsh_mod_init() {          \
        return new ClassName();                     \
    }
