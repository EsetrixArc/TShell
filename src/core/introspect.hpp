#pragma once
// =============================================================================
//  introspect.hpp — Pipeline introspector for TShell
// =============================================================================
//  Three functions called directly from exec.cpp to record pipeline data,
//  plus introspectInit() which registers PostExec hook for timing closure.
//
//  Usage from exec.cpp:
//    introspectBeginPipeline(fullText);
//    // ... for each stage after waitpid:
//    introspectRecordStage(idx, cmd, type, stdinDesc, stdoutDesc, ms, exit);
//    introspectEndPipeline(lastRc);
//
//  Query from builtins / mods:
//    tshell introspect   →  tshIntrospectPrint(g_lastPipeline)
// =============================================================================
#include <string>

// Call once at startup — registers hooks for timing.
void introspectInit();

// Called by runPipeline() at the start of each pipeline execution.
void introspectBeginPipeline(const std::string& fullText);

// Called after each stage completes (after waitpid in the multi-pipe loop,
// or after runCommand returns for single-stage pipelines).
void introspectRecordStage(size_t            stageIdx,
                           const std::string& command,
                           const std::string& type,       // "external"|"mod"|"builtin"
                           const std::string& stdinDesc,
                           const std::string& stdoutDesc,
                           double             durationMs,
                           int                exitCode);

// Called by runPipeline() once all stages have finished.
void introspectEndPipeline(int lastExitCode);
