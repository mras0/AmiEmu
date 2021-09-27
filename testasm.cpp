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
    };  

    for (const auto& tc : test_cases) {
        const uint32_t start_pc = 0x1000;
        std::vector<uint16_t> code;
        try {
            code = assemble(start_pc, tc.text);
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

int main()
{
    try {
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
