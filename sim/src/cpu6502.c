/*
 * cpu6502.c - Minimal MOS 6502 emulator (see cpu6502.h).
 *
 * Table-driven decoder in the style of the well-known fake6502 by Mike
 * Chambers, rewritten here. Official opcodes are fully implemented with
 * decimal mode; common undocumented opcodes are implemented; the rest run as
 * NOPs with the correct addressing mode so the PC stays in sync.
 */
#include "cpu6502.h"

/* Status register bit masks. */
#define FLAG_C 0x01
#define FLAG_Z 0x02
#define FLAG_I 0x04
#define FLAG_D 0x08
#define FLAG_B 0x10
#define FLAG_U 0x20
#define FLAG_V 0x40
#define FLAG_N 0x80

uint16_t cpu_pc;
uint8_t  cpu_a, cpu_x, cpu_y, cpu_sp, cpu_status;
uint64_t cpu_clockticks;

/* Per-instruction decode state. */
static uint16_t ea;            /* effective address */
static uint8_t  am_acc;        /* 1 if the instruction operates on the accumulator */
static uint8_t  penaltyop;     /* set by ops that pay a page-cross penalty */
static uint8_t  penaltyaddr;   /* set by addressing modes that may cross a page */
static uint32_t extra_cycles;  /* branch-taken / page-cross extras */

static inline uint8_t  rd(uint16_t a)             { return read6502(a); }
static inline void     wr(uint16_t a, uint8_t v)  { write6502(a, v); }
static inline uint16_t rd16(uint16_t a)           { return (uint16_t)rd(a) | ((uint16_t)rd(a + 1) << 8); }

static inline void setzn(uint8_t v) {
    if (v) cpu_status &= ~FLAG_Z; else cpu_status |= FLAG_Z;
    if (v & 0x80) cpu_status |= FLAG_N; else cpu_status &= ~FLAG_N;
}
static inline void setcarry(int c) { if (c) cpu_status |= FLAG_C; else cpu_status &= ~FLAG_C; }

static inline void push8(uint8_t v)  { wr(0x100 + cpu_sp, v); cpu_sp--; }
static inline uint8_t pull8(void)    { cpu_sp++; return rd(0x100 + cpu_sp); }
static inline void push16(uint16_t v){ push8((uint8_t)(v >> 8)); push8((uint8_t)(v & 0xff)); }
static inline uint16_t pull16(void)  { uint8_t lo = pull8(); uint8_t hi = pull8(); return lo | (hi << 8); }

static inline uint8_t getvalue(void)      { return am_acc ? cpu_a : rd(ea); }
static inline void     putvalue(uint8_t v){ if (am_acc) cpu_a = v; else wr(ea, v); }

/* ---------------------------------------------------------------- addressing */
static void imp(void)  { }
static void acc(void)  { am_acc = 1; }
static void imm(void)  { ea = cpu_pc++; }
static void zp(void)   { ea = rd(cpu_pc++); }
static void zpx(void)  { ea = (rd(cpu_pc++) + cpu_x) & 0xff; }
static void zpy(void)  { ea = (rd(cpu_pc++) + cpu_y) & 0xff; }
static void rel(void)  { uint16_t o = rd(cpu_pc++); if (o & 0x80) o |= 0xff00; ea = cpu_pc + o; }
static void abso(void) { ea = rd16(cpu_pc); cpu_pc += 2; }
static void absx(void) { uint16_t b = rd16(cpu_pc); cpu_pc += 2; ea = b + cpu_x;
                         if ((b & 0xff00) != (ea & 0xff00)) penaltyaddr = 1; }
static void absy(void) { uint16_t b = rd16(cpu_pc); cpu_pc += 2; ea = b + cpu_y;
                         if ((b & 0xff00) != (ea & 0xff00)) penaltyaddr = 1; }
static void ind(void)  { uint16_t p = rd16(cpu_pc); cpu_pc += 2;
                         /* 6502 page-boundary bug on the high byte fetch */
                         uint16_t lo = rd(p);
                         uint16_t hi = rd((p & 0xff00) | ((p + 1) & 0x00ff));
                         ea = lo | (hi << 8); }
static void indx(void) { uint8_t z = (rd(cpu_pc++) + cpu_x) & 0xff;
                         ea = rd(z) | (rd((z + 1) & 0xff) << 8); }
static void indy(void) { uint8_t z = rd(cpu_pc++);
                         uint16_t b = rd(z) | (rd((z + 1) & 0xff) << 8);
                         ea = b + cpu_y;
                         if ((b & 0xff00) != (ea & 0xff00)) penaltyaddr = 1; }

