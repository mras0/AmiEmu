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
        //const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.2 (A500-A2000).rom";
        const char* const rom_file = "../../rom.bin";
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

        uint64_t stop_inst = ~0ULL;
        while (cpu.instruction_count() != stop_inst) {
            try {
                cpu.step(custom.current_ipl());
                custom.step();
                if (auto f = custom.new_frame())
                    g.update_image(f);
                //if (cpu.instruction_count() == 2727715 - 10)
                //    cpu.trace(&std::cout);
                //if (cpu.state().pc == 0xFC0C00) {
                //    cpu.trace(&std::cout);
                //    stop_inst = cpu.instruction_count() + 5;
                //}
                //
                if (!steps_to_update--) {
                    if (!g.update())
                        break;
                    steps_to_update = steps_per_update;
                }
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
