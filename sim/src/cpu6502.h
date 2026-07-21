/*
 * cpu6502.h - Minimal MOS 6502 emulator for driving SID tunes.
 *
 * All official opcodes are implemented (including decimal mode) plus the
 * most common undocumented opcodes (SLO, RLA, SRE, RRA, DCP, ISC, LAX, SAX).
 * Remaining undocumented opcodes execute as NOPs with correct byte length and
 * cycle count so the program counter never desyncs.
 *
 * The host program must provide read6502()/write6502().
 */
#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>

/* Memory access callbacks, implemented by the host program. */
extern uint8_t read6502(uint16_t address);
extern void    write6502(uint16_t address, uint8_t value);

/* Externally visible CPU state (so the host can set up subroutine calls). */
extern uint16_t cpu_pc;
extern uint8_t  cpu_a, cpu_x, cpu_y, cpu_sp, cpu_status;
extern uint64_t cpu_clockticks;   /* monotonically increasing cycle counter */

/* Reset the CPU (loads PC from the reset vector at $FFFC). */
void cpu_reset(void);

/* Execute one instruction; returns the number of cycles it took. */
uint32_t cpu_step(void);

/*
 * Call a subroutine at 'addr' and run until it returns (balanced RTS), or until
 * 'maxinstr' instructions have executed (runaway guard). Registers a/x/y should
 * be set before calling. Returns the number of instructions executed.
 */
uint32_t cpu_call(uint16_t addr, uint32_t maxinstr);

#endif /* CPU6502_H */
