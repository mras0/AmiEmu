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
#include "adf.h"

namespace {

volatile bool ctrl_c;

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
    for (size_t j = i + 1; j < l;) {
        if (isspace(line[j])) {
            args.push_back(line.substr(i, j - i));
            while (isspace(line[j]) && j < l)
                ++j;
            i = j;
        } else {
            ++j;
        }
    }
    if (i < l)
        args.push_back(line.substr(i));
    return args;
}

std::pair<bool, uint32_t> get_simple_expr(const std::string& arg)
{
    if (arg.empty())
        return { false, 0 };

    auto prec = [](char c) {
        switch (c) {
        case '+':
        case '-':
            return 1;
        case '*':
        case '/':
            return 2;
        default:
            return 0;
        }
    };
    auto doop = [](char c, uint32_t a, uint32_t b) -> uint32_t {
        switch (c) {
        case '+':
            return a + b;
        case '-':
            return a - b;
        case '*':
            return a * b;
        case '/':
            return b ? a / b : 0xffffffff;
        }
        assert(false);
        return 0;
    };

    struct stack_elem {
        bool isop;
        uint32_t val;
    };
    std::vector<stack_elem> s;
    std::vector<char> opstack;

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
        const char c = arg[i];
        if (auto pre = prec(c); pre) {
            while (!opstack.empty() && prec(c) <= prec(opstack.back())) {
                s.push_back({ true, static_cast<uint32_t>(opstack.back()) });
                opstack.pop_back();
            }
            opstack.push_back(c);
            ++i;
        } else {
            size_t j = i + 1;
            for (; j < size && !prec(arg[j]); ++j)
                ;
            const auto [valid, num] = from_hex(arg.substr(i, j - i));
            if (!valid)
                return { false, 0 };
            s.push_back({ false, num });
            i = j;
        }
    }
    while (!opstack.empty()) {
        s.push_back({ true, static_cast<uint32_t>(opstack.back()) });
        opstack.pop_back();
    }

    std::vector<uint32_t> vals;
    for (auto& se : s) {
        if (se.isop) {
            if (vals.size() < 2)
                return { false, 0 };
            auto b = vals.back();
            vals.pop_back();
            auto a = vals.back();
            vals.pop_back();
            vals.push_back(doop(static_cast<char>(se.val), a, b));
        } else {
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
    std::string hd;
    std::string exe;
    std::string debug_script;
    uint32_t chip_size;
    uint32_t slow_size;
    uint32_t fast_size;
    uint8_t cpu_scale;
    uint32_t floppy_speed;
    bool test_mode;
    bool nosound;
    bool debug;

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, "Command line arguments", 1 };
        sf.handle(rom);
        sf.handle(df0);
        sf.handle(df1);
        sf.handle(hd);
        sf.handle(exe);
        sf.handle(chip_size);
        sf.handle(slow_size);
        sf.handle(fast_size);
        sf.handle(cpu_scale);
    }
};

