#include <iostream>
#include <stdint.h>
#include <stdexcept>
#include <cassert>
#include <fstream>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <signal.h>
#include <cstring>
#include <iomanip>
#include <variant>
#include <map>

#include "ioutil.h"
#include "instruction.h"
#include "disasm.h"
#include "memory.h"
#include "cia.h"
#include "custom.h"
#include "cpu.h"
#include "disk_drive.h"
#include "gui.h"
#include "debug.h"
#include "wavedev.h"
#include "asm.h"
#include "autoconf.h"
#include "harddisk.h"
#include "rtc.h"
#include "state_file.h"
#include "disk_file.h"
#include "debug_board.h"

namespace {

volatile bool ctrl_c;

cpu_state debugger_cpu_state;
uint32_t debugger_ans; // Last result
uint32_t debugger_reg[100];

struct debugger_function {
    std::string name;
    std::variant<std::function<uint32_t()>, std::function<uint32_t(uint32_t)>, std::function<uint32_t(uint32_t, uint32_t)>> func;
};
std::vector<debugger_function> debugger_functions;

void ctrl_c_handler(int)
{
    ctrl_c = true;
}

std::vector<std::string> split_line(const std::string& line)
{
    std::vector<std::string> args;
    const size_t l = line.length();
    size_t i = 0;
    while (i < l && isspace(line[i]))
        ++i;
    if (i == l)
        return {};

    while (i < l) {
        if (isspace(line[i])) {
            ++i;
            continue;
        }
        if (line[i] == '"') {
            std::string s;
            s.push_back(line[i]); // Keep quote
            ++i;
            while (i < l) {
                if (line[i] == '\\') {
                    ++i;
                    if (i >= l) {
                        std::cerr << "Unterminated escape sequence\n";
                        return {};
                    }
                    switch (line[i]) {
                    case '\"':
                        s.push_back('\"');
                        break;
                    case '\'':
                        s.push_back('\'');
                        break;
                    case '\\':
                        s.push_back('\\');
                        break;
                    case 't':
                        s.push_back('\t');
                        break;
                    case 'r':
                        s.push_back('\r');
                        break;
                    case 'n':
                        s.push_back('\n');
                        break;
                    case 'x':
                        if (i + 3 >= l) {
                            std::cerr << "Invalid escape sequence: \\" << line.substr(i) << "\n";
                            return {};
                        }
                        ++i;
                        if (auto [ok, val] = from_hex(line.substr(i, 2)); ok) {
                            s.push_back(static_cast<char>(val & 0xff));
                            ++i; // One more char consumed below
                        } else {
                            std::cerr << "Invalid escape sequence: \\x" << line.substr(i, 2) << "\n";
                            return {};
                        }
                        break;
                    default:
                        std::cerr << "Unknown escape character: \\" << line[i] << "\n";
                        return {};
                    }
                    ++i;
                    continue;
                }

                s.push_back(line[i]); // Keep quote
                if (line[i] == '"')
                    break;
                ++i;
            }
            if (i >= l) {
                std::cerr << "Unterminated quoted string\n";
                return {};
            }
            args.push_back(s);
            ++i;
            continue;
        }
        const auto start = i;
        ++i;
        while (i < l && !isspace(line[i]))
            ++i;
        args.push_back(line.substr(start, i - start));
    }

    return args;
}

std::string unquote(const std::string& s)
{
    if (s.size() < 2 || s[0] != '\"')
        return s;
    assert(s.back() == '\"');
    return s.substr(1, s.size() - 2);
}

std::optional<std::vector<uint8_t>> unhex_bytes(const std::string& s)
{
    if (s.empty() || s.size() % 2)
        return {};
    std::vector<uint8_t> res(s.size() / 2);
    for (size_t i = 0; i < res.size(); ++i) {
        const auto d1 = digitval(s[i * 2 + 0]);
        const auto d2 = digitval(s[i * 2 + 1]);
        if ((d1 | d2) > 15)
            return {};
        res[i] = d1 << 4 | d2;
    }
    return res;
}

std::pair<uint32_t, const void*> get_register_address(const char* s, const cpu_state& st)
{
    std::string reg { s };
    if (reg.empty()) {
        std::cerr << "Unknown register \"" << reg << "\"\n";
        return { 0, nullptr };
    }
    if (reg[0] >= '0' && reg[0] <= '9') {
        // Internal register
        const auto [ok, idx] = number_from_string(reg.c_str(), 10);
        if (ok) {
            if (idx < std::size(debugger_reg))
                return { 4, &debugger_reg[idx] };
            std::cerr << "Out of range for internal register \"" << reg << "\"\n";
        } else {
            std::cerr << "Invalid register \"" << reg << "\"\n";
        }
        return { 0, nullptr };
    }
    for (auto& c : reg) {
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    }

    if (reg == "SSP")
        return { 4, &st.ssp };
    else if (reg == "USP")
        return { 4, &st.usp };
    else if (reg == "PC")
        return { 4, &st.pc };
    else if (reg == "SR")
        return { 2, &st.sr };
    else if (reg == "ANS")
        return { 4, &debugger_ans };

    if (reg.size() == 2 && reg[1] >= '0' && reg[1] <= '7') {
        const auto idx = reg[1] - '0';
        if (reg[0] == 'D')
            return { 4, &st.d[idx] };
        else if (reg[0] == 'A')
            return { 4, &const_cast<cpu_state&>(st).A(idx) };
    }

    std::cerr << "Unknown register \"" << reg << "\"\n";
    return { 0, nullptr };
}

std::pair<bool, uint32_t> get_register(const char* s)
{
    const auto [size, ptr] = get_register_address(s, debugger_cpu_state);
    if (!size)
        return { false, 0 };
    assert(ptr);
    if (size == 2)
        return { true, *reinterpret_cast<const uint16_t*>(ptr) };
    assert(size == 4);
    return { true, *reinterpret_cast<const uint32_t*>(ptr) };
}

std::pair<bool, uint32_t> get_number(const std::string& s)
{
    if (s.empty())
        return { false, 0 };
    if (s[0] == 'r' || s[0] == 'R')
        return get_register(s.c_str() + 1);
    else if (s[0] == '!')
        return number_from_string(s.c_str() + 1, 10);
    else if (s[0] == '%')
        return number_from_string(s.c_str() + 1, 2);
    else if (s[0] == '$')
        return number_from_string(s.c_str() + 1, 16);
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return number_from_string(s.c_str() + 2, 16);
    else if (s.size() > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
        return number_from_string(s.c_str() + 2, 8);
    return number_from_string(s.c_str(), 16);
}

std::pair<bool, uint32_t> get_simple_expr(const std::string& arg)
{
    if (arg.empty())
        return { false, 0 };

    constexpr unsigned char op_lsh = 0x80;
    constexpr unsigned char op_rsh = 0x81;
    constexpr unsigned char op_leq = 0x82;
    constexpr unsigned char op_geq = 0x83;
    constexpr unsigned char op_eq  = 0x84;
    constexpr unsigned char op_neq = 0x85;
    constexpr unsigned char op_func_base = 0x90;

    assert(debugger_functions.size() < 0x100 - op_func_base);

    auto prec = [](unsigned char c) {
        switch (c) {
        case '|':
            return 94;
        case '^':
            return 95;
        case '&':
            return 96;
        case op_eq:
        case op_neq:
            return 97;
        case '<':
        case '>':
        case op_leq:
        case op_geq:
            return 98;
        case op_lsh:
        case op_rsh:
            return 99;
        case '+':
        case '-':
            return 100;
        case '*':
        case '/':
        case '%':
            return 200;
        default:
            return 0;
        }
    };
    auto doop = [](unsigned char c, uint32_t a, uint32_t b) -> uint32_t {
        switch (c) {
        case '+':
            return a + b;
        case '-':
            return a - b;
        case '*':
            return a * b;
        case '/':
            return b ? a / b : 0xffffffff;
        case '%':
            return b ? a % b : 0xffffffff;
        case '|':
            return a | b;
        case '^':
            return a ^ b;
        case '&':
            return a & b;
        case op_lsh:
            return a << b;
        case op_rsh:
            return a >> b;
        case '<':
            return a < b;
        case '>':
            return a > b;
        case op_leq:
            return a <= b;
        case op_geq:
            return a >= b;
        case op_eq:
            return a == b;
        case op_neq:
            return a != b;
        }
        assert(false);
        return 0;
    };

    enum class elem_type { number, op, func };
    struct stack_elem {
        elem_type type;
        uint32_t val;
    };
    std::vector<stack_elem> s;
    std::vector<unsigned char> opstack;

    #if 0
    auto print_state = [&]() {
        for (const auto& se : s) {
            if (se.isop)
                std::cout << static_cast<char>(se.val);
            else
                std::cout << "$" << hexfmt(se.val);
            std::cout << " ";
        }
        std::cout << "\t";
        for (const auto op : opstack)
            std::cout << op << " ";
        std::cout << "\n";
    };
    #endif

    // Extremely simple shunting yard expression parser
    for (size_t i = 0, size = arg.size(); i < size;) {
        uint8_t c = arg[i];
        if (c == ',' && i + 1 < size) {
            // Ignore
            ++i;
            continue;
        }
        if ((c == '<' || c == '>') && i + 2 < size && (arg[i + 1] == c || arg[i + 1] == '=')) {
            ++i;
            if (arg[i] == c)
                c = c == '<' ? op_lsh : op_rsh;
            else {
                assert(arg[i] == '=');
                c = c == '<' ? op_leq : op_geq;
            }
        } else if (c == '=' && i + 2 < size && arg[i + 1] == '=') {
            ++i;
            c = op_eq;
        } else if (c == '!' && i + 2 < size && arg[i + 1] == '=') {
            ++i;
            c = op_neq;
        }
        if (auto pre = prec(c); pre) {
            while (!opstack.empty() && prec(c) <= prec(opstack.back())) {
                s.push_back({ elem_type::op, static_cast<uint32_t>(opstack.back()) });
                opstack.pop_back();
            }
            opstack.push_back(c);
            ++i;
        } else if (c == '(') {
            opstack.push_back(c);
            ++i;
        } else if (c == ')') {
            ++i;
            for (;;) {
                if (opstack.empty())
                    return { false, 0 };
                if (opstack.back() == '(') {
                    opstack.pop_back();
                    if (!opstack.empty() && opstack.back() >= op_func_base) { // Function at the top of the stack
                        s.push_back({ elem_type::func, static_cast<uint32_t>(opstack.back() - op_func_base) });
                        opstack.pop_back();
                    }
                    break;
                }
                s.push_back({ elem_type::op, static_cast<uint32_t>(opstack.back()) });
                opstack.pop_back();
            }
        } else {
            size_t j = i + 1;
            for (; j < size && !strchr("(),=*/%+-|<>&$^!", arg[j]); ++j)
                ;
            const auto tok = arg.substr(i, j - i);
            i = j;
            uint8_t func_index = 0xFF;
            for (size_t cnt = 0; cnt < debugger_functions.size(); ++cnt) {
                if (tok == debugger_functions[cnt].name) {
                    func_index = static_cast<uint8_t>(cnt);
                    break;
                }
            }
            if (func_index < 0x80) {
                opstack.push_back(op_func_base + func_index);
            } else {
                const auto [valid, num] = get_number(tok);
                if (!valid)
                    return { false, 0 };
                s.push_back({ elem_type::number, num });
            }
        }
    }
    while (!opstack.empty()) {
        if (opstack.back() == '(')
            return { false, 0 };
        s.push_back({ elem_type::op, static_cast<uint32_t>(opstack.back()) });
        opstack.pop_back();
    }

    std::vector<uint32_t> vals;
    for (auto& se : s) {
        if (se.type == elem_type::op) {
            if (vals.size() < 2)
                return { false, 0 };
            auto b = vals.back();
            vals.pop_back();
            auto a = vals.back();
            vals.pop_back();
            assert(se.val < 0x100);
            vals.push_back(doop(static_cast<unsigned char>(se.val), a, b));
        } else if (se.type == elem_type::func) {
            assert(se.val < debugger_functions.size());
            const auto& func = debugger_functions[se.val];
            if (vals.size() < func.func.index())
                return { false, 0 };
            std::vector<uint32_t> args;
            for (size_t i = 0; i < func.func.index(); ++i) {
                args.push_back(vals.back());
                vals.pop_back();
            }

            uint32_t res = 0;
            switch (func.func.index()) {
            case 0:
                res = std::get<0>(func.func)();
                break;
            case 1:
                res = std::get<1>(func.func)(args[0]);
                break;
            case 2:
                res = std::get<2>(func.func)(args[0], args[1]);
                break;
            default:
                assert(false);
            }
            vals.push_back(res);
        } else {
            assert(se.type == elem_type::number);
            vals.push_back(se.val);
        }
    }

    if (vals.size() != 1)
        return { false, 0 };
    return { true, vals[0] };
}

std::tuple<bool, uint32_t, uint32_t> get_addr_and_lines(const std::vector<std::string>& args, uint32_t def_addr, uint32_t def_lines)
{
    uint32_t addr = def_addr, lines = def_lines;

    if (args.size() > 1) {
        if (args.size() > 1) {
            auto fh = get_simple_expr(args[1]);
            if (!fh.first) {
                std::cout << "Invalid address \"" << args[1] << "\"\n";
                return { false, def_addr, def_lines };
            }
            addr = fh.second;
            if (args.size() > 2) {
                if (fh = get_simple_expr(args[2]); fh.first)
                    lines = fh.second;
                else {
                    std::cout << "Invalid lines \"" << args[2] << "\"\n";
                    return { false, def_addr, def_lines };
                }
            }
        }
    }

    return { true, addr, lines };
}

uint32_t disasm_stmts(memory_handler& mem, uint32_t start, uint32_t count)
{
    if (start & 1) {
        std::cout << "PC at odd address $" << hexfmt(start) << "\n";
        return 0;
    }
    uint32_t addr = start;
    while (count--) {
        uint16_t iw[max_instruction_words];
        const auto pc = addr;
        iw[0] = mem.read_u16(addr);
        addr += 2;
        for (uint16_t i = 1; i < instructions[iw[0]].ilen; ++i) {
            iw[i] = mem.read_u16(addr);
            addr += 2;
        }
        disasm(std::cout, pc, iw, max_instruction_words);
        std::cout << "\n";
    }
    return addr - start;
}

uint32_t copper_disasm(memory_handler& mem, uint32_t start, uint32_t count)
{
    if (start & 1) {
        std::cout << "Odd address $" << hexfmt(start) << "\n";
        return 0;
    }

    const auto indent = std::string(24, ' ');
    uint32_t addr = start;
    while (count--) {
        const uint16_t i1 = mem.read_u16(addr);
        const uint16_t i2 = mem.read_u16(addr+2);
        std::cout << hexfmt(addr) << ": " << hexfmt(i1) << " " << hexfmt(i2) << "\t;  ";
        if (i1 & 1) { // wait or skip
            const auto vp = (i1 >> 8) & 0xff;
            const auto hp = i1 & 0xfe;
            const auto ve = 0x80 | ((i2 >> 8) & 0x7f);
            const auto he = i2 & 0xfe;
            //} else if ((s_.vpos & ve) > (vp & ve) || ((s_.vpos & ve) == (vp & ve) && ((s_.hpos >> 1) & he) >= (hp & he))) {
            std::cout << (i2 & 1 ? "Skip if" : "Wait for") << " vpos >= $" << hexfmt(vp & ve, 2) << " and hpos >= $" << hexfmt(hp & he, 2) << "\n";
            std::cout << indent << ";  VP " << hexfmt(vp, 2) << ", VE " << hexfmt(ve & 0x7f, 2) << "; HP " << hexfmt(hp, 2) << ", HE " << hexfmt(he, 2) << "; BFD " << !!(i2 & 0x8000) << "\n";
        } else {
            std::cout << custom_regname(i1) << " := $" << hexfmt(i2) << "\n";
        }

        addr += 4;

        if (i1 == 0xffff && i2 == 0xfffe) {
            std::cout << indent << ";  End of copper list\n";
            break;
        }

    }
    return addr - start;
}

void write_bmp(const std::string& filename, const uint32_t* data, uint32_t w, uint32_t h, uint32_t stride)
{
    std::ofstream bmp { filename, std::ofstream::binary };
    if (!bmp || !bmp.is_open())
        throw std::runtime_error { "Error creating " + filename };

    constexpr uint32_t bmp_file_header_size = 0x0E;
    constexpr uint32_t bmp_info_header_size = 0x28;

    const uint32_t image_bytes = w * h * 3;

    auto u16 = [&bmp](uint16_t w) {
        bmp.put(static_cast<char>(w & 0xff));
        bmp.put(static_cast<char>((w >> 8) & 0xff));
    };
    auto u32 = [&bmp](uint32_t l) {
        bmp.put(static_cast<char>(l & 0xff));
        bmp.put(static_cast<char>((l >> 8) & 0xff));
        bmp.put(static_cast<char>((l >> 16) & 0xff));
        bmp.put(static_cast<char>((l >> 24) & 0xff));
    };

    // File header
    u16('M' << 8 | 'B'); // bfType
    u32(bmp_file_header_size + bmp_info_header_size + image_bytes); // bfSize
    u32(0); // bfReserved1+bfReserved2
    u32(bmp_file_header_size + bmp_info_header_size); // bfOffBits
    assert(bmp.tellp() == bmp_file_header_size);
    u32(bmp_info_header_size); // biSize
    u32(w); // biWidth
    u32(h); // biHeight
    u16(1); // biPlanes
    u16(24); // biBitCount
    u32(0); // biCompression
    u32(image_bytes); // biSizeImage
    u32(0); // biXPelsPerMeter
    u32(0); // biYPelsPerMeter
    u32(0); // biClrUsed
    u32(0); // biClrImportant

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t pixel = data[x + (h - 1 - y) * stride];
            bmp.put(static_cast<char>(pixel & 0xff));
            bmp.put(static_cast<char>((pixel >> 8) & 0xff));
            bmp.put(static_cast<char>((pixel >> 16) & 0xff));
        }
    }
    if (!bmp)
        throw std::runtime_error { "Error writing to " + filename };
}

