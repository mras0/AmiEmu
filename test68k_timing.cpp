#include "test68k.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cassert>

#include "ioutil.h"
#include "cpu.h"
#include "memory.h"
#include "disasm.h"
#include "asm.h"

// From Yacht (Yet Another Cycle Hunting Table)                

namespace {

// TODO:
// ORI, ANDI, EORI to SR/CCR
// BCHG,BSET,BCLR,BTST
// MOVEP
// MOVE from SR
// MOVE to CCR, SR
// MOVEM
// TAS
// CHK

const char* const line0000_immediate = R"(
#<data>,<ea> :    |                
  .B or .W :      |                
    Dn            |  8(2/0)  0(0/0)
    (An)          | 12(2/1)  4(1/0)
    (An)+         | 12(2/1)  4(1/0)
    -(An)         | 12(2/1)  6(1/0)
    (d16,An)      | 12(2/1)  8(2/0)
    (d8,An,Xn)    | 12(2/1) 10(2/0)
    (xxx).W       | 12(2/1)  8(2/0)
    (xxx).L       | 12(2/1) 12(3/0)
  .L :            |
    Dn            | 16(3/0)  0(0/0)
    (An)          | 20(3/2)  8(2/0)
    (An)+         | 20(3/2)  8(2/0)
    -(An)         | 20(3/2) 10(2/0)
    (d16,An)      | 20(3/2) 12(3/0)
    (d8,An,Xn)    | 20(3/2) 14(3/0)
    (xxx).W       | 20(3/2) 12(3/0)
    (xxx).L       | 20(3/2) 16(4/0)
)";

const char* const cmpi_table = R"(
#<data>,<ea> :    |                
  .B or .W :      |                
    Dn            |  8(2/0)  0(0/0)
    (An)          |  8(2/0)  4(1/0)
    (An)+         |  8(2/0)  4(1/0)
    -(An)         |  8(2/0)  6(1/0)
    (d16,An)      |  8(2/0)  8(2/0)
    (d8,An,Xn)    |  8(2/0) 10(2/0)
    (xxx).W       |  8(2/0)  8(2/0)
    (xxx).L       |  8(2/0) 12(3/0)
  .L :            |                
    Dn            | 14(3/0)  0(0/0)
    (An)          | 12(3/0)  8(2/0)
    (An)+         | 12(3/0)  8(2/0)
    -(An)         | 12(3/0) 10(2/0)
    (d16,An)      | 12(3/0) 12(3/0)
    (d8,An,Xn)    | 12(3/0) 14(3/0)
    (xxx).W       | 12(3/0) 12(3/0)
    (xxx).L       | 12(3/0) 16(4/0)
)";

const char* const move_table = R"(
<ea>,Dn :         |        
  .B or .W :      |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          |  8(2/0)
    (An)+         |  8(2/0)
    -(An)         | 10(2/0)
    (d16,An)      | 12(3/0)
    (d8,An,Xn)    | 14(3/0)
    (xxx).W       | 12(3/0)
    (xxx).L       | 16(4/0)
    #<data>       |  8(2/0)
  .L :            |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          | 12(3/0)
    (An)+         | 12(3/0)
    -(An)         | 14(3/0)
    (d16,An)      | 16(4/0)
    (d8,An,Xn)    | 18(4/0)
    (xxx).W       | 16(4/0)
    (xxx).L       | 20(5/0)
    #<data>       | 12(3/0)
<ea>,(An) :       |        
  .B or .W :      |        
    Dn            |  8(1/1)
    An            |  8(1/1)
    (An)          | 12(2/1)
    (An)+         | 12(2/1)
    -(An)         | 14(2/1)
    (d16,An)      | 16(3/1)
    (d8,An,Xn)    | 18(3/1)
    (xxx).W       | 16(3/1)
    (xxx).L       | 20(4/1)
    #<data>       | 12(2/1)
  .L :            |        
    Dn            | 12(1/2)
    An            | 12(1/2)
    (An)          | 20(3/2)
    (An)+         | 20(3/2)
    -(An)         | 22(3/2)
    (d16,An)      | 24(4/2)
    (d8,An,Xn)    | 26(4/2)
    (xxx).W       | 24(4/2)
    (xxx).L       | 28(5/2)
    #<data>       | 20(3/2)
