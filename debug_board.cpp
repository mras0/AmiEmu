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
    {
    }

    void set_callbacks(const ptr1_cb& init, const ptr2_cb& add_task, const ptr1_cb& rem_task, const ptr2_cb& load_seg)
    {
        init_ = init;
        add_task_ = add_task;
        rem_task_ = rem_task;
        load_seg_ = load_seg;
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

    ptr1_cb init_;
    ptr2_cb add_task_;
    ptr1_cb rem_task_;
    ptr2_cb load_seg_;
    uint32_t ptr1_ = 0;
    uint32_t ptr2_ = 0;

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

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        if (offset == EXPROM_BASE) {
            ptr1_ = val << 16;
            return;
        } else if (offset == EXPROM_BASE + 2) {
            ptr1_ |= val;
            return;
        } else if (offset == EXPROM_BASE + 4) {
            ptr2_ = val << 16;
            return;
        } else if (offset == EXPROM_BASE + 6) {
            ptr2_ |= val;
            return;
        } else if (offset == EXPROM_BASE + 16) {
            switch (val) {
            case 1:
                if (init_)
                    init_(ptr1_);
                return;
            case 2:
                if (add_task_)
                    add_task_(ptr1_, ptr2_);
                return;
            case 3:
                if (rem_task_)
                    rem_task_(ptr1_);
                return;
            case 4:
                if (load_seg_)
                    load_seg_(ptr1_, ptr2_);
                return;
            case 5:
                if (load_seg_)
                    load_seg_(ptr1_ | 0x80000000, ptr2_);
                return;
            }
        }
        std::cerr << "debug_board: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
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

void debug_board::set_callbacks(const ptr1_cb& init, const ptr2_cb& add_task, const ptr1_cb& rem_task, const ptr2_cb& load_seg)
{
    impl_->set_callbacks(init, add_task, rem_task, load_seg);
}