/* ------------------------------------------------------------- instructions */
static void adc_val(uint8_t v) {
    if (cpu_status & FLAG_D) {
        int carry = (cpu_status & FLAG_C) ? 1 : 0;
        int lo = (cpu_a & 0x0f) + (v & 0x0f) + carry;
        int hi = (cpu_a >> 4) + (v >> 4);
        if (lo > 9) { lo += 6; hi++; }
        uint16_t bin = (uint16_t)cpu_a + v + carry;
        if ((bin & 0xff) == 0) cpu_status |= FLAG_Z; else cpu_status &= ~FLAG_Z;
        if (hi & 0x08) cpu_status |= FLAG_N; else cpu_status &= ~FLAG_N;
        if ((~(cpu_a ^ v) & (cpu_a ^ (hi << 4))) & 0x80) cpu_status |= FLAG_V; else cpu_status &= ~FLAG_V;
        if (hi > 9) hi += 6;
        setcarry(hi > 15);
        cpu_a = (uint8_t)(((hi << 4) | (lo & 0x0f)) & 0xff);
    } else {
        uint16_t r = (uint16_t)cpu_a + v + ((cpu_status & FLAG_C) ? 1 : 0);
        setcarry(r & 0xff00);
        if ((~(cpu_a ^ v) & (cpu_a ^ r)) & 0x80) cpu_status |= FLAG_V; else cpu_status &= ~FLAG_V;
        cpu_a = (uint8_t)(r & 0xff);
        setzn(cpu_a);
    }
}
static void sbc_val(uint8_t v) {
    if (cpu_status & FLAG_D) {
        int carry = (cpu_status & FLAG_C) ? 1 : 0;
        int lo = (cpu_a & 0x0f) - (v & 0x0f) - (1 - carry);
        int hi = (cpu_a >> 4) - (v >> 4);
        if (lo & 0x10) { lo -= 6; hi--; }
        if (hi & 0x10) hi -= 6;
        uint16_t bin = (uint16_t)cpu_a - v - (1 - carry);
        setcarry(!(bin & 0xff00));
        if ((bin & 0xff) == 0) cpu_status |= FLAG_Z; else cpu_status &= ~FLAG_Z;
        if (bin & 0x80) cpu_status |= FLAG_N; else cpu_status &= ~FLAG_N;
        if ((cpu_a ^ v) & (cpu_a ^ bin) & 0x80) cpu_status |= FLAG_V; else cpu_status &= ~FLAG_V;
        cpu_a = (uint8_t)(((hi << 4) | (lo & 0x0f)) & 0xff);
    } else {
        uint8_t iv = v ^ 0xff;
        uint16_t r = (uint16_t)cpu_a + iv + ((cpu_status & FLAG_C) ? 1 : 0);
        setcarry(r & 0xff00);
        if ((~(cpu_a ^ iv) & (cpu_a ^ r)) & 0x80) cpu_status |= FLAG_V; else cpu_status &= ~FLAG_V;
        cpu_a = (uint8_t)(r & 0xff);
        setzn(cpu_a);
    }
}

static void op_adc(void) { penaltyop = 1; adc_val(getvalue()); }
static void op_sbc(void) { penaltyop = 1; sbc_val(getvalue()); }
static void op_and(void) { penaltyop = 1; cpu_a &= getvalue(); setzn(cpu_a); }
static void op_ora(void) { penaltyop = 1; cpu_a |= getvalue(); setzn(cpu_a); }
static void op_eor(void) { penaltyop = 1; cpu_a ^= getvalue(); setzn(cpu_a); }

static void op_asl(void) { uint8_t v = getvalue(); setcarry(v & 0x80); v <<= 1; putvalue(v); setzn(v); }
static void op_lsr(void) { uint8_t v = getvalue(); setcarry(v & 0x01); v >>= 1; putvalue(v); setzn(v); }
static void op_rol(void) { uint8_t v = getvalue(); int c = cpu_status & FLAG_C; setcarry(v & 0x80);
                           v = (uint8_t)((v << 1) | (c ? 1 : 0)); putvalue(v); setzn(v); }
static void op_ror(void) { uint8_t v = getvalue(); int c = cpu_status & FLAG_C; setcarry(v & 0x01);
                           v = (uint8_t)((v >> 1) | (c ? 0x80 : 0)); putvalue(v); setzn(v); }

static void op_bit(void) { uint8_t v = getvalue();
                           if (cpu_a & v) cpu_status &= ~FLAG_Z; else cpu_status |= FLAG_Z;
                           cpu_status = (cpu_status & 0x3f) | (v & 0xc0); }

