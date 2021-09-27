#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <string>
#include <cassert>
#include "ioutil.h"
#include "disasm.h"
#include "asm.h"
#include "memory.h"

#define CHECK_BIN(lhs, op, rhs)                                                                                                  \
    do {                                                                                                                         \
        const auto l = (lhs);                                                                                                              \
        const auto r = (rhs);                                                                                                              \
        if (!(l op r)) {                                                                                                                   \
            std::ostringstream oss;                                                                                                        \
            oss << "Check failed " << #lhs << " (" << test_format { l } << ") " << #op << " " << #rhs << " (" << test_format { r } << ")"; \
            test_failed(oss.str(), __func__, __FILE__, __LINE__);                                                                          \
        }                                                                                                                                  \
    } while (0)

#define CHECK_EQ(lhs, rhs) CHECK_BIN(lhs, ==, rhs)

void test_failed(const std::string& msg, const char* function, const char* file, int line)
{
    std::ostringstream oss;
    oss << "Test failed in " << function << " " << file << ":" << line << ": " << msg;
    throw std::runtime_error { oss.str() };
}

template<typename T>
class test_format {
public:
    explicit test_format(const T& val)
        : val_ { val }
    {
    }

    friend std::ostream& operator<<(std::ostream& os, const test_format& tf)
    {
        if constexpr (std::is_integral_v<T>)
            return os << "$" << hexfmt(tf.val_);
        else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>)
            return os << '"' << tf.val_ << '"';
        else
            return os << tf.val_;
    }

private:
    const T& val_;
};

void disasm_code(std::ostream& os, uint32_t start_pc, const std::vector<uint16_t>& code)
{
    uint32_t pc = start_pc;
    for (size_t i = 0; i < code.size();) {
        const auto nw = disasm(os, pc, &code[i], code.size() - i);
        os << "\n";
        pc += 2 * nw;
        i += nw;
    }
}

