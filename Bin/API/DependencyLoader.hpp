#pragma once
// =============================================================================
//  TShell DependencyLoader — v2.0
//
//  Allows mods to expose typed APIs (Dependencies) that other mods can consume
//  at runtime without knowing the concrete type of the provider.
//
//  ── Provider side ────────────────────────────────────────────────────────────
//
//      #include <TSh/DependencyLoader.hpp>
//      #include <TSh/ModdingAPI.hpp>
//
//      class MyMod : public Mod {
//      public:
//          int returnInt(int a) { return a; }
//          // ... usual Init/Start/Execute
//      };
//
//      class MyDep : public Dependency {
//      public:
//          MyDep(MyMod* inst) : m_mod(inst) {}
//          int returnInt(int a) { return m_mod->returnInt(a); }
//      private:
//          MyMod* m_mod;
//      };
//
//      TSH_MOD_EXPORT(MyMod)
//      TSH_DEP_EXPORT(MyDep, MyMod)
//
//  ── Consumer side ────────────────────────────────────────────────────────────
//
//      #include <TSh/DependencyLoader.hpp>
//
//      class OtherMod : public Mod {
//          int Init() override {
//              auto* dep = TSH_GET_DEP(MyDep);          // MyDep* or nullptr
//              if (dep) dep->returnInt(42);
//              return 0;
//          }
//      };
//
//  ── Version-gated consumer ───────────────────────────────────────────────────
//
//      auto* dep = TSH_GET_DEP_VER(MyDep, VERSION(1,0,0), VERSION(1,99,99));
//
// =============================================================================

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>

// ---------------------------------------------------------------------------
//  Version
// ---------------------------------------------------------------------------
struct Version {
    uint16_t major = 0, minor = 0, patch = 0;

    constexpr Version() = default;
    constexpr Version(uint16_t ma, uint16_t mi, uint16_t pa)
        : major(ma), minor(mi), patch(pa) {}

    bool operator<(const Version& o)  const noexcept {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return patch < o.patch;
    }
    bool operator<=(const Version& o) const noexcept { return !(o < *this); }
    bool operator>(const Version& o)  const noexcept { return o < *this; }
    bool operator>=(const Version& o) const noexcept { return !(*this < o); }
    bool operator==(const Version& o) const noexcept {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
    bool operator!=(const Version& o) const noexcept { return !(*this == o); }

    std::string toString() const {
        return std::to_string(major) + '.' +
               std::to_string(minor) + '.' +
               std::to_string(patch);
    }
};

#define VERSION(ma, mi, pa) Version((ma), (mi), (pa))

// ---------------------------------------------------------------------------
//  Dependency base class
// ---------------------------------------------------------------------------
class Dependency {
public:
    virtual ~Dependency() = default;
};

// ---------------------------------------------------------------------------
//  DependencyEntry — one registered dependency slot
// ---------------------------------------------------------------------------
struct DependencyEntry {
    std::string                 name;       // typeid name of the Dep class
    std::string                 modType;    // typeid name of the paired Mod class
    Version                     version;
    std::shared_ptr<Dependency> instance;   // null until the Mod is constructed
    // factory(rawModPtr) -> builds a Dep wrapping that Mod instance
    std::function<std::shared_ptr<Dependency>(void*)> factory;
};

// ---------------------------------------------------------------------------
//  Global dependency registry
//  (inline → definition lives in each TU that includes this header;
//   the linker merges them into one copy in the final binary)
// ---------------------------------------------------------------------------
inline std::map<std::string, DependencyEntry>& tsh_dep_registry() {
    static std::map<std::string, DependencyEntry> reg;
    return reg;
}

// ---------------------------------------------------------------------------
//  Internal helpers
// ---------------------------------------------------------------------------
namespace tsh_dep_detail {

template <typename DepClass, typename ModClass>
inline void registerDep(const char* depName, Version ver) {
    auto& e   = tsh_dep_registry()[depName];
    e.name    = depName;
    e.modType = typeid(ModClass).name();
    e.version = ver;
    e.factory = [](void* ptr) -> std::shared_ptr<Dependency> {
        return std::make_shared<DepClass>(static_cast<ModClass*>(ptr));
    };
    // instance stays null until the Mod is actually constructed.
}

// Called from the TSH_MOD_EXPORT shim after `new ModClass()` succeeds.
// Walks the registry and instantiates every Dependency whose modType
// matches this Mod's typeid.
inline void instantiateDepsForMod(void* modPtr, const char* modTypeName) {
    for (auto& [key, entry] : tsh_dep_registry()) {
        if (entry.modType != modTypeName) continue;
        if (!entry.factory || entry.instance) continue;
        try {
            entry.instance = entry.factory(modPtr);
        } catch (const std::exception& e) {
            std::cerr << "[deploader] failed to construct dep '"
                      << key << "': " << e.what() << '\n';
        } catch (...) {
            std::cerr << "[deploader] failed to construct dep '"
                      << key << "': unknown exception\n";
        }
    }
}

} // namespace tsh_dep_detail

// ---------------------------------------------------------------------------
//  TSH_DEP_EXPORT(DepClass, ModClass)
//
//  Place this after TSH_MOD_EXPORT in your provider .cpp.
//  It registers the Dependency class and the factory that binds it to the
//  Mod instance. The version defaults to 1.0.0; use TSH_DEP_EXPORT_VER for
//  explicit versioning.
// ---------------------------------------------------------------------------
#define TSH_DEP_EXPORT(DepClass, ModClass)                                          \
    namespace {                                                                     \
        struct DepClass##_Registrar {                                               \
            DepClass##_Registrar() {                                                \
                tsh_dep_detail::registerDep<DepClass, ModClass>(                    \
                    #DepClass, Version(1, 0, 0));                                   \
            }                                                                       \
        } DepClass##_registrar_instance;                                            \
    }

