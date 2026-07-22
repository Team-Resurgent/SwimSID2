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
#   make            # build the firmware (build/SwinSID88.elf + .hex)
#   make elf        # only the ELF file (what the emulator needs)
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
ASFLAGS  = -mmcu=$(MMCU) -x assembler-with-cpp -Wall -DLAZY_JONES_FIX
LDFLAGS  = -mavr4 -T $(LDSCRIPT)

BUILD    = build
NAME     = SwinSID88

SRC      = src/SwinSID88.asm
DEPS     = src/SwinSID88.h

OBJ = $(BUILD)/$(NAME).o
ELF = $(BUILD)/$(NAME).elf
HEX = $(BUILD)/$(NAME).hex

all: $(ELF) $(HEX)

elf: $(ELF)

$(BUILD):
	mkdir -p $(BUILD)

$(OBJ): $(SRC) $(DEPS) | $(BUILD)
	$(AVR_CC) $(ASFLAGS) -c $(SRC) -o $@

$(ELF): $(OBJ) $(LDSCRIPT)
	$(AVR_LD) $(LDFLAGS) -o $@ $<

$(HEX): $(ELF)
	$(OBJCOPY) -j .text -j .wavetable -j .data -O ihex $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all elf clean
