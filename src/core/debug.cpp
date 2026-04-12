// debug.cpp — Structured debug/trace/verbose logging for tsh
#include "debug.hpp"
#include "globals.hpp"
#include "color.hpp"

#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <unistd.h>

DebugConfig    g_debugCfg;
PipelineRecord g_lastPipeline;

static std::ofstream g_logFile;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static const char* levelName(DebugLevel l) {
    switch (l) {
        case DebugLevel::Trace:   return "TRACE";
        case DebugLevel::Verbose: return "VERBOSE";
        case DebugLevel::Info:    return "INFO";
        case DebugLevel::Warn:    return "WARN";
        case DebugLevel::Error:   return "ERROR";
    }
    return "?";
}

static const char* levelColor(DebugLevel l) {
    switch (l) {
        case DebugLevel::Trace:   return Color::DIM;
        case DebugLevel::Verbose: return Color::CYAN;
        case DebugLevel::Info:    return Color::BWHITE;
        case DebugLevel::Warn:    return Color::BYELLOW;
        case DebugLevel::Error:   return Color::BRED;
    }
    return "";
}

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return ss.str();
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static const char* cmdTypeName(CmdType t) {
    switch (t) {
        case CmdType::Builtin:  return "builtin";
        case CmdType::External: return "external";
        case CmdType::Mod:      return "mod";
        default:                return "unknown";
    }
}

// ---------------------------------------------------------------------------
//  tshDebugLog
// ---------------------------------------------------------------------------

void tshDebugLog(DebugLevel level, const std::string& subsystem, const std::string& msg) {
    if (!g_debugCfg.enabled) {
        if (level < DebugLevel::Warn) return;
    }
    if (level < g_debugCfg.minLevel) return;

    std::ostringstream line;
    if (g_debugCfg.timestamps) line << "[" << timestamp() << "] ";
    if (g_debugCfg.showLevel)  line << "[" << levelName(level) << "] ";
    if (!subsystem.empty())    line << "[" << subsystem << "] ";
    line << msg;

    const std::string plain = line.str();

    if (g_debugCfg.toStderr) {
        if (g_cfg.colorEnable)
            std::cerr << levelColor(level) << plain << Color::RESET << "\n";
        else
            std::cerr << plain << "\n";
    }

    if (g_logFile.is_open()) g_logFile << plain << "\n";
}

// ---------------------------------------------------------------------------
//  tshDebugJson — rich record
// ---------------------------------------------------------------------------

void tshDebugJson(const ExecRecord& rec) {
    if (!g_cfg.debugJson) return;

    std::ostream& out = g_logFile.is_open() ? (std::ostream&)g_logFile : std::cout;

    // Compute duration if caller didn't fill it in but gave a startTime
    double ms = rec.durationMs;

    out << "{\n";
    out << "  \"command\": \""      << jsonEscape(rec.command)  << "\",\n";
    out << "  \"type\": \""         << cmdTypeName(rec.type)    << "\",\n";
    out << "  \"pipeline_index\": " << rec.pipelineIdx          << ",\n";
    out << "  \"start_time\": \""   << timestamp()              << "\",\n";
    out << "  \"duration_ms\": "    << ms                       << ",\n";
    out << "  \"exit_code\": "      << rec.exitCode             << ",\n";

    // args array
    out << "  \"args\": [";
    for (size_t i = 0; i < rec.args.size(); ++i) {
        if (i) out << ", ";
        out << "\"" << jsonEscape(rec.args[i]) << "\"";
    }
    out << "],\n";

    out << "  \"expanded\": \""     << jsonEscape(rec.expanded.empty() ? rec.command : rec.expanded) << "\",\n";

    // env object
    out << "  \"env\": {";
    {
        bool first = true;
        // Always include PWD and HOME; then any extra env from snapshot
        auto emit = [&](const std::string& k, const std::string& v) {
            if (!first) out << ", ";
            out << "\n    \"" << jsonEscape(k) << "\": \"" << jsonEscape(v) << "\"";
            first = false;
        };
        const char* pwd  = getenv("PWD");
        const char* home = getenv("HOME");
        if (pwd)  emit("PWD",  pwd);
        if (home) emit("HOME", home);
        for (auto& [k,v] : rec.envSnapshot) emit(k, v);
    }
    out << "\n  },\n";

    // io object
    out << "  \"io\": {\n";
    out << "    \"stdin\": \""  << jsonEscape(rec.stdinDesc)  << "\",\n";
    out << "    \"stdout\": \"" << jsonEscape(rec.stdoutDesc) << "\"\n";
    out << "  }\n";
    out << "}\n";
    out.flush();
}

