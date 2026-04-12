# TShell 🐢

A modular, hackable C++ shell with a built-in mod system, themes, tab completion, job control, and a pipeline introspector.

---

## Features

- **Mod system** — load/unload `.so` mods at runtime; mods can register commands, hooks, prompt tokens, completions, themes, and keybindings
- **Safe mode** (`--safe`) — per-capability sandboxing for untrusted mods (filesystem, env, network, command injection)
- **Themes** — ship `.theme` files or register themes from mods; switch live with `tshell theme`
- **Tab completion** — readline-backed, extensible by mods via `registerCompletions()`
- **Job control** — `fg`, `bg`, `jobs`; full pipeline support (`|`, `&&`, `||`, `;`)
- **History** — deduplication, frecency tracking, configurable size
- **Script mode** — `tsh script.tsh [args…]` for non-interactive use
- **Pipeline introspector** — hook into `PreParse` / `PreExec` / `PostExec` events from any mod
- **Debug JSON** — set `debugjson on` in config to emit structured execution records
- **tshc** — mod compiler, scaffolder, and `.tmod` bundler

---

## Building

### Interactive build (recommended)

```bash
./build.sh
```

This compiles the build helper and launches a step-by-step config UI where you choose:

| Option | Description |
|--------|-------------|
| Color output | Enable/disable ANSI color in the shell |
| Standalone vs system | Standalone disables mods; system install keeps full mod support |
| Debug / Release | Debug adds AddressSanitizer and debug symbols |
| Build tshc | Whether to also compile the mod compiler tool |
| Install | Automatically run `make install` after building |

### Manual build

```bash
make          # build TShell.o (the shell binary)
make tshc     # build the tshc mod compiler
make mods     # compile all mods in Bin/Mods/
make install  # install tsh + tshc to ~/.local/bin
make debug    # debug build with ASan + full symbols
make clean    # remove all build artifacts
```

### Dependencies

| Package | Purpose |
|---------|---------|
| `g++` ≥ 10 | C++17 compiler |
| `libreadline-dev` | Line editing and history |
| `make` | Build system |

On Arch Linux: `sudo pacman -S base-devel readline`  
On Debian/Ubuntu: `sudo apt install build-essential libreadline-dev`

---

## Installation (Arch Linux — AUR/PKGBUILD)

```bash
makepkg -si
```

The PKGBUILD installs:

| Path | Contents |
|------|----------|
| `/usr/bin/tsh` | The shell binary |
| `/usr/bin/tshc` | Mod compiler |
| `/usr/lib/tshell/mods/` | Bundled mods |
| `/usr/include/tshell/` | Public API headers for mod authors |
| `/usr/share/tshell/themes/` | Bundled themes |
| `/usr/share/tshell/tshcfg.example` | Example config |

---

## Configuration

Copy the example config and edit it:

```bash
cp tshcfg.example ~/.tshcfg
```

Key settings:

```sh
# ~/.tshcfg

theme FancyML-1          # active theme
color on                 # ANSI color
history ~/.tsh_history   # history file
histmax 2000             # history limit
mods    ~/.tsh/Mods      # where mods are loaded from
themes  Bin/Themes       # where .theme files are scanned

alias ll=ls -la --color=auto
set EDITOR=vim
```

Available built-in themes: `FancyML-1`, `Classic`, `Minimal`, `Arrow`, `Git`, `Pastel`, `Root`.

---

## Mods

Mods are shared libraries (`.so`) loaded at startup from the configured `mods` directory.

### Using tshc

```bash
tshc new    MyMod           # scaffold a C++ mod
tshc new    MyMod --py      # scaffold a Python mod
tshc build  MyMod/          # compile mod.cpp → main.so and bundle as MyMod.tmod
tshc install MyMod/         # build + copy into Bin/Mods/
tshc unpack  MyMod.tmod     # extract a .tmod bundle
tshc info    MyMod.tmod     # show manifest without extracting
```

