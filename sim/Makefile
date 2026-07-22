# Build the SwinSID emulator as a self-contained DLL plus a thin CLI exe
# (native Windows, MSYS2 UCRT64).
#
# The engine has two backends, both compiled into swinsid.dll:
#   firmware  - the SwinSID firmware under simavr (the thing being tested)
#   reference - libsidplayfp (a complete, cycle-accurate C64 + reSIDfp), the
#               "real machine" reference to compare the firmware against
#
# Build dependencies (MSYS2 UCRT64):
#   third_party/simavr  -> libsimavr.a (build with: make build-simavr)
#   libsidplayfp        -> pacman -S mingw-w64-ucrt-x86_64-libsidplayfp
#                          (statically linked, so the DLL stays self-contained)
#
# Run from an MSYS2 UCRT64 shell:
#   make            # builds ../tools/swinsid.dll, swinsid_sim.exe, wavstat.exe
#   make clean
#
# Layout:
#   src/       C/C++ sources (engine + public API in swinsid.h, CLI, wavstat)
#   build/     object files + import library (rebuildable)
#   ../tools/  the built DLL + executables (committed)

CC        ?= gcc
CXX       ?= g++
PKGCONFIG ?= pkg-config

SIMAVR_DIR ?= ../third_party/simavr
SIMAVR_INC  = $(SIMAVR_DIR)/simavr/sim
SIMAVR_LIB  = $(wildcard $(SIMAVR_DIR)/simavr/obj-*/libsimavr.a)

# libsidplayfp (reference engine), from the MSYS2 package. --static resolves the
# private deps (gcrypt, ...) so we can bake them into the DLL; -lgpg-error is
# appended because libgcrypt.a needs it and the .pc does not list it.
SIDPLAYFP_CFLAGS = $(shell $(PKGCONFIG) --cflags libsidplayfp)
SIDPLAYFP_LIBS   = $(shell $(PKGCONFIG) --libs --static libsidplayfp) -lgpg-error

SRCDIR  = src
BUILD   = build
TOOLS   = ../tools

CSTD    = -O2 -Wall -Wextra -Wno-unused-parameter

CFLAGS  = $(CSTD) -I$(SRCDIR) -I$(SIMAVR_INC)
# Engine core (C++): needs simavr + libsidplayfp headers. libsidplayfp's simple
# render API (play/isPlaying) is deprecated but stable, so silence that warning.
CORE_CXXFLAGS = $(CSTD) -std=c++17 -Wno-deprecated-declarations \
                -I$(SRCDIR) -I$(SIMAVR_INC) $(SIDPLAYFP_CFLAGS)
# miniaudio.h (vendored in src/) emits many warnings under -Wall -Wextra;
# compile the file that includes it plainly.
RTFLAGS = -O2 -I$(SRCDIR)
# Statically link the GCC/C++/pthread runtimes (and libsidplayfp's deps) so
# swinsid.dll only depends on system DLLs and is self-contained.
STATIC  = -static -static-libgcc -static-libstdc++
LDLIBS  = $(SIMAVR_LIB) -lelf -lws2_32 -lm -lole32

# Engine objects that make up the DLL.
DLL_OBJS = $(BUILD)/swinsid_core.o $(BUILD)/cpu6502.o $(BUILD)/sidfile.o \
           $(BUILD)/wav.o $(BUILD)/realtime.o

DLL     = $(TOOLS)/swinsid.dll
IMPLIB  = $(BUILD)/libswinsid.dll.a
SIM     = $(TOOLS)/swinsid_sim.exe
WAVSTAT = $(TOOLS)/wavstat.exe

all: check $(DLL) $(SIM) $(WAVSTAT)

check:
	@if [ -z "$(SIMAVR_LIB)" ]; then \
		echo "ERROR: libsimavr.a not found under $(SIMAVR_DIR)/simavr/obj-*/"; \
		echo "Build simavr first (see repo setup notes)."; \
		exit 1; \
	fi
	@if ! $(PKGCONFIG) --exists libsidplayfp; then \
		echo "ERROR: libsidplayfp not found (pkg-config)."; \
		echo "Install it:  pacman -S mingw-w64-ucrt-x86_64-libsidplayfp"; \
		exit 1; \
	fi

$(BUILD):
	mkdir -p $(BUILD)

$(TOOLS):
	mkdir -p $(TOOLS)

# --- Self-contained engine DLL (+ import library for the exe). ---
$(DLL): $(DLL_OBJS) | $(TOOLS)
	$(CXX) -shared $(STATIC) -o $@ $(DLL_OBJS) -Wl,--out-implib,$(IMPLIB) \
		$(SIDPLAYFP_LIBS) $(LDLIBS)

# --- Thin CLI front-end, linked against the DLL's import library. ---
$(SIM): $(BUILD)/swinsid_cli.o $(DLL) | $(TOOLS)
	$(CC) -O2 -o $@ $(BUILD)/swinsid_cli.o -L$(BUILD) -lswinsid

$(WAVSTAT): $(SRCDIR)/wavstat.c | $(TOOLS)
	$(CC) -O2 -o $@ $<

# --- Object files. ---
$(BUILD)/swinsid_core.o: $(SRCDIR)/swinsid_core.cpp $(SRCDIR)/swinsid.h $(SRCDIR)/cpu6502.h $(SRCDIR)/sidfile.h $(SRCDIR)/wav.h $(SRCDIR)/realtime.h | $(BUILD)
	$(CXX) $(CORE_CXXFLAGS) -c $< -o $@

$(BUILD)/swinsid_cli.o: $(SRCDIR)/swinsid_cli.c $(SRCDIR)/swinsid.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/cpu6502.o: $(SRCDIR)/cpu6502.c $(SRCDIR)/cpu6502.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sidfile.o: $(SRCDIR)/sidfile.c $(SRCDIR)/sidfile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wav.o: $(SRCDIR)/wav.c $(SRCDIR)/wav.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# miniaudio implementation - relaxed flags, own rule.
$(BUILD)/realtime.o: $(SRCDIR)/realtime.c $(SRCDIR)/realtime.h $(SRCDIR)/miniaudio.h | $(BUILD)
	$(CC) $(RTFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) $(DLL) $(SIM) $(WAVSTAT)

.PHONY: all check clean