<ea>,(An)+ :      |        
  .B or .W :      |        
    Dn            |  8(1/1)
    An            |  8(1/1)
    (An)          | 12(2/1)
    (An)+         | 12(2/1)
    -(An)         | 14(2/1)
    (d16,An)      | 16(3/1)
    (d8,An,Xn)    | 18(3/1)
    (xxx).W       | 16(3/1)
    (xxx).L       | 20(4/1)
    #<data>       | 12(2/1)
  .L :            |        
    Dn            | 12(1/2)
    An            | 12(1/2)
    (An)          | 20(3/2)
    (An)+         | 20(3/2)
    -(An)         | 22(3/2)
    (d16,An)      | 24(4/2)
    (d8,An,Xn)    | 26(4/2)
    (xxx).W       | 24(4/2)
    (xxx).L       | 28(5/2)
    #<data>       | 20(3/2)
<ea>,-(An) :      |        
  .B or .W :      |        
    Dn            |  8(1/1)
    An            |  8(1/1)
    (An)          | 12(2/1)
    (An)+         | 12(2/1)
    -(An)         | 14(2/1)
    (d16,An)      | 16(3/1)
    (d8,An,Xn)    | 18(3/1)
    (xxx).W       | 16(3/1)
    (xxx).L       | 20(4/1)
    #<data>       | 12(2/1)
  .L :            |        
    Dn            | 12(1/2)
    An            | 12(1/2)
    (An)          | 20(3/2)
    (An)+         | 20(3/2)
    -(An)         | 22(3/2)
    (d16,An)      | 24(4/2)
    (d8,An,Xn)    | 26(4/2)
    (xxx).W       | 24(4/2)
    (xxx).L       | 28(5/2)
    #<data>       | 20(3/2)
<ea>,(d16,An) :   |        
  .B or .W :      |        
    Dn            | 12(2/1)
    An            | 12(2/1)
    (An)          | 16(3/1)
    (An)+         | 16(3/1)
    -(An)         | 18(3/1)
    (d16,An)      | 20(4/1)
    (d8,An,Xn)    | 22(4/1)
    (xxx).W       | 20(4/1)
    (xxx).L       | 24(5/1)
    #<data>       | 16(3/1)
  .L :            |        
    Dn            | 16(2/2)
    An            | 16(2/2)
    (An)          | 24(4/2)
    (An)+         | 24(4/2)
    -(An)         | 26(4/2)
    (d16,An)      | 28(5/2)
    (d8,An,Xn)    | 30(5/2)
    (xxx).W       | 28(5/2)
    (xxx).L       | 32(6/2)
    #<data>       | 24(4/2)
<ea>,(d8,An,Xn) : |        
  .B or .W :      |        
    Dn            | 14(2/1)
    An            | 14(2/1)
    (An)          | 18(3/1)
    (An)+         | 18(3/1)
    -(An)         | 20(3/1)
    (d16,An)      | 22(4/1)
    (d8,An,Xn)    | 24(4/1)
    (xxx).W       | 22(4/1)
    (xxx).L       | 26(5/1)
    #<data>       | 18(3/1)
  .L :            |        
    Dn            | 18(2/2)
    An            | 18(2/2)
    (An)          | 26(4/2)
    (An)+         | 26(4/2)
    -(An)         | 28(4/2)
    (d16,An)      | 30(5/2)
    (d8,An,Xn)    | 32(5/2)
    (xxx).W       | 30(5/2)
    (xxx).L       | 34(6/2)
    #<data>       | 26(4/2)
<ea>,(xxx).W :    |        
  .B or .W :      |        
    Dn            | 12(2/1)
    An            | 12(2/1)
    (An)          | 16(3/1)
    (An)+         | 16(3/1)
    -(An)         | 18(3/1)
    (d16,An)      | 20(4/1)
    (d8,An,Xn)    | 22(4/1)
    (xxx).W       | 20(4/1)
    (xxx).L       | 24(5/1)
    #<data>       | 16(3/1)
  .L :            |        
    Dn            | 16(2/2)
    An            | 16(2/2)
    (An)          | 24(4/2)
    (An)+         | 24(4/2)
    -(An)         | 26(4/2)
    (d16,An)      | 28(5/2)
    (d8,An,Xn)    | 30(5/2)
    (xxx).W       | 28(5/2)
    (xxx).L       | 32(6/2)
    #<data>       | 24(4/2)