void mem_search(const std::vector<uint8_t>& ram, uint32_t base_address, const std::vector<uint8_t>& needle, uint32_t start_address, uint32_t end_address, unsigned& match_count)
{
    constexpr unsigned max_matches = 20;
    if (needle.size() > ram.size() || needle.empty() || match_count)
        return;
    if (!(start_address <= base_address + ram.size() && base_address <= end_address))
        return;

    for (size_t i = 0, e = ram.size() - needle.size(); i < e && base_address + i < end_address; ++i) {
        if (base_address + i >= start_address && memcmp(&ram[i], &needle[0], needle.size()) == 0) {
            const auto addr = base_address + static_cast<uint32_t>(i);
            std::cout << "Found at $" << hexfmt(addr) << "\n";
            if (!match_count)
                debugger_ans = addr; // Save first match
            if (++match_count > max_matches) {
                std::cout << "maximum number of matches reached\n";
                return;
            }
        }
    }
}

constexpr uint32_t ln_Type = 0x0008;
constexpr uint32_t ln_Name = 0x000a;
constexpr uint8_t NT_TASK = 1; // Exec task
constexpr uint8_t NT_PROCESS = 13; // AmigaDOS Process
constexpr uint32_t pr_SegList = 0x0080;
constexpr uint32_t pr_CLI = 0x00ac;
constexpr uint32_t pr_ReturnAddr = 0x00b0;
constexpr uint32_t cli_CommandName = 0x0010;
constexpr uint32_t cli_Module = 0x003c;

std::string read_string(memory_handler& mem, uint32_t ptr)
{
    std::string s;
    for (uint32_t i = 0; i < 64; ++i) {
        const uint8_t b = mem.read_u8(ptr + i);
        if (!b)
            break;
        if (b >= 0x20 && b <= 0x7f) {
            s.push_back(b);
            continue;
        }
        s += "\\x" + hexstring(b);
    }
    return s;
}

struct disable_bool {
    disable_bool(bool& val)
        : val_(val)
        , orig_val_(val)
    {
        val_ = false;
    }
    ~disable_bool()
    {
        val_ = orig_val_;
    }
    bool& val_;
    const bool orig_val_;
};

constexpr int default_disk_insertion_delay = 2 * 50;

} // unnamed namespace

struct memwatch {
    bool enabled;
    uint32_t address;
    uint8_t size;
    enum flagtype { read = 1, write = 2 } flags;
    friend std::ostream& operator<<(std::ostream& os, const memwatch& mw)
    {
        os << "address $" << hexfmt(mw.address) << " size $" << hexfmt(mw.size, 1) << " ";
        if (mw.flags & read)
            os << "R";
        if (mw.flags & write)
            os << "W";
        return os;
    }
};

struct command_line_arguments {
    std::string state_filename;
    std::string rom;
    std::string df0;
    std::string df1;
    std::vector<std::string> hds;
    std::vector<std::string> shared_folders;
    std::string debug_script;
    uint32_t chip_size;
    uint32_t slow_size;
    uint32_t fast_size;
    uint8_t cpu_scale;
    uint32_t floppy_speed;
    bool test_mode;
    bool nosound;
    bool debug;
    bool debug_board;

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, "Command line arguments", 1 };
        sf.handle(rom);
        sf.handle(df0);
        sf.handle(df1);
        sf.handle(hds);
        sf.handle(shared_folders);
        sf.handle(chip_size);
        sf.handle(slow_size);
        sf.handle(fast_size);
        sf.handle(cpu_scale);
        sf.handle(debug_board);
    }
};

void usage(const std::string& msg)
{
    std::cerr << "Command line arguments:\n"
        "[-rom rom-file]\n"
        "[-df0/-df1 disk/exe]"
        "[-hd file]\n"
        "[-share path]\n"
        "[-chip size]\n"
        "[-slow size]\n"
        "[-fast size]\n"
        "[-nosound]\n"
        "[-floppyspeed X]\n"
        "[-cpuscale X]\n"
        "[-state statefile]\n"
        "[-debug]\n"
        "[-debugscript file]\n"
        "[-testmode]\n"
        "[-debugboard]\n"
        "[-help]\n";
    throw std::runtime_error { msg };
}

