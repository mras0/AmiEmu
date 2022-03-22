#include "debug_board.h"
#include "memory.h"
#include "ioutil.h"
#include "autoconf.h"
#include "state_file.h"
//#include <stdexcept>
//#include <fstream>
#include <iostream>
//#include <cstring>

namespace {

#include "debug_exprom.h"

}

class debug_board::impl final : public memory_area_handler, public autoconf_device {
public:
    explicit impl(memory_handler& mem)
        : autoconf_device { mem, *this, config }
        , mem_ { mem }
    {
    }

private:
    static constexpr board_config config {
        .type = ERTF_DIAGVALID,
        .size = 64 << 10,
        .product_number = 0x88,
        .hw_manufacturer = 1338,
        .serial_no = 1,
        .rom_vector_offset = EXPROM_BASE,
    };

    memory_handler& mem_;

    void reset() override
    {
    }

    void handle_state(state_file& sf) override
    {
        const state_file::scope scope { sf, "DebugBoard", 1 };
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset < config.rom_vector_offset + sizeof(debug_exprom)) {
            return debug_exprom[offset - config.rom_vector_offset];
        }

        std::cerr << "debug_board: Read U8 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset + 1 < config.rom_vector_offset + sizeof(debug_exprom)) {
            offset -= config.rom_vector_offset;
            return static_cast<uint16_t>(debug_exprom[offset] << 8 | debug_exprom[offset + 1]);
        }

        std::cerr << "debug_board: Read U16 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        std::cerr << "debug_board: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
    }

    uint16_t ptr_hi_ = 0;

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        if (offset == EXPROM_BASE) {
            ptr_hi_ = val;
            return;
        } else if (offset == EXPROM_BASE + 2) {
            print_task(ptr_hi_ << 16 | val);
            return;
        }
        std::cerr << "debug_board: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
    }

    std::string read_string(uint32_t ptr)
    {
        std::string s; 
        for (uint32_t i = 0; i < 64; ++i) {
            const uint8_t b = mem_.read_u8(ptr + i);
            if (!b)
                break;
            s.push_back(b);
        }
        return s;
    }

    void print_task(uint32_t task_ptr)
    {
        constexpr uint32_t ln_Type = 0x0008;
        constexpr uint32_t ln_Name = 0x000a;
        //constexpr uint8_t NT_TASK    = 1; // Exec task
        constexpr uint8_t NT_PROCESS = 13; // AmigaDOS Process
        //constexpr uint32_t pr_CLI = 0x00ac;
        constexpr uint32_t pr_ReturnAddr = 0x00b0;
        //constexpr uint32_t cli_CommandName = 0x0010;

        const auto type = mem_.read_u8(task_ptr + ln_Type);

        std::cout << "debug_board: New task start at $" << hexfmt(task_ptr) << " TYPE=$" << hexfmt(type) << " Name=\"" << read_string(mem_.read_u32(task_ptr + ln_Name)) << "\"\n";
        if (type != NT_PROCESS)
            return;
        std::cout << "Maybe add breakpoint at $" << hexfmt(task_ptr + pr_ReturnAddr) << "\n";
    }
};

debug_board::debug_board(memory_handler& mem)
    : impl_{ new impl(mem) }
{
}

debug_board::~debug_board() = default;

autoconf_device& debug_board::autoconf_dev()
{
    return *impl_;
}
