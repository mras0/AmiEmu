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
#include "gui.h"

int main(int argc, char* argv[])
{
    try {

        //const char* const rom_file = "../../Misc/DiagROM/DiagROM";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.3 A500.rom";
        const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.2 (A500-A2000).rom";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 2.0 (A600).rom";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 3.1 (A600).rom";
        //const char* const rom_file = "../../rom.bin";
        //const char* const rom_file = "../../aros.rom";
        memory_handler mem { 1U << 20 };
        rom_area_handler rom { mem, read_file(rom_file) };
        cia_handler cias { mem, rom };
        custom_handler custom { mem, cias };

        const auto slow_base = 0xC00000, slow_size = 0xDC0000 - 0xC00000;
        ram_handler slow_ram { slow_size }; // For KS1.2
        mem.register_handler(slow_ram, slow_base, slow_size);

        m68000 cpu { mem };

        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "-trace"))
                cpu.trace(&std::cout);
            else
                throw std::runtime_error { "Unrecognized command line parameter: " + std::string { argv[i] } };
        }

        int n = 0;
        for (uint32_t a = 0x00FE8CF8; a; a -= 2) {
            if (mem.read_u16(a) == 0x4e75) {
                std::cout << hexfmt(a) << "\n";
                if (++n == 2)
                    assert(0);
            }
        }

        //cpu.trace(&std::cout);


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
                const int trace_len = 50;
                //f 00fc0f54
                // w 0 dff058 2 w
                //if (cpu.state().pc == 0x00fc0f54) {
                //    cpu.trace(&std::cout);
                //    trace_cnt = 1;
                //}
                //if (cpu.instruction_count() == 2839201) {
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