command_line_arguments parse_command_line_arguments(int argc, char* argv[])
{
    command_line_arguments args {};
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            auto get_string_arg = [&](const std::string& name, std::string& arg) {
                if (name.compare(&argv[i][1]) != 0)
                    return false;
                if (++i == argc)
                    usage("Missing argument for " + name);
                if (!arg.empty())
                    usage(name + " specified multiple times");
                arg = argv[i];
                return true;
            };
            auto get_size_arg = [&](const std::string& name, uint32_t& arg, uint32_t max_size) {
                std::string s = arg ? " " : "";// Non-empty if already specified so get_string_arg will warn
                if (!get_string_arg(name, s))
                    return false;

                char* ep = nullptr;
                uint32_t mult = 1;
                const auto num = strtoul(s.c_str(), &ep, 10);
                if (*ep == 'k' || *ep == 'K') {
                    ++ep;
                    mult = 1<<10;
                } else if (*ep == 'm' || *ep == 'M') {
                    ++ep;
                    mult = 1 << 20;
                }
                if (*ep)
                    usage("Invalid number for " + name);
                if (!num || num >= 16 << 20 || static_cast<uint64_t>(num) * mult > max_size || ((num * mult) & ((256 << 10) - 1)))
                    usage("Invalid or out of range number for mult");
                arg = num * mult;
                return true;
            };
            auto get_number_arg = [&](const std::string& name, auto& val, auto maxsize) {
                using T = std::decay_t<decltype(val)>;
                std::string s = args.cpu_scale ? " " : ""; // Non-empty if already specified so get_string_arg will warn
                if (!get_string_arg(name, s))
                    return false;
                char* ep = nullptr;
                const auto num = strtoul(s.c_str(), &ep, 10);
                if (*ep || num < 1 || num > static_cast<T>(maxsize))
                    usage("Argument invalid or out of range for " + name + " must be in range [1; " + std::to_string(maxsize) + "]");
                val = static_cast<T>(num);
                return true;
            };

            if (get_string_arg("df0", args.df0))
                continue;
            else if (get_string_arg("df1", args.df1))
                continue;
            else if (std::string s; get_string_arg("hd", s)) {
                args.hds.push_back(s);
                continue;
            } else if (std::string share; get_string_arg("share", share)) {
                args.shared_folders.push_back(share);
                continue;
            } else if (get_string_arg("rom", args.rom))
                continue;
            else if (get_size_arg("chip", args.chip_size, max_chip_size))
                continue;
            else if (get_size_arg("slow", args.slow_size, max_slow_size))
                continue;
            else if (get_size_arg("fast", args.fast_size, max_fast_size))
                continue;
            else if (get_number_arg("floppyspeed", args.floppy_speed, 0x4000)) // Just some limit
                continue;
            else if (get_string_arg("state", args.state_filename))
                continue;
            else if (get_number_arg("cpuscale", args.cpu_scale, 255))
                continue;
            else if (!std::strcmp(&argv[i][1], "help"))
                usage("");
            else if (!std::strcmp(&argv[i][1], "testmode")) {
                args.test_mode = true;
                args.nosound = true;
                continue;
            } else if (!std::strcmp(&argv[i][1], "nosound")) {
                args.nosound = true;
                continue;
            } else if (!std::strcmp(&argv[i][1], "debug")) {
                args.debug = true;
                continue;
            } else if (!std::strcmp(&argv[i][1], "debugboard")) {
                args.debug_board = true;
                continue;
            } else if (get_string_arg("debugscript", args.debug_script)) {
                args.debug = true; // debugscript implies -debug
                continue;
            }
        }
        usage("Unrecognized command line parameter: " + std::string { argv[i] });
    }
    if (args.rom.empty()) {
        args.rom = "rom.bin";
    }
    if (args.chip_size == 0 && args.slow_size == 0 && args.fast_size == 0) {
        args.chip_size = 512 << 10;
        args.slow_size = 512 << 10;
    } else if (args.chip_size == 0) {
        args.chip_size = 512 << 10;
    }
    if (args.chip_size < 256 << 10) {
        usage("Invalid configuration (chip RAM too small)");
    }
    if (!args.cpu_scale)
        args.cpu_scale = 1;
    if (!args.floppy_speed)
        args.floppy_speed = 16;
    return args;
}

std::vector<uint8_t> patch(std::vector<uint8_t>&& rom)
{
    if (!strcmp(reinterpret_cast<const char*>(&rom[0x18]), "exec 33.192 (8 Oct 1986)\r\n")) {
        std::cerr << "Patching KS1.2 (33.192) expansion.library/ConfigBoard\n";
        put_u16(&rom[0x4d9c], 0x426F); // CLR.W 4(A7)
        put_u16(&rom[0x4d9e], 0x0004);
        put_u16(&rom[0x4db2], 0x0000); // MOVE.L 2(A7), D0 -> MOVE.L 0(A7), D0
    }
    return rom;
}

class amiga {
public:
    explicit amiga(const command_line_arguments& cmdline_args);
    ~amiga();

    void run();
    void handle_machine_state(state_file& sf);

    memory_handler& mem_handler()
    {
        return mem;
    }

private:
    // Hardware + config
    command_line_arguments cmdline_args;
    disk_drive df0 { "DF0:" }, df1 { "DF1:" };
    disk_drive* drives[max_drives] = { &df0, &df1 };
    memory_handler mem;
    rom_area_handler rom;
    cia_handler cias { mem, rom, drives };
    custom_handler custom;
    autoconf_handler autoconf { mem };
    real_time_clock rtc { mem };
    m68000 cpu { mem };
    std::unique_ptr<ram_handler> slow_ram;
    std::unique_ptr<fastmem_handler> fast_ram;
    std::unique_ptr<harddisk> hd;
    std::unique_ptr<debug_board> dbg_board;

    // Serial data handler
    std::vector<uint8_t> serdata;
    char escape_sequence[32];
    uint8_t escape_sequence_pos = 0;
    uint8_t crlf = 0;
    void serial_data_handler(uint8_t numbits, uint8_t data);
    void serial_data_flush();

    //
    // GUI
    //
    static constexpr unsigned steps_per_update = 1000000;
    unsigned steps_to_update = 0;
    std::unique_ptr<gui> g;
    std::vector<gui::event> events;
    std::unique_ptr<disk_file> pending_disk;
    uint8_t pending_disk_drive = 0xff;
    uint32_t disk_chosen_countdown = 0;
    bool new_frame = false;
    bool quit = false;
    void process_event(const gui::event& evt);

    //
    // Debug stuff
    //
    bool debug_mode = false;
    enum { wait_none,
        wait_next_inst,
        wait_exact_pc,
        wait_exact_inst,
        wait_rtx,
        wait_non_rom_pc,
        wait_vpos,
        wait_frames,
        wait_process,
    } wait_mode = wait_none;
    uint32_t wait_arg = 0;
    std::string wait_process_name;
    uint32_t exception_break_mask = 0;
    uint32_t chip_cycles_count = 0, cpu_cycles_count = 0;
    uint32_t last_vhpos = 0;
    std::vector<uint32_t> breakpoints;
    int illegal_access_debug_mode = -1; // 0 = print, 1 = break
    std::vector<cpu_state> cpu_history = std::vector<cpu_state>(1024); // XXX
    uint32_t cpu_history_pos = 0;
    std::unique_ptr<std::ifstream> debug_script;

    struct mem_use {
        bus_use use;
        uint32_t addr;
        uint16_t val;
    };

    std::vector<mem_use> dma_usage = std::vector<mem_use>(vpos_per_field * hpos_per_line / 2);
    std::vector<memwatch> memwatches;
    std::vector<uint32_t> tasks;
    std::unique_ptr<std::ofstream> trace_file;
    bool profiling_ = false;
    std::map<uint32_t, uint64_t> profiling_data_;
    
    void activate_debugger();
    void check_debug_break();
    bool check_wait_process_name(const std::string& process_name) const;
    void check_wait_process(uint32_t process_ptr, const std::string& process_name);
    void on_task_init(const uint32_t exec_base);
    void on_task_added(const uint32_t task_ptr, uint32_t initial_pc);
    void on_task_removed(const uint32_t task_ptr);
    void on_load_seg(uint32_t name_ptr, const uint32_t seg_list_bptr);
    void debugger_loop();

    //
    // Audio
    //
    std::unique_ptr<wavedev> audio;
    std::mutex audio_mutex_;
    std::condition_variable audio_buffer_ready_cv;
    std::condition_variable audio_buffer_played_cv;
    int16_t audio_buffer[2][audio_buffer_size];
    bool audio_buffer_ready[2] = { false, false };
    int audio_next_to_play = 0;
    int audio_next_to_fill = 0;

    void audio_callback(int16_t* buf, size_t sz);

    //
    // Main stuff
    //
    custom_handler::step_result custom_step {};
    m68000::step_result cpu_step {};
    bool cpu_active = false;
    uint32_t cycles_todo = 0;
    enum { no_reset,
        cpu_reset,
        keyboard_reset
    } reset = no_reset;

    void step();

    void cstep(bool cpu_waiting);
    void do_all_custom_cylces();
    uint8_t read_ipl();
    void on_memory_access(uint32_t addr, uint32_t data, uint8_t size, bool write);
    void on_illegal_acess(uint32_t addr, uint32_t data, uint8_t size, bool write);

    //
    // Other stuff
    //

    void insert_disk(uint8_t drive, const char* filename, int delay = default_disk_insertion_delay);
};

