# Build the SwinSID emulator/renderer harness (native Windows, MSYS2 UCRT64).
#
# Requires simavr to have been built in ../third_party/simavr (see the top of
# the repo for setup). Run from an MSYS2 UCRT64 shell:
#   make            # builds ../tools/swinsid_sim.exe and ../tools/wavstat.exe
#   make clean
#
# Layout:
#   src/     C sources for the harness
#   build/   object files (rebuildable, gitignored)
#   ../tools/  the built executables (committed)

CC        ?= gcc
SIMAVR_DIR ?= ../third_party/simavr
SIMAVR_INC  = $(SIMAVR_DIR)/simavr/sim
SIMAVR_LIB  = $(wildcard $(SIMAVR_DIR)/simavr/obj-*/libsimavr.a)

SRCDIR  = src
BUILD   = build
TOOLS   = ../tools

CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter -I$(SIMAVR_INC)
# miniaudio.h (vendored in src/) emits many warnings under -Wall -Wextra;
# compile the file that includes it plainly.
RTFLAGS = -O2
LDLIBS  = $(SIMAVR_LIB) -lelf -lws2_32 -lm -lole32

OBJS = $(BUILD)/swinsid_sim.o $(BUILD)/cpu6502.o $(BUILD)/sidfile.o \
       $(BUILD)/wav.o $(BUILD)/realtime.o

SIM     = $(TOOLS)/swinsid_sim.exe
WAVSTAT = $(TOOLS)/wavstat.exe

all: check $(SIM) $(WAVSTAT)

check:
	@if [ -z "$(SIMAVR_LIB)" ]; then \
		echo "ERROR: libsimavr.a not found under $(SIMAVR_DIR)/simavr/obj-*/"; \
		echo "Build simavr first (see repo setup notes)."; \
		exit 1; \
	fi

$(BUILD):
	mkdir -p $(BUILD)

$(TOOLS):
	mkdir -p $(TOOLS)

$(SIM): $(OBJS) | $(TOOLS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(WAVSTAT): $(SRCDIR)/wavstat.c | $(TOOLS)
	$(CC) -O2 -o $@ $<

$(BUILD)/swinsid_sim.o: $(SRCDIR)/swinsid_sim.c $(SRCDIR)/cpu6502.h $(SRCDIR)/sidfile.h $(SRCDIR)/wav.h $(SRCDIR)/realtime.h | $(BUILD)
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
	rm -rf $(BUILD) $(SIM) $(WAVSTAT)

.PHONY: all check clean