static void cmp_reg(uint8_t reg) { uint8_t v = getvalue();
                                   setcarry(reg >= v);
                                   setzn((uint8_t)(reg - v)); }
static void op_cmp(void) { penaltyop = 1; cmp_reg(cpu_a); }
static void op_cpx(void) { cmp_reg(cpu_x); }
static void op_cpy(void) { cmp_reg(cpu_y); }

static void op_dec(void) { uint8_t v = (uint8_t)(getvalue() - 1); putvalue(v); setzn(v); }
static void op_inc(void) { uint8_t v = (uint8_t)(getvalue() + 1); putvalue(v); setzn(v); }
static void op_dex(void) { cpu_x--; setzn(cpu_x); }
static void op_dey(void) { cpu_y--; setzn(cpu_y); }
static void op_inx(void) { cpu_x++; setzn(cpu_x); }
static void op_iny(void) { cpu_y++; setzn(cpu_y); }

static void op_lda(void) { penaltyop = 1; cpu_a = getvalue(); setzn(cpu_a); }
static void op_ldx(void) { penaltyop = 1; cpu_x = getvalue(); setzn(cpu_x); }
static void op_ldy(void) { penaltyop = 1; cpu_y = getvalue(); setzn(cpu_y); }
static void op_sta(void) { putvalue(cpu_a); }
static void op_stx(void) { putvalue(cpu_x); }
static void op_sty(void) { putvalue(cpu_y); }

static void op_tax(void) { cpu_x = cpu_a; setzn(cpu_x); }
static void op_tay(void) { cpu_y = cpu_a; setzn(cpu_y); }
static void op_txa(void) { cpu_a = cpu_x; setzn(cpu_a); }
static void op_tya(void) { cpu_a = cpu_y; setzn(cpu_a); }
static void op_tsx(void) { cpu_x = cpu_sp; setzn(cpu_x); }
static void op_txs(void) { cpu_sp = cpu_x; }

static void op_clc(void) { cpu_status &= ~FLAG_C; }
static void op_sec(void) { cpu_status |= FLAG_C; }
static void op_cli(void) { cpu_status &= ~FLAG_I; }
static void op_sei(void) { cpu_status |= FLAG_I; }
static void op_cld(void) { cpu_status &= ~FLAG_D; }
static void op_sed(void) { cpu_status |= FLAG_D; }
static void op_clv(void) { cpu_status &= ~FLAG_V; }

static void op_pha(void) { push8(cpu_a); }
static void op_pla(void) { cpu_a = pull8(); setzn(cpu_a); }
static void op_php(void) { push8(cpu_status | FLAG_B | FLAG_U); }
static void op_plp(void) { cpu_status = (pull8() & ~FLAG_B) | FLAG_U; }

static void branch(int cond) {
    if (cond) {
        uint16_t old = cpu_pc;
        cpu_pc = ea;
        extra_cycles++;
        if ((old & 0xff00) != (cpu_pc & 0xff00)) extra_cycles++;
    }
}
static void op_bpl(void) { branch(!(cpu_status & FLAG_N)); }
static void op_bmi(void) { branch(cpu_status & FLAG_N); }
static void op_bvc(void) { branch(!(cpu_status & FLAG_V)); }
static void op_bvs(void) { branch(cpu_status & FLAG_V); }
static void op_bcc(void) { branch(!(cpu_status & FLAG_C)); }
static void op_bcs(void) { branch(cpu_status & FLAG_C); }
static void op_bne(void) { branch(!(cpu_status & FLAG_Z)); }
static void op_beq(void) { branch(cpu_status & FLAG_Z); }

static void op_jmp(void) { cpu_pc = ea; }
static void op_jsr(void) { push16(cpu_pc - 1); cpu_pc = ea; }
static void op_rts(void) { cpu_pc = pull16() + 1; }
static void op_rti(void) { cpu_status = (pull8() & ~FLAG_B) | FLAG_U; cpu_pc = pull16(); }
static void op_brk(void) { cpu_pc++; push16(cpu_pc); push8(cpu_status | FLAG_B | FLAG_U);
                           cpu_status |= FLAG_I; cpu_pc = rd16(0xfffe); }
static void op_nop(void) { penaltyop = 1; }

/* Common undocumented opcodes. */
static void op_slo(void) { uint8_t v = getvalue(); setcarry(v & 0x80); v <<= 1; putvalue(v);
                           cpu_a |= v; setzn(cpu_a); }