amiga::amiga(const command_line_arguments& args)
    : cmdline_args { args }
    , mem { cmdline_args.chip_size }
    , rom { mem, patch(read_file(cmdline_args.rom)) }
    , custom { mem, cias, slow_base + cmdline_args.slow_size, cmdline_args.floppy_speed }
{
    if (cmdline_args.slow_size) {
        slow_ram = std::make_unique<ram_handler>(cmdline_args.slow_size);
        mem.register_handler(*slow_ram, slow_base, cmdline_args.slow_size);
#if 0 // XXX: Figure out behavior for custom mirror..
      // Mirror up to 0xd80000
            const uint32_t slow_end = slow_base + max_slow_size;
            for (uint32_t addr = slow_base + cmdline_args.slow_size; addr < slow_end; addr += cmdline_args.slow_size) {
                uint32_t size = cmdline_args.slow_size;
                if (addr + size > slow_end)
                    size = slow_end - addr;
                mem.register_handler(*slow_ram, addr, size);
            }
#endif
    }
    if (cmdline_args.fast_size) {
        fast_ram = std::make_unique<fastmem_handler>(mem, cmdline_args.fast_size);
        autoconf.add_device(*fast_ram);
    }
    if (!cmdline_args.df0.empty())
        df0.insert_disk(load_disk_file(cmdline_args.df0));
    if (!cmdline_args.df1.empty())
        df1.insert_disk(load_disk_file(cmdline_args.df1));

    custom.set_serial_data_handler([this](uint8_t numbits, uint8_t data) { serial_data_handler(numbits, data); });

    if (!cmdline_args.test_mode) {
        g = std::make_unique<gui>(graphics_width, graphics_height, std::array<std::string, 4> { cmdline_args.df0, cmdline_args.df1, "", "" });
        df0.set_disk_activity_handler([this](uint8_t track, bool write) {
            g->disk_activty(0, track, write);
        });
        df1.set_disk_activity_handler([this](uint8_t track, bool write) {
            g->disk_activty(1, track, write);
        });
        g->set_on_pause_callback([&](bool pause) {
            if (audio) {
                audio->set_paused(pause);
            }
        });
    }

    if (!cmdline_args.debug_script.empty()) {
        debug_script = std::make_unique<std::ifstream>(cmdline_args.debug_script);
        if (!*debug_script || !debug_script->is_open())
            throw std::runtime_error { "Debug script not found: " + cmdline_args.debug_script };
    }

    cpu.set_cycle_handler([this](uint8_t cycles) {
        assert(cpu_active);
        assert(cycles % 2 == 0);
        cycles_todo += cycles;
    });
    cpu.set_read_ipl([this]() {
        return read_ipl();
    });
    mem.set_memory_interceptor([this](uint32_t addr, uint32_t data, uint8_t size, bool write) {
        on_memory_access(addr, data, size, write);
    });
    mem.set_illegal_access_handler([&](uint32_t addr, uint32_t data, uint8_t size, bool write) {
        on_illegal_acess(addr, data, size, write);
    });

    if (!cmdline_args.hds.empty() || !cmdline_args.shared_folders.empty()) {
        auto should_disable_autoboot = [&]() {
            return df0.ofs_bootable_disk_inserted();
        };
        hd = std::make_unique<harddisk>(mem, cpu_active, should_disable_autoboot, cmdline_args.hds, cmdline_args.shared_folders);
        autoconf.add_device(hd->autoconf_dev());
    }

    if (cmdline_args.debug_board) {
        dbg_board = std::make_unique<debug_board>(mem);
        autoconf.add_device(dbg_board->autoconf_dev());

        dbg_board->set_callbacks(
            [this](uint32_t exec_base) { on_task_init(exec_base); },
            [this](uint32_t task_ptr, uint32_t initial_pc) { on_task_added(task_ptr, initial_pc); },
            [this](uint32_t task_ptr) { on_task_removed(task_ptr); },
            [this](uint32_t name_ptr, uint32_t seg_list_bptr) { on_load_seg(name_ptr, seg_list_bptr); });
    }

    if (!cmdline_args.nosound && !wavedev::is_null()) {
        audio = std::make_unique<wavedev>(audio_sample_rate, audio_buffer_size, [this](int16_t* buf, size_t sz) {
            this->audio_callback(buf, sz);
        });
    }

    if (cmdline_args.debug)
        activate_debugger();
}

amiga::~amiga()
{
    if (audio) {
        // Make sure audio stops
        std::unique_lock<std::mutex> lock { audio_mutex_ };
        audio_buffer_ready[0] = true;
        audio_buffer_ready[1] = true;
        audio_buffer_ready_cv.notify_all();
    }
}

void amiga::handle_machine_state(state_file& sf)
{
    mem.handle_state(sf);
    if (slow_ram)
        slow_ram->handle_state(sf);
    custom.handle_state(sf);
    cias.handle_state(sf);
    rtc.handle_state(sf);
    autoconf.handle_state(sf);
    cpu.handle_state(sf);
    sf.handle(cycles_todo);
    if (sf.loading()) {
        // Fake up something for the debugger
        cpu_step.current_pc = cpu_step.last_pc = cpu.state().pc;
        cpu_step.instruction = cpu.state().prefecth_val;
    }
}

void amiga::cstep(bool cpu_waiting)
{
    const bool cpu_was_active = cpu_active;
    cpu_active = false;
    custom_step = custom.step(cpu_waiting, cpu_step.current_pc);
    cpu_active = cpu_was_active;

    if (!(custom_step.hpos & 1)) {
        dma_usage[custom_step.vpos * (hpos_per_line / 2) + custom_step.hpos / 2] = { custom_step.bus, custom_step.dma_addr, custom_step.dma_val };
        if (profiling_)
            ++profiling_data_[cpu_step.current_pc];
    }

    ++chip_cycles_count;

    if (wait_mode == wait_vpos && wait_arg == (static_cast<uint32_t>(custom_step.vpos) << 9 | custom_step.hpos)) {
        if (wait_arg == 0)
            new_frame = true;
        activate_debugger();
        wait_mode = wait_none;
    } else if (!(custom_step.vpos | custom_step.hpos)) {
        new_frame = true;
        if (wait_mode == wait_frames && wait_arg-- == 0) {
            activate_debugger();
            wait_mode = wait_none;
        }

        if (audio) {
            std::unique_lock<std::mutex> lock { audio_mutex_ };
            if (audio_buffer_ready[audio_next_to_fill]) {
                audio_buffer_played_cv.wait(lock, [&]() { return !audio_buffer_ready[audio_next_to_fill]; });
            }
            memcpy(audio_buffer[audio_next_to_fill], custom_step.audio, sizeof(audio_buffer[audio_next_to_fill]));
            audio_buffer_ready[audio_next_to_fill] = true;
            audio_next_to_fill = !audio_next_to_fill;
        }
        audio_buffer_ready_cv.notify_one();

#ifdef WRITE_SOUND
        static std::ofstream sound_out { "c:/temp/sound.raw", std::ofstream::binary };
        sound_out.write((const char*)custom_step.audio, 4 * audio_samples_per_frame);
#endif
#if 0
                static auto last_time = std::chrono::high_resolution_clock::now();
                static int frame_cnt = 0;
                if (++frame_cnt == 100) {
                    auto now = std::chrono::high_resolution_clock::now();
                    const auto t = std::chrono::duration_cast<std::chrono::microseconds>(now - last_time).count();
                    std::cout << "FPS: " << frame_cnt * 1e6 / t << "\n"; 
                    last_time = now;
                    frame_cnt = 0;
                }
#endif
    }
}

void amiga::do_all_custom_cylces()
{
    const uint32_t n = cycles_todo / cmdline_args.cpu_scale;
    for (uint32_t i = 0; i < n; ++i)
        cstep(false);

    cycles_todo -= cmdline_args.cpu_scale * n;
    cpu_cycles_count += cmdline_args.cpu_scale * n;
}

uint8_t amiga::read_ipl()
{
    ///// HACK: Delay IPL change from Paula by 3 CCKs.
    ///// Reality is more complicated: https://github.com/dirkwhoffmann/vAmiga/issues/274
    ///// And below isn't correct if Paula IPL changes between delay start and end, but let's see...
    /// if (cpu_ipl_delay) {
    ///    if (--cpu_ipl_delay == 0)
    ///        cpu_ipl = custom_step.ipl;
    ///} else if (cpu_ipl != custom_step.ipl) {
    ///    // Add longer delay for INT2/INT6 (CIA timer interrupts) to match timing of Razor1911-Voyage...
    ///    cpu_ipl_delay = (custom_step.ipl == 2 || custom_step.ipl == 6) ? 16 : 12;
    ///}

    do_all_custom_cylces(); // Sync
    if (DEBUG_INTS)
        std::cout << "PC=$" << hexfmt(cpu.state().pc) << " vpos=$" << hexfmt(custom_step.vpos) << " hpos=$" << hexfmt(custom_step.hpos) << " (clock $" << hexfmt(custom_step.hpos >> 1, 2) << ") IPL Poll\n";
    return custom.current_ipl();
}

void amiga::on_memory_access(uint32_t addr, uint32_t data, uint8_t size, bool write)
{
    for (const auto& mw : memwatches) {
        if (!mw.enabled)
            continue;
        if (mw.size && mw.size != size)
            continue;
        if (write) {
            if (!(mw.flags & memwatch::write))
                continue;
        } else if (!(mw.flags & memwatch::read))
            continue;
        const auto end = mw.address + (mw.size ? mw.size : 4);
        if (addr < end && mw.address < addr + size) {
            std::cout << "PC=" << hexfmt(cpu_step.last_pc) << " Memwatch $" << hexfmt(&mw - &memwatches[0]) << " " << mw << " hit! Address=$" << hexfmt(addr) << " Access size=" << (int)size;
            if (write)
                std::cout << " Write of value: $" << hexfmt(data, size * 2) << "\n";
            else
                std::cout << " Read\n";
            activate_debugger();
        }
    }

    // TMEPTEMP
    //std::cout << "Memory access to $" << hexfmt(addr) << " cycles = " << cpu_cycles_count << " (todo " << cycles_todo << ") HPOS=$" << hexfmt(custom_step.hpos) << " Eclock=" << hexfmt(custom_step.eclock_cycle) << "\n";

    if (!cpu_active)
        return;

    const auto scale = cmdline_args.cpu_scale;

    assert(size == 1 || size == 2);
    (void)size;
    if (addr < max_chip_size || (addr >= slow_base && addr < custom_base_addr + custom_mem_size)) {
        // Sync with Agnus
        // CPU access to chip/custom memory always takes at least 2 CCKs (https://eab.abime.net/showpost.php?p=1333186&postcount=7)
        // First cycle transfers address to Agnus (bus doesn't need to be free)
        // Second cycle does actual transfer (must be free)
        // Testing on an ACA500plus shows that the CPU first has to "align" with the bus clock: https://github.com/dirkwhoffmann/vAmiga/issues/661

        const uint32_t cck_cycles = 2 * scale;
        do_all_custom_cylces();

        if (scale > 1 && cycles_todo) {
            cpu_cycles_count += cycles_todo;
            cycles_todo = 0;
            cstep(false);
        }

        // Align with CCK
        if (custom_step.hpos & 1) {
            cpu_cycles_count += scale;
            cstep(false);
        }

        // Transfer address to agnus (Takes one CCK, cycle does not have to be free)
        cycles_todo = cck_cycles;
        do_all_custom_cylces();

        // Wait for free cycle
        while (!custom_step.free_chip_cycle) {
            cpu_cycles_count += scale;
            cstep(true);
        }
        assert(!(custom_step.hpos & 1));
        auto& du = dma_usage[custom_step.vpos * (hpos_per_line / 2) + custom_step.hpos / 2];
        du = { write ? bus_use::cpu_write : bus_use::cpu_read, addr, write ? static_cast<uint16_t>(data) : mem.hack_peek_u16(addr) };
        cycles_todo = cck_cycles; // Transfer always takes 1 CCK
    } else if (addr >= cia_base_addr && addr < cia_base_addr + cia_mem_size) {
        // Get up to date
        do_all_custom_cylces();

        // Sync with EClock
        //
        // Eclock is low for 6 system clocks and then high for 4 during which the transfer happens
        // And address needs to be latched 3 cycles before EClk rises or falls
        while (custom_step.eclock_cycle != 3) {
            cpu_cycles_count += scale;
            cstep(false);
        }
        while (custom_step.eclock_cycle) {
            cpu_cycles_count += scale;
            cstep(false);
        }
    } else {
        cycles_todo += 4;
    }
}

