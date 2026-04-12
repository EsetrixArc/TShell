#pragma once
#include <string>

std::string buildPrompt();
std::string getCwd();
std::string getHostname();
void        loadThemeFile(const std::string& path);
void        loadThemesFromDir(const std::string& dir);
void        loadFrecency();
void        saveFrecency();
void        trackDir(const std::string& path);
