#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <stdint.h>

enum class opsize {
    none,
    b,
    w,
    l,
};

enum class inst_type {
    ILLEGAL,
    ABCD,
    ADD,
    ADDA,
    ADDI,
    ADDQ,
    ADDX,
    AND,
    ANDI,
    ASL,
    ASR,
    BCHG,
    BCLR,
    BRA,
    BSET,
    BSR,
    BTST,
    Bcc,
    CHK,
    CLRX,
    CMP,
    CMPA,
    CMPI,
    CMPM,
    DBcc,
    DIVS,
    DIVU,
    EOR,
    EORI,
    EXG,
    EXT,
    JMP,
    JSR,
    LEA,
    LINK,
    LSL,
    LSR,
    MOVE,
    MOVEA,
    MOVEQ,
    MULS,
    MULU,
    NBCD,
    NEG,
    NEGX,
    NOP,
    NOT,
    OR,
    ORI,
    PEA,
    RESET,
    ROL,
    ROR,
    ROXL,
    ROXR,
    RTE,
    RTR,
    RTS,
    SBCD,
    STOP,
    SUB,
    SUBA,
    SUBI,
    SUBQ,
    SUBX,
    SWAP,
    Scc,
    TAS,
    TRAP,
    TRAPV,
    TST,
    UNLK,
};

struct instruction {
    inst_type type;
    const char* const name;
    opsize size;
    uint8_t nea;
    uint8_t ea[2];
    uint8_t data;
    uint8_t extra;
};

constexpr uint8_t extra_cond_flag = 1 << 0; // Upper 4 bits are condition code
constexpr uint8_t extra_disp_flag = 1 << 1; // Displacement word follows

constexpr uint8_t ea_disp = 0b01'000'011;
constexpr uint8_t ea_sr   = 0b01'000'100;

extern const instruction instructions[65536];

#endif