void amiga::on_illegal_acess(uint32_t addr, uint32_t data, uint8_t size, bool write)
{
    if (illegal_access_debug_mode < 0)
        return;
    std::cout << "[MEM] Illegal " << (write ? "write to " : "read from ") << "$" << hexfmt(addr) << " size " << (int)size;
    if (write)
        std::cout << " data=$" << hexfmt(data, size * 2);
    std::cout << " PC=$" << hexfmt(cpu_step.current_pc) << "\n";
    if (illegal_access_debug_mode)
        activate_debugger();
}

void amiga::audio_callback(int16_t* buf, size_t sz)
{
    static_assert(2 * audio_samples_per_frame == audio_buffer_size);
    constexpr auto warn_interval = std::chrono::seconds(2);
    static auto last_warning = std::chrono::steady_clock::now() - warn_interval;
    const auto now = std::chrono::steady_clock::now();
    static unsigned num_warnings = 0;
    assert(sz == audio_samples_per_frame);
    {
        std::unique_lock<std::mutex> lock { audio_mutex_ };
        if (!audio_buffer_ready[audio_next_to_play]) {
            ++num_warnings;
            memset(buf, 0, sz * 2 * sizeof(int16_t));
            // Deadlocks with SDL
            // audio_buffer_ready_cv.wait(lock, [&]() { return audio_buffer_ready[audio_next_to_play]; });
        } else {
            memcpy(buf, audio_buffer[audio_next_to_play], sz * 2 * sizeof(int16_t));
        }
        audio_buffer_ready[audio_next_to_play] = false;
        audio_next_to_play = !audio_next_to_play;
    }
    audio_buffer_played_cv.notify_one();
    if (num_warnings && now - last_warning > warn_interval) {
        std::cerr << "Audio buffer not ready! (" << num_warnings << ")\n";
        last_warning = now;
        num_warnings = 0;
    }
}

void amiga::serial_data_handler([[maybe_unused]] uint8_t numbits, uint8_t data)
{
    assert(numbits == 8);
    if (data == 0x1b) {
        assert(escape_sequence_pos == 0);
        escape_sequence[escape_sequence_pos++] = 0x1b;
        return;
    } else if (escape_sequence_pos) {
        if (escape_sequence_pos >= sizeof(escape_sequence)) {
            // Overflow
            assert(0);
            escape_sequence_pos = 0;
            return;
        }
        escape_sequence[escape_sequence_pos++] = data;

        if (isalpha(data)) {
            if (data == 'm') {
                // ESC CSI 'n' m -> SGR (Select Graphic Rendition)
                escape_sequence_pos = 0;
                return;
            } else if (data == 'H') {
                // ESC CSI 'n' ; 'm' H -> Moves the cursor to row n, column m
                escape_sequence_pos = 0;
                return;
            }
            std::cout << "TODO: check escape sequence 'ESC" << std::string(escape_sequence + 1, escape_sequence_pos - 1) << "'\n";
            escape_sequence_pos = 0;
        }
        return;
    }
    // std::cout << "Serdata: $" << hexfmt(data) << ": " << (char)(isprint(data) ? data : ' ') << "\n";
    if (data == '\r') {
        if (crlf & 1) {
            // LF CR
            serdata.push_back('\r');
            serdata.push_back('\n');
            crlf = 0;
            return;
        }
        crlf |= 2;
        return;
    } else if (data == '\n') {
        if (crlf & 2) {
            // CR LF
            serdata.push_back('\r');
            serdata.push_back('\n');
            crlf = 0;
            return;
        }
        crlf |= 1;
        return;
    }
    if (crlf) {
        // Plain CR or LF
        assert(crlf == 1 || crlf == 2);
        serdata.push_back('\r');
        serdata.push_back('\n');
        crlf = 0;
    }
    serdata.push_back(data ? data : ' ');
}

void amiga::serial_data_flush()
{
    if (!serdata.empty()) {
        if (g)
            g->serial_data(serdata);
        else
            std::cout << "[SERIAL] " << std::string(serdata.begin(), serdata.end()) << "\n";
        serdata.clear();
    }
}

void amiga::activate_debugger()
{
    if (g)
        g->set_active(false);
    debug_mode = true;
}

void amiga::check_debug_break()
{

    // TODO refactor

    constexpr uint32_t min_rom_addr = 0x00e0'0000;

    if (cpu_step.exception && exception_break_mask & (1 << cpu_step.exception)) {
        std::cout << "Breaking on exception " << static_cast<int>(cpu_step.exception) << "\n";
        goto activate;
    }

#ifdef DEBUG_BREAK_INST
    if (cpu_step.instruction == debug_break_instruction_num)
        goto activate;
#endif

#if 0
    if (cpu_step.instruction == 0x4eae) {
        // JSR ofs(A6)
        if (auto ofs = static_cast<int16_t>(mem.hack_peek_u16(cpu_step.last_pc + 2)); ofs < 0) {
            static std::map<uint32_t, std::string> names;
            static uint32_t exec_base, dos_base;
            const auto a6 = cpu.state().a[6];
            std::string name;
            if (auto it = names.find(a6); it != names.end()) {
                name = it->second;
            } else {
                auto na = mem.read_u32(a6 + 0x0a); // Name
                for (;;) {
                    auto b = mem.read_u8(na++);
                    if (!b)
                        break;
                    name.push_back(b);
                }
                names.insert({ a6, name });
                if (name == "exec.library")
                    exec_base = a6;
                else if (name == "dos.library")
                    dos_base = a6;
            }
            if (a6 == dos_base && ofs == -156 /*unload seg*/) {
                print_cpu_state(std::cout, cpu.state());
                std::cout << "JSR -" << (-ofs) << "(A6) ; " << name << "\n";
                if (cpu.state().d[1] * 4 >= 0xa00000)
                    activate_debugger();
            }
        }
    }
#endif

    switch (wait_mode) {
    case wait_none:
        goto check_breakpoint;
    case wait_next_inst:
        if (wait_arg) {
            --wait_arg;
            goto check_breakpoint;
        }
        break;
    case wait_exact_pc:
        if (cpu_step.current_pc != wait_arg)
            goto check_breakpoint;
        break;
    case wait_exact_inst:
        if (cpu_step.instruction != wait_arg)
            goto check_breakpoint;
        break;
    case wait_rtx:
        if (cpu_step.instruction != 0x4e73 && cpu_step.instruction != 0x4e75 && cpu_step.instruction != 0x4e77) // RTE, RTS or RTR
            goto check_breakpoint;
        break;
    case wait_non_rom_pc:
        if (cpu_step.current_pc >= min_rom_addr)
            goto check_breakpoint;
        // Ignore if next instruction is JMP ABS.L to ROM
        if (mem.read_u16(cpu_step.current_pc) == 0x4ef9 && mem.read_u32(cpu_step.current_pc + 2) >= min_rom_addr)
            goto check_breakpoint;
        break;
    case wait_vpos: // Handled in cstep
    case wait_frames:
    case wait_process: // Handled elsewhere
        goto check_breakpoint;
    }

activate:
    activate_debugger();
    cpu.trace(nullptr);
    wait_mode = wait_none;
    return;

check_breakpoint:
    if (auto it = std::find(breakpoints.begin(), breakpoints.end(), cpu_step.current_pc); it != breakpoints.end()) {
        std::cout << "Breakpoint hit\n";
        goto activate;
    }
}

bool amiga::check_wait_process_name(const std::string& process_name) const
{
    assert(!cpu_active); // should already be disabled
    auto end_pos = process_name.find_last_of(":/");
    auto prname = toupper_str(end_pos != std::string::npos ? process_name.substr(end_pos + 1) : process_name);
    return prname == toupper_str(wait_process_name);
}

void amiga::check_wait_process(uint32_t process_ptr, const std::string& process_name)
{
    if (wait_mode != wait_process || !check_wait_process_name(process_name))
        return;
    wait_process_name.clear();
    uint32_t seglist = 0;
    if (auto cli_ptr = mem.read_u32(process_ptr + pr_CLI) << 2; cli_ptr) {
        seglist = mem.read_u32(cli_ptr + cli_Module) << 2;
    } else if (auto seglist_array = mem.read_u32(process_ptr + pr_SegList) << 2; seglist_array) {
        // HACK HACK HACK
        // Ignore the size in the seglist array. It seems to report 2 for programs started from the WB
        // even though entry 3 works fine..
        // auto sz = mem.read_u32(seglist_array);
        seglist = mem.read_u32(seglist_array + 4 * 3) << 2; // Entry 3
    }

    if (!seglist) {
        std::cerr << "Unable to find seglist for process \"" << process_name << "\"\n";
        wait_mode = wait_none;
        activate_debugger();
        return;
    }

    std::cout << "Setting breakpoint for \"" << process_name << "\" at $" << hexfmt(wait_arg) << "\n";

    wait_mode = wait_exact_pc;
    wait_arg = seglist + 4;
}

void amiga::on_task_init(const uint32_t exec_base)
{
    disable_bool db { cpu_active };
    constexpr uint32_t ThisTask = 276;
    // Bit of a hack, but add initial process (could scan task lists instead, but doesn't seem to be necessary)
    tasks.push_back(mem.read_u32(exec_base + ThisTask));
}

void amiga::on_task_added(const uint32_t task_ptr, [[maybe_unused]] uint32_t initial_pc)
{
    disable_bool db { cpu_active };
    const auto type = mem.read_u8(task_ptr + ln_Type);
    if (type != NT_TASK && type != NT_PROCESS) {
        std::cerr << "Warning AddTask called with wrong node type $" << hexfmt(type) << " address $" << hexfmt(task_ptr) << "\n";
        return;
    }
    if (auto it = std::find(tasks.begin(), tasks.end(), task_ptr); it != tasks.end()) {
        std::cerr << "Task with address $" << hexfmt(task_ptr) << " already added!\n";
        return;
    }
    tasks.push_back(task_ptr);
    const auto task_name = read_string(mem, mem.read_u32(task_ptr + ln_Name));
    std::cout << (type == NT_TASK ? "Task" : "Process") << " \"" << task_name << "\" started address $" << hexfmt(task_ptr) << " initialPC=$" << hexfmt(initial_pc) << "\n";
    if (type == NT_PROCESS)
        check_wait_process(task_ptr, task_name);
}

void amiga::on_task_removed(const uint32_t task_ptr)
{
    disable_bool db { cpu_active };
    auto it = std::find(tasks.begin(), tasks.end(), task_ptr);
    const auto type = mem.read_u8(task_ptr + ln_Type);
    const char* type_name = (type == NT_TASK ? "Task" : "Process");
    if (it == tasks.end()) {
        std::cerr << type_name << " \"" << read_string(mem, mem.read_u32(task_ptr + ln_Name)) << "\" with address $" << hexfmt(task_ptr) << " removed but not previously known!\n";
        return;
    }
    tasks.erase(it);
    std::cout << type_name << " \"" << read_string(mem, mem.read_u32(task_ptr + ln_Name)) << "\" removed address $" << hexfmt(task_ptr) << "\n";
}

