#include "Modloader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <dlfcn.h>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

// =============================================================================
//  Tiny JSON extractor (string and int values, single-level)
// =============================================================================

static std::string jsonStr(const std::string& src, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return "";
    auto colon = src.find(':', kp + needle.size());
    if (colon == std::string::npos) return "";
    auto q1 = src.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    auto q2 = src.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return src.substr(q1 + 1, q2 - q1 - 1);
}

static int jsonInt(const std::string& src, const std::string& key, int def = 0) {
    std::string needle = "\"" + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return def;
    auto colon = src.find(':', kp + needle.size());
    if (colon == std::string::npos) return def;
    size_t p = colon + 1;
    while (p < src.size() && std::isspace((unsigned char)src[p])) ++p;
    if (p >= src.size() || !std::isdigit((unsigned char)src[p])) return def;
    return std::atoi(src.c_str() + p);
}

// =============================================================================
//  Manifest parser
// =============================================================================

ModManifest Modloader::parseManifest(const std::string& jsonPath) {
    ModManifest m;
    std::ifstream f(jsonPath);
    if (!f) { m.error = "cannot open " + jsonPath; return m; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str();

    m.id          = jsonStr(src, "id");
    m.name        = jsonStr(src, "name");
    m.version     = jsonStr(src, "version");
    m.author      = jsonStr(src, "author");
    m.description = jsonStr(src, "description");
    m.lang        = jsonStr(src, "lang");
    m.entry       = jsonStr(src, "entry");
    m.apiVersion  = jsonInt(src, "apiVersion", TSH_API_VERSION);

    if (m.lang.empty())  m.lang  = "cpp";
    if (m.entry.empty()) m.entry = (m.lang == "python") ? "mod.py" : "main.so";
    if (m.id.empty())    { m.error = "manifest missing 'id'"; return m; }
    if (m.name.empty())  m.name = m.id;

    m.valid = true;
    return m;
}

// =============================================================================
//  Crash-isolation probe
//  Fork a child that just dlopen()s the .so and calls the init function.
//  Returns true if the child exits 0 (no crash).
// =============================================================================

static bool probeLoad(const std::string& soPath) {
    pid_t pid = fork();
    if (pid < 0) return true; // can't probe, try anyway
    if (pid == 0) {
        // child
        void* h = dlopen(soPath.c_str(), RTLD_NOW);
        if (!h) _exit(1);
        typedef Mod* (*InitFn)();
        InitFn fn = reinterpret_cast<InitFn>(dlsym(h, "tsh_mod_init"));
        if (!fn) { fn = reinterpret_cast<InitFn>(dlsym(h, "main")); }
        if (!fn) _exit(2);
        Mod* m = fn();
        if (!m) _exit(3);
        // don't call Init() in the probe — just verify it constructs
        dlclose(h);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// =============================================================================
//  Load a single .so
// =============================================================================

typedef Mod* (*ModInitFn)();

LoadedMod* Modloader::tryLoadSo(const std::string& soPath,
                                 const ModManifest& manifest,
                                 ShellCallbacks*    callbacks,
                                 const std::string& sourcePath) {
    void* handle = dlopen(soPath.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "[modloader] dlopen(" << soPath << "): " << dlerror() << "\n";
        return nullptr;
    }
    dlerror();

    Mod* rawMod = nullptr;
    auto fn = reinterpret_cast<ModInitFn>(dlsym(handle, "tsh_mod_init"));
    if (dlerror() || !fn) {
        dlerror();
        fn = reinterpret_cast<ModInitFn>(dlsym(handle, "main"));
        if (dlerror() || !fn) {
            std::cerr << "[modloader] no entry point in " << soPath << "\n";
            dlclose(handle);
            return nullptr;
        }
    }

    rawMod = fn();
    if (!rawMod) {
        std::cerr << "[modloader] entry returned null: " << soPath << "\n";
        dlclose(handle);
        return nullptr;
    }

    rawMod->shell = callbacks;

    if (manifest.valid) {
        if (rawMod->meta.id.empty())          rawMod->meta.id          = manifest.id;
        if (rawMod->meta.name.empty())        rawMod->meta.name        = manifest.name;
        if (rawMod->meta.version.empty())     rawMod->meta.version     = manifest.version;
        if (rawMod->meta.author.empty())      rawMod->meta.author      = manifest.author;
        if (rawMod->meta.description.empty()) rawMod->meta.description = manifest.description;
    }
    if (rawMod->meta.name.empty())
        rawMod->meta.name = fs::path(soPath).stem().string();

    // Auto-register any prompt formats the mod declares statically.
    // The mod may also call shell->registerPromptFormat() from Init() for dynamic ones.
    if (callbacks && callbacks->registerPromptFormat) {
        for (auto fmt : rawMod->getPromptFormats()) {
            if (fmt.ownerId.empty()) fmt.ownerId = rawMod->meta.id;
            callbacks->registerPromptFormat(fmt);
        }
    }

    auto* lm      = new LoadedMod();
    lm->handle     = handle;
    lm->mod        = std::unique_ptr<Mod>(rawMod);
    lm->manifest   = manifest;
    lm->sourcePath = sourcePath;
    return lm;
}

// =============================================================================
//  Main loader
// =============================================================================

std::vector<LoadedMod> Modloader::loadMods(const std::string& path,
                                            ShellCallbacks*    callbacks,
                                            bool               safeMode) {
    std::vector<LoadedMod> result;
    std::error_code ec;

    if (!fs::exists(path, ec)) {
        std::cout << "[modloader] mods directory not found: " << path << "\n";
        return result;
    }

    for (auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        fs::path p = entry.path();

        // ── Case 1: bare <name>.so ──────────────────────────────────────────
        if (entry.is_regular_file(ec) && p.extension() == ".so") {
            if (safeMode && !probeLoad(p.string())) {
                std::cerr << "[modloader] safe-mode: skipping unstable mod " << p.filename() << "\n";
                continue;
            }
            ModManifest m; m.valid = false;
            auto* lm = tryLoadSo(p.string(), m, callbacks, p.string());
            if (lm) {
                std::cout << "[modloader] loaded (legacy): " << p.filename().string() << "\n";
                result.push_back(std::move(*lm));
                delete lm;
            }
            continue;
        }

        // ── Case 2: directory with manifest.json ────────────────────────────
        if (entry.is_directory(ec)) {
            std::string manifestPath = (p / "manifest.json").string();
            ModManifest manifest;
            if (fs::exists(manifestPath, ec)) {
                manifest = parseManifest(manifestPath);
                if (!manifest.valid) {
                    std::cerr << "[modloader] bad manifest in " << p.string()
                              << ": " << manifest.error << "\n";
                    continue;
                }
            } else {
                manifest.name = p.filename().string();
            }

            // Python mod
            if (manifest.lang == "python") {
                std::string pyPath = (p / manifest.entry).string();
                if (!fs::exists(pyPath, ec)) {
                    std::cerr << "[modloader] python entry not found: " << pyPath << "\n";
                    continue;
                }
                std::cout << "[modloader] python mod (runtime): " << manifest.name << "\n";
                // Python mods are invoked via the shell at exec time, not dlopen'd
                // We create a lightweight proxy Mod that shells out to python3
                struct PyMod : public Mod {
                    std::string scriptPath;
                    int Init()  override { return 0; }
                    int Start() override { return 0; }
                    int Execute(int argc, char* argv[]) override {
                        std::string cmd = "python3 \"" + scriptPath + "\"";
                        for (int i = 1; i < argc; ++i) {
                            cmd += " \"";
                            cmd += argv[i];
                            cmd += "\"";
                        }
                        return std::system(cmd.c_str());
                    }
                };
                auto* pm = new PyMod();
                pm->scriptPath       = pyPath;
                pm->meta.id          = manifest.id;
                pm->meta.name        = manifest.name;
                pm->meta.version     = manifest.version;
                pm->meta.author      = manifest.author;
                pm->meta.description = manifest.description;
                pm->shell            = callbacks;

                auto* lm = new LoadedMod();
                lm->mod        = std::unique_ptr<Mod>(pm);
                lm->handle     = nullptr;
                lm->manifest   = manifest;
                lm->sourcePath = p.string();
                result.push_back(std::move(*lm));
                delete lm;
                continue;
            }

            // C++ mod
            std::string soPath = (p / manifest.entry).string();
            if (!fs::exists(soPath, ec)) {
                std::cerr << "[modloader] entry '" << manifest.entry
                          << "' not found in " << p.string() << "\n";
                continue;
            }
            if (safeMode && !probeLoad(soPath)) {
                std::cerr << "[modloader] safe-mode: skipping unstable mod "
                          << manifest.name << "\n";
                continue;
            }
            auto* lm = tryLoadSo(soPath, manifest, callbacks, p.string());
            if (lm) {
                std::cout << "[modloader] loaded: " << manifest.name
                          << " v" << manifest.version << "\n";
                result.push_back(std::move(*lm));
                delete lm;
            }
            continue;
        }

        // ── Case 3: <name>.tmod  (zip) ─────────────────────────────────────
        if (entry.is_regular_file(ec) && p.extension() == ".tmod") {
            fs::path tmp = fs::temp_directory_path() / ("tsh_tmod_" + p.stem().string());
            fs::create_directories(tmp, ec);
            std::string cmd = "unzip -qo \"" + p.string() + "\" -d \"" + tmp.string() + "\"";
            if (std::system(cmd.c_str()) != 0) {
                std::cerr << "[modloader] failed to extract " << p.string() << "\n";
                continue;
            }
            std::string mfPath = (tmp / "manifest.json").string();
            ModManifest manifest;
            if (fs::exists(mfPath, ec)) manifest = parseManifest(mfPath);
            std::string soPath = (tmp / (manifest.valid ? manifest.entry : "main.so")).string();
            if (!fs::exists(soPath, ec)) {
                std::cerr << "[modloader] .tmod missing entry: " << p.string() << "\n";
                fs::remove_all(tmp, ec);
                continue;
            }
            if (safeMode && !probeLoad(soPath)) {
                std::cerr << "[modloader] safe-mode: skipping unstable .tmod "
                          << p.filename() << "\n";
                continue;
            }
            auto* lm = tryLoadSo(soPath, manifest, callbacks, p.string());
            if (lm) {
                std::cout << "[modloader] loaded (.tmod): " << lm->mod->meta.name << "\n";
                result.push_back(std::move(*lm));
                delete lm;
            }
            continue;
        }
    }

    return result;
}