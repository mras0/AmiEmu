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
    CLR,
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
    MOVEM,
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
    uint8_t ilen;
    uint8_t nea;
    uint8_t ea[2];
    uint8_t data;
    uint8_t extra;
};

enum ea_m {
    ea_m_Dn            = 0b000, // Dn
    ea_m_An            = 0b001, // An
    ea_m_A_ind         = 0b010, // (An)
    ea_m_A_ind_post    = 0b011, // (An)+
    ea_m_A_ind_pre     = 0b100, // -(An)
    ea_m_A_ind_disp16  = 0b101, // (d16, An)
    ea_m_A_ind_index   = 0b110, // (d8, An, Xn)
    ea_m_Other         = 0b111, // (Other)

    ea_m_inst_data     = 0b1000, // Hack value from mktab
};

enum ea_other {
    ea_other_abs_w     = 0b000, // (addr.W)
    ea_other_abs_l     = 0b001, // (addr.L)
    ea_other_pc_disp16 = 0b010, // (d16, PC)
    ea_other_pc_index  = 0b011, // (d8, PC, Xn)
    ea_other_imm       = 0b100,
};

constexpr uint8_t extra_cond_flag = 1 << 0; // Upper 4 bits are condition code
constexpr uint8_t extra_disp_flag = 1 << 1; // Displacement word follows

constexpr uint8_t ea_disp    = 0b01'000'011;
constexpr uint8_t ea_sr      = 0b01'000'100;
constexpr uint8_t ea_ccr     = 0b01'000'101;
constexpr uint8_t ea_reglist = 0b01'000'110;

constexpr unsigned max_instruction_words = 5; // 68020+ can be 11 words

extern const instruction instructions[65536];

#endif