void amiga::on_load_seg(uint32_t name_ptr, const uint32_t seg_list_bptr)
{
    disable_bool db { cpu_active };
    std::string name;
    if (name_ptr & 0x80000000) {
        // BSTR
        name_ptr = (name_ptr & ~0x80000000) * 4;
        int len = mem.read_u8(name_ptr++);
        for (int i = 0; i < len; ++i)
            name.push_back(mem.read_u8(name_ptr++));
    } else {
        name = read_string(mem, name_ptr);
    }
    std::cout << "LoadSeg \"" << name << "\" seglist=$" << hexfmt(seg_list_bptr) << "\n";

    if (wait_mode != wait_process)
        return;
    if (check_wait_process_name(name)) {
        wait_mode = wait_exact_pc;
        wait_arg = (seg_list_bptr + 1) << 2;
        wait_process_name.clear();
        std::cout << "Setting breakpoint for \"" << name << "\" at $" << hexfmt(wait_arg) << "\n";
    }
}

void amiga::debugger_loop()
{
    const auto& s = cpu.state();
    if (g) {
        serial_data_flush();
        g->set_debug_memory(mem.ram(), custom.get_regs());
        g->set_debug_windows_visible(true);
        g->update_image(custom_step.frame);
    }

    // Match Winuae output
    // Cycles: 8 Chip, 16 CPU. (V=0 H=31 -> V=0 H=39)
    const uint32_t vhpos = custom_step.vpos << 8 | custom_step.hpos >> 1;
    std::cout << "Cycles: " << (chip_cycles_count >> 1) << " Chip, " << cpu_cycles_count << " CPU. (V=" << (last_vhpos >> 8) << " H=" << (last_vhpos & 0xff) << " -> V=" << (vhpos >> 8) << " H=" << (vhpos & 0xff) << ")\n";
    last_vhpos = vhpos;
    chip_cycles_count = cpu_cycles_count = 0;

    cpu.show_state(std::cout);
    disasm_stmts(mem, cpu_step.current_pc, 1);

    debugger_cpu_state = s;

    uint32_t disasm_pc = s.pc, hexdump_addr = 0, cop_addr = custom.copper_ptr(0);
    for (;;) {
        std::string line;
        if (debug_script) {
            for (;;) {
                if (!std::getline(*debug_script, line)) {
                    debug_script.reset();
                    line.clear();
                    break;
                }
                line = trim(line);
                if (!line.empty() && line[0] != '#') {
                    std::cout << "> " << line << "\n";
                    break;
                }
            }
        }
        if (line.empty()) {
            if (g) {
                if (!g->debug_prompt(line)) {
                    debug_mode = false;
                    quit = true;
                    break;
                }
            } else {
                std::cout << "> " << std::flush;
                if (!std::getline(std::cin, line)) {
                    debug_mode = false;
                    quit = true;
                    break;
                }
            }
        }
        const auto args = split_line(line);
        if (args.empty() || args[0].empty() || args[0][0] == '#')
            continue;
        if (args[0] == "c") {
            custom.show_debug_state(std::cout);
        } else if (args[0] == "d") {
            auto [valid, pc, lines] = get_addr_and_lines(args, disasm_pc, 10);
            if (valid) {
                disasm_pc = pc;
                disasm_pc += disasm_stmts(mem, disasm_pc, lines);
            }
        } else if (args[0] == "e") {
            custom.show_registers(std::cout);
        } else if (args[0] == "f") {
            if (args.size() > 1) {
                auto [valid, pc] = get_simple_expr(args[1]);
                if (valid && !(pc & 1)) {
                    if (auto it = std::find(breakpoints.begin(), breakpoints.end(), pc); it != breakpoints.end()) {
                        breakpoints.erase(it);
                        std::cout << "Breakpoint removed\n";
                    } else {
                        breakpoints.push_back(pc);
                    }
                } else {
                    std::cerr << "Invalid address\n";
                }
            } else {
                wait_mode = wait_non_rom_pc;
                break;
            }
        } else if (args[0] == "fd") {
            breakpoints.clear();
            std::cout << "All breakpoints deleted\n";
        } else if (args[0] == "fi") {
            if (args.size() > 1) {
                auto [valid, inst] = get_simple_expr(args[1]);
                if (valid && inst < 0x10000) {
                    wait_mode = wait_exact_inst;
                    wait_arg = inst;
                    break;
                } else {
                    std::cerr << "Invalid instruction\n";
                }
            } else {
                wait_mode = wait_rtx;
                break;
            }
        } else if (args[0] == "g") {
            if (args.size() == 2) {
                if (const auto [ok, addr] = get_simple_expr(args[1]); ok) {
                    const_cast<cpu_state&>(s).pc = addr;
                    break;
                } else {
                    std::cerr << "Invalid address\n";
                }
            } else if (args.size() == 1) {
                break;
            } else {
                std::cerr << "Invalid number of arguments\n";
            }
        } else if (args[0] == "H" || args[0] == "HH") {
            uint32_t cnt = 10;
            if (args.size() > 1) {
                if (auto [valid, cntr] = get_simple_expr(args[1]); valid) {
                    cnt = cntr;
                } else {
                    cnt = 0;
                    std::cout << "Invalid number for \"" << args[0] << "\"  \"" << args[1] << "\"\n";
                }
            }
            const uint32_t max_cnt = static_cast<uint32_t>(cpu_history.size());
            cnt = std::min(cnt, max_cnt);
            auto idx = (cpu_history_pos - 1 - cnt) % max_cnt;
            while (cnt--) {
                const auto& ch = cpu_history[idx++ % max_cnt];
                if (args[0] == "HH")
                    print_cpu_state(std::cout, ch);
                disasm_stmts(mem, ch.pc, 1);
            }
        } else if (args[0] == "il") {
            if (args.size() > 1) {
                if (auto [valid, mask] = get_simple_expr(args[1]); valid)
                    exception_break_mask = mask;
                else
                    std::cerr << "Invalid mask\n";
            } else {
                exception_break_mask = ~0U;
            }
            std::cout << "Exception break mask: $" << hexfmt(exception_break_mask) << "\n";
        } else if (args[0] == "insert_disk") {
            if (args.size() >= 2) {
                const int drive = args[1].length() == 1 && isdigit(args[1][0]) ? args[1][0] - '0' : -1;
                if (drive >= 0 && drive < max_drives && drives[drive]) {
                    int delay = default_disk_insertion_delay;
                    if (args.size() >= 4) {
                        auto d = get_simple_expr(args[3]);
                        if (d.first)
                            delay = d.second;
                        else
                            delay = -1;
                    }
                    if (delay >= 0)
                        insert_disk(static_cast<uint8_t>(drive), args.size() >= 3 ? unquote(args[2]).c_str() : "", delay);
                    else
                        std::cerr << "Invalid delay";
                } else {
                    std::cerr << "Invalid drive";
                }
            } else {
                std::cerr << "Drive missing\n";
            }
        } else if (args[0] == "m") {
            auto [valid, addr, lines] = get_addr_and_lines(args, hexdump_addr, 20);
            addr &= ~1;
            if (valid) {
                std::vector<uint8_t> data(lines * 16);
                for (size_t i = 0; i < lines * 8; ++i)
                    put_u16(&data[2 * i], mem.read_u16(static_cast<uint32_t>(addr + 2 * i)));
                hexdump16(std::cout, addr, data.data(), data.size());
                hexdump_addr = addr + lines * 16;
            }
        } else if (args[0][0] == 'o') {
            if (args[0] == "o") {
                auto [valid, addr, lines] = get_addr_and_lines(args, cop_addr, 20);
                if (valid) {
                    cop_addr = addr & ~1;
                    cop_addr += copper_disasm(mem, cop_addr, lines);
                }
            } else if (args[0] == "o1") {
                cop_addr = custom.copper_ptr(1);
                cop_addr += copper_disasm(mem, cop_addr, 20);
            } else if (args[0] == "o2") {
                cop_addr = custom.copper_ptr(2);
                cop_addr += copper_disasm(mem, cop_addr, 20);
            } else {
                goto unknown_command;
            }
        } else if (args[0] == "prof") {
            if (args.size() > 1) {
                if (args[1] == "1") {
                    if (!profiling_) {
                        profiling_ = true;
                        profiling_data_.clear();
                    } else {
                        std::cerr << "Profiling already enabled\n";
                    }
                } else if (args[1] == "0") {
                    profiling_ = false;
                }
            } else if (!profiling_) {
                std::cerr << "Profiling is not enabled\n";
            } else {
                // Slow and ugly, but show some data
                std::map<uint64_t, uint32_t> data;
                std::map<std::string, uint64_t> insts;
                std::map<uint64_t, std::string> reverse_insts;
                uint64_t total = 0;
                for (const auto [addr, cycles] : profiling_data_) {
                    data[cycles] = addr;
                    total += cycles;
                    insts[instructions[mem.hack_peek_u16(addr)].name] += cycles;
                }
                for (const auto& i: insts)
                    reverse_insts[i.second] = i.first;
                auto it = data.crbegin();
                for (unsigned i = 0; i < 200 && it != data.crend(); ++i, ++it) {
                    std::cout << hexfmt(it->second) << " " << std::setw(10) << static_cast<double>(it->first) * 100 / total << "\t";
                    uint16_t iwords[max_instruction_words];
                    iwords[0] = mem.hack_peek_u16(it->second);
                    for (int j = 1; j < instructions[iwords[0]].ilen; ++j)
                    iwords[j] = mem.hack_peek_u16(it->second + j * 2);
                    disasm(std::cout, it->second, iwords, max_instruction_words);
                    std::cout << "\n";
                }
                auto it2 = reverse_insts.crbegin();
                for (unsigned i = 0; i < 200 && it2 != reverse_insts.crend(); ++i, ++it2) {
                    std::cout << it2->second << "\t" << std::setw(10) << static_cast<double>(it2->first) * 100 / total << "\n";
                }
            }
        } else if (args[0] == "q") {
            quit = true;
            break;
        } else if (args[0] == "r") {
            if (args.size() == 1) {
                cpu.show_state(std::cout);
            } else if (args.size() == 3) {
                const auto [size, ptr] = get_register_address(args[1].c_str(), s);
                if (size) {
                    assert((size == 2 || size == 4) && ptr);
                    if (const auto [ok, val] = get_simple_expr(args[2]); ok) {
                        if (size == 2) {
                            if (val < 65536)
                                *reinterpret_cast<uint16_t*>(const_cast<void*>(ptr)) = static_cast<uint16_t>(val);
                            else
                                std::cerr << "Value out of range for 16-bit register\n";
                        } else {
                            *reinterpret_cast<uint32_t*>(const_cast<void*>(ptr)) = val;
                        }
                    } else {
                        std::cerr << "Invalid value\n";
                    }
                }
            } else {
                std::cerr << "Invalid number of arguments\n";
            }
        } else if (args[0] == "reset") {
            reset = keyboard_reset;
        } else if (args[0] == "s") {
            if (args.size() > 1 && !args[1].empty()) {
                std::vector<uint8_t> needle;
                bool ok = true;
                if (args[1][0] == '"') {
                    assert(args[1].back() == '"');
                    needle = std::vector<uint8_t>(args[1].begin() + 1, args[1].end() - 1);
                } else {
                    auto bytes = unhex_bytes(args[1]);
                    if (!bytes) {
                        std::cerr << "Invalid bytes\n";
                        ok = false;
                    } else {
                        needle = std::move(*bytes);
                    }
                }
                uint32_t start_address = 0;
                uint32_t end_address = 1 << 24;
                if (ok && args.size() > 2) {
                    std::tie(ok, start_address) = get_simple_expr(args[2]);
                    if (!ok)
                        std::cerr << "Invalid start address\n";
                }
                if (ok && args.size() > 3) {
                    std::tie(ok, end_address) = get_simple_expr(args[3]);
                    if (!ok)
                        std::cerr << "Invalid length\n";
                    end_address += start_address;
                }
                if (ok) {
                    unsigned match_count = 0;
                    mem_search(mem.ram(), 0, needle, start_address, end_address, match_count);
                    if (slow_ram)
                        mem_search(slow_ram->ram(), slow_base, needle, start_address, end_address, match_count);
                    if (fast_ram)
                        mem_search(fast_ram->ram(), fast_ram->base_address(), needle, start_address, end_address, match_count);
                    mem_search(rom.rom(), 0xf80000, needle, start_address, end_address, match_count); // Meh, not correct but w/e
                    if (!match_count)
                        debugger_ans = 0xffffffff;
                }
            } else {
                std::cerr << "Missing arguments\n";
            }
        } else if (args[0] == "t") {
            wait_arg = ~0U;
            if (args.size() > 1) {
                auto fh = get_simple_expr(args[1]);
                if (fh.first && fh.second > 0) {
                    wait_arg = fh.second - 1;
                } else {
                    std::cout << "Invalid argument (expected number of instructions)\n";
                }
            } else {
                wait_arg = 0;
            }
            if (wait_arg != ~0U) {
                wait_mode = wait_next_inst;
                if (wait_arg > 0)
                    cpu.trace(&std::cout);
            }
            break;
        } else if (args[0] == "trace_file") {
            if (args.size() > 1) {
                auto f = std::make_unique<std::ofstream>(args[1].c_str());
                if (*f) {
                    trace_file.reset(f.release());
                    debug_stream = trace_file.get();
                    std::cout << "Tracing to \"" << args[1] << "\"\n";
                } else {
                    std::cout << "Error creating \"" << args[1] << "\"\n";
                }
            } else {
                std::cout << "Tracing to screen\n";
                trace_file.reset();
                debug_stream = &std::cout;
            }
        } else if (args[0] == "trace_flags") {
            if (args.size() > 1) {
                auto fh = get_simple_expr(args[1]);
                if (fh.first) {
                    debug_flags = fh.second;
                } else {
                    debug_flags = 0;
                    for (size_t i = 1; i < args.size(); ++i) {
                        if (args[i] == "copper")
                            debug_flags |= debug_flag_copper;
                        else if (args[i] == "bpl")
                            debug_flags |= debug_flag_bpl;
                        else if (args[i] == "sprite")
                            debug_flags |= debug_flag_sprite;
                        else if (args[i] == "disk")
                            debug_flags |= debug_flag_disk;
                        else if (args[i] == "blitter")
                            debug_flags |= debug_flag_blitter;
                        else if (args[i] == "audio")
                            debug_flags |= debug_flag_audio;
                        else if (args[i] == "cia")
                            debug_flags |= debug_flag_cia;
                        else if (args[i] == "ints")
                            debug_flags |= debug_flag_ints;
                        else
                            std::cerr << "Unknown trace flag \"" << args[i] << "\"\n";
                    }
                }
            }
            std::cout << "debug flags: $" << hexfmt(debug_flags) << "\n";
        } else if (args[0] == "v") {
            uint32_t v = 0, h = 0;
            if (args.size() > 1) {
                auto vh = get_simple_expr(args[1]);
                if (!vh.first || vh.second >= vpos_per_field)
                    goto vcmdinvalidargs;
                v = vh.second;
                if (args.size() > 2) {
                    auto hh = get_simple_expr(args[2]);
                    if (!hh.first || hh.second >= hpos_per_line / 2)
                        goto vcmdinvalidargs;
                    h = hh.second;
                }
            }
            if (0) {
            vcmdinvalidargs:
                std::cerr << "Invalid args to v\n";
            } else {
                std::cout << "Line $" << hexfmt(v, 2) << "\n";
                const auto outer_end = std::min(static_cast<uint32_t>(hpos_per_line / 2), h + 80);
                for (uint32_t outer_hp = h; outer_hp < outer_end;) {
                    const auto end = std::min(outer_hp + 8, outer_end);
                    for (int infoline = 0; infoline < 4; ++infoline) {
                        for (uint32_t hp = outer_hp; hp < end; ++hp) {
                            std::cout << " ";
                            if (infoline == 0) {
                                std::cout << "[" << hexfmt(hp, 2) << " " << std::setw(3) << std::right << hp << "] ";
                                continue;
                            }
                            const auto& mi = dma_usage[v * (hpos_per_line / 2) + hp];
                            if (mi.use == bus_use::none || (infoline > 1 && mi.use == bus_use::refresh)) {
                                std::cout << "         ";
                                continue;
                            }
                            if (infoline == 1) {
                                std::cout << std::setw(8) << std::left;
                                switch (mi.use) {
                                case bus_use::none:
                                    assert(false);
                                    break;
                                case bus_use::refresh:
                                    std::cout << "REFRESH";
                                    break;
                                case bus_use::disk:
                                    std::cout << "DISK";
                                    break;
                                case bus_use::audio:
                                    std::cout << "AUDIO";
                                    break;
                                case bus_use::bitplane:
                                    std::cout << "BITPLANE";
                                    break;
                                case bus_use::sprite:
                                    std::cout << "SPRITE";
                                    break;
                                case bus_use::copper:
                                    std::cout << "COPPER";
                                    break;
                                case bus_use::blitter:
                                    std::cout << "BLITTER";
                                    break;
                                case bus_use::cpu_read:
                                    std::cout << "CPU R";
                                    break;
                                case bus_use::cpu_write:
                                    std::cout << "CPU W";
                                    break;
                                }
                            } else if (infoline == 2) {
                                std::cout << "    " << hexfmt(mi.val);
                            } else if (infoline == 3) {
                                std::cout << hexfmt(mi.addr);
                            } else {
                                assert(false);
                            }
                            std::cout << " ";
                        }
                        std::cout << "\n";
                    }
                    std::cout << "\n";
                    outer_hp = end;
                }
            }
        } else if (args[0] == "w") {
            if (args.size() > 1) {
                auto [nvalid, num] = get_simple_expr(args[1]);
                if (!nvalid)
                    goto memwatch_invalid_args;
                if (args.size() == 2) {
                    if (num >= memwatches.size()) {
                        std::cerr << "memwatch index out of range\n";
                        goto memwatch_invalid_args;
                    }
                    memwatches[num].enabled = false;
                    std::cout << "Memwatch $" << hexfmt(num) << " disabled\n";
                } else {
                    if (args.size() < 5) {
                        std::cerr << "Too few arguments\n";
                        goto memwatch_invalid_args;
                    }
                    auto [avalid, address] = get_simple_expr(args[2]);
                    auto [svalid, size] = get_simple_expr(args[3]);
                    uint8_t flags = 0;
                    for (const char c : args[4]) {
                        if (c == 'r' || c == 'R')
                            flags |= memwatch::read;
                        else if (c == 'w' || c == 'W')
                            flags |= memwatch::write;
                        else {
                            std::cerr << "Invalid mode (r/w)\n";
                            goto memwatch_invalid_args;
                        }
                    }
                    if (!avalid || !svalid || !flags)
                        goto memwatch_invalid_args;
                    if (size != 0 && size != 1 && size != 2 && size != 4) {
                        std::cerr << "Invalid size\n";
                        goto memwatch_invalid_args;
                    }
                    if (num >= memwatches.size())
                        memwatches.resize(num + 1);
                    auto& mw = memwatches[num];
                    mw.enabled = true;
                    mw.address = address;
                    mw.size = static_cast<uint8_t>(size);
                    mw.flags = static_cast<memwatch::flagtype>(flags);
                    std::cout << "Added memwatch $" << hexfmt(num) << ": " << mw << "\n";
                }
            } else {
            memwatch_invalid_args:
                std::cerr << "Invalid arguments to memwatch\n";
            }
        } else if (args[0] == "W") {
            if (args.size() > 2) {
                auto [avalid, address] = get_simple_expr(args[1]);
                if (avalid) {
                    for (uint32_t i = 2; i < args.size(); ++i) {
                        const auto& a = args[i];
                        uint8_t size = 0;
                        if (a.length() == 2)
                            size = 1;
                        else if (a.length() == 4)
                            size = 2;
                        else if (a.length() == 8)
                            size = 4;
                        else {
                            std::cerr << "Invalid length for value \"" << a << "\n";
                            break;
                        }
                        auto [dvalid, data] = get_simple_expr(args[i]);
                        if (!dvalid) {
                            std::cerr << "Invalid value \"" << a << "\n";
                            break;
                        }
                        if (size > 1 && (address & 1)) {
                            std::cerr << "Won't write to odd address " << hexfmt(address) << "\n";
                            break;
                        }
                        switch (size) {
                        case 1:
                            mem.write_u8(address, static_cast<uint8_t>(data));
                            break;
                        case 2:
                            mem.write_u16(address, static_cast<uint16_t>(data));
                            break;
                        case 4:
                            mem.write_u32(address, data);
                            break;
                        }
                        std::cout << "Wrote $" << hexfmt(data, size * 2) << " to $" << hexfmt(address) << "\n";
                        address += size;
                    }
                } else {
                    std::cerr << "Invalid adddress\n";
                }
            } else {
                std::cerr << "Missing arguments address, value(s)\n";
            }
        } else if (args[0] == "wd") {
            if (args.size() > 1) {
                if (auto [valid, arg] = get_simple_expr(args[1]); valid && ((arg == 0) || (arg == 1) || (arg == ~0U))) {
                    illegal_access_debug_mode = static_cast<int>(arg);
                } else {
                    std::cerr << "Invalid argument to wd\n";
                }
            } else {
                illegal_access_debug_mode = -1;
            }
            std::cout << "Illegal access debug ";
            switch (illegal_access_debug_mode) {
            case -1:
                std::cout << "disabled\n";
                break;
            case 0:
                std::cout << "logging\n";
                break;
            case 1:
                std::cout << "break\n";
                break;
            }
        } else if (args[0] == "write_mem") {
            if (args.size() > 1) {
                std::ofstream f(args[1], std::ofstream::binary);
                if (f) {
                    auto write_part = [&f](const void* data, uint32_t size, uint32_t base) {
                        uint8_t part_header[8];
                        put_u32(&part_header[0], base);
                        put_u32(&part_header[4], size);
                        f.write("PART", 4);
                        f.write(reinterpret_cast<const char*>(part_header), sizeof(part_header));
                        f.write(reinterpret_cast<const char*>(data), size);
                    };
                    f.write("MEMDUMP!", 8);
                    write_part(mem.ram().data(), static_cast<uint32_t>(mem.ram().size()), 0);
                    if (fast_ram && fast_ram->base_address())
                        write_part(fast_ram->ram().data(), static_cast<uint32_t>(fast_ram->ram().size()), fast_ram->base_address());
                    if (slow_ram)
                        write_part(slow_ram->ram().data(), static_cast<uint32_t>(slow_ram->ram().size()), slow_base);
                    f.write("REGS", 4);
                    auto put_reg = [&f](uint32_t val) {
                        uint8_t dat[4];
                        put_u32(dat, val);
                        f.write(reinterpret_cast<const char*>(dat), 4);
                    };
                    put_reg(s.pc);
                    put_reg(s.sr);
                    for (int i = 0; i < 8; ++i)
                        put_reg(s.d[i]);
                    for (int i = 0; i < 8; ++i)
                        put_reg(s.A(i));
                    put_reg(s.sr & srm_s ? s.usp : s.ssp);
                } else {
                    std::cerr << "Error creating \"" << args[1] << "\"\n";
                }
            } else {
                std::cerr << "Missing argument (file)\n";
            }
        } else if (args[0] == "write_screenshot") {
            if (args.size() > 1) {
                try {
                    write_bmp(args[1], custom_step.frame, graphics_width, graphics_height, graphics_width);
                } catch (const std::exception& e) {
                    std::cerr << e.what() << '\n';
                }
            } else {
                std::cerr << "Missing argument (file)\n";
            }
        } else if (args[0] == "write_state") {
            if (args.size() > 1) {
                assert(cycles_todo == 0);
                state_file sf { state_file::dir::save, args[1] };
                cmdline_args.handle_state(sf);
                handle_machine_state(sf);
            } else {
                std::cout << "Missing argument (file)\n";
            }
        } else if (args[0] == "z") {
            wait_mode = wait_exact_pc;
            wait_arg = s.pc + instructions[mem.read_u16(s.pc)].ilen * 2;
            break;
        } else if (args[0] == "zf") {
            // Wait for next frame(s)
            if (args.size() > 1) {
                auto [valid, count] = get_simple_expr(args[1]);
                if (valid) {
                    wait_mode = wait_frames;
                    wait_arg = count;
                    break;
                } else {
                    std::cerr << "Invalid argument (expected number of frames)\n";
                }
            } else {
                wait_mode = wait_vpos;
                wait_arg = 0;
                break;
            }
        } else if (args[0] == "zp") {
            if (dbg_board) {
                if (args.size() > 1 && !args[1].empty() && args[1][0] == '"') {
                    // TODO: WinUAE also supports process address
                    wait_mode = wait_process;
                    wait_process_name = unquote(args[1]);
                } else {
                    std::cerr << "Missing or invalid argument for zp (quoted process name)\n";
                }
            } else {
                std::cerr << "Debug board required\n";
            }
        } else if (args[0] == "zv") {
            // Wait for video position
            if (args.size() > 1) {
                uint32_t waitpos = 0;
                auto vpos = get_simple_expr(args[1]);
                if (vpos.first && vpos.second <= 313) {
                    waitpos = vpos.second << 9;
                    if (args.size() > 2) {
                        auto hpos = get_simple_expr(args[2]);
                        if (hpos.first && hpos.second < 455) {
                            waitpos |= hpos.second;
                        } else {
                            std::cout << "Invalid hpos\n";
                            waitpos = ~0U;
                        }
                    }
                    if (waitpos != ~0U) {
                        wait_mode = wait_vpos;
                        wait_arg = waitpos;
                        break;
                    }
                } else {
                    std::cout << "Invalid vpos\n";
                }
            } else {
                std::cout << "Missing argument(s) vpos [hpos]\n";
            }
        } else if (args[0][0] == '?') {
            std::string expr = args[0].substr(1);
            for (size_t i = 1; i < args.size(); ++i)
                expr += args[i];
            const auto [valid, num] = get_simple_expr(expr);
            if (valid) {
                std::cout << "$" << hexfmt(num) << " = %" << binfmt(num) << " = " << num << "\n";
                debugger_ans = num;
            } else {
                std::cout << "Invalid expression\n";
            }
        } else {
        unknown_command:
            std::cout << "Unknown command \"" << args[0] << "\"\n";
        }
    }
    debug_mode = false;
    if (g) {
        g->set_debug_windows_visible(false);
        g->set_active(true);
    }
}