static void op_rla(void) { uint8_t v = getvalue(); int c = cpu_status & FLAG_C; setcarry(v & 0x80);
                           v = (uint8_t)((v << 1) | (c ? 1 : 0)); putvalue(v); cpu_a &= v; setzn(cpu_a); }
static void op_sre(void) { uint8_t v = getvalue(); setcarry(v & 0x01); v >>= 1; putvalue(v);
                           cpu_a ^= v; setzn(cpu_a); }
static void op_rra(void) { uint8_t v = getvalue(); int c = cpu_status & FLAG_C; setcarry(v & 0x01);
                           v = (uint8_t)((v >> 1) | (c ? 0x80 : 0)); putvalue(v); adc_val(v); }
static void op_dcp(void) { uint8_t v = (uint8_t)(getvalue() - 1); putvalue(v); cmp_reg(cpu_a); }
static void op_isc(void) { uint8_t v = (uint8_t)(getvalue() + 1); putvalue(v); sbc_val(v); }
static void op_lax(void) { penaltyop = 1; cpu_a = cpu_x = getvalue(); setzn(cpu_a); }
static void op_sax(void) { putvalue(cpu_a & cpu_x); }

typedef void (*fp)(void);

static const fp addrtable[256] = {
/*        0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/*0*/   imp,  indx, imp,  indx, zp,   zp,   zp,   zp,   imp,  imm,  acc,  imm,  abso, abso, abso, abso,
/*1*/   rel,  indy, imp,  indy, zpx,  zpx,  zpx,  zpx,  imp,  absy, imp,  absy, absx, absx, absx, absx,
/*2*/   abso, indx, imp,  indx, zp,   zp,   zp,   zp,   imp,  imm,  acc,  imm,  abso, abso, abso, abso,
/*3*/   rel,  indy, imp,  indy, zpx,  zpx,  zpx,  zpx,  imp,  absy, imp,  absy, absx, absx, absx, absx,
/*4*/   imp,  indx, imp,  indx, zp,   zp,   zp,   zp,   imp,  imm,  acc,  imm,  abso, abso, abso, abso,
/*5*/   rel,  indy, imp,  indy, zpx,  zpx,  zpx,  zpx,  imp,  absy, imp,  absy, absx, absx, absx, absx,
/*6*/   imp,  indx, imp,  indx, zp,   zp,   zp,   zp,   imp,  imm,  acc,  imm,  ind,  abso, abso, abso,
/*7*/   rel,  indy, imp,  indy, zpx,  zpx,  zpx,  zpx,  imp,  absy, imp,  absy, absx, absx, absx, absx,
/*8*/   imm,  indx, imm,  indx, zp,   zp,   zp,   zp,   imp,  imm,  imp,  imm,  abso, abso, abso, abso,
/*9*/   rel,  indy, imp,  indy, zpx,  zpx,  zpy,  zpy,  imp,  absy, imp,  absy, absx, absx, absy, absy,
/*A*/   imm,  indx, imm,  indx, zp,   zp,   zp,   zp,   imp,  imm,  imp,  imm,  abso, abso, abso, abso,
/*B*/   rel,  indy, imp,  indy, zpx,  zpx,  zpy,  zpy,  imp,  absy, imp,  absy, absx, absx, absy, absy,
/*C*/   imm,  indx, imm,  indx, zp,   zp,   zp,   zp,   imp,  imm,  imp,  imm,  abso, abso, abso, abso,
/*D*/   rel,  indy, imp,  indy, zpx,  zpx,  zpx,  zpx,  imp,  absy, imp,  absy, absx, absx, absx, absx,
/*E*/   imm,  indx, imm,  indx, zp,   zp,   zp,   zp,   imp,  imm,  imp,  imm,  abso, abso, abso, abso,
/*F*/   rel,  indy, imp,  indy, zpx,  zpx,  zpx,  zpx,  imp,  absy, imp,  absy, absx, absx, absx, absx
};