<ea>,(xxx).L :    |        
  .B or .W :      |        
    Dn            | 16(3/1)
    An            | 16(3/1)
    (An)          | 20(4/1)
    (An)+         | 20(4/1)
    -(An)         | 22(4/1)
    (d16,An)      | 24(5/1)
    (d8,An,Xn)    | 26(5/1)
    (xxx).W       | 24(5/1)
    (xxx).L       | 28(6/1)
    #<data>       | 20(4/1)
  .L :            |        
    Dn            | 20(3/2)
    An            | 20(3/2)
    (An)          | 28(5/2)
    (An)+         | 28(5/2)
    -(An)         | 30(5/2)
    (d16,An)      | 32(6/2)
    (d8,An,Xn)    | 34(6/2)
    (xxx).W       | 32(6/2)
    (xxx).L       | 36(7/2)
    #<data>       | 28(5/2)
<ea>,An :         |        
  .W :            |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          |  8(2/0)
    (An)+         |  8(2/0)
    -(An)         | 10(2/0)
    (d16,An)      | 12(3/0)
    (d8,An,Xn)    | 14(3/0)
    (xxx).W       | 12(3/0)
    (xxx).L       | 16(4/0)
    #<data>       |  8(2/0)
  .L :            |        
    Dn            |  4(1/0)
    An            |  4(1/0)
    (An)          | 12(3/0)
    (An)+         | 12(3/0)
    -(An)         | 14(3/0)
    (d16,An)      | 16(4/0)
    (d8,An,Xn)    | 18(4/0)
    (xxx).W       | 16(4/0)
    (xxx).L       | 20(5/0)
    #<data>       | 12(3/0)
)";

const char* const line1111_rmw_table = R"(
<ea> :            |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
  .L :            |                
    Dn            |  6(1/0)  0(0/0)
    (An)          | 12(1/2)  8(2/0)
    (An)+         | 12(1/2)  8(2/0)
    -(An)         | 12(1/2) 10(2/0)
    (d16,An)      | 12(1/2) 12(3/0)
    (d8,An,Xn)    | 12(1/2) 14(3/0)
    (xxx).W       | 12(1/2) 12(3/0)
    (xxx).L       | 12(1/2) 16(4/0)
)";

const char* const nbcd_table = R"(
<ea> :            |                
  .B :            |                
    Dn            |  6(1/0)  0(0/0)
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
)";

const char* const pea_table = R"(
<ea> :            |        
  .L :            |        
    (An)          | 12(1/2)
    (d16,An)      | 16(2/2)
    (d8,An,Xn)    | 20(2/2)
    (xxx).W       | 16(2/2)
    (xxx).L       | 20(3/2)
)";

const char* const swap_table = R"(
Dn :              |                 
  .W :            |  4(1/0)
)";

const char* const ext_table = R"(
Dn :              |                 
  .W :            |  4(1/0)         
  .L :            |  4(1/0)         
)";

const char* const tst_table = R"(
<ea> :            |                 
  .B or .W :      |                 
    Dn            |  4(1/0)  0(0/0) 
    (An)          |  4(1/0)  4(1/0) 
    (An)+         |  4(1/0)  4(1/0) 
    -(An)         |  4(1/0)  6(1/0) 
    (d16,An)      |  4(1/0)  8(2/0) 
    (d8,An,Xn)    |  4(1/0) 10(2/0) 
    (xxx).W       |  4(1/0)  8(2/0) 
    (xxx).L       |  4(1/0) 12(3/0) 
  .L :            |                 
    Dn            |  4(1/0)  0(0/0) 
    (An)          |  4(1/0)  8(2/0) 
    (An)+         |  4(1/0)  8(2/0) 
    -(An)         |  4(1/0) 10(2/0) 
    (d16,An)      |  4(1/0) 12(3/0) 
    (d8,An,Xn)    |  4(1/0) 14(3/0) 
    (xxx).W       |  4(1/0) 12(3/0) 
    (xxx).L       |  4(1/0) 16(4/0) 
)";