#define TSH_DEP_EXPORT_VER(DepClass, ModClass, ver)                                 \
    namespace {                                                                     \
        struct DepClass##_Registrar {                                               \
            DepClass##_Registrar() {                                                \
                tsh_dep_detail::registerDep<DepClass, ModClass>(                    \
                    #DepClass, (ver));                                               \
            }                                                                       \
        } DepClass##_registrar_instance;                                            \
    }

// ---------------------------------------------------------------------------
//  TSH_MOD_EXPORT(ModClass)
//
//  Supersedes the macro in ModdingAPI.hpp when DependencyLoader is included.
//  After constructing the Mod it triggers dep instantiation for all deps
//  bound to this Mod type.
//
//  Include DependencyLoader.hpp AFTER ModdingAPI.hpp so this override wins.
// ---------------------------------------------------------------------------
#ifdef TSH_MOD_EXPORT
#undef TSH_MOD_EXPORT
#endif

#define TSH_MOD_EXPORT(ModClass)                                                    \
    extern "C" ModClass* tsh_mod_init() {                                           \
        auto* inst = new ModClass();                                                \
        tsh_dep_detail::instantiateDepsForMod(                                      \
            static_cast<void*>(inst), typeid(ModClass).name());                     \
        return inst;                                                                \
    }

// ---------------------------------------------------------------------------
//  TSH_GET_DEP(DepClass) — returns DepClass* or nullptr
// ---------------------------------------------------------------------------
#define TSH_GET_DEP(DepClass)                                                       \
    ([]() -> DepClass* {                                                            \
        auto& reg = tsh_dep_registry();                                             \
        auto  it  = reg.find(#DepClass);                                            \
        if (it == reg.end() || !it->second.instance) {                              \
            std::cerr << "[deploader] dependency '" #DepClass "' not available\n";  \
            return nullptr;                                                         \
        }                                                                           \
        return dynamic_cast<DepClass*>(it->second.instance.get());                  \
    }())

// ---------------------------------------------------------------------------
//  TSH_GET_DEP_VER(DepClass, ver_min, ver_max) — version-gated lookup
// ---------------------------------------------------------------------------
#define TSH_GET_DEP_VER(DepClass, ver_min, ver_max)                                 \
    ([]() -> DepClass* {                                                            \
        auto& reg = tsh_dep_registry();                                             \
        auto  it  = reg.find(#DepClass);                                            \
        if (it == reg.end() || !it->second.instance) {                              \
            std::cerr << "[deploader] dependency '" #DepClass "' not available\n";  \
            return nullptr;                                                         \
        }                                                                           \
        const Version& v = it->second.version;                                      \
        if (v < (ver_min) || v > (ver_max)) {                                       \
            std::cerr << "[deploader] '" #DepClass "' version " << v.toString()     \
                      << " outside required range ["                                \
                      << (ver_min).toString() << ", "                               \
                      << (ver_max).toString() << "]\n";                             \
            return nullptr;                                                         \
        }                                                                           \
        return dynamic_cast<DepClass*>(it->second.instance.get());                  \
    }())

// ---------------------------------------------------------------------------
//  Legacy compat — EXPOSE_DEPENDENCY / GETDEP (v1 API)
// ---------------------------------------------------------------------------
#define EXPOSE_DEPENDENCY(classname, ver) \
    static_assert(false, \
        "EXPOSE_DEPENDENCY is removed. Use TSH_DEP_EXPORT(" #classname ", YourModClass) instead.")

#define GETDEP(name, ver_min, ver_max) TSH_GET_DEP_VER(name, ver_min, ver_max)
