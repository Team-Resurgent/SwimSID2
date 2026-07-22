# SwinSID firmware build.
#
# Produces the linked ELF that simavr loads (keeping the .mmcu section intact)
# as well as the .hex. All build output goes to build/ and is named SwinSID88.*.
#
# This build works on native Windows (MSYS2 UCRT64) as well as Linux, because
# avr-gcc is used to preprocess+assemble (it knows where <avr/io.h> lives, so no
# include path needs hard-coding); linking uses avr-ld with the project linker
# script.
#
# Usage (from a shell with the AVR toolchain on PATH, in this directory):
#   make            # build both variants: build/SwinSID88-{pal,ntsc}.elf + .hex
#   make pal        # only the PAL variant
#   make ntsc       # only the NTSC variant
#   make elf        # just the two ELFs (what the emulator needs)
#   make clean
#
# On Windows this means an MSYS2 UCRT64 shell; see sim/README.md for setup.

AVR_CC   = avr-gcc
AVR_LD   = avr-ld
OBJCOPY  = avr-objcopy
MMCU     = atmega88a
LDSCRIPT = src/SwinSID88.ld

# Only the "Lazy Jones fix" firmware is built: -DLAZY_JONES_FIX selects
# CodeKiller's fix from the variant-guarded blocks in SwinSID88.asm.
#
# Video standard: both variants are built. The NTSC one adds -DSWINSID_NTSC,
# which picks the NTSC sample-rate divisor so the pitch tracks a real machine
# (PAL leaves it at the default). See the Timer0 comment in SwinSID88.asm.
ASFLAGS  = -mmcu=$(MMCU) -x assembler-with-cpp -Wall -DLAZY_JONES_FIX
LDFLAGS  = -mavr4 -T $(LDSCRIPT)

BUILD    = build
NAME     = SwinSID88

SRC      = src/SwinSID88.asm
DEPS     = src/SwinSID88.h

PAL_OBJ  = $(BUILD)/$(NAME)-pal.o
PAL_ELF  = $(BUILD)/$(NAME)-pal.elf
PAL_HEX  = $(BUILD)/$(NAME)-pal.hex
NTSC_OBJ = $(BUILD)/$(NAME)-ntsc.o
NTSC_ELF = $(BUILD)/$(NAME)-ntsc.elf
NTSC_HEX = $(BUILD)/$(NAME)-ntsc.hex

# Build both variants by default.
all: pal ntsc

pal:  $(PAL_ELF)  $(PAL_HEX)
ntsc: $(NTSC_ELF) $(NTSC_HEX)

# Both ELFs are what the emulator/player load (PAL + NTSC).
elf: $(PAL_ELF) $(NTSC_ELF)

$(BUILD):
	mkdir -p $(BUILD)

# PAL uses the default timing; NTSC adds -DSWINSID_NTSC.
$(PAL_OBJ): $(SRC) $(DEPS) | $(BUILD)
	$(AVR_CC) $(ASFLAGS) -c $(SRC) -o $@

$(NTSC_OBJ): $(SRC) $(DEPS) | $(BUILD)
	$(AVR_CC) $(ASFLAGS) -DSWINSID_NTSC -c $(SRC) -o $@

$(PAL_ELF): $(PAL_OBJ) $(LDSCRIPT)
	$(AVR_LD) $(LDFLAGS) -o $@ $<

$(NTSC_ELF): $(NTSC_OBJ) $(LDSCRIPT)
	$(AVR_LD) $(LDFLAGS) -o $@ $<

$(PAL_HEX): $(PAL_ELF)
	$(OBJCOPY) -j .text -j .wavetable -j .data -O ihex $< $@

$(NTSC_HEX): $(NTSC_ELF)
	$(OBJCOPY) -j .text -j .wavetable -j .data -O ihex $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all pal ntsc elf clean