void amiga::process_event(const gui::event& evt)
{
    switch (evt.type) {
    case gui::event_type::quit:
        quit = true;
        break;
    case gui::event_type::reset:
        reset = keyboard_reset;
        break;
    case gui::event_type::keyboard:
        cias.keyboard_event(evt.keyboard.pressed, evt.keyboard.scancode);
        break;
    case gui::event_type::mouse_button:
        if (evt.mouse_button.left)
            cias.set_button_state(0, evt.mouse_button.pressed);
        else
            custom.set_rbutton_state(evt.mouse_button.pressed);
        break;
    case gui::event_type::mouse_move:
        custom.mouse_move(evt.mouse_move.dx, evt.mouse_move.dy);
        break;
    case gui::event_type::disk_inserted:
        assert(evt.disk_inserted.drive < max_drives && drives[evt.disk_inserted.drive]);
        insert_disk(evt.disk_inserted.drive, evt.disk_inserted.filename);
        break;
    case gui::event_type::debug_mode:
        activate_debugger();
        break;
    case gui::event_type::joystick: {
        cias.set_button_state(1, evt.joystick.button1);
        uint16_t dat = 0;
        if (evt.joystick.left)
            dat |= 1 << 9;
        if (evt.joystick.right)
            dat |= 1 << 1;
        if (evt.joystick.up ^ evt.joystick.left)
            dat |= 1 << 8;
        if (evt.joystick.down ^ evt.joystick.right)
            dat |= 1 << 0;
        custom.set_joystate(dat, evt.joystick.button2);
        break;
    }
    default:
        assert(0);
    }
}

