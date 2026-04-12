#pragma once
#include <string>
#include <vector>

std::string              expandAll(const std::string& s);
std::string              expandToken(const std::string& s, size_t& pos);
std::vector<std::string> parseArgs(const std::string& input);
std::vector<std::string> expandBraces(const std::string& s);
std::vector<std::string> expandGlob(const std::string& pattern);
std::string              captureCommand(const std::string& cmd);
