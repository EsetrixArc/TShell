// introspect.cpp — Pipeline introspector implementation
// =============================================================================
#include "introspect.hpp"
#include "globals.hpp"
#include "debug.hpp"
#include "exec.hpp"

#include <chrono>
#include <unistd.h>

// ---------------------------------------------------------------------------
//  introspectInit — register startup hooks
// ---------------------------------------------------------------------------

void introspectInit() {
    // PostExec: the REPL loop fires this after every top-level command.
    // We use it to give g_lastPipeline its final exit code and wall time
    // for the simple (single-command) case where runPipeline is bypassed
    // through execLine → handleBuiltin (builtins don't call Begin/End).
    g_hooks[TshEvent::PostExec].push_back([](TshHookContext& ctx) {
        // If the pipeline record was properly filled by Begin/End, total time
        // is already set. Only patch it here if it's zero (builtin path).
        if (g_lastPipeline.totalMs == 0.0) {
            auto now = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(
                            now - g_lastPipeline.startTime).count();
            g_lastPipeline.totalMs  = ms;
            g_lastPipeline.exitCode = ctx.exitCode;
        }
    });
}

// ---------------------------------------------------------------------------
//  introspectBeginPipeline
// ---------------------------------------------------------------------------

void introspectBeginPipeline(const std::string& fullText) {
    g_lastPipeline             = PipelineRecord{};
    g_lastPipeline.fullText    = fullText;
    g_lastPipeline.startTime   = std::chrono::steady_clock::now();
    g_lastPipeline.stageCount  = 0;
    g_lastPipeline.stages.clear();
}

// ---------------------------------------------------------------------------
//  introspectRecordStage
// ---------------------------------------------------------------------------

void introspectRecordStage(size_t             stageIdx,
                           const std::string& command,
                           const std::string& type,
                           const std::string& stdinDesc,
                           const std::string& stdoutDesc,
                           double             durationMs,
                           int                exitCode)
{
    // Grow stages vector to accommodate out-of-order or single-stage calls
    if (stageIdx >= g_lastPipeline.stages.size())
        g_lastPipeline.stages.resize(stageIdx + 1);

    PipelineStageRecord& s = g_lastPipeline.stages[stageIdx];
    s.command    = command;
    s.stdinDesc  = stdinDesc;
    s.stdoutDesc = stdoutDesc;
    s.durationMs = durationMs;
    s.exitCode   = exitCode;

    // Parse args from command text (simple split)
    std::istringstream ss(command);
    std::string a;
    s.args.clear();
    while (ss >> a) s.args.push_back(a);

    // Annotate type in the args[0] display label if non-external
    (void)type; // stored implicitly; displayed by tshIntrospectPrint via stdinDesc field

    g_lastPipeline.stageCount = (int)g_lastPipeline.stages.size();
}

// ---------------------------------------------------------------------------
//  introspectEndPipeline
// ---------------------------------------------------------------------------

void introspectEndPipeline(int lastExitCode) {
    auto now = std::chrono::steady_clock::now();
    g_lastPipeline.totalMs  = std::chrono::duration<double, std::milli>(
                                  now - g_lastPipeline.startTime).count();
    g_lastPipeline.exitCode = lastExitCode;
}