// ---------------------------------------------------------------------------
//  tshDebugJsonSimple — legacy thin wrapper
// ---------------------------------------------------------------------------

void tshDebugJsonSimple(const std::string& command, int exitCode,
                        const std::vector<std::string>& logs) {
    if (!g_cfg.debugJson) return;

    // Determine command type heuristically
    static const std::vector<std::string> builtins = {
        "cd","exit","echo","export","alias","unalias","source","help",
        "jobs","fg","bg","tshell","retry","timeout","type","history","watch"
    };

    CmdType ctype = CmdType::Unknown;
    std::string head = command.substr(0, command.find(' '));
    for (auto& b : builtins) if (b == head) { ctype = CmdType::Builtin; break; }
    if (ctype == CmdType::Unknown) {
        // Check if it's a registered mod command
        for (auto& [k,_] : g_tshCommands)
            if (k == head) { ctype = CmdType::Mod; break; }
    }
    if (ctype == CmdType::Unknown) ctype = CmdType::External;

    ExecRecord rec;
    rec.command    = command;
    rec.expanded   = command;
    rec.exitCode   = exitCode;
    rec.type       = ctype;
    rec.durationMs = 0.0;  // not available in legacy path

    // parse args from command string (simple split)
    std::istringstream ss(command);
    std::string a;
    while (ss >> a) rec.args.push_back(a);

    // Encode legacy logs into envSnapshot with numeric keys
    for (size_t i=0; i<logs.size(); ++i)
        rec.envSnapshot["log_" + std::to_string(i)] = logs[i];

    tshDebugJson(rec);
}

// ---------------------------------------------------------------------------
//  Pipeline introspector printer
// ---------------------------------------------------------------------------

void tshIntrospectPrint(const PipelineRecord& rec) {
    bool color = g_cfg.colorEnable;
    auto C = [&](const char* c) -> const char* { return color ? c : ""; };

    std::cout << C(Color::BBLUE) << "Pipeline introspection" << C(Color::RESET) << "\n";
    std::cout << C(Color::DIM)   << "  Command:  " << C(Color::RESET) << rec.fullText << "\n";
    std::cout << C(Color::DIM)   << "  Stages:   " << C(Color::RESET) << rec.stageCount << "\n";
    std::cout << C(Color::DIM)   << "  Total:    " << C(Color::RESET) << rec.totalMs << " ms\n";
    std::cout << C(Color::DIM)   << "  Exit:     " << C(Color::RESET) << rec.exitCode << "\n";

    if (!rec.stages.empty()) {
        std::cout << "\n";
        std::cout << C(Color::BYELLOW) << "  Stage breakdown:\n" << C(Color::RESET);
        for (size_t i = 0; i < rec.stages.size(); ++i) {
            const auto& s = rec.stages[i];
            std::cout << C(Color::BWHITE) << "  [" << i << "] " << C(Color::RESET)
                      << s.command << "\n";
            std::cout << "       stdin:  " << s.stdinDesc  << "\n";
            std::cout << "       stdout: " << s.stdoutDesc << "\n";
            std::cout << "       exit:   " << s.exitCode   << "\n";
            if (s.durationMs > 0.0)
                std::cout << "       time:   " << s.durationMs << " ms\n";
        }
    }
}

// ---------------------------------------------------------------------------
//  Log file management
// ---------------------------------------------------------------------------

void tshDebugOpenLog(const std::string& path) {
    if (g_logFile.is_open()) g_logFile.close();
    g_logFile.open(path, std::ios::app);
    if (!g_logFile.is_open())
        std::cerr << "tsh: debug: could not open log file: " << path << "\n";
    else
        g_debugCfg.logFile = path;
}

void tshDebugCloseLog() {
    if (g_logFile.is_open()) g_logFile.close();
    g_debugCfg.logFile.clear();
}
