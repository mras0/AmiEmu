#include <iostream>
#include <stdint.h>
#include <stdexcept>
#include <cassert>

//#define TRACE_LOG

#ifdef TRACE_LOG
#include <sstream>
#endif

#include "ioutil.h"
#include "instruction.h"
#include "disasm.h"
#include "memory.h"
#include "cia.h"
#include "custom.h"
#include "cpu.h"
#include "disk_drive.h"
#include "gui.h"

namespace {
std::string trim(const std::string& line)
{
    const size_t l = line.length();
    size_t s, e;
    for (s = 0; s < l && isspace(line[s]); ++s)
        ;
    for (e = l; e-- && isspace(line[e]);)
        ;
    return line.substr(s, e + 1 - s);
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

std::pair<bool, uint32_t> from_hex(const std::string& s)
{
    if (s.empty() || s.length() > 8)
        return { false, 0 };
    uint32_t val = 0;
    for (const char c : s) {
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return { false, 0 };
        val = val << 4 | d;
    }
    return { true, val };
}

std::tuple<bool, uint32_t, uint32_t> get_addr_and_lines(const std::vector<std::string>& args, uint32_t def_addr, uint32_t def_lines)
{
    uint32_t addr = def_addr, lines = def_lines;

    if (args.size() > 1) {
        if (args.size() > 1) {
            auto fh = from_hex(args[1]);
            if (!fh.first) {
                std::cout << "Invalid address \"" << args[1] << "\"\n";
                return { false, def_addr, def_lines };
            }
            addr = fh.second;
            if (args.size() > 2) {
                if (fh = from_hex(args[2]); fh.first)
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

} // unnamed namespace


#if 0
void rom_tag_scan(const std::vector<uint8_t>& rom)
{
    const uint32_t rom_base = static_cast<uint32_t>(0x1000000 - rom.size());
    constexpr uint32_t romtag = 0x4AFC;

    /*
    FC00B6  4AFC                        RTC_MATCHWORD   (start of ROMTAG marker)
FC00B8  00FC00B6                    RT_MATCHTAG     (pointer RTC_MATCHWORD)
FC00BC  00FC323A                    RT_ENDSKIP      (pointer to end of code)
FC00C0  00                          RT_FLAGS        (no flags)
FC00C1  21                          RT_VERSION      (version number)
FC00C2  09                          RT_TYPE         (NT_LIBRARY)
FC00C3  78                          RT_PRI          (priority = 126)
FC00C4  00FC00A8                    RT_NAME         (pointer to name)
FC00C8  00FC0018                    RT_IDSTRING     (pointer to ID string)
FC00CC  00FC00D2                    RT_INIT         (execution address)* 
    */
    
    for (uint32_t offset = 0; offset < rom.size() - 6; offset += 2) {
        if (get_u16(&rom[offset]) != romtag)
            continue;
        if (get_u32(&rom[offset + 2]) != offset + rom_base)
            continue;
        std::cout << "Found tag at $" << hexfmt(rom_base + offset) << "\n";
        const uint32_t endskip = get_u32(&rom[offset + 6]);
        const uint8_t flags = rom[offset + 10];
        const uint8_t version = rom[offset + 11];
        const uint8_t type = rom[offset + 12];
        const uint8_t priority = rom[offset + 13];
        const uint32_t name_addr = get_u32(&rom[offset + 14]);
        const uint32_t id_addr = get_u32(&rom[offset + 18]);
        const uint32_t init_addr = get_u32(&rom[offset + 22]);

        std::cout << "  End=$" << hexfmt(endskip) << "\n";
        std::cout << "  Flags=$" << hexfmt(flags) << " version=$" << hexfmt(version) << " type=$" << hexfmt(type) << " priority=$" << hexfmt(priority) << "\n";
        std::cout << "  Name='" << reinterpret_cast<const char*>(&rom[name_addr - rom_base]) << "'\n";
        std::cout << "  ID='" << reinterpret_cast<const char*>(&rom[name_addr - rom_base]) << "'\n";
        std::cout << "  Init=$" << hexfmt(init_addr) << "\n";
    }
}
#endif

struct command_line_arguments {
    std::string rom;
    std::string df0;
};

void usage(const std::string& msg)
{
    std::cerr << "Command line arguments: -rom rom-file [-df0 adf-file] [-help]\n";
    throw std::runtime_error { msg };
}

command_line_arguments parse_command_line_arguments(int argc, char* argv[])
{
    command_line_arguments args;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-df0")) {
            if (++i == argc)
                usage("Missing df0 argument");
            if (!args.df0.empty())
                usage("Multiple df0 arguments");
            args.df0 = argv[i];
        } else if (!strcmp(argv[i], "-rom")) {
            if (++i == argc)
                usage("Missing rom argument");
            if (!args.rom.empty())
                usage("Multiple rom arguments");
            args.rom = argv[i];
        } else {
            usage("Unrecognized command line parameter: " + std::string { argv[i] });
        }
    }
    if (args.rom.empty())
        usage("No ROM selected");
    return args;
}

int main(int argc, char* argv[])
{
    try {
        const auto cmdline_args = parse_command_line_arguments(argc, argv);
        disk_drive df0 {};
        disk_drive* drives[max_drives] = { &df0 };
        memory_handler mem { 1U << 20 };
        rom_area_handler rom { mem, read_file(cmdline_args.rom) };
        cia_handler cias { mem, rom, drives };
        custom_handler custom { mem, cias };

        //const auto slow_base = 0xC00000, slow_size = 0xDC0000 - 0xC00000;
        //ram_handler slow_ram { slow_size }; // For KS1.2
        //mem.register_handler(slow_ram, slow_base, slow_size);

        m68000 cpu { mem };

        if (!cmdline_args.df0.empty())
            df0.insert_disk(read_file(cmdline_args.df0));

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

        gui g { graphics_width, graphics_height };
        auto serdata_flush = [&g, &serdata]() {
            if (!serdata.empty()) {
                g.serial_data(serdata);
                serdata.clear();
            }
        };

        df0.set_disk_activity_handler([](uint8_t track, bool write) {
            std::cout << "DF0: " << (write ? "Write" : "Read") << " track $" << hexfmt(track) << "\n";
        });

#ifdef TRACE_LOG
        std::ostringstream oss;
        constexpr size_t pc_log_size = 0x1000;
        std::string pc_log[pc_log_size];
        size_t pc_log_pos = 0;
        constexpr size_t trace_start_inst = 200'000'000;
#endif

        const unsigned steps_per_update = 10000;
        unsigned steps_to_update = 0;
        std::vector<gui::event> events;
        std::vector<uint8_t> pending_disk;
        uint32_t disk_chosen_countdown = 0;
        constexpr uint32_t invalid_pc = ~0U;
        bool debug_mode = false;
        uint32_t wait_for_pc = invalid_pc;

        for (bool quit = false; !quit;) {
            try {
                if (!events.empty()) {
                    auto evt = events[0];
                    events.erase(events.begin());
                    switch (evt.type) {
                    case gui::event_type::quit:
                        quit = true;
                        break;
                    case gui::event_type::keyboard:
                        cias.keyboard_event(evt.keyboard.pressed, evt.keyboard.scancode);
                        break;
                    case gui::event_type::mouse_button:
                        if (evt.mouse_button.left)
                            cias.set_lbutton_state(evt.mouse_button.pressed);
                        else
                            custom.set_rbutton_state(evt.mouse_button.pressed);
                        break;
                    case gui::event_type::mouse_move:
                        custom.mouse_move(evt.mouse_move.dx, evt.mouse_move.dy);
                        break;
                    case gui::event_type::disk_inserted:
                        std::cout << "DF0: Ejecting\n";
                        df0.insert_disk(std::vector<uint8_t>()); // Eject any existing disk
                        if (evt.disk_inserted.filename[0]) {
                            disk_chosen_countdown = 25; // Give SW (e.g. Defender of the Crown) time to recognize that the disk has changed
                            std::cout << "Reading " << evt.disk_inserted.filename << "\n";
                            pending_disk = read_file(evt.disk_inserted.filename);
                        }
                        break;
                    case gui::event_type::debug_mode:
                        debug_mode = true;
                        break;
                    default:
                        assert(0);
                    }
                }

                if (wait_for_pc != invalid_pc && cpu.state().pc == wait_for_pc) {
                    g.set_active(false);
                    debug_mode = true;
                    wait_for_pc = invalid_pc;
                }

                if (debug_mode) {
                    const auto& s = cpu.state();
                    auto show_state = [&]() {
                        print_cpu_state(std::cout, s);
                        disasm_stmts(mem, s.pc, 1);
                    };

                    show_state();
                    uint32_t disasm_pc = s.pc, hexdump_addr = 0;
                    for (;;) {
                        std::cout << "> " << std::flush;
                        std::string line;
                        if (!std::getline(std::cin, line)) {
                            assert(0);
                            debug_mode = false;
                            break;
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
                        } else if (args[0] == "g") {
                            debug_mode = false;
                            g.set_active(true);
                            break;
                        } else if (args[0] == "m") {
                            auto [valid, addr, lines] = get_addr_and_lines(args, hexdump_addr, 20);
                            if (valid) {
                                std::vector<uint8_t> data(lines * 16);
                                for (size_t i = 0; i < lines * 8; ++i)
                                    put_u16(&data[2 * i], mem.read_u16(static_cast<uint32_t>(addr + 2 * i)));
                                hexdump16(std::cout, addr, data.data(), data.size());
                                hexdump_addr = addr + lines * 16;
                            }
                        } else if (args[0] == "q") {
                            quit = true;
                            break;
                        } else if (args[0] == "r") {
                            show_state();
                        } else if (args[0] == "t") {
                            break;
                        } else if (args[0] == "z") {
                            wait_for_pc = s.pc + instructions[mem.read_u16(s.pc)].ilen * 2;
                            debug_mode = false;
                            g.set_active(true);
                            break;
                        } else {
                            std::cout << "Unknown command \"" << args[0] << "\"\n";
                        }
                    }
                }

                cpu.step(custom.current_ipl());
                custom.step();

#ifdef TRACE_LOG
                if (cpu.instruction_count() == trace_start_inst) {
                    std::cout << "Starting trace\n";
                    cpu.trace(&oss);
                } else if (cpu.instruction_count() > trace_start_inst) {
                    pc_log[pc_log_pos] = oss.str();
                    oss.str("");
                    oss.clear();
                    pc_log_pos = (pc_log_pos + 1) % pc_log_size;
                }
#endif

                if (auto f = custom.new_frame()) {
                    g.update_image(f);
                    if (disk_chosen_countdown) {
                        if (--disk_chosen_countdown == 0) {
                            std::cout << "DF0: Inserting disk\n";
                            df0.insert_disk(std::move(pending_disk));
                        }
                    }
                    goto update;
                }
                if (!steps_to_update--) {
                update:
                    g.led_state(cias.power_led_on());
                    serdata_flush();
                    auto new_events = g.update();
                    assert(events.empty()); // events are comming too fast to process
                    events.insert(events.end(), new_events.begin(), new_events.end());
                    steps_to_update = steps_per_update;
                }
            } catch (const std::exception& e) {
#ifdef TRACE_LOG
                for (size_t i = 0; i < sizeof(pc_log) / sizeof(*pc_log); ++i) {
                    std::cerr << pc_log[(pc_log_pos + i) % pc_log_size] << "\n";
                }
#endif
                cpu.show_state(std::cerr);
                std::cerr << "\n" << e.what() << "\n\n";

#ifndef TRACE_LOG
                serdata_flush();
                debug_mode = true;
                g.set_active(false);
#else
                throw;
#endif
            }
        }

        std::cout << "Exited\n";
        cpu.show_state(std::cout);

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
