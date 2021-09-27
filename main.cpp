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
        const char* const rom_file = "../../Misc/DiagROM/DiagROM";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.3 A500.rom";
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.2 (A500-A2000).rom";
        //const char* const rom_file = "../../rom.bin";
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


        gui g {graphics_width, graphics_height};
        const unsigned steps_per_update = 10000;
        unsigned steps_to_update = 0;

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
                    auto new_events = g.update();
                    assert(events.empty()); // events are comming too fast to process
                    events.insert(events.end(), new_events.begin(), new_events.end());
                    steps_to_update = steps_per_update;
                }
                //if (cpu.instruction_count() == 34289275 - 10)
                //    cpu.trace(&std::cout);
            } catch (...) {
                cpu.show_state(std::cerr);
                throw;
            }
        }        


    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