### Mod anatomy

```
Bin/Mods/MyMod/
  mod.cpp         ← C++ source
  main.so         ← compiled shared library (produced by tshc build)
  manifest.json   ← metadata
```

**manifest.json**
```json
{
  "id":          "com.example.mymod",
  "name":        "MyMod",
  "version":     "1.0.0",
  "author":      "you",
  "description": "Does something useful",
  "entry":       "main.so",
  "apiVersion":  5
}
```

### Mod API (v5) quick reference

```cpp
#include "Bin/API/ModdingAPI.hpp"

class MyMod : public Mod {
public:
    MyMod() {
        meta.id          = "com.example.mymod";
        meta.name        = "MyMod";
        meta.version     = "1.0.0";
        meta.author      = "you";
        meta.description = "Example mod";
    }

    int Init() override {
        // Register a shell command
        shell->registerCommand("mymod");

        // Register a prompt format
        shell->registerPromptFormat("MyFmt", "%bgreen$USER%reset > ", "Green user prompt");

        // Register a custom prompt token  (replaces $MYTOKEN in prompt strings)
        shell->registerToken("MYTOKEN", [] { return std::string("hi"); });

        // Register a hook
        shell->registerHook(TshEvent::PostExec, [](TshHookContext& ctx) {
            if (ctx.exitCode != 0)
                std::cerr << "[mymod] last command failed\n";
        });

        // Register tab completions
        shell->registerCompletions("mymod",
            [](const std::string& partial, const std::vector<std::string>& argv)
            -> std::vector<CompletionEntry> {
                return {{"option1", "first option"}, {"option2", "second"}};
            });

        // Persistent key-value store  (~/.tsh_moddata/<id>/)
        shell->storePut("mykey", "myvalue");
        std::string v = shell->storeGet("mykey", "default");

        return 0;
    }

    int Start()   override { return 0; }
    int Execute(int argc, char* argv[]) override {
        shell->print("%bcyanMyMod%reset: hello from Execute");
        return 0;
    }
};

TSH_MOD_EXPORT(MyMod)
```

### Safe mode

Run with `tsh --safe` to sandbox mods. Each mod declares what it needs in `securityPolicy()`:

```cpp
ModSecurityPolicy securityPolicy() override {
    ModSecurityPolicy p;
    p.allowRunCmd        = false;
    p.allowSetVar        = true;
    p.allowEnvironmentAccess = false;
    p.filesystemAccess   = "store"; // "none" | "store" | "home" | "full"
    return p;
}
```

Capabilities not declared are blocked at startup.

---

## Bundled mods

| Mod | Description |
|-----|-------------|
| `rewriter` | Rewrites input via the `PreParse` hook |
| `smartcomp` | Smarter tab completion |
| `customprompt` | Custom prompt format demo |
| `hookdemo` | Shows how to use `PreExec` / `PostExec` hooks |
| `exampleCommand` | Registers a new command |

---

## Prompt tokens

| Token | Value |
|-------|-------|
| `$USER` | Current user |
| `$HOST` | Hostname |
| `$CWD` | Working directory |
| `$EXITSTR` | Exit code indicator |
| `$GITBRANCH` | Git branch (requires gitbranch mod) |
| `$ANY_ENV_VAR` | Any exported environment variable |

Color tokens: `%reset %bold %dim %red %green %yellow %blue %magenta %cyan %white %bred %bgreen %byellow %bblue %bmagenta %bcyan %bwhite`

---

## Built-in commands

`cd` · `exit` · `echo` · `export` · `alias` · `unalias` · `source` · `help` · `jobs` · `fg` · `bg` · `tshell` · `retry` · `timeout` · `type` · `history` · `watch`

`tshell reload` — hot-reload config  
`tshell theme list` — list themes  
`tshell mods list` — list loaded mods

---

## License

MIT