void amiga::insert_disk(uint8_t drive, const char* filename, int delay)
{
    assert(drive < max_drives && drives[drive]);

    std::unique_ptr<disk_file> disk;
    if (*filename) {
        std::cout << "Reading " << filename << "\n";
        try {
            disk = load_disk_file(filename);
        } catch (const std::exception& e) {
            std::cerr << "Failed to read " << filename << ": " << e.what() << "\n";
            return;
        }
    }
    // Hack: overwrite cmdline args for disk file
    if (drive == 0) {
        cmdline_args.df0 = filename;
    } else {
        assert(drive == 1);
        cmdline_args.df1 = filename;
    }
    std::cout << drives[drive]->name() << " Ejecting\n";
    drives[drive]->insert_disk(nullptr); // Eject any existing disk
    if (!*filename)
        return;
    if (delay && !disk_chosen_countdown) {
        assert(!pending_disk);
        disk_chosen_countdown = delay; // Give SW (e.g. Defender of the Crown) time to recognize that the disk has changed
        pending_disk = std::move(disk);
        pending_disk_drive = drive;
    } else {
        std::cout << drives[drive]->name() << " Inserting disk (" << filename << ")\n";
        drives[drive]->insert_disk(std::move(disk));
    }
}

void amiga::run()
{
    // If not loading from state ensure that custom chips have been "stepped" once
    if (!cpu.state().instruction_count)
        cstep(false);
    while (!quit) {
        try {
            step();
        } catch (const std::exception& e) {
            cpu.show_state(std::cerr);
            std::cerr << "\n"
                      << e.what() << "\n\n";
            serial_data_flush();
            if (cmdline_args.test_mode)
                throw;
            activate_debugger();
        }
    }
}

void amiga::step()
{
    if (!events.empty()) {
        auto evt = events[0];
        events.erase(events.begin());
        process_event(evt);
    }

    if (debug_mode)
        debugger_loop();

    if (!cpu_step.stopped)
        memcpy(&cpu_history[cpu_history_pos++ % cpu_history.size()], &cpu.state(), sizeof(cpu_state));

    cpu_active = true;
    cpu_step = cpu.step();
    cpu_active = false;

    if (cpu_step.stopped) {
        do {
            cstep(false);
        } while (!custom.current_ipl() && !new_frame && !debug_mode);
    } else {
        check_debug_break();

        if (cpu_step.instruction == reset_instruction_num)
            reset = cpu_reset;
        do_all_custom_cylces();
    }

    if (reset != no_reset) {
        std::cout << (reset == cpu_reset ? "CPU reset" : "Keyboard reset") << "\n";
        if (!debug_mode) {
            tasks.clear();
            mem.reset();
            if (reset != cpu_reset)
                cpu.reset();
            reset = no_reset;
        }
    }

    if (new_frame) {
        if (disk_chosen_countdown) {
            if (--disk_chosen_countdown == 0) {
                std::cout << drives[pending_disk_drive]->name() << " Inserting disk\n";
                drives[pending_disk_drive]->insert_disk(std::move(pending_disk));
                pending_disk_drive = 0xff;
            }
        }
        new_frame = false;
        goto update;
    }
    if (!steps_to_update--) {
    update:
        serial_data_flush();
        steps_to_update = steps_per_update;
        if (ctrl_c) {
            ctrl_c = false;
            signal(SIGINT, &ctrl_c_handler);
            activate_debugger();
        }
        if (g) {
            g->update_image(custom_step.frame);
            g->led_state(cias.power_led_on());
            auto new_events = g->update();
            events.insert(events.end(), new_events.begin(), new_events.end());
        }
    }
}

int main(int argc, char* argv[])
{
    try {
        auto cmdline_args = parse_command_line_arguments(argc, argv);
        std::unique_ptr<state_file> state;
        if (!cmdline_args.state_filename.empty()) {
            state = std::make_unique<state_file>(state_file::dir::load, cmdline_args.state_filename);
            cmdline_args.handle_state(*state);
        }

        signal(SIGINT, &ctrl_c_handler);

        amiga amiga_ { cmdline_args };
        if (state)
            amiga_.handle_machine_state(*state);

        debugger_functions.push_back({ "get_u8", [&mem = amiga_.mem_handler()](uint32_t addr) -> uint32_t { return mem.read_u8(addr); } });
        debugger_functions.push_back({ "get_u16", [&mem = amiga_.mem_handler()](uint32_t addr) -> uint32_t { return mem.read_u16(addr); } });
        debugger_functions.push_back({ "get_u32", [&mem = amiga_.mem_handler()](uint32_t addr) -> uint32_t { return mem.read_u32(addr); } });
        debugger_functions.push_back({ "extb", [](uint32_t val) { return sext(val, opsize::b); } });
        debugger_functions.push_back({ "extw", [](uint32_t val) { return sext(val, opsize::w); } });

        amiga_.run();

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
