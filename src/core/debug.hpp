#pragma once
// =============================================================================
//  debug.hpp — Structured debug/trace/verbose logging for tsh
// =============================================================================
//
//  tshell debug on|off         — toggle all debug output
//  tshell trace on|off         — set -x style tracing
//  tshell verbose on|off       — parsing step output
//  tshell debug json on|off    — JSON-format exec records
//  tshell debug log <file>     — log debug output to file
//  tshell debug level <n>      — set minimum level (0=all, 3=errors only)
//
//  JSON record format (tshell debug json on):
//  {
//    "command":        "help",
//    "type":           "builtin",
//    "pipeline_index": 0,
//    "start_time":     "17:28:56.448",
//    "duration_ms":    2,
//    "exit_code":      0,
//    "args":           ["help"],
//    "expanded":       "help",
//    "env":            { "PWD": "..." },
//    "io":             { "stdin": "tty", "stdout": "tty" }
//  }
// =============================================================================

#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

enum class DebugLevel {
    Trace   = 0,
    Verbose = 1,
    Info    = 2,
    Warn    = 3,
    Error   = 4,
};

struct DebugConfig {
    bool        enabled    = false;
    bool        toStderr   = true;
    std::string logFile;
    DebugLevel  minLevel   = DebugLevel::Trace;
    bool        timestamps = true;
    bool        showLevel  = true;
    bool        jsonMode   = false;
};

// =============================================================================
//  Rich JSON execution record
// =============================================================================

enum class CmdType { Builtin, External, Mod, Unknown };

struct ExecRecord {
    std::string              command;
    std::string              expanded;
    std::vector<std::string> args;
    CmdType                  type        = CmdType::Unknown;
    int                      pipelineIdx = 0;
    int                      exitCode    = 0;
    std::chrono::steady_clock::time_point startTime;
    double                   durationMs  = 0.0;
    std::string              stdinDesc   = "tty";
    std::string              stdoutDesc  = "tty";
    std::map<std::string,std::string> envSnapshot;
};

// =============================================================================
//  Pipeline introspector
// =============================================================================

struct PipelineStageRecord {
    std::string              command;
    int                      exitCode   = 0;
    double                   durationMs = 0.0;
    std::string              stdinDesc;
    std::string              stdoutDesc;
    std::vector<std::string> args;
};

struct PipelineRecord {
    std::string                           fullText;
    int                                   stageCount = 0;
    double                                totalMs    = 0.0;
    int                                   exitCode   = 0;
    std::vector<PipelineStageRecord>      stages;
    std::chrono::steady_clock::time_point startTime;
};

extern DebugConfig    g_debugCfg;
extern PipelineRecord g_lastPipeline;

// Public API
void tshDebugLog(DebugLevel level, const std::string& subsystem, const std::string& msg);
void tshDebugJson(const ExecRecord& rec);
void tshDebugJsonSimple(const std::string& command, int exitCode,
                        const std::vector<std::string>& logs = {});
void tshIntrospectPrint(const PipelineRecord& rec);
void tshDebugOpenLog(const std::string& path);
void tshDebugCloseLog();

inline void tshTrace(const std::string& s, const std::string& m)   { tshDebugLog(DebugLevel::Trace,   s, m); }
inline void tshVerbose(const std::string& s, const std::string& m) { tshDebugLog(DebugLevel::Verbose, s, m); }
inline void tshWarn(const std::string& s, const std::string& m)    { tshDebugLog(DebugLevel::Warn,    s, m); }
inline void tshError(const std::string& s, const std::string& m)   { tshDebugLog(DebugLevel::Error,   s, m); }

#define TSH_TRACE(subsys, msg)   tshDebugLog(DebugLevel::Trace,   (subsys), (msg))
#define TSH_VERBOSE(subsys, msg) tshDebugLog(DebugLevel::Verbose, (subsys), (msg))
#define TSH_INFO(subsys, msg)    tshDebugLog(DebugLevel::Info,     (subsys), (msg))
#define TSH_WARN(subsys, msg)    tshDebugLog(DebugLevel::Warn,     (subsys), (msg))
#define TSH_ERROR(subsys, msg)   tshDebugLog(DebugLevel::Error,    (subsys), (msg))
