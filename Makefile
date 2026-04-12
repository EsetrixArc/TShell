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

MOD_FLAGS := -std=c++17 -O2 -shared -fPIC -I.

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

install: $(BIN) tshc
	mkdir -p ~/.local/bin
	cp $(BIN)     ~/.local/bin/tsh
	cp tshc       ~/.local/bin/tshc
	@echo "Installed tsh and tshc to ~/.local/bin"

clean:
	rm -f $(BIN) $(BIN)_debug tshc
	@for dir in Bin/Mods/*/; do rm -f "$$dir/main.so"; done
