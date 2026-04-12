#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <stdexcept>

// =============================================================================
//  Dependency System for TShell Mods
//
//  Usage (in dependency mod):
//      class MyDep : public Dependency { ... };
//      EXPOSE_DEPENDENCY(MyDep, VERSION(1,0,0))
//
//  Usage (in dependent mod):
//      GETDEP(MyDep, VERSION(1,0,0), VERSION(2,0,0))
//      class MyMod : public Mod, public MyDep { ... };
// =============================================================================

class Dependency {
public:
    virtual ~Dependency() = default;
};

struct Version {
    uint16_t major, minor, patch;
    
    Version(uint16_t ma = 0, uint16_t mi = 0, uint16_t pa = 0)
        : major(ma), minor(mi), patch(pa) {}
    
    bool operator<(const Version& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return patch < o.patch;
    }
    
    bool operator<=(const Version& o) const { return !(o < *this); }
    bool operator>(const Version& o) const { return o < *this; }
    bool operator>=(const Version& o) const { return !(*this < o); }
    bool operator==(const Version& o) const {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
    
    std::string toString() const {
        return std::to_string(major) + "." + 
               std::to_string(minor) + "." + 
               std::to_string(patch);
    }
};

#define VERSION(i, ii, iii) Version(i, ii, iii)

// Registry of exposed dependencies
inline std::map<std::string, std::pair<Version, void*>>& getDependencyRegistry() {
    static std::map<std::string, std::pair<Version, void*>> reg;
    return reg;
}

// EXPOSE_DEPENDENCY macro — registers a dependency with version
#define EXPOSE_DEPENDENCY(classname, version)                                  \
    namespace {                                                                \
        struct classname##_Registrar {                                         \
            classname##_Registrar() {                                          \
                getDependencyRegistry()[#classname] =                          \
                    std::make_pair(version, reinterpret_cast<void*>(nullptr)); \
            }                                                                  \
        } classname##_registrar_instance;                                      \
    }

// GETDEP macro — verifies version range and makes the dependency available
#define GETDEP(name, version_min, version_max)                                 \
    namespace {                                                                \
        struct name##_Checker {                                                \
            name##_Checker() {                                                 \
                auto& reg = getDependencyRegistry();                           \
                auto it = reg.find(#name);                                     \
                if (it == reg.end()) {                                         \
                    throw std::runtime_error(                                  \
                        "Dependency " #name " not found");                     \
                }                                                              \
                Version v = it->second.first;                                  \
                if (v < version_min || v > version_max) {                      \
                    throw std::runtime_error(                                  \
                        "Dependency " #name " version " + v.toString() +       \
                        " outside required range [" +                          \
                        version_min.toString() + ", " +                        \
                        version_max.toString() + "]");                         \
                }                                                              \
            }                                                                  \
        } name##_checker_instance;                                             \
    }