static const fp optable[256] = {
/*        0       1       2       3       4       5       6       7       8       9       A       B       C       D       E       F */
/*0*/   op_brk, op_ora, op_nop, op_slo, op_nop, op_ora, op_asl, op_slo, op_php, op_ora, op_asl, op_nop, op_nop, op_ora, op_asl, op_slo,
/*1*/   op_bpl, op_ora, op_nop, op_slo, op_nop, op_ora, op_asl, op_slo, op_clc, op_ora, op_nop, op_slo, op_nop, op_ora, op_asl, op_slo,
/*2*/   op_jsr, op_and, op_nop, op_rla, op_bit, op_and, op_rol, op_rla, op_plp, op_and, op_rol, op_nop, op_bit, op_and, op_rol, op_rla,
/*3*/   op_bmi, op_and, op_nop, op_rla, op_nop, op_and, op_rol, op_rla, op_sec, op_and, op_nop, op_rla, op_nop, op_and, op_rol, op_rla,
/*4*/   op_rti, op_eor, op_nop, op_sre, op_nop, op_eor, op_lsr, op_sre, op_pha, op_eor, op_lsr, op_nop, op_jmp, op_eor, op_lsr, op_sre,
/*5*/   op_bvc, op_eor, op_nop, op_sre, op_nop, op_eor, op_lsr, op_sre, op_cli, op_eor, op_nop, op_sre, op_nop, op_eor, op_lsr, op_sre,
/*6*/   op_rts, op_adc, op_nop, op_rra, op_nop, op_adc, op_ror, op_rra, op_pla, op_adc, op_ror, op_nop, op_jmp, op_adc, op_ror, op_rra,
/*7*/   op_bvs, op_adc, op_nop, op_rra, op_nop, op_adc, op_ror, op_rra, op_sei, op_adc, op_nop, op_rra, op_nop, op_adc, op_ror, op_rra,
/*8*/   op_nop, op_sta, op_nop, op_sax, op_sty, op_sta, op_stx, op_sax, op_dey, op_nop, op_txa, op_nop, op_sty, op_sta, op_stx, op_sax,
/*9*/   op_bcc, op_sta, op_nop, op_nop, op_sty, op_sta, op_stx, op_sax, op_tya, op_sta, op_txs, op_nop, op_nop, op_sta, op_nop, op_nop,
/*A*/   op_ldy, op_lda, op_ldx, op_lax, op_ldy, op_lda, op_ldx, op_lax, op_tay, op_lda, op_tax, op_nop, op_ldy, op_lda, op_ldx, op_lax,
/*B*/   op_bcs, op_lda, op_nop, op_lax, op_ldy, op_lda, op_ldx, op_lax, op_clv, op_lda, op_tsx, op_nop, op_ldy, op_lda, op_ldx, op_lax,
/*C*/   op_cpy, op_cmp, op_nop, op_dcp, op_cpy, op_cmp, op_dec, op_dcp, op_iny, op_cmp, op_dex, op_nop, op_cpy, op_cmp, op_dec, op_dcp,
/*D*/   op_bne, op_cmp, op_nop, op_dcp, op_nop, op_cmp, op_dec, op_dcp, op_cld, op_cmp, op_nop, op_dcp, op_nop, op_cmp, op_dec, op_dcp,
/*E*/   op_cpx, op_sbc, op_nop, op_isc, op_cpx, op_sbc, op_inc, op_isc, op_inx, op_sbc, op_nop, op_sbc, op_cpx, op_sbc, op_inc, op_isc,
/*F*/   op_beq, op_sbc, op_nop, op_isc, op_nop, op_sbc, op_inc, op_isc, op_sed, op_sbc, op_nop, op_isc, op_nop, op_sbc, op_inc, op_isc
};

static const uint8_t ticktable[256] = {
/*      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/*0*/   7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
/*1*/   2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*2*/   6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
/*3*/   2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*4*/   6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
/*5*/   2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*6*/   6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
/*7*/   2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*8*/   2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
/*9*/   2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
/*A*/   2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
/*B*/   2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
/*C*/   2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
/*D*/   2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
/*E*/   2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
/*F*/   2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7
};

void cpu_reset(void) {
    cpu_a = cpu_x = cpu_y = 0;
    cpu_sp = 0xfd;
    cpu_status = FLAG_U | FLAG_I;
    cpu_pc = rd16(0xfffc);
    cpu_clockticks = 0;
}

uint32_t cpu_step(void) {
    am_acc = 0;
    penaltyop = 0;
    penaltyaddr = 0;
    extra_cycles = 0;

    uint8_t op = rd(cpu_pc++);
    addrtable[op]();
    optable[op]();

    uint32_t cyc = ticktable[op] + extra_cycles;
    if (penaltyop && penaltyaddr) cyc++;
    cpu_clockticks += cyc;
    return cyc;
}

uint32_t cpu_call(uint16_t addr, uint32_t maxinstr) {
    uint8_t sp0 = cpu_sp;
    push16(0xffff);          /* sentinel; final RTS restores sp to sp0 */
    cpu_pc = addr;
    uint32_t n = 0;
    while (cpu_sp != sp0 && n < maxinstr) {
        cpu_step();
        n++;
    }
    return n;
}
