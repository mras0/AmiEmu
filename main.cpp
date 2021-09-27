#include <iostream>
#include <stdint.h>
#include <stdexcept>
#include <cassert>

#include "ioutil.h"
#include "instruction.h"
#include "disasm.h"
#include "memory.h"
#include "cia.h"
#include "custom.h"
#include "cpu.h"
#include "disk_drive.h"
#include "gui.h"

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

int main(int argc, char* argv[])
{
    try {
        //const char* const rom_file = "../../Misc/DiagROM/DiagROM";
        const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.3 A500.rom";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.2 (A500-A2000).rom";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 2.0 (A600).rom";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 3.1 (A600).rom";
        //const char* const rom_file = "../../rom.bin";
        //const char* const rom_file = "../../aros.rom";
        //const char* const rom_file = "../../Misc/simple-amiga-rom/out/rom.bin";
        disk_drive df0 {};
        disk_drive* drives[max_drives] = { &df0 };
        memory_handler mem { 1U << 20 };
        rom_area_handler rom { mem, read_file(rom_file) };
        cia_handler cias { mem, rom, drives };
        custom_handler custom { mem, cias };

        //const auto slow_base = 0xC00000, slow_size = 0xDC0000 - 0xC00000;
        //ram_handler slow_ram { slow_size }; // For KS1.2
        //mem.register_handler(slow_ram, slow_base, slow_size);

        m68000 cpu { mem };

        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "-trace"))
                cpu.trace(&std::cout);
            else
                throw std::runtime_error { "Unrecognized command line parameter: " + std::string { argv[i] } };
        }

        df0.insert_disk(read_file("../../Misc/AmigaWorkbench/Workbench13.adf"));

        //rom_tag_scan(rom.rom());

        // Serial data handler
        std::vector<uint8_t> serdata;
        char escape_sequence[32];
        uint8_t escape_sequence_pos = 0;
        uint8_t crlf = 0;
        custom.set_serial_data_handler([&](uint8_t numbits, uint8_t data) { 
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

        const unsigned steps_per_update = 10000;
        unsigned steps_to_update = 0;
        int trace_cnt = -1;
        std::vector<gui::event> events;
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
                    default:
                        assert(0);
                    }
                }

                cpu.step(custom.current_ipl());
                custom.step();

                if (auto f = custom.new_frame()) {
                    g.update_image(f);
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
                const int trace_len = 1000;
                //f 00fc0f54
                // w 0 dff058 2 w
                //if (cpu.state().pc == 0x00FE8DCE) {
                //    cpu.trace(&std::cout);
                //    trace_cnt = trace_len;
                //}
                //if (cpu.instruction_count() == 6433261-10) {
                //    cpu.trace(&std::cout);
                //    trace_cnt = trace_len+1;
                //}
                if (trace_cnt >= 0) {
                    if (trace_cnt-- == 0)
                        break;
                }
            } catch (const std::exception& e) {
                cpu.show_state(std::cerr);
                std::cerr << "\n" << e.what() << "\n\n";

                // Wait for GUI to exit
                serdata_flush();
                for (;;) {
                    for (const auto& evt : g.update())
                        if (evt.type == gui::event_type::quit)
                            throw;
                }
            }
        }

        std::cout << "Exited after " << cpu.instruction_count() << " instructions\n";


    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
