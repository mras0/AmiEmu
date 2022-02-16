#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <stdint.h>

enum class opsize {
    none,
    b,
    w,
    l,
};

constexpr uint8_t opsize_bytes(opsize size)
{
    return size == opsize::l ? 4 : size == opsize::b ? 1 : 2;
}

constexpr uint32_t opsize_msb_mask(opsize size)
{
    return size == opsize::b ? 0x80 : size == opsize::w ? 0x8000 : 0x80000000;
}

constexpr uint32_t opsize_all_mask(opsize size)
{
    return size == opsize::b ? 0xff : size == opsize::w ? 0xffff : 0xffffffff;
}

constexpr int32_t sext(uint32_t val, opsize size)
{
    switch (size) {
    case opsize::b:
        return static_cast<int8_t>(val & 0xff);
    case opsize::w:
        return static_cast<int16_t>(val & 0xffff);
    default:
        return static_cast<int32_t>(val);
    }
}

enum class inst_type {
    ILLEGAL,
#ifdef DEBUG_BREAK_INST
    DBGBRK,
#endif
    ABCD,
    ADD,
    ADDA,
    ADDQ,
    ADDX,
    AND,
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
    CMPM,
    DBcc,
    DIVS,
    DIVU,
    EOR,
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
    MOVEP,
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
    // Other hack values (see below are possible)
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
constexpr uint8_t extra_priv_flag = 1 << 2; // Instruction is privileged (causes a trap if executed in user mode)

constexpr uint8_t ea_m_shift = 3;
constexpr uint8_t ea_xn_mask = 7;

constexpr uint8_t ea_pc_index  = ea_m_Other << ea_m_shift | ea_other_pc_index;
constexpr uint8_t ea_immediate = ea_m_Other << ea_m_shift | ea_other_imm;
constexpr uint8_t ea_data3     = 0b01'000'000;
constexpr uint8_t ea_data4     = 0b01'000'001;
constexpr uint8_t ea_data8     = 0b01'000'010;
constexpr uint8_t ea_disp      = 0b01'000'011;
constexpr uint8_t ea_sr        = 0b01'000'100;
constexpr uint8_t ea_ccr       = 0b01'000'101;
constexpr uint8_t ea_reglist   = 0b01'000'110;
constexpr uint8_t ea_bitnum    = 0b01'000'111;
constexpr uint8_t ea_usp       = 0b01'001'000;

constexpr unsigned max_instruction_words = 5; // 68020+ can be 11 words

constexpr uint16_t illegal_instruction_num = 0x4afc; // Designated illegal instruction
constexpr uint16_t reset_instruction_num   = 0x4e70;
constexpr uint16_t stop_instruction_num    = 0x4e72;
constexpr uint16_t movec_instruction_dr0_num = 0x4e7a; // Not supported on 68000
constexpr uint16_t movec_instruction_dr1_num = 0x4e7b; // Not supported on 68000

#ifdef DEBUG_BREAK_INST
constexpr uint16_t debug_break_instruction_num = DEBUG_BREAK_INST;
#endif

extern const instruction instructions[65536];

#endif
