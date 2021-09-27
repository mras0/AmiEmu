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
        const char* const rom_file = "../../rom.bin";
        memory_handler mem { 1U<<20 };
        rom_area_handler rom { mem, read_file(rom_file) };
        cia_handler cias { mem, rom };
        custom_handler custom { mem };
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

        for (;;) {
            try {
                cpu.step(custom.current_ipl());
                custom.step();
                if (auto f = custom.new_frame())
                    g.update_image(f);
                //if (cpu.state().pc == 0xFC00E2) // After delay loop
                //    cpu.trace(true);
                //if (cpu.state().pc == 0xFC046C)
                //    cpu.trace(true);
                //if (cpu.instruction_count() == 1524631 - 25)
                //    cpu.trace(true);
                //else if (cpu.instruction_count() == 7110+2)
                //    break;


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