void usage(const std::string& msg)
{
    std::cerr << "Command line arguments:\n"
        "[-rom rom-file]\n"
        "[-df0/-df1 adf-file]"
        "[-hd file]\n"
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
            else if (get_string_arg("hd", args.hd))
                continue;
            else if (get_string_arg("exe", args.exe))
                continue;
            else if (get_string_arg("rom", args.rom))
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

    if (!args.exe.empty() && !args.df0.empty())
        usage("-exe and -df0 are incompatible");
    return args;
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
        disk_drive df0 {"DF0:"}, df1 {"DF1:"};
        disk_drive* drives[max_drives] = { &df0, &df1 };
        std::unique_ptr<ram_handler> slow_ram;
        std::unique_ptr<fastmem_handler> fast_ram;
        memory_handler mem { cmdline_args.chip_size };
        rom_area_handler rom { mem, read_file(cmdline_args.rom) };
        cia_handler cias { mem, rom, drives };
        custom_handler custom { mem, cias, slow_base + cmdline_args.slow_size, cmdline_args.floppy_speed };
        autoconf_handler autoconf { mem };

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

        real_time_clock rtc { mem };

        m68000 cpu { mem };

        if (!cmdline_args.df0.empty())
            df0.insert_disk(read_file(cmdline_args.df0));
        else if (!cmdline_args.exe.empty()) {
            // Create auto-booting (OFS) disk with the exectuable
            const auto file = read_file(cmdline_args.exe);
            std::string filename = cmdline_args.exe;
            if (auto pos = filename.find_last_of("/\\"); pos != std::string::npos)
                filename = filename.substr(pos + 1);
            auto ss = ":" + filename;
            auto disk = adf::new_disk("test");
            disk.make_dir("S");
            disk.write_file("s/startup-sequence", std::vector<uint8_t>(ss.begin(), ss.end()));
            disk.write_file(filename, file);
            df0.insert_disk(std::vector<uint8_t>(disk.get()));
        }
        if (!cmdline_args.df1.empty())
            df1.insert_disk(read_file(cmdline_args.df1));

        std::cout << "Memory configuration: Chip: " << (cmdline_args.chip_size >> 10) << " KB, Slow: " << (cmdline_args.slow_size >> 10) << " KB, Fast: " << (cmdline_args.fast_size >> 10) << " KB\n";

        //rom_tag_scan(rom.rom());

        // Serial data handler
        std::vector<uint8_t> serdata;
        char escape_sequence[32];
        uint8_t escape_sequence_pos = 0;
        uint8_t crlf = 0;
        custom.set_serial_data_handler([&]([[maybe_unused]] uint8_t numbits, uint8_t data) {
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
            //std::cout << "Serdata: $" << hexfmt(data) << ": " << (char)(isprint(data) ? data : ' ') << "\n";
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
            });

        std::unique_ptr<gui> g;
        if (!cmdline_args.test_mode)
            g = std::make_unique<gui>(graphics_width, graphics_height, std::array<std::string, 4>{ cmdline_args.df0, cmdline_args.df1, "", "" });
        else
            signal(SIGINT, &ctrl_c_handler);
        auto serdata_flush = [&g, &serdata]() {
            if (!serdata.empty()) {
                if (g)
                    g->serial_data(serdata);
                else
                    std::cout << "[SERIAL] " << std::string(serdata.begin(), serdata.end()) << "\n";
                serdata.clear();
            }
        };

        if (g) {
            df0.set_disk_activity_handler([&](uint8_t track, bool write) {
                g->disk_activty(0, track, write);
            });
            df1.set_disk_activity_handler([&](uint8_t track, bool write) {
                g->disk_activty(1, track, write);
            });
        }

        const unsigned steps_per_update = 1000000;
        unsigned steps_to_update = 0;
        std::vector<gui::event> events;
        std::vector<uint8_t> pending_disk;
        uint8_t pending_disk_drive = 0xff;
        uint32_t disk_chosen_countdown = 0;
        enum {wait_none, wait_next_inst, wait_exact_pc, wait_exact_inst, wait_rtx, wait_non_rom_pc, wait_vpos, wait_frames } wait_mode = wait_none;
        uint32_t wait_arg = 0;
        uint32_t chip_cycles_count = 0, cpu_cycles_count = 0;
        uint32_t last_vhpos = 0;
        std::vector<uint32_t> breakpoints;
        bool debug_mode = false;
        std::unique_ptr<std::ifstream> debug_script;
        custom_handler::step_result custom_step {};
        m68000::step_result cpu_step {};
        std::unique_ptr<std::ofstream> trace_file;
        bool new_frame = false;
        bool cpu_active = false;
        uint32_t cycles_todo = 0;
        std::mutex audio_mutex_;
        std::condition_variable audio_buffer_ready_cv;
        std::condition_variable audio_buffer_played_cv;
        int16_t audio_buffer[2][audio_buffer_size];
        bool audio_buffer_ready[2] = { false, false };
        int audio_next_to_play = 0;
        int audio_next_to_fill = 0;
        enum { no_reset, cpu_reset, keyboard_reset } reset = no_reset;

        if (!cmdline_args.debug_script.empty()) {
            debug_script = std::make_unique<std::ifstream>(cmdline_args.debug_script);
            if (!*debug_script || !debug_script->is_open())
                throw std::runtime_error { "Debug script not found: " + cmdline_args.debug_script };
        }

        struct mem_use {
            bus_use use;
            uint32_t addr;
            uint16_t val;
        };
        std::vector<mem_use> dma_usage(vpos_per_field * hpos_per_line / 2);

        cpu.set_cycle_handler([&](uint8_t cycles) {
            assert(cpu_active);
            cycles_todo += cycles;
        });

        auto active_debugger = [&]() {
            if (g)
                g->set_active(false);
            debug_mode = true;
        };

        std::unique_ptr<wavedev> audio;

        //#define WRITE_SOUND
        #ifdef WRITE_SOUND
        std::ofstream sound_out { "c:/temp/sound.raw" };
        #endif

        std::unique_ptr<harddisk> hd;
        if (!cmdline_args.hd.empty()) {
            hd = std::make_unique<harddisk>(mem, cpu_active, cmdline_args.hd);
            autoconf.add_device(hd->autoconf_dev());
        }

        auto handle_machine_state = [&](state_file& sf) {
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
        };

        if (state)
            handle_machine_state(*state);

        auto cstep = [&](bool cpu_waiting) {
            const bool cpu_was_active = cpu_active;
            cpu_active = false;
            custom_step = custom.step(cpu_waiting, cpu_step.current_pc);
            cpu_active = cpu_was_active;

            if (!(custom_step.hpos & 1))
                dma_usage[custom_step.vpos * (hpos_per_line / 2) + custom_step.hpos / 2] = { custom_step.bus, custom_step.dma_addr, custom_step.dma_val };

            ++chip_cycles_count;

            if (wait_mode == wait_vpos && wait_arg == (static_cast<uint32_t>(custom_step.vpos) << 9 | custom_step.hpos)) {
                active_debugger();
                wait_mode = wait_none;
            } else if (!(custom_step.vpos | custom_step.hpos)) {
                new_frame = true;
                if (wait_mode == wait_frames && wait_arg-- == 0) {
                    active_debugger();
                    wait_mode = wait_none;
                }
                
                if (audio)
                {
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
                for (int i = 0; i < audio_samples_per_frame; ++i) {
                    sound_out.write((const char*)&custom_step.audio[i * 2], 2);
                }
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
        };

        auto do_all_custom_cylces = [&]() {
            while (cycles_todo >= cmdline_args.cpu_scale) {
                cstep(false);
                cycles_todo -= cmdline_args.cpu_scale;
                cpu_cycles_count += cmdline_args.cpu_scale;
            }
        };

        if (!cmdline_args.nosound) {
            audio = std::make_unique<wavedev>(audio_sample_rate, audio_buffer_size, [&](int16_t* buf, size_t sz) {
                static_assert(2 * audio_samples_per_frame == audio_buffer_size);
                assert(sz == audio_samples_per_frame);
                {
                    std::unique_lock<std::mutex> lock { audio_mutex_ };
                    if (!audio_buffer_ready[audio_next_to_play]) {
                        std::cerr << "Audio buffer not ready!\n";
                        audio_buffer_ready_cv.wait(lock, [&]() { return audio_buffer_ready[audio_next_to_play]; });
                    }
                    memcpy(buf, audio_buffer[audio_next_to_play], sz * 2 * sizeof(int16_t));
                    audio_buffer_ready[audio_next_to_play] = false;
                    audio_next_to_play = !audio_next_to_play;
                }
                audio_buffer_played_cv.notify_one();
            });
        }

        // Make sure audio stops
        struct at_exit {
            explicit at_exit(std::function<void()> ae)
                : ae_ { ae }
            {
                assert(ae_);
            }

            ~at_exit()
            {
                ae_();
            }

        private:
            std::function<void()> ae_;
        } kill_audio([&]() {
            std::unique_lock<std::mutex> lock { audio_mutex_ };
            audio_buffer_ready[0] = true;
            audio_buffer_ready[1] = true;
            audio_buffer_ready_cv.notify_all();
        });



        if (g) {
            g->set_on_pause_callback([&](bool pause) {
                if (audio)
                    audio->set_paused(pause);
            });
        }

        constexpr uint32_t min_rom_addr = 0x00e0'0000;

        std::vector<memwatch> memwatches;

        mem.set_memory_interceptor([&](uint32_t addr, uint32_t data, uint8_t size, bool write) {
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
                    active_debugger();
                }
            }

            if (!cpu_active)
                return;
            assert(size == 1 || size == 2);
            (void)size;
            if (addr < max_chip_size || (addr >= slow_base && addr < custom_base_addr + custom_mem_size)) {
                // Sync with Agnus
                cycles_todo += 2;
                do_all_custom_cylces();
                while (!custom_step.free_chip_cycle) {
                    ++cpu_cycles_count;
                    cstep(true);
                }
                assert(!(custom_step.hpos & 1));
                auto& du = dma_usage[custom_step.vpos * (hpos_per_line / 2) + custom_step.hpos / 2];
                du = { write ? bus_use::cpu_write : bus_use::cpu_read, addr, write ? static_cast<uint16_t>(data) : mem.hack_peek_u16(addr) };
                cycles_todo += 2;
            } else if (addr >= cia_base_addr && addr < cia_base_addr + cia_mem_size) {
                // Get up to date
                do_all_custom_cylces();
                // Sync with EClock
                //
                // Eclock is low for 6 system clocks and then high for 4 during which the transfer happens
                // And address needs to be latched 3 cycles before EClk rises or falls
                while (custom_step.eclock_cycle != 3) {
                    ++cpu_cycles_count;
                    cstep(false);
                }
                while (custom_step.eclock_cycle) {
                    ++cpu_cycles_count;
                    cstep(false);
                }
            } else {
                cycles_todo += 4;
            }
        });

        cpu.set_read_ipl([&]() {
            ///// HACK: Delay IPL change from Paula by 3 CCKs.
            ///// Reality is more complicated: https://github.com/dirkwhoffmann/vAmiga/issues/274
            ///// And below isn't correct if Paula IPL changes between delay start and end, but let's see...
            ///if (cpu_ipl_delay) {
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
        });

        //cpu.trace(&std::cout);

        if (cmdline_args.debug)
            active_debugger();

        for (bool quit = false; !quit;) {
            try {
                if (!events.empty()) {
                    auto evt = events[0];
                    events.erase(events.begin());
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
                        std::cout << drives[evt.disk_inserted.drive]->name() << " Ejecting\n";
                        drives[evt.disk_inserted.drive]->insert_disk(std::vector<uint8_t>()); // Eject any existing disk
                        if (evt.disk_inserted.filename[0]) {
                            disk_chosen_countdown = 25; // Give SW (e.g. Defender of the Crown) time to recognize that the disk has changed
                            std::cout << "Reading " << evt.disk_inserted.filename << "\n";
                            pending_disk = read_file(evt.disk_inserted.filename);
                            pending_disk_drive = evt.disk_inserted.drive;
                        }
                        // Hack overwrite cmdline args for disk file
                        if (evt.disk_inserted.drive == 0) {
                            cmdline_args.df0 = evt.disk_inserted.filename;
                        } else {
                            assert(evt.disk_inserted.drive == 1);
                            cmdline_args.df1 = evt.disk_inserted.filename;
                        }
                        break;
                    case gui::event_type::debug_mode:
                        debug_mode = true;
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

                if (debug_mode) {
                    const auto& s = cpu.state();
                    if (g) {
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
                        if (args.empty())
                            continue;
                        assert(!args[0].empty());
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
                            break;
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
                        } else if (args[0] == "q") {
                            quit = true;
                            break;
                        } else if (args[0] == "r") {
                            cpu.show_state(std::cout);
                        } else if (args[0] == "t") {
                            wait_arg = ~0U;
                            if (args.size() > 1) {
                                auto fh = from_hex(args[1]);
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
                                auto fh = from_hex(args[1]);
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
                                auto vh = from_hex(args[1]);
                                if (!vh.first || vh.second >= vpos_per_field)
                                    goto vcmdinvalidargs;
                                v = vh.second;
                                if (args.size() > 2) {
                                    auto hh = from_hex(args[2]);
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
                        } else if (args[0] == "write_mem") {
                            if (args.size() > 1) {
                                // TODO: Also fast and slow mem (if present)
                                std::ofstream f(args[1], std::ofstream::binary);
                                if (f) {
                                    std::cout << "Warning: Only writing chipmem\n";
                                    f.write(reinterpret_cast<const char*>(mem.ram().data()), mem.ram().size());
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
                                state_file sf{ state_file::dir::save, args[1] };
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
                            const auto [valid, num] = get_simple_expr(args[0].substr(1));
                            if (valid)
                                std::cout << "$" << hexfmt(num) << " = %" << binfmt(num) << " = " << num << "\n";
                            else
                                std::cout << "Invalid expression\n";                                
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

                cpu_active = true;
                cpu_step = cpu.step();
                cpu_active = false;
                   
                if (cpu_step.stopped) {
                    do {
                        cstep(false);
                    } while (!custom.current_ipl() && !new_frame && !debug_mode);
                } else {
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
                        if (mem.read_u16(cpu_step.current_pc)  == 0x4ef9 && mem.read_u32(cpu_step.current_pc + 2) >= min_rom_addr)
                            goto check_breakpoint;
                        break;
                    case wait_vpos: // Handled in cstep
                    case wait_frames:
                        goto check_breakpoint;
                    }

                    active_debugger();
                    cpu.trace(nullptr);
                    wait_mode = wait_none;

check_breakpoint:
                    if (auto it = std::find(breakpoints.begin(), breakpoints.end(), cpu_step.current_pc); it != breakpoints.end()) {
                        active_debugger();
                        std::cout << "Breakpoint hit\n";
                    }

                    if (cpu_step.instruction == reset_instruction_num)
                        reset = cpu_reset;
                    do_all_custom_cylces();
                }

                if (reset != no_reset) {
                    std::cout << (reset == cpu_reset ? "CPU reset" : "Keyboard reset") << "\n";
                    if (!debug_mode) {
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
                    serdata_flush();
                    steps_to_update = steps_per_update;
                    if (g) {
                        g->update_image(custom_step.frame);
                        g->led_state(cias.power_led_on());
                        auto new_events = g->update();
                        events.insert(events.end(), new_events.begin(), new_events.end());
                    } else if (ctrl_c) {
                        ctrl_c = false;
                        signal(SIGINT, &ctrl_c_handler);
                        active_debugger();
                    }
                }
            } catch (const std::exception& e) {
                cpu.show_state(std::cerr);
                std::cerr << "\n" << e.what() << "\n\n";
                serdata_flush();
                if (cmdline_args.test_mode)
                    throw;
                active_debugger();
            }
        }

        std::cout << "Exited\n";
        cpu.show_state(std::cout);

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
