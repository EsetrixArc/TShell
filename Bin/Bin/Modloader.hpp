#pragma once
#include <vector>
#include <memory>
#include <string>
#include "../../Bin/API/ModdingAPI.hpp"

struct ModManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string entry      = "main.so";
    std::string lang       = "cpp";   // "cpp" or "python"
    int         apiVersion = TSH_API_VERSION;
    bool        valid      = false;
    std::string error;
};

struct LoadedMod {
    std::unique_ptr<Mod> mod;
    void*                handle     = nullptr; // dlopen handle (nullptr for python mods)
    ModManifest          manifest;
    std::string          sourcePath;
};

class Modloader {
public:
    // safe_mode: skip mods that fail probe, don't crash shell
    std::vector<LoadedMod> loadMods(const std::string& path,
                                    ShellCallbacks*    callbacks,
                                    bool               safeMode = false);

private:
    LoadedMod* tryLoadSo(const std::string& soPath,
                         const ModManifest& manifest,
                         ShellCallbacks*    callbacks,
                         const std::string& sourcePath);
    ModManifest parseManifest(const std::string& jsonPath);
};