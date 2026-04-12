CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -fPIC -pthread
CXXFLAGS += $(EXTRA_CXXFLAGS)
LDFLAGS  := -ldl -lreadline -pthread

# Source files
CORE_SRCS := \
    src/core/globals.cpp    \
    src/core/debug.cpp      \
    src/core/config.cpp     \
    src/core/vars.cpp       \
    src/core/jobs.cpp       \
    src/core/expand.cpp     \
    src/core/parser.cpp     \
    src/core/prompt.cpp     \
    src/core/exec.cpp       \
    src/core/builtins.cpp   \
    src/core/completion.cpp \
    src/core/mods.cpp       \
    src/core/stsc.cpp       \
    src/core/dfs.cpp        \
    src/core/introspect.cpp

SRCS := main.cpp $(CORE_SRCS) Bin/Bin/Modloader.cpp
BIN  := TShell

MOD_FLAGS := -std=c++17 -O2 -shared -fPIC -I. -IBin/API

.PHONY: all clean mods install tshc debug

# ── Shell ─────────────────────────────────────────────────────────────────────

all: $(BIN)

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

# Debug build: no optimisation, address sanitiser, full debug symbols
debug: $(SRCS)
	$(CXX) $(CXXFLAGS) -O0 -g -fsanitize=address,undefined -o $(BIN)_debug $(SRCS) $(LDFLAGS)

# ── tshc compiler tool ────────────────────────────────────────────────────────

tshc: tools/tshc.cpp
	$(CXX) -std=c++17 -O2 -o tshc tools/tshc.cpp

# ── Mods ─────────────────────────────────────────────────────────────────────

mods:
	@for dir in Bin/Mods/*/; do \
	    if [ -f "$$dir/Makefile" ]; then \
	        $(MAKE) -C "$$dir"; \
	    elif [ -f "$$dir/mod.cpp" ]; then \
	        echo "Building $$dir..."; \
	        $(CXX) $(MOD_FLAGS) -I. "$$dir/mod.cpp" -o "$$dir/main.so"; \
	    fi; \
	done

# ── Install ───────────────────────────────────────────────────────────────────

DESTDIR     ?=
PREFIX      ?= /usr
BINDIR      := $(DESTDIR)$(PREFIX)/bin
INCLUDEDIR  := $(DESTDIR)$(PREFIX)/include/TSh
MODSDIR     := $(DESTDIR)$(PREFIX)/lib/tshell/mods
THEMESDIR   := $(DESTDIR)$(PREFIX)/share/tshell/themes

install: $(BIN) tshc
	install -Dm755 $(BIN)  $(BINDIR)/tsh
	install -Dm755 tshc    $(BINDIR)/tshc
	# API headers -> /usr/include/TSh/  (#include <TSh/ModdingAPI.hpp>)
	install -d $(INCLUDEDIR)
	install -m644 Bin/API/ModdingAPI.hpp     $(INCLUDEDIR)/ModdingAPI.hpp
	install -m644 Bin/API/DependencyLoader.hpp $(INCLUDEDIR)/DependencyLoader.hpp
	# System mods
	@for dir in Bin/Mods/*/; do \
	    [ -d "$$dir" ] || continue; \
	    modname=$$(basename "$$dir"); \
	    install -d $(MODSDIR)/$$modname; \
	    [ -f "$$dir/main.so" ]       && install -m755 "$$dir/main.so"       $(MODSDIR)/$$modname/main.so; \
	    [ -f "$$dir/manifest.json" ] && install -m644 "$$dir/manifest.json" $(MODSDIR)/$$modname/manifest.json; \
	done
	# System themes
	install -d $(THEMESDIR)
	@for t in Bin/Themes/*; do \
	    [ -e "$$t" ] && install -m644 "$$t" $(THEMESDIR)/$$(basename "$$t"); \
	done
	@echo "Installed tsh, tshc, headers, mods and themes"

install-user: $(BIN) tshc
	install -Dm755 $(BIN) ~/.local/bin/tsh
	install -Dm755 tshc   ~/.local/bin/tshc
	@echo "Installed tsh and tshc to ~/.local/bin (user)"

clean:
	rm -f $(BIN) $(BIN)_debug tshc
	@for dir in Bin/Mods/*/; do rm -f "$$dir/main.so"; done
