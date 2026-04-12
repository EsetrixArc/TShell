#pragma once
#include "globals.hpp"

void loadConfig(const std::string& path, ShellConfig& cfg);
bool validateConfig(const std::string& path);