bool simple_asm_tests()
{
    // TODO: Check invalid combinations

    const struct {
        const char* text;
        std::vector<uint16_t> code;
    } test_cases[] = {
        { "MOVEQ #42, d2", { 0x742a } },
        { "MOVE.L d3, d4", { 0x2803 } },
        { "RTS", { 0x4e75 } },
        { "label bra label", { 0x60FE } },
        { "label move.l #$12345678, $1234.w\n bra.w label", { 0x21fc, 0x1234, 0x5678, 0x1234, 0x6000, 0xfff6 } },
        { "MOVE.W #$1234, d2", { 0x343c, 0x1234 } },
        { "bra label\nrts\nlabel rts", { 0x6000, 0x0004, 0x4e75, 0x4e75 } },
        { "\trts\n  lab: moveq #0, d0\nmove.l #lab, a1\n", { 0x4e75, 0x7000, 0x227c, 0x0000, 0x1002 } },
        { "move.l #l2, d0\nl2: dc.w $4afc\n", { 0x203c, 0x0000, 0x1006, 0x4afc } },
        { "move.l #$12345678, val\nval dc.l $4afc4afc\n", { 0x23fc, 0x1234, 0x5678, 0x0000, 0x100a, 0x4afc, 0x4afc } },
        { "  x: move.w #$1234, x.w\n", { 0x31fc, 0x1234, 0x1000 } },
        { "move.l d3, (a2)\n", { 0x2483 } },
        { "move.l d4, (a7)+\n", { 0x2ec4 } },
        { "move.l -(a3), a0\n", { 0x2063 } },
        { "move.b #-12, d0\n", { 0x103c, 0x00f4 } },
        { "move.l -12.w, d0\n", { 0x2038, 0xfff4 } },
        { "move.w -1234(a0), d2\n", { 0x3428, 0xfb2e } },
        { "move.w 8(a2,d2.w), d3\n", { 0x3632, 0x2008 } },
        { "move.w 0(a3,d4.l), d3\n", { 0x3633, 0x4800 } },
        { "move.l 10(a2,a1.w), d3\n", {0x2632, 0x900a} },
        { "move.w lab(pc), a0\nlab dc.w $4afc\n", { 0x307a, 0x0002, 0x4afc } },
        { "move.l lab(pc,d0.w), $12345678\nrts\nlab dc.w $4afc\n", { 0x23fb, 0x0008, 0x1234, 0x5678, 0x4e75, 0x4afc} },
        { "x: move.w x(pc), d0\n", {0x303a, 0xfffe} },
        { "ADD.B #12, d0\n", { 0x0600, 0x000c } },
        { "ADD.L #$12345678, 10(a0,d0.l)\n", { 0x06b0, 0x1234, 0x5678, 0x080a } },
        { "ADD.W #$1234, a0\n", { 0xd0fc, 0x1234 } },
        { "ADD.L #$12345678, a3\n", { 0xd7fc, 0x1234, 0x5678, } },
        { "ADD.B (a0), d0\n", { 0xd010 } },
        { "ADD.L d5, (a3)+\n", { 0xdb9b } },
        { "ADD.L d2, d3\n", { 0xd682 } },
        { "SUB.B #12, d0\n", { 0x0400, 0x000c } },
        { "SUB.L #$12345678, 10(a0,d0.l)\n", { 0x04b0, 0x1234, 0x5678, 0x080a } },
        { "SUB.W #$1234, a0\n", { 0x90fc, 0x1234 } },
        { "SUB.L #$12345678, a3\n", { 0x97fc, 0x1234, 0x5678, } },
        { "SUB.B (a0), d0\n", { 0x9010 } },
        { "SUB.L d5, (a3)+\n", { 0x9b9b } },
        { "SUB.L d2, d3\n", { 0x9682 } },
        { "AND.B #$34, (a0)\n", { 0x0210, 0x0034 } },
        { "AND.L #$1234, (a3)+\n", { 0x029b, 0x0000, 0x1234 } },
        { "AND.W #42, 12(a2)\n", {0x026a, 0x002a, 0x000c } },
        { "AND.L d4, -(a2)\n", { 0xc9a2 } },
        { "AND.L d2, d3\n", { 0xc682 } },
        { "EOR.B #$34, (a0)\n", { 0x0a10, 0x0034 } },
        { "EOR.L #$1234, (a3)+\n", { 0x0a9b, 0x0000, 0x1234 } },
        { "EOR.W d3, (a1)\n", { 0xb751 } },
        { "EOR.L d4, -(a2)\n", { 0xb9a2 } },
        { "EOR.L d2, d3\n", { 0xb583 } },
        { "OR.B #$34, (a0)\n", { 0x0010, 0x0034 } },
        { "OR.L #$1234, (a3)+\n", { 0x009b, 0x0000, 0x1234 } },
        { "OR.L d4, -(a2)\n", { 0x89a2 } },
        { "OR.L d2, d3\n", { 0x8682 } },
        { "BCHG #7, d2\n", { 0x0842, 0x0007 } },
        { "BCHG d0, (a2)\n", { 0x0152 } },
        { "BCLR #7, d2\n", { 0x0882, 0x0007 } },
        { "BCLR d0, (a2)\n", { 0x0192 } },
        { "BSET #7, d2\n", { 0x08c2, 0x0007 } },
        { "BSET d0, (a2)\n", { 0x01d2 } },
        { "BTST #7, d2\n", { 0x0802, 0x0007 } },
        { "BTST d0, (a2)\n", { 0x0112 } },
        { "CMP.B (a1)+, d2\n", { 0xb419 } },
        { "CMP.W (a1), a1\n", { 0xb2d1 } },
        { "CMP.L #42, a2\n", { 0xb5fc, 0x0000, 0x002a } },
        { "CMP.W #42, a2\n", { 0xb4fc, 0x002a } },
        { "CMP.B #42, (a0)\n", { 0x0c10, 0x002a } },
        { "CMP.L d0, d2\n", { 0xb480 } },
        { "add.w a3, d1", { 0xd24b } },
        { "swap d4", { 0x4844 } },
        { "ext.w d2", { 0x4882 } },
        { "ext.l d3", { 0x48c3 } },
        { "clr.b 12(a0)", { 0x4228, 0x000c } },
        { "clr.w -(a7)", { 0x4267 } },
        { "clr.l d7", { 0x4287 } },
        { "neg.b 2(a0,d0.l)", { 0x4430, 0x0802 } },
        { "neg.w -(a7)", { 0x4467 } },
        { "neg.l d7", { 0x4487 } },
        { "negx.b (a0)", { 0x4010 } },
        { "negx.w -(a7)", { 0x4067 } },
        { "negx.l d7", { 0x4087 } },
        { "not.b (a0)", { 0x4610 } },
        { "not.w -(a7)", { 0x4667 } },
        { "not.l d7", { 0x4687 } },
        { "nbcd d0", { 0x4800 } },
        { "nbcd.b (a0)", { 0x4810 } },
        { "tst.b (a0)", { 0x4a10 } },
        { "tst.w -(a7)", { 0x4a67} },
        { "tst.l d7", { 0x4a87 } },
        { "pea 16(a0)", { 0x4868,  0x0010 } },
        { "lea 42(a0,d2.w), a3", { 0x47f0, 0x202a } },
        { "jsr (a0)", { 0x4e90 } },
        { "l jsr l(pc)", { 0x4eba, 0xfffe } },
        { "jmp $1234.w", { 0x4ef8, 0x1234 } },
        { "addq.l #8, d0", { 0x5080 } },
        { "addq.w #3, 2(a0,d0.w)", { 0x5070, 0x0002 } },
        { "subq.w #2, a0", { 0x5148 } },
        { "subq.l #8, -(a7)", { 0x51a7} },
        { "exg d0, d1", { 0xc141 } },
        { "exg a0, a1", { 0xc149 } },
        { "exg.l d0, a1", { 0xc189 } },
        { "rte", { 0x4e73 } },
        { "nop", { 0x4e71, } },
        { "illegal", { 0x4afc } },
        { "reset", { 0x4e70 } },
        { "stop #$2700", { 0x4e72, 0x2700 } },
        { "or #$ab, ccr", { 0x003c, 0x00ab } },
        { "or #$abcd, sr", { 0x007c, 0xabcd } },
        { "and #$ab, ccr", { 0x023c, 0x00ab } },
        { "and #$abcd, sr", { 0x027c, 0xabcd } },
        { "eor #$ab, ccr", { 0x0a3c, 0x00ab } },
        { "eor #$abcd, sr", { 0x0a7c, 0xabcd } },
        { "move #$ab, ccr", { 0x44fc, 0x00ab } },
        { "move #$abcd, sr", { 0x46fc, 0xabcd } },
        { "move sr, d0", { 0x40c0 } },
        { "move.w sr, 12(a2)", { 0x40ea, 0x000c } },
        { "move sp, d0", { 0x300f } },
        { "move usp, a0", { 0x4e68 } },
        { "move a3, usp", { 0x4e63 } },
        { "label bsr label", { 0x61fe } },
        { "label move.l #$12345678, $1234.w\n bsr.w label", { 0x21fc, 0x1234, 0x5678, 0x1234, 0x6100, 0xfff6} },
        { "label BHI label", { 0x62fe } },
        { "label BLS label", { 0x63fe } },
        { "label BCC label", { 0x64fe } },
        { "label BCS label", { 0x65fe } },
        { "label BNE label", { 0x66fe } },
        { "label BEQ label", { 0x67fe } },
        { "label BVC label", { 0x68fe } },
        { "label BVS label", { 0x69fe } },
        { "label BPL label", { 0x6afe } },
        { "label BMI label", { 0x6bfe } },
        { "label BGE label", { 0x6cfe } },
        { "label BLT label", { 0x6dfe } },
        { "label BGT label", { 0x6efe } },
        { "label BLE label", { 0x6ffe } },
        { "label DBT d3, label",  { 0x50cb, 0xfffe } },
        { "label DBF d3, label",  { 0x51cb, 0xfffe } },
        { "label DBHI d3, label", { 0x52cb, 0xfffe } },
        { "label DBLS d3, label", { 0x53cb, 0xfffe } },
        { "label DBCC d3, label", { 0x54cb, 0xfffe } },
        { "label DBCS d3, label", { 0x55cb, 0xfffe } },
        { "label DBNE d3, label", { 0x56cb, 0xfffe } },
        { "label DBEQ d3, label", { 0x57cb, 0xfffe } },
        { "label DBVC d3, label", { 0x58cb, 0xfffe } },
        { "label DBVS d3, label", { 0x59cb, 0xfffe } },
        { "label DBPL d3, label", { 0x5acb, 0xfffe } },
        { "label DBMI d3, label", { 0x5bcb, 0xfffe } },
        { "label DBGE d3, label", { 0x5ccb, 0xfffe } },
        { "label DBLT d3, label", { 0x5dcb, 0xfffe } },
        { "label DBGT d3, label", { 0x5ecb, 0xfffe } },
        { "label DBLE d3, label", { 0x5fcb, 0xfffe } },
        { "ST d0",  { 0x50c0 } },
        { "SF d3",  { 0x51c3 } },
        { "SHI (a0)", { 0x52d0 } },
        { "SLS 12(a1)", { 0x53e9, 0x000c } },
        { "SCC (a2)+", { 0x54da } },
        { "SCS $12.w", { 0x55f8, 0x0012 } },
        { "SNE $12345678.l", { 0x56f9, 0x1234, 0x5678 } },
        { "SEQ d3", { 0x57c3 } },
        { "SVC d3", { 0x58c3 } },
        { "SVS d3", { 0x59c3 } },
        { "SPL d3", { 0x5ac3 } },
        { "SMI d3", { 0x5bc3 } },
        { "SGE d3", { 0x5cc3 } },
        { "SLT d3", { 0x5dc3 } },
        { "SGT d3", { 0x5ec3 } },
        { "SLE d3", { 0x5fc3 } },
        { "ADDX.B d0, d1", { 0xd300 } },
        { "ADDX.w d2, d3", { 0xd742 } },
        { "ADDX.l -(a2), -(a3)", { 0xd78a } },
        { "SUBX.B -(a6), -(a7)", { 0x9f0e } },
        { "SUBX.w d2, d3", { 0x9742 } },
        { "SUBX.l -(a2), -(a3)", { 0x978a } },
        { "DIVU (a0), d0" , { 0x80d0 } },
        { "DIVU d2, d7" , { 0x8ec2 } },
        { "DIVS 12(a3), d3" , { 0x87eb, 0x000c } },
        { "DIVS #40, d4" , { 0x89fc, 0x0028 } },
        { "MULU (a0), d0" , { 0xc0d0 } },
        { "MULU d5, d4" , { 0xc8c5 } },
        { "MULS -(a7), d3" , { 0xc7e7 } },
        { "MULS #40, d4" , { 0xc9fc, 0x0028 } },
        { "TRAP #0", { 0x4e40 } },
        { "TRAP #7", { 0x4e47 } },
        { "TRAP #15", { 0x4e4f } },
        { "TRAPV", { 0x4e76 } },
        { "RTR", { 0x4e77 } },
        { "LINK A5, #$1234", { 0x4e55, 0x1234 } },
        { "UNLK A2", { 0x4e5a } },
        { "ABCD d3, d4", { 0xc903 } },
        { "ABCD.B -(a0), -(a2)", {  0xc508 } },
        { "SBCD d5, d1", { 0x8305 } },
        { "SBCD.B -(a5), -(a1)", { 0x830d } },
        { "CHK d0, d2", { 0x4580 } },
        { "CHK $34(a2,d3.l), d2", { 0x45b2, 0x3834 } },
        { "CMPM.B (a1)+, (a6)+", { 0xbd09 } },
        { "CMPM.W (a2)+, (a3)+", { 0xb74a } },
        { "CMPM.L (a7)+, (a0)+", { 0xb18f } },
        { "MOVEM.W d0-d7/a0-a6, -(a7)", { 0x48a7, 0xfffe } },
        { "MOVEM d0/d3/d2/a0-a2/a3-a4, (a1)", { 0x4891, 0x1f0d } },
        { "MOVEM.L d0-d1/a2-a3, $1234.l", { 0x48f9, 0x0c03, 0x0000, 0x1234 } },
        { "MOVEM.W (sp)+, d0-d2/a2/a3", { 0x4c9f, 0x0c07 } },
        { "MOVEM.L 12(a0), d0-a7", { 0x4ce8, 0xffff, 0x000c } },
        { "l MOVEM.L l(pc,d0.w), d0-a7", { 0x4cfb, 0xffff, 0x00fc } },
        { "MOVEM d0, 2(a1,d0.w)", { 0x48b1, 0x0001, 0x0002 } }, 
        { "MOVEM.L a7, 2(a3)", { 0x48eb, 0x8000, 0x0002 } },
        { "ASL.B d0, d1", { 0xe121 } },
        { "ASL.W #4, d1", { 0xe941 } },
        { "ASL 2(a0)", { 0xe1e8, 0x0002 } },
        { "ASL.L #1, d2", { 0xe382 } },
        { "ASR.B d0, d2", { 0xe022 } },
        { "ASR #8, d7", { 0xe047 } },
        { "ASR (a0)+", { 0xe0d8 } },
        { "ASR.L d4, d6", { 0xe8a6 } },
        { "LSL.B d0, d1", { 0xe129 } },
        { "LSL.W #4, d1", { 0xe949 } },
        { "LSL 2(a0)", { 0xe3e8, 0x0002 } },
        { "LSL.L #1, d2", { 0xe38a } },
        { "LSR.B d0, d2", { 0xe02a } },
        { "LSR #8, d7", { 0xe04f } },
        { "LSR (a0)+", { 0xe2d8 } },
        { "LSR.L d4, d6", { 0xe8ae } },
        { "ROL.B d0, d1", { 0xe139 } },
        { "ROL.W #4, d1", { 0xe959 } },
        { "ROL -100.w", { 0xe7f8, 0xff9c } },
        { "ROL.L #1, d2", { 0xe39a } },
        { "ROR.B d0, d2", { 0xe03a } },
        { "ROR #8, d7", { 0xe05f } },
        { "ROR (a0)+", { 0xe6d8 } },
        { "ROR.L d4, d6", { 0xe8be } },
        { "ROXL.B d0, d1", { 0xe131 } },
        { "ROXL.W #4, d1", { 0xe951 } },
        { "ROXL 2(a0)", { 0xe5e8, 0x0002 } },
        { "ROXL.L #1, d2", { 0xe392 } },
        { "ROXR.B d0, d2", { 0xe032 } },
        { "ROXR #8, d7", { 0xe057 } },
        { "ROXR $12345678.l", { 0xe4f9, 0x1234, 0x5678 } },
        { "ROXR.L d4, d6", { 0xe8b6 } },
        { "TAS d2", { 0x4ac2 } },
        { "TAS (a2)", { 0x4ad2 } },
        { "TAS (a1)+", { 0x4ad9 } },
        { "TAS -(a3)", { 0x4ae3 } },
        { "TAS 12(a2)", { 0x4aea, 0x000c } },
        { "TAS 2(a3,d0.w)", { 0x4af3, 0x0002 } },
        { "TAS $1234.w", { 0x4af8, 0x1234 } },
        { "TAS $12345678.l", { 0x4af9, 0x1234, 0x5678 } },
        { "MOVEP.W d2, $1234(a3)", { 0x058b, 0x1234 } },
        { "MOVEP.W $1234(a7), d3", { 0x070f, 0x1234 } },
        { "MOVEP.L d4, $1234(a7)", { 0x09cf, 0x1234 } },
        { "MOVEP.L $1234(a2), d0", { 0x014a, 0x1234 } },

        { "DC.B $12, $34, $56\nEVEN\nDC.W $abcd\n", { 0x1234, 0x5600, 0xabcd } },
    };

    for (const auto& tc : test_cases) {
        const uint32_t start_pc = 0x1000;
        std::vector<uint16_t> code;
        try {
            auto res = assemble(start_pc, tc.text);
            if (res.size() & 1)
                throw std::runtime_error("Odd number of bytes");
            for (size_t i = 0; i < res.size(); i += 2) {
                code.push_back(get_u16(&res[i]));
            }

        } catch (const std::exception& e) {
            std::cerr << "Failed to assemble: " << e.what() << "\nCode:\n" << tc.text << "\n";
            return false;
        }

        if (code != tc.code) {
            std::cerr << "Test case failed for:\n" << tc.text << "\n\n";
            std::cerr << "Expected:\n";
            disasm_code(std::cout, start_pc, tc.code);
            std::cerr << "Got:\n";
            disasm_code(std::cout, start_pc, code);
            return false;
        }    
    }
    return true;
}