const char* const add_sub_table = R"(
<ea>,Dn :         |                
  .B or .W :      |                
    Dn            |  4(1/0)  0(0/0)
    An            |  4(1/0)  0(0/0)
    (An)          |  4(1/0)  4(1/0)
    (An)+         |  4(1/0)  4(1/0)
    -(An)         |  4(1/0)  6(1/0)
    (d16,An)      |  4(1/0)  8(2/0)
    (d8,An,Xn)    |  4(1/0) 10(2/0)
    (xxx).W       |  4(1/0)  8(2/0)
    (xxx).L       |  4(1/0) 12(3/0)
    #<data>       |  4(1/0)  4(1/0)
  .L :            |                
    Dn            |  8(1/0)  0(0/0)
    An            |  8(1/0)  0(0/0)
    (An)          |  6(1/0)  8(2/0)
    (An)+         |  6(1/0)  8(2/0)
    -(An)         |  6(1/0) 10(2/0)
    (d16,An)      |  6(1/0) 12(3/0)
    (d8,An,Xn)    |  6(1/0) 14(3/0)
    (xxx).W       |  6(1/0) 12(3/0)
    (xxx).L       |  6(1/0) 16(4/0)
    #<data>       |  8(1/0)  8(2/0)
Dn,<ea> :         |                
  .B or .W :      |                
    (An)          |  8(1/1)  4(1/0)
    (An)+         |  8(1/1)  4(1/0)
    -(An)         |  8(1/1)  6(1/0)
    (d16,An)      |  8(1/1)  8(2/0)
    (d8,An,Xn)    |  8(1/1) 10(2/0)
    (xxx).W       |  8(1/1)  8(2/0)
    (xxx).L       |  8(1/1) 12(3/0)
  .L :            |                
    (An)          | 12(1/2)  8(2/0)
    (An)+         | 12(1/2)  8(2/0)
    -(An)         | 12(1/2) 10(2/0)
    (d16,An)      | 12(1/2) 12(3/0)
    (d8,An,Xn)    | 12(1/2) 14(3/0)
    (xxx).W       | 12(1/2) 12(3/0)
    (xxx).L       | 12(1/2) 16(4/0)
)";

const struct {
    const char* table;
    std::vector<std::string> instructions;
} timing_tests[] = {
    { line0000_immediate , { "EOR", "OR", "AND", "SUB", "ADD" } },
    { cmpi_table         , { "CMP" } },
    { move_table         , { "MOVE" } },
    { line1111_rmw_table , { "CLR", "NEGX", "NEG", "NOT" } },
    { nbcd_table         , { "NBCD" } },
    { pea_table          , { "PEA" } },
    { swap_table         , { "SWAP" } },
    { ext_table          , { "EXT" } },
    { tst_table          , { "TST" } },
    { add_sub_table      , { "ADD", "SUB" } },
};

std::pair<unsigned, unsigned> interpret_cycle_text(const std::string& s)
{
    unsigned ic, ir, iw, ec, er, ew;
    char sep[6];
    std::istringstream iss { s };
    if (iss >> ic >> sep[0] >> ir >> sep[1] >> iw >> sep[2]) {
        assert(sep[0] == '(');
        assert(sep[1] == '/');
        assert(sep[2] == ')');
        if (iss >> ec >> sep[3] >> er >> sep[4] >> ew >> sep[5]) {
            assert(sep[3] == '(');
            assert(sep[4] == '/');
            assert(sep[5] == ')');
            return { ic + ec, ir + iw + er + ew };
        }
        return { ic, ir + iw }; // For move
    }
    throw std::runtime_error { "Unsupported cycle specification: " + s };
}

std::string replace(const std::string& templ, const std::string& needle, const std::string& repl)
{
    auto res = templ;
    for (;;) {
        auto pos = res.find(needle);
        if (pos == std::string::npos)
            return res;
        res = res.substr(0, pos) + repl + res.substr(pos + needle.length());
    }
}

