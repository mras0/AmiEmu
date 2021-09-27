#include "cia.h"
#include "ioutil.h"
#include <cassert>
#include <iostream>

namespace {

// CIAA Port A: /FIR1 /FIR0  /RDY /TK0  /WPRO /CHNG /LED  OVL
// CIAA Port B: Parallel port

// CIAB Port A: /DTR  /RTS  /CD   /CTS  /DSR   SEL   POUT  BUSY
// CIAB Port B: /MTR  /SEL3 /SEL2 /SEL1 /SEL0 /SIDE  DIR  /STEP

static constexpr const char* const regnames[16] = {
    "pra",
    "prb",
    "ddra",
    "ddrb",
    "talo",
    "tahi",
    "tblo",
    "tbhi",
    "todlo",
    "todmid",
    "todhi",
    "unused",
    "sdr",
    "icr",
    "cra",
    "crb",
};

enum regnum {
    pra     = 0x0, // Port A
    prb     = 0x1, // Port B
    ddra    = 0x2, // Direction for port A (0=input, 1=output)
    ddrb    = 0x3, // Direction for port B (input -> read as 0xff if not driven due to pull-ups)
    talo    = 0x4, // Timer A low byte (.715909 Mhz NTSC; .709379 Mhz PAL)
    tahi    = 0x5, // Timer A high byte
    tblo    = 0x6, // Timer B low byte
    tbhi    = 0x7, // Timer B high byte
    todlo   = 0x8, // Event counter bits 7-0 (CIAA: vsync, CIAB: hsync)
    todmid  = 0x9, // Event counter bits 15-8
    todhi   = 0xA, // Event counter bits 23-16
    unused  = 0xB, // not connected
    sdr     = 0xC, // Serial data register (CIAA: connected to keyboard, CIAB: not used)
    icr     = 0xD, // Interrupt control register
    cra     = 0xE, // Control register A
    crb     = 0xF, // Control register B
};

}

// Handles both MOS Technology 8520 Complex Interface Adapter chips
// CIAA can generate INT2, CIAB can generate INT6
class cia_handler::impl : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler, rom_area_handler& rom_handler)
        : mem_handler_ { mem_handler }
        , rom_handler_ { rom_handler }
        , regs_ {}
    {
        assert(get_port_output(0, 0) == 0xff);
        // Since all ports are set to input, OVL in CIA pra is high -> OVL set
        mem_handler_.register_handler(*this, 0xBF0000, 0x10000);
        rom_handler_.set_overlay(true);
    }

    uint8_t read_u8(uint32_t addr, uint32_t) override
    {
        if (valid_address(addr)) {
            return handle_read(!(addr & 1), (addr >> 8) & 0xf);
        }
        std::cerr << "To handle 8-bit read from CIA: addr=$" << hexfmt(addr) << "\n";
        assert(0);
        return 0xFF;
    }

    uint16_t read_u16(uint32_t addr, uint32_t) override
    {
        std::cerr << "Invalid 16-bit read from CIA: addr=$" << hexfmt(addr) << "\n";
        assert(0);
        return 0xFF;
    }

    void write_u8(uint32_t addr, uint32_t, uint8_t val) override
    {
        if (valid_address(addr)) {
            handle_write(!(addr & 1), (addr >> 8) & 0xf, val);
        } else {
            std::cerr << "[CIA] Ignoring Invalid CIA byte write : " << hexfmt(addr) << " val = $" << hexfmt(val) << "\n";
        }
    }

    void write_u16(uint32_t addr, uint32_t, uint16_t val) override
    {
        std::cerr << "Invalid 16-bit write to CIA: " << hexfmt(addr) << " val = $" << hexfmt(val) << "\n";
        assert(0);
    }

private:
    memory_handler& mem_handler_;
    rom_area_handler& rom_handler_;
    uint8_t regs_[2][16];

    static constexpr bool valid_address(uint32_t addr)
    {
        if ((addr & 1) && !(addr & 0xF0) && addr >= 0xBFE001 && addr <= 0xBFEF01) {
            return true;
        } else if (!(addr & 1) && !(addr & 0xF0) && addr >= 0xBFD000 && addr <= 0xBFDF00) {
            return true;
        }
        return false;
    }

    uint8_t handle_read(uint8_t idx, uint8_t reg)
    {
        assert(idx < 2 && reg < 16);
        switch (reg) {
        case pra:
        case prb:
            //std::cerr << "[CIA] TODO: Not handling input pins for CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << "\n";
            return regs_[idx][reg];
        case ddra:
        case ddrb:
            return regs_[idx][reg];
        default:
            std::cerr << "[CIA] TODO: Handle read from CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << "\n";
        }
        return 0xFF;
    }

    void handle_write(uint8_t idx, uint8_t reg, uint8_t val)
    {
        assert(idx < 2 && reg < 16);
        const uint8_t port_a_before = get_port_output(idx, 0);

        switch (reg) {
        case pra:
        case prb:
        case ddra:
        case ddrb:
            regs_[idx][reg] = val;
            break;
        default:
            std::cerr << "[CIA] Ignoring write to CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << " val $" << hexfmt(val) << "\n";
            return;
        }

        if (idx == 0) {
            const uint8_t port_a_after = get_port_output(idx, 0);
            const uint8_t port_a_diff = port_a_before ^ port_a_after;
            // OVL changed
            if (port_a_diff & 1)
                rom_handler_.set_overlay(!!(port_a_after & 1));
            if (port_a_diff & 2)
                std::cerr << "[CIA] Turn LED " << (port_a_after & 2 ? "off" : "on") << '\n';
        }
    }

    uint8_t get_port_output(uint8_t idx, uint8_t port)
    {
        assert(idx < 2 && port < 2);
        const uint8_t ddr = regs_[idx][ddra + port];
        return (regs_[idx][pra + port] & ddr) | (0xff & ~ddr);
    }
};

cia_handler::cia_handler(memory_handler& mem_handler, rom_area_handler& rom_handler)
    : impl_ { std::make_unique<impl>(mem_handler, rom_handler) }
{
}

cia_handler::~cia_handler() = default;