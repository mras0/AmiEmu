#include "rtc.h"
#include "memory.h"
#include "ioutil.h"
#include "state_file.h"
#include <chrono>
#include <iostream>
#include <ctime>

#ifndef WIN32
/*errno_t*/void localtime_s(
    struct tm* const tmDest,
    time_t const* const sourceTime)
{
    localtime_r(sourceTime, tmDest);
}
#endif

class real_time_clock::impl final : public memory_area_handler {
public:
    explicit impl(memory_handler& mem)
    {
        mem.register_handler(*this, 0xdc0000, 0x1000);
    }

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, "RTC", 1 };
        sf.handle_blob(cr_, sizeof(cr_));
    }

private:
    static constexpr uint8_t crd_hold = 1;
    static constexpr uint8_t crd_busy = 2;
    static constexpr uint8_t crd_irq = 4;
    static constexpr uint8_t crd_30sec = 8;
    static constexpr uint8_t cre_mask = 1;
    static constexpr uint8_t cre_stnd = 2;
    static constexpr uint8_t cre_t0 = 4;
    static constexpr uint8_t cre_t1 = 8;
    static constexpr uint8_t crf_rest = 1;
    static constexpr uint8_t crf_stop = 2;
    static constexpr uint8_t crf_24hr = 4;
    static constexpr uint8_t crf_test = 8;
    uint8_t cr_[3] = { 0, 0, crf_24hr };

    void reset() override
    {
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        if (offset < 0x40) {
            time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            struct tm tm;
            localtime_s(&tm, &tt);

            if (!(offset & 1))
                return 0x00;
            const uint8_t reg = static_cast<uint8_t>(offset >> 2);
            switch (reg) {
            case 0x00:
                return static_cast<uint8_t>(tm.tm_sec % 10);
            case 0x01:
                return static_cast<uint8_t>(tm.tm_sec / 10);
            case 0x02:
                return static_cast<uint8_t>(tm.tm_min % 10);
            case 0x03:
                return static_cast<uint8_t>(tm.tm_min / 10);
            case 0x04:
                return static_cast<uint8_t>(tm.tm_hour % 10);
            case 0x05:
                return static_cast<uint8_t>(tm.tm_hour / 10); // TODO AM/PM mode
            case 0x06:
                return static_cast<uint8_t>(tm.tm_mday % 10);
            case 0x07:
                return static_cast<uint8_t>(tm.tm_mday / 10);
            case 0x08:
                return static_cast<uint8_t>((tm.tm_mon + 1) % 10);
            case 0x09:
                return static_cast<uint8_t>((tm.tm_mon + 1) / 10);
            case 0x0a:
                return static_cast<uint8_t>(tm.tm_year % 10);
            case 0x0b:
                return static_cast<uint8_t>((tm.tm_year / 10) & 0xf);
            case 0x0c:
                return static_cast<uint8_t>(tm.tm_wday);
            case 0x0d:
            case 0x0e:
            case 0x0f:
                return cr_[reg - 0x0d];
            }
        }
        std::cerr << "[RTC] Unhandled read from offset $" << hexfmt(offset) << "\n";
        return 0xff;
    }

    uint16_t read_u16(uint32_t addr, uint32_t offset) override
    {
        return read_u8(addr, offset) << 8 | read_u8(addr + 1, offset + 1);
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        if (offset < 0x40) {
            if (!(offset & 1))
                return;
            const uint8_t reg = static_cast<uint8_t>(offset >> 2);
            std::cerr << "[RTC] Unhandled write to offset $" << hexfmt(offset) << " val $" << hexfmt(val, 1) << " (register $" << hexfmt(reg, 1) << ")\n";
            return;
        }
        std::cerr << "[RTC] Unhandled write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
    }

    void write_u16(uint32_t addr, uint32_t offset, uint16_t val) override
    {
        write_u8(addr, offset, static_cast<uint8_t>(val >> 8));
        write_u8(addr + 1, offset + 1, static_cast<uint8_t>(val));
    }
};



real_time_clock::real_time_clock(memory_handler& mem) : impl_{ new impl{mem} }
{
}

real_time_clock::~real_time_clock() = default;

void real_time_clock::handle_state(class state_file& sf)
{
    impl_->handle_state(sf);
}
