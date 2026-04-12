#pragma once
#include <string>
#include <vector>
#include "../../Bin/API/ModdingAPI.hpp"

int         runPipeline(const std::string& text);
int         runPipelineTop(const std::string& text);
int         execLine(const std::string& text);
int         runCommand(const std::string& text, int inFd, int outFd, int errFd);
int         runScript(const std::string& path, const std::vector<std::string>& args);
int         runScriptLines(const std::vector<std::string>& lines, size_t& idx);
std::string suggestCommand(const std::string& bad);
std::string resolveInPath(const std::string& name);
void        fireHook(TshEvent event, const std::string& data, int exitCode);