struct tester {
    static constexpr uint32_t code_pos = 0x1000;
    static constexpr uint32_t code_size = 0x2000;
    memory_handler mem { code_pos + code_size };
    std::vector<uint16_t> code_words;

    bool tests_from_table(const char* table, const std::vector<std::string>& insts)
    {
        std::istringstream iss { table };
        std::string code_template;
        std::vector<std::string> sizes;

        auto& ram = mem.ram();

        bool all_ok = true;

        for (std::string line; std::getline(iss, line);) {
            if (line.empty())
                continue;
            assert(line.length() > 10);
            const auto divpos = line.find_first_of('|');
            assert(divpos != std::string::npos);
            //std::cout << line << "\n";
            if (line[0] != ' ') {
                code_template = trim(line.substr(0, line.find_first_of(':')));
                continue;
            }
            if (line[2] == '.') {
                if (line.compare(2, 10, ".B or .W :") == 0)
                    sizes = { ".B", ".W" };
                else if (line.compare(2, 4, ".B :") == 0)
                    sizes = { ".B" };
                else if (line.compare(2, 4, ".W :") == 0)
                    sizes = { ".W" };
                else if (line.compare(2, 4, ".L :") == 0)
                    sizes = { ".L" };
                else
                    throw std::runtime_error { "Size not recognied: " + line };
                continue;
            }
            assert(line.compare(0, 4, "    ") == 0);
            auto replacement = trim(line.substr(4, divpos - 4));
            const auto [cycles, memaccesses] = interpret_cycle_text(line.substr(divpos + 1));

            //std::cout << "\tReplacement = \"" << replacement << "\" cycles:" << cycles << "/" << memaccesses << "\n";
            for (const auto& inst : insts) {
                for (const auto& s : sizes) {
                    if (s == ".B" && replacement == "An")
                        continue;

                    auto args = replace(code_template, "<ea>", replacement);

                    args = replace(args, "-(An)", "-(A2)");
                    args = replace(args, "(xxx)", "$12");
                    args = replace(args, "(d16,An)", "$12(A0)");
                    args = replace(args, "(d8,An,Xn)", "$12(A0,D0.L)");
                    args = replace(args, "Dn", "D1");
                    args = replace(args, "An", "A3");
                    args = replace(args, "Dx", "D2");
                    args = replace(args, "Dy", "D3");
                    args = replace(args, "<data>", "$34");


                    //std::cout << "\tTest " << i << s << "\t" << args << "!\n";

                    const auto stmt = inst + s + "\t" + args;

                    try {
                        code_words = assemble(code_pos, stmt.c_str());
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << " while assembling:\n" << stmt << "\n";
                        return false;
                    }

                    memset(&ram[0], 0, ram.size());
                    for (size_t i = 0; i < std::size(code_words); ++i)
                        put_u16(&ram[code_pos + i * 2], code_words[i]);

                    cpu_state input_state {};
                    input_state.sr = 0x2000;
                    input_state.pc = code_pos;
                    input_state.a[2] = 8;
                    input_state.usp = 0x400;
                    input_state.ssp = 0x400;

                    m68000 cpu { mem, input_state };

                    const auto step_res = cpu.step();
                    if (step_res.clock_cycles != cycles || step_res.mem_accesses != memaccesses) {
                        std::cerr << "Test failed for:\n" << stmt << "\n";
                        std::cerr << "Expected clock cycles:    " << (int)cycles << "\tActual: " << (int)step_res.clock_cycles << "\n";
                        std::cerr << "Expected memory accesses: " << (int)memaccesses << "\tActual: " << (int)step_res.mem_accesses << "\n";
                        //return false;
                        all_ok = false;
                    }

                }
            }
        }
        return all_ok;
    }
};

} // unnamed namespace

bool test_timing()
{
    tester t;

    bool all_ok = true;
    for (const auto& tc : timing_tests) {
        const bool ok = t.tests_from_table(tc.table, tc.instructions);
        all_ok &= ok;
    }

    return all_ok;
}