bool test_reglist_string()
{
    const struct {
        uint16_t rl;
        bool rev;
        const char* expected;
    } test_cases[] = {
        { 0, false, "(empty register list)" },
        { 1 << 0, false, "D0" },
        { 1 << 7, false, "D7" },
        { 1 << 0 | 1 << 1 | 1 << 4, false, "D0-D1/D4" },
        { 0x7fff, false, "D0-A6" },
        { 0xfffe, false, "D1-A7" },
        { 0xf00f, false, "D0-D3/A4-A7" },
        { 0x8000, false, "A7" },
        { 0, true, "(empty register list)" },
        { 1 << 0, true, "A7" },
        { 1 << 7, true, "A0" },
        { 1 << 0 | 1 << 1 | 1 << 4, true, "A3/A6-A7" },
        { 0x7fff, true, "D1-A7" },
        { 0xfffe, true, "D0-A6" },
        { 0xf00f, true, "D0-D3/A4-A7" },
        { 0x8000, true, "D0" },
    };

    for (const auto& tc : test_cases) {
        const auto res = reg_list_string(tc.rl, tc.rev);
        if (res != tc.expected) {
            std::cerr << "Test case failure for reg_list_string. List=$" << hexfmt(tc.rl) << " Reverse=" << (tc.rev ? "yes" : "no") << "\n";
            std::cerr << "Expected: \"" << tc.expected << "\"\n";
            std::cerr << "Got:      \"" << res << "\"\n";
            return false;
        }
    }
    return true;
}

int main()
{
    try {
        if (!test_reglist_string())
            return 1;

        if (!simple_asm_tests())
            return 1;
        //const uint32_t start_pc = 0x1000;
        //auto code = assemble(start_pc, "\tMOVEQ #123, d3\nlabel MOVE #$42, 32766.W\n\tMOVE.B #42, d0\nMOVE.L d7, a2\n\tBRA.W label\nRTS\n");
        //disasm_code(std::cout, start_pc, code);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
