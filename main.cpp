#include <iostream>
#include <stdint.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cassert>

#include "ioutil.h"
#include "instruction.h"
#include "disasm.h"

std::vector<uint8_t> read_file(const std::string& path)
{
    std::ifstream in { path, std::ifstream::binary };
    if (!in) {
        throw std::runtime_error { "Error opening " + path };
    }

    in.seekg(0, std::ifstream::end);
    const auto len = in.tellg();
    in.seekg(0, std::ifstream::beg);

    std::vector<uint8_t> buf(len);
    if (len) {
        in.read(reinterpret_cast<char*>(&buf[0]), len);
    }
    if (!in) {
        throw std::runtime_error { "Error reading from " + path };
    }
    return buf;
}

uint16_t get_u16(const uint8_t* d)
{
    return d[0] << 8 | d[1];
}

uint32_t get_u32(const uint8_t* d)
{
    return static_cast<uint32_t>(d[0]) << 24 | d[1] << 16 | d[2] << 8 | d[3];
}

void put_u16(uint8_t* d, uint16_t val)
{
    d[0] = static_cast<uint8_t>(val >> 8);
    d[1] = static_cast<uint8_t>(val);
}

class memory_area_handler {
public:
    virtual uint8_t read_u8(uint32_t addr, uint32_t offset) = 0;
    virtual uint16_t read_u16(uint32_t addr, uint32_t offset) = 0;
    uint32_t read_u32(uint32_t addr, uint32_t offset) {
        return read_u16(addr, offset) << 16 | read_u16(addr + 2, offset + 2);
    }
    
    virtual void write_u8(uint32_t addr, uint32_t offset, uint8_t val) = 0;
    virtual void write_u16(uint32_t addr, uint32_t offset, uint16_t val) = 0;
    void write_u32(uint32_t addr, uint32_t offset, uint32_t val) {
        write_u16(addr, offset, val >> 16);
        write_u16(addr + 2, offset + 2, val & 0xffff);
    }
};

class default_handler : public memory_area_handler {
public:
    uint8_t read_u8(uint32_t addr, uint32_t) override
    {
        std::cerr << "[MEM] Unhandled read from $" << hexfmt(addr) << "\n";
        return 0xff;
    }
    uint16_t read_u16(uint32_t addr, uint32_t) override
    {
        std::cerr << "[MEM] Unhandled read from $" << hexfmt(addr) << "\n";
        return 0xffff;
    }
    void write_u8(uint32_t addr, uint32_t, uint8_t val) override
    {
        std::cerr << "[MEM] Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
    }
    void write_u16(uint32_t addr, uint32_t, uint16_t val) override
    {
        std::cerr << "[MEM] Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
    }
};

class ram_handler : public memory_area_handler {
public:
    explicit ram_handler(uint32_t size)
    {
        ram_.resize(size);
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        assert(offset < ram_.size());
        return ram_[offset];
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        assert(offset < ram_.size()-1);
        return get_u16(&ram_[offset]);
    }
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        assert(offset < ram_.size());
        ram_[offset] = val;
    }
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        assert(offset < ram_.size() - 1);
        put_u16(&ram_[offset], val);
    }

private:
    std::vector<uint8_t> ram_;
};

class memory_handler {
public:
    explicit memory_handler(uint32_t ram_size)
        : ram_ { ram_size }
        , ram_area_ { 0, ram_size, &ram_ }
    {
    }

    memory_handler(const memory_handler&) = delete;
    memory_handler& operator=(const memory_handler&) = delete;

    void register_handler(memory_area_handler& h, uint32_t base, uint32_t len)
    {
        auto a = &find_area(base);
        assert(a == &def_area_ || a == &ram_area_);
        areas_.push_back(area {
            base, len, &h });
    }

    void unregister_handler(memory_area_handler& h, uint32_t base, uint32_t len)
    {
        auto& a = find_area(base);
        assert(a.base == base && a.len == len && a.handler == &h);
        areas_.erase(areas_.begin() + (&a - &areas_[0]));
    }

    uint8_t read_u8(uint32_t addr)
    {
        auto& a = find_area(addr);
        return a.handler->read_u8(addr, addr - a.base);
    }

    uint16_t read_u16(uint32_t addr)
    {
        assert(!(addr & 1));
        auto& a = find_area(addr);
        return a.handler->read_u16(addr, addr - a.base);
    }

    uint32_t read_u32(uint32_t addr)
    {
        assert(!(addr & 1));
        auto& a = find_area(addr);
        return a.handler->read_u32(addr, addr - a.base);
    }

    void write_u8(uint32_t addr, uint8_t val)
    {
        auto& a = find_area(addr);
        return a.handler->write_u8(addr, addr - a.base, val);
    }

    void write_u16(uint32_t addr, uint16_t val)
    {
        auto& a = find_area(addr);
        return a.handler->write_u16(addr, addr - a.base, val);
    }

    void write_u32(uint32_t addr, uint32_t val)
    {
        auto& a = find_area(addr);
        return a.handler->write_u32(addr, addr - a.base, val);
    }

private:
    struct area {
        uint32_t base;
        uint32_t len;
        memory_area_handler* handler;
    };
    std::vector<area> areas_;
    default_handler def_handler_;
    ram_handler ram_;
    area def_area_ { 0, 1U << 24, &def_handler_ };
    area ram_area_;

    area& find_area(uint32_t addr)
    {
        addr &= 0xffffff;
        for (auto& a : areas_) {
            if (addr >= a.base && addr < a.base + a.len)
                return a;
        }
        if (addr < ram_area_.len) {
            return ram_area_;
        }
        return def_area_;
    }
};

class rom_area_handler : public memory_area_handler {
public:
    explicit rom_area_handler(memory_handler& mem_handler, std::vector<uint8_t>&& data)
        : mem_handler_ { mem_handler }
        , rom_data_ { std::move(data) }
    {
        const auto size = static_cast<uint32_t>(rom_data_.size());
        if (size != 256 * 1024 && size != 512 * 1024) {
            throw std::runtime_error { "Unexpected size of ROM: $" + hexstring(size) };
        }
        mem_handler_.register_handler(*this, 0xf80000, size);
        if (rom_data_.size() != 512 * 1024)
            mem_handler_.register_handler(*this, 0xfc0000, size);
    }

    void set_overlay(bool ovl)
    {
        const auto size = static_cast<uint32_t>(rom_data_.size());
        std::cerr << "[ROM handler] Turning overlay " << (ovl ? "on" : "off") << "\n";
        if (ovl)
            mem_handler_.register_handler(*this, 0, size);
        else
            mem_handler_.unregister_handler(*this, 0, size);
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        assert(offset < rom_data_.size());
        return rom_data_[offset];
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        assert(offset < rom_data_.size() - 1);
        return get_u16(&rom_data_[offset]);
    }

    void write_u8(uint32_t addr, uint32_t offset, uint8_t val) override
    {
        std::cerr << "[MEM] Write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
        throw std::runtime_error { "FIXME" };
    }

    void write_u16(uint32_t addr, uint32_t offset, uint16_t val) override
    {
        std::cerr << "[MEM] Write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
        throw std::runtime_error { "FIXME" };
    }

private:
    memory_handler& mem_handler_;
    std::vector<uint8_t> rom_data_;
};

// Handles both MOS Technology 8520 Complex Interface Adapter chips
// CIAA can generate INT2, CIAB can generate INT6
class cia_handler : public memory_area_handler {
public:
    explicit cia_handler(memory_handler& mem_handler, rom_area_handler& rom_handler)
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
            std::cerr << "TODO: Invalid CIA byte write : " << hexfmt(addr) << " val = $" << hexfmt(val) << "\n";
            assert(0);
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
            std::cerr << "[CIA] TODO: Not handling input pins for CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << "\n";
            return regs_[idx][reg];
        case ddra:
        case ddrb:
            return regs_[idx][reg];
        default:
            std::cerr << "TODO: Handle read from CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << "\n";
            assert(0);
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
            std::cerr << "TODO: Handle write to CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << " val $" << hexfmt(val) << "\n";
            assert(0);
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
};

class custom_handler : public memory_area_handler {
public:
    explicit custom_handler(memory_handler& mem_handler)
        : mem_handler_ { mem_handler }
    {
        mem_handler_.register_handler(*this, 0xDFF000, 0x1000);
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        std::cerr << "Unhandled read from custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")\n";
        assert(false);
        return 0xff;
    }
    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        std::cerr << "Unhandled read from custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")\n";
        assert(false);
        return 0xffff;
    }
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")"
                  << " val $" << hexfmt(val) << "\n";
        assert(false);
    }
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        switch (offset) {
        case SERPER:
        case COP1LCH:
        case COP1LCL:
        case COP2LCH:
        case COP2LCL:
        case COPJMP1:
        case COPJMP2:
        case DIWSTRT:
        case DIWSTOP:
        case DDFSTRT:
        case DDFSTOP:
        case INTENA:
        case INTREQ:
        case DMACON:
        case BPL1PTH:
        case BPL1PTL:
        case BPLCON0:
        case BPLCON1:
        case BPLCON2:
        case BPLCON3:
        case BPLMOD1:
        case BPLMOD2:
        case BPL1DAT:
        ignore:
            std::cerr << "[CUSTOM] Ignoring write to " << regname(offset) << " val $" << hexfmt(val) << "\n";
            return;
        }

        if (offset >= COLOR00 && offset <= COLOR31)
            goto ignore;

        std::ostringstream oss;
        oss << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")" << " val $" << hexfmt(val);
        throw std::runtime_error { oss.str() };
    }

private:
    memory_handler& mem_handler_;

    // Name, Offset, R(=0)/W(=1)
#define CUSTOM_REGS(X) \
    X(SERPER  , 0x032 , 1) /* Serial port period and control                          */ \
    X(COP1LCH , 0x080 , 1) /* Coprocessor first location register (high 3 bits)       */ \
    X(COP1LCL , 0x082 , 1) /* Coprocessor first location register (low 15 bits)       */ \
    X(COP2LCH , 0x084 , 1) /* Coprocessor second location register (high 3 bits)      */ \
    X(COP2LCL , 0x086 , 1) /* Coprocessor second location register (low 15 bits)      */ \
    X(COPJMP1 , 0x088 , 1) /* Coprocessor restart at first location                   */ \
    X(COPJMP2 , 0x08A , 1) /* Coprocessor restart at second location                  */ \
    X(DIWSTRT , 0x08E , 1) /* Display window start (upper left vert-horiz position)   */ \
    X(DIWSTOP , 0x090 , 1) /* Display window stop (lower right vert.-horiz. position) */ \
    X(DDFSTRT , 0x092 , 1) /* Display bitplane data fetch start (horiz. position)     */ \
    X(DDFSTOP , 0x094 , 1) /* Display bitplane data fetch stop (horiz. position)      */ \
    X(DMACON  , 0x096 , 1) /* DMA control write (clear or set)                        */ \
    X(INTENA  , 0x09A , 1) /* Interrupt enable bits (clear or set bits)               */ \
    X(INTREQ  , 0x09C , 1) /* Interrupt request bits (clear or set bits)              */ \
    X(BPL1PTH , 0x0E0 , 1) /* Bitplane 1 pointer (high 3 bits)                        */ \
    X(BPL1PTL , 0x0E2 , 1) /* Bitplane 1 pointer (low 15 bits)                        */ \
    X(BPL2PTH , 0x0E4 , 1) /* Bitplane 2 pointer (high 3 bits)                        */ \
    X(BPL2PTL , 0x0E6 , 1) /* Bitplane 2 pointer (low 15 bits)                        */ \
    X(BPL3PTH , 0x0E8 , 1) /* Bitplane 3 pointer (high 3 bits)                        */ \
    X(BPL3PTL , 0x0EA , 1) /* Bitplane 3 pointer (low 15 bits)                        */ \
    X(BPL4PTH , 0x0EC , 1) /* Bitplane 4 pointer (high 3 bits)                        */ \
    X(BPL4PTL , 0x0EE , 1) /* Bitplane 4 pointer (low 15 bits)                        */ \
    X(BPL5PTH , 0x0F0 , 1) /* Bitplane 5 pointer (high 3 bits)                        */ \
    X(BPL5PTL , 0x0F2 , 1) /* Bitplane 5 pointer (low 15 bits)                        */ \
    X(BPL6PTH , 0x0F4 , 1) /* Bitplane 6 pointer (high 3 bits)                        */ \
    X(BPL6PTL , 0x0F6 , 1) /* Bitplane 6 pointer (low 15 bits)                        */ \
    X(BPLCON0 , 0x100 , 1) /* Bitplane control register                               */ \
    X(BPLCON1 , 0x102 , 1) /* Bitplane control register                               */ \
    X(BPLCON2 , 0x104 , 1) /* Bitplane control register                               */ \
    X(BPLCON3 , 0x106 , 1) /* Bitplane control register  (ECS only)                   */ \
    X(BPLMOD1 , 0x108 , 1) /* Bitplane modulo (odd planes)                            */ \
    X(BPLMOD2 , 0x10A , 1) /* Bitplane modulo (even planes)                           */ \
    X(BPL1DAT , 0x110 , 1) /* Bitplane 1 data (parallel-to-serial convert)            */ \
    X(BPL2DAT , 0x112 , 1) /* Bitplane 2 data (parallel-to-serial convert)            */ \
    X(BPL3DAT , 0x114 , 1) /* Bitplane 3 data (parallel-to-serial convert)            */ \
    X(BPL4DAT , 0x116 , 1) /* Bitplane 4 data (parallel-to-serial convert)            */ \
    X(BPL5DAT , 0x118 , 1) /* Bitplane 5 data (parallel-to-serial convert)            */ \
    X(BPL6DAT , 0x11A , 1) /* Bitplane 6 data (parallel-to-serial convert)            */ \
    X(COLOR00 , 0x180 , 1) /* Color table 00                                          */ \
    X(COLOR01 , 0x182 , 1) /* Color table 01                                          */ \
    X(COLOR02 , 0x184 , 1) /* Color table 02                                          */ \
    X(COLOR03 , 0x186 , 1) /* Color table 03                                          */ \
    X(COLOR04 , 0x188 , 1) /* Color table 04                                          */ \
    X(COLOR05 , 0x18A , 1) /* Color table 05                                          */ \
    X(COLOR06 , 0x18C , 1) /* Color table 06                                          */ \
    X(COLOR07 , 0x18E , 1) /* Color table 07                                          */ \
    X(COLOR08 , 0x190 , 1) /* Color table 08                                          */ \
    X(COLOR09 , 0x192 , 1) /* Color table 09                                          */ \
    X(COLOR10 , 0x194 , 1) /* Color table 10                                          */ \
    X(COLOR11 , 0x196 , 1) /* Color table 11                                          */ \
    X(COLOR12 , 0x198 , 1) /* Color table 12                                          */ \
    X(COLOR13 , 0x19A , 1) /* Color table 13                                          */ \
    X(COLOR14 , 0x19C , 1) /* Color table 14                                          */ \
    X(COLOR15 , 0x19E , 1) /* Color table 15                                          */ \
    X(COLOR16 , 0x1A0 , 1) /* Color table 16                                          */ \
    X(COLOR17 , 0x1A2 , 1) /* Color table 17                                          */ \
    X(COLOR18 , 0x1A4 , 1) /* Color table 18                                          */ \
    X(COLOR19 , 0x1A6 , 1) /* Color table 19                                          */ \
    X(COLOR20 , 0x1A8 , 1) /* Color table 20                                          */ \
    X(COLOR21 , 0x1AA , 1) /* Color table 21                                          */ \
    X(COLOR22 , 0x1AC , 1) /* Color table 22                                          */ \
    X(COLOR23 , 0x1AE , 1) /* Color table 23                                          */ \
    X(COLOR24 , 0x1B0 , 1) /* Color table 24                                          */ \
    X(COLOR25 , 0x1B2 , 1) /* Color table 25                                          */ \
    X(COLOR26 , 0x1B4 , 1) /* Color table 26                                          */ \
    X(COLOR27 , 0x1B6 , 1) /* Color table 27                                          */ \
    X(COLOR28 , 0x1B8 , 1) /* Color table 28                                          */ \
    X(COLOR29 , 0x1BA , 1) /* Color table 29                                          */ \
    X(COLOR30 , 0x1BC , 1) /* Color table 30                                          */ \
    X(COLOR31 , 0x1BE , 1) /* Color table 31                                          */ \

// keep this line clear (for macro continuation)

    static std::string regname(uint32_t offset)
    {
        switch (offset) {
    #define CHECK_NAME(name, offset, _) case offset: return #name;
            CUSTOM_REGS(CHECK_NAME)
    #undef CHECK_NAME
        }
        return "$" + hexstring(offset, 3);
    }

    enum regnum {
    #define REG_NUM(name, offset, _) name = offset,
        CUSTOM_REGS(REG_NUM)
    #undef REG_NUM
    };
};

enum sr_bit_index {
    // User byte
    sri_c     = 0, // Carry
    sri_v     = 1, // Overflow
    sri_z     = 2, // Zero
    sri_n     = 3, // Negative
    sri_x     = 4, // Extend
    // System byte
    sri_ipl   = 8, // 3 bits Interrupt priority mask
    sri_m     = 12, // Master/interrupt state
    sri_s     = 13, // Supervisor/user state
    sri_trace = 14, // 2 bits Trace (0b00 = No trace, 0b10 = Trace on any instruction, 0b01 = Trace on change of flow, 0b11 = Undefined)
};

enum sr_mask : uint16_t {
    srm_c     = 1 << sri_c,
    srm_v     = 1 << sri_v,
    srm_z     = 1 << sri_z,
    srm_n     = 1 << sri_n,
    srm_x     = 1 << sri_x,
    srm_ipl   = 7 << sri_ipl,
    srm_m     = 1 << sri_m,
    srm_s     = 1 << sri_s,
    srm_trace = 3 << sri_trace,

    srm_illegal  = 1 << 5 | 1 << 6 | 1 << 7 | 1 << 11,
    srm_ccr_no_x = srm_c | srm_v | srm_z | srm_n,
    srm_ccr      = srm_ccr_no_x | srm_x,
};

enum class conditional : uint8_t {
    t  = 0b0000, // True                1
    f  = 0b0001, // False               0
    hi = 0b0010, // High                (not C) and (not Z)
    ls = 0b0011, // Low or Same         C or V
    cc = 0b0100, // Carray Clear (HI)   not C
    cs = 0b0101, // Carry Set (LO)      C
    ne = 0b0110, // Not Equal           not Z
    eq = 0b0111, // Equal               Z
    vc = 0b1000, // Overflow Clear      not V
    vs = 0b1001, // Overflow Set        V
    pl = 0b1010, // Plus                not N
    mi = 0b1011, // Minus               N
    ge = 0b1100, // Greater or Equal    (N and V) or ((not N) and (not V))
    lt = 0b1101, // Less Than           (N and (not V)) or ((not N) and V))
    gt = 0b1110, // Greater Than        (N and V and (not Z)) or ((not N) and (not V) and (not Z))
    le = 0b1111, // Less or Equal       Z or (N and (not V)) or ((not N) and V)
};

const char* const conditional_strings[16] = {
    "t",
    "f",
    "hi",
    "ls",
    "cc",
    "cs",
    "ne",
    "eq",
    "vc",
    "vs",
    "pl",
    "mi",
    "ge",
    "lt",
    "gt",
    "le",
};

enum class interrupt_vector : uint8_t {
    reset_ssp = 0,
    reset_pc = 1,
    bus_error = 2,
    address_error = 3,
    illegal_instruction = 4,
    zero_divide = 5,
    chk_exception = 6,
    trapv_instruction = 7,
    privililege_violation = 8,
    trace = 9,
    line_1010 = 10,
    line_1111 = 11,
    level1 = 25,
    level2 = 26,
    level3 = 27,
    level4 = 28,
    level5 = 29,
    level6 = 30,
    level7 = 31,
};

struct cpu_state {
    uint32_t d[8];
    uint32_t a[7];
    uint32_t ssp;
    uint32_t usp;
    uint32_t pc;
    uint16_t sr;

    uint32_t& A(unsigned idx)
    {
        assert(idx < 8);
        return idx < 7 ? a[idx] : (sr & srm_s ? ssp : usp);
    }

    uint32_t A(unsigned idx) const
    {
        return const_cast<cpu_state&>(*this).A(idx);
    }

    void update_sr(sr_mask mask, uint16_t val)
    {
        assert((mask & srm_illegal) == 0);
        assert((val & ~mask) == 0);
        sr = (sr & ~mask) | val;
    }

    bool eval_cond(conditional c) const
    {
        const bool C = !!(sr & srm_c);
        const bool V = !!(sr & srm_v);
        const bool Z = !!(sr & srm_z);
        const bool N = !!(sr & srm_n);

        switch (c) {
        case conditional::t:
            return true;
        case conditional::f:
            return false;
        case conditional::hi: // (not C) and (not Z)
            return !C && !Z;
        case conditional::ls: // C or V
            return C || V;
        case conditional::cc: // not C
            return !C;
        case conditional::cs: // C
            return C;
        case conditional::ne: // not Z
            return !Z;
        case conditional::eq: // Z
            return Z;
        case conditional::vc: // not V
            return !V;
        case conditional::vs: // V
            return V;
        case conditional::pl: // not N
            return !N;
        case conditional::mi: // N
            return N;
        case conditional::ge: // (N and V) or ((not N) and (not V))
            return (N && V) || (!N && !V);
        case conditional::lt: // (N and (not V)) or ((not N) and V))
            return (N && !V) || (!N && V);
        case conditional::gt: // (N and V and (not Z)) or ((not N) and (not V) and (not Z))
            return (N && V && !Z) || (!N && !V && !Z);
        case conditional::le: // Z or (N and (not V)) or ((not N) and V)
            return Z || (N && !V) || (!N && V);
        default:
            assert(!"Condition not implemented");
        }
        return false;
    }
};

void print_cpu_state(std::ostream& os, const cpu_state& s)
{
    for (int i = 0; i < 8; ++i) {
        if (i)
            os << " ";
        os << "D" << i << "=" << hexfmt(s.d[i]);
    }
    os << "\n";
    for (int i = 0; i < 8; ++i) {
        if (i)
            os << " ";
        os << "A" << i << "=" << hexfmt(s.A(i));
    }
    os << "\n";
    os << "PC=" << hexfmt(s.pc) << " SR=" << hexfmt(s.sr) << " SSP=" << hexfmt(s.ssp) << " USP=" << hexfmt(s.usp) << " CCR: ";
    
    for (unsigned i = 5; i--;) {
        if ((s.sr & (1 << i)))
            os << "CVZNX"[i];
        else
            os << '-';
    }
    os << '\n';
}

constexpr int32_t sext(uint32_t val, opsize size)
{
    switch (size) {
    case opsize::b:
        return static_cast<int8_t>(val & 0xff);
    case opsize::w:
        return static_cast<int16_t>(val & 0xfffff);
    default:
        return static_cast<int32_t>(val);
    }
}

class m68000 {
public:
    explicit m68000(memory_handler& mem)
        : mem_ { mem }
    {
        memset(&state_, 0, sizeof(state_));
        state_.sr = srm_s | srm_ipl; // 0x2700
        state_.ssp = mem.read_u32(0);
        state_.pc = mem.read_u32(4);

        std::fill(std::begin(iwords_), std::end(iwords_), illegal_instruction_num);
        inst_ = &instructions[illegal_instruction_num];
    }

    const cpu_state& state() const
    {
        return state_;
    }

    uint64_t instruction_count() const
    {
        return instruction_count_;
    }

    void trace(bool enabled)
    {
        trace_ = enabled;
    }

    void show_state(std::ostream& os)
    {
        os << "After " << instruction_count_ << " instructions:\n";
        print_cpu_state(os, state_);
        disasm(os, start_pc_, iwords_, inst_->ilen);
        os << '\n';
    }

    void step()
    {
        ++instruction_count_;

        if (trace_)
            print_cpu_state(std::cout, state_);

        start_pc_ = state_.pc;
        iwords_[0] = mem_.read_u16(state_.pc);
        state_.pc += 2;
        inst_ = &instructions[iwords_[0]];
        for (unsigned i = 1; i < inst_->ilen; ++i) {
            iwords_[i] = mem_.read_u16(state_.pc);
            state_.pc += 2;
        }

        if (trace_) {
            disasm(std::cout, start_pc_, iwords_, inst_->ilen);
            std::cout << '\n';
        }

        if (inst_->type == inst_type::ILLEGAL) {
            if (iwords_[0] == 0x4e7b) {
                // For now only handle this specific one (from kickstart 1.3)
                do_interrupt(interrupt_vector::illegal_instruction);
                goto out;
            }
            throw std::runtime_error { "ILLEGAL" };
        }

        assert(inst_->nea <= 2);

        iword_idx_ = 1;

        // Pre-increment & handle ea
        for (uint8_t i = 0; i < inst_->nea; ++i) {
            const auto ea = inst_->ea[i];
            // Don't predecrement here for MOVEM
            if (inst_->type != inst_type::MOVEM && (ea >> ea_m_shift) == ea_m_A_ind_pre) {
                state_.A(ea & ea_xn_mask) -= opsize_bytes(inst_->size);
            }
            handle_ea(i);
        }

        switch (inst_->type) {
#define HANDLE_INST(t) \
    case inst_type::t: \
        handle_##t();  \
        break;
            HANDLE_INST(ADD);
            HANDLE_INST(ADDA);
            HANDLE_INST(ADDQ);
            HANDLE_INST(AND);
            HANDLE_INST(Bcc);
            HANDLE_INST(BRA);
            HANDLE_INST(BSR);
            HANDLE_INST(BCLR);
            HANDLE_INST(BSET);
            HANDLE_INST(BTST);
            HANDLE_INST(CLR);
            HANDLE_INST(CMP);
            HANDLE_INST(CMPA);
            HANDLE_INST(DBcc);
            HANDLE_INST(EOR);
            HANDLE_INST(EXG);
            HANDLE_INST(EXT);
            HANDLE_INST(JMP);
            HANDLE_INST(JSR);
            HANDLE_INST(LEA);
            HANDLE_INST(LSL);
            HANDLE_INST(LSR);
            HANDLE_INST(MOVE);
            HANDLE_INST(MOVEA);
            HANDLE_INST(MOVEM);
            HANDLE_INST(MOVEQ);
            HANDLE_INST(MULU);
            HANDLE_INST(NOT);
            HANDLE_INST(OR);
            HANDLE_INST(PEA);
            HANDLE_INST(RTS);
            HANDLE_INST(SUB);
            HANDLE_INST(SUBA);
            HANDLE_INST(SUBQ);
            HANDLE_INST(SWAP);
            HANDLE_INST(TST);
#undef HANDLE_INST
        default: {
            std::ostringstream oss;
            disasm(oss, start_pc_, iwords_, inst_->ilen);
            throw std::runtime_error { "Unhandled instruction: " + oss.str() };
        }
        }

        assert(iword_idx_ == inst_->ilen);

        // Post-increment
        for (uint8_t i = 0; i < inst_->nea; ++i) {
            const auto ea = inst_->ea[i];
            // MOVEM handles any increments
            if (inst_->type != inst_type::MOVEM && (ea >> ea_m_shift) == ea_m_A_ind_post) {
                state_.A(ea & ea_xn_mask) += opsize_bytes(inst_->size);
            }
        }

out:
        if (trace_)
            std::cout << "\n";
    }

private:
    memory_handler& mem_;
    cpu_state state_;

    // Decoder state
    uint32_t start_pc_;
    uint16_t iwords_[max_instruction_words];
    uint8_t iword_idx_;
    const instruction* inst_;
    uint32_t ea_data_[2]; // For An/Dn/Imm/etc. contains the value, for all others the address
    uint64_t instruction_count_ = 0;
    bool trace_ = false;

    uint32_t read_reg(uint32_t val)
    {
        switch (inst_->size) {
        case opsize::b:
            return val & 0xff;
        case opsize::w:
            return val & 0xffff;
        case opsize::l:
            return val;
        }
        assert(!"invalid opsize");
        return val;
    }

    uint32_t read_mem(uint32_t addr)
    {
        switch (inst_->size) {
        case opsize::b:
            return mem_.read_u8(addr);
        case opsize::w:
            return mem_.read_u16(addr);
        case opsize::l:
            return mem_.read_u32(addr);
        }
        assert(!"invalid opsize");
        return 0;
    }

    void handle_ea(uint8_t idx)
    {
        assert(idx < inst_->nea);
        auto& res = ea_data_[idx];
        const auto ea = inst_->ea[idx];
        switch (ea >> ea_m_shift) {
        case ea_m_Dn:
            res = read_reg(state_.d[ea & ea_xn_mask]);
            return;
        case ea_m_An:
            res = read_reg(state_.A(ea & ea_xn_mask));
            return;
        case ea_m_A_ind:
        case ea_m_A_ind_post:
        case ea_m_A_ind_pre:
            res = state_.A(ea & ea_xn_mask);
            return;
        case ea_m_A_ind_disp16:
            assert(iword_idx_ < inst_->ilen);
            res = state_.A(ea & ea_xn_mask) + sext(iwords_[iword_idx_++], opsize::w);
            return;
        case ea_m_A_ind_index: {
            assert(iword_idx_ < inst_->ilen);
            const auto extw = iwords_[iword_idx_++];
            assert(!(extw & (7 << 8)));
            res = state_.A(ea & ea_xn_mask) + sext(extw, opsize::b);
            uint32_t r = (extw >> 12) & 7;
            if ((extw >> 15) & 1) {
                r = state_.A(r);
            } else {
                r = state_.d[r];
            }
            if (!((extw >> 11) & 1))
                r = sext(r, opsize::w);
            res += r;
            return;
        }
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
                assert(iword_idx_ < inst_->ilen);
                res = static_cast<int32_t>(static_cast<int16_t>(iwords_[iword_idx_++]));
                return;
            case ea_other_abs_l:
                assert(iword_idx_ + 1 < inst_->ilen);
                res = iwords_[iword_idx_++] << 16;
                res |= iwords_[iword_idx_++];
                return;
            case ea_other_pc_disp16:
                assert(iword_idx_ < inst_->ilen);
                res = start_pc_ + 2 + sext(iwords_[iword_idx_++], opsize::w);
                return;
            case ea_other_pc_index:
                break;
            case ea_other_imm:
                switch (inst_->size) {
                case opsize::b:
                    assert(iword_idx_ < inst_->ilen);
                    res = iwords_[iword_idx_++] & 0xff;
                    return;
                case opsize::w:
                    assert(iword_idx_ < inst_->ilen);
                    res = iwords_[iword_idx_++];
                    return;
                case opsize::l:
                    assert(iword_idx_ + 1 < inst_->ilen);
                    res = iwords_[iword_idx_++] << 16;
                    res |= iwords_[iword_idx_++];
                    return;
                }
                break;
            }
            break;
        case ea_m_inst_data:
            if (ea == ea_sr) {
                res = state_.sr;
                return;
            } else if (ea == ea_ccr) {
                res = state_.sr & srm_ccr;
                return;
            } else if (ea == ea_reglist) {
                assert(iword_idx_ < inst_->ilen);
                res = iwords_[iword_idx_++];
                return;
            } else if (ea == ea_bitnum) {
                assert(iword_idx_ < inst_->ilen);
                res = iwords_[iword_idx_++];
                return;
            }
            assert(ea <= ea_disp);
            if (inst_->extra & extra_disp_flag) {
                assert(ea == ea_disp);
                assert(iword_idx_ < inst_->ilen);
                res = start_pc_ + 2 + static_cast<int16_t>(iwords_[iword_idx_++]);
                return;
            } else if (ea == ea_disp) {
                res = start_pc_ + 2 + static_cast<int8_t>(inst_->data);
                return;
            } else {
                res = inst_->data;
                return;
            }
            break;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) };
    }

    uint32_t read_ea(uint8_t idx)
    {
        assert(idx < inst_->nea);
        const auto ea = inst_->ea[idx];
        const auto val = ea_data_[idx];
        switch (ea >> ea_m_shift) {
        case ea_m_Dn:
        case ea_m_An:
            return val;
        case ea_m_A_ind:
        case ea_m_A_ind_post:
        case ea_m_A_ind_pre:
        case ea_m_A_ind_disp16:
        case ea_m_A_ind_index:
            return read_mem(val);
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
            case ea_other_pc_disp16:
            case ea_other_pc_index:
                return read_mem(val);
            case ea_other_imm:
                return val;
            }
            break;
        case ea_m_inst_data:
            return val;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) };
    }

    void write_mem(uint32_t addr, uint32_t val)
    {
        switch (inst_->size) {
        case opsize::b:
            mem_.write_u8(addr, static_cast<uint8_t>(val));
            return;
        case opsize::w:
            mem_.write_u16(addr, static_cast<uint16_t>(val));
            return;
        case opsize::l:
            mem_.write_u32(addr, val);
            return;
        }
        assert(!"Invalid opsize");
    }

    void write_ea(uint8_t idx, uint32_t val)
    {
        assert(idx < inst_->nea);
        const auto ea = inst_->ea[idx];
        switch (ea >> ea_m_shift) {
        case ea_m_Dn: {
            auto& reg = state_.d[ea & ea_xn_mask];
            switch (inst_->size) {
            case opsize::b:
                reg = (reg & 0xffffff00) | (val & 0xff);
                return;
            case opsize::w:
                reg = (reg & 0xffff0000) | (val & 0xffff);
                return;
            case opsize::l:
                reg = val;
                return;
            default:
                assert(!"Invalid opsize");
            }
            break;
        }
        case ea_m_An: {
            auto& reg = state_.A(ea & ea_xn_mask);
            switch (inst_->size) {
            case opsize::w:
                reg = (reg & 0xffff0000) | (val & 0xffff);
                return;
            case opsize::l:
                reg = val;
                return;
            default:
                assert(!"Invalid opsize");
            }
            return;
        }
        case ea_m_A_ind:
        case ea_m_A_ind_post:
        case ea_m_A_ind_pre:
        case ea_m_A_ind_disp16:
        case ea_m_A_ind_index:
            write_mem(ea_data_[idx], val);
            return;
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
            case ea_other_pc_disp16:
            case ea_other_pc_index:
                write_mem(ea_data_[idx], val);
                return;
            case ea_other_imm:
                assert(!"Write to immediate?!");
                break;
            }
            break;
        case ea_m_inst_data:
            if (ea == ea_sr) {
                assert(!(val & srm_illegal));
                state_.sr = static_cast<uint16_t>(val);
                return;
            }
            break;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) + " val = $" + hexstring(val) };
    }

    void update_flags(sr_mask srmask, uint32_t res, uint32_t carry)
    {
        assert(inst_->size != opsize::none);
        const uint32_t mask = opsize_msb_mask(inst_->size);
        uint16_t ccr = 0;
        if (carry & mask) {
            ccr |= srm_c;
            ccr |= srm_x;
        }
        if (((carry << 1) ^ carry) & mask)
            ccr |= srm_v;
        if (!(res & ((mask << 1) - 1)))
            ccr |= srm_z;
        if (res & mask)
            ccr |= srm_n;

        state_.update_sr(srmask, (ccr & srmask));
    }

    void update_flags_rot(uint32_t res, uint32_t cnt, bool carry)
    {
        assert(inst_->size != opsize::none);
        const uint32_t mask = opsize_msb_mask(inst_->size);
        uint16_t ccr = 0;
        if (carry) {
            ccr |= srm_c;
            ccr |= srm_x;
        }
        // V always cleared
        if (!(res & ((mask << 1) - 1)))
            ccr |= srm_z;
        if (res & mask)
            ccr |= srm_n;
        // X not affected if zero shift count...
        state_.update_sr(cnt ? srm_ccr : srm_ccr_no_x, ccr);
    }

    void push_u16(uint16_t val)
    {
        auto& a7 = state_.A(7);
        a7 -= 2;
        mem_.write_u16(a7, val);
    }

    void push_u32(uint32_t val)
    {
        auto& a7 = state_.A(7);
        a7 -= 4;
        mem_.write_u32(a7, val);
    }

    void do_interrupt(interrupt_vector vec)
    {
        std::cerr << "[CPU] Interrupt $" << hexfmt(static_cast<uint8_t>(vec)) << " triggered at PC=$" << hexfmt(start_pc_) << "\n";

        const uint16_t saved_sr = state_.sr;
        const uint8_t vec_num = static_cast<uint8_t>(vec);
        assert(vec_num < static_cast<uint8_t>(interrupt_vector::level1)); // TODO: Update IPL, get from bus cycle
        assert(vec_num > static_cast<uint8_t>(interrupt_vector::reset_pc)); // RESET doesn't save anything
        
        state_.update_sr(static_cast<sr_mask>(srm_trace | srm_s), srm_s); // Clear trace, set superviser mode
        // Now always on supervisor stack

        // From MC68000UM 6.2.5
        // "The current program
        // counter value and the saved copy of the status register are stacked using the SSP. The
        // stacked program counter value usually points to the next unexecuted instruction.
        // However, for bus error and address error, the value stacked for the program counter is
        // unpredictable and may be incremented from the address of the instruction that caused the
        // error."
        push_u32(state_.pc);
        push_u16(saved_sr);

        state_.pc = mem_.read_u32(static_cast<uint32_t>(vec) * 4);
    }

    void handle_ADD()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l + r;
        write_ea(1, res);
        // All flags updated
        update_flags(srm_ccr, res, (l & r) | ((l | r) & ~res));
    }

    void handle_ADDA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        state_.A(inst_->ea[1] & ea_xn_mask) += s; // And the operation performed on the full 32-bit value
        // No flags
    }

    void handle_ADDQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift == ea_m_inst_data) && inst_->ea[0] <= ea_disp);
        handle_ADD();
    }

    void handle_AND()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l & r;
        write_ea(1, res);
        update_flags(srm_ccr_no_x, res, 0);
    }

    void handle_Bcc()
    {
        assert(inst_->nea == 1 && inst_->ea[0] == ea_disp && (inst_->extra & extra_cond_flag));
        if (!state_.eval_cond(static_cast<conditional>(inst_->extra >> 4)))
            return;
        state_.pc = ea_data_[0];
    }

    void handle_BRA()
    {
        assert(inst_->nea == 1);
        state_.pc = ea_data_[0];
    }

    void handle_BSR()
    {
        assert(inst_->nea == 1);
        push_u32(state_.pc);
        state_.pc = ea_data_[0];
    }

    std::pair<uint32_t, uint32_t> bit_op_helper()
    {
        assert(inst_->nea == 2);
        auto bitnum = read_ea(0);
        const auto num = read_ea(1);
        if (inst_->size == opsize::b) {
            bitnum &= 7;
        } else {
            assert(inst_->size == opsize::l);
            bitnum &= 31;
        }
        const bool is_set = !!((num >> bitnum) & 1);
        state_.update_sr(srm_z, is_set ? srm_z : 0); // Set according to the previous state of the bit
        return { bitnum, num };
    }

    void handle_BCLR()
    {
        const auto [bitnum, num] = bit_op_helper();
        write_ea(1, num & ~(1 << bitnum));
    }

    void handle_BSET()
    {
        const auto [bitnum, num] = bit_op_helper();
        write_ea(1, num | (1 << bitnum));
    }

    void handle_BTST()
    {
        bit_op_helper(); // discard return value on purpose
    }

    void handle_CLR()
    {
        assert(inst_->nea == 1);
        // TODO: read cycle?
        write_ea(0, 0);
    }

    void handle_CMP()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r;
        update_flags(srm_ccr_no_x, res, (~l & r) | (~(l ^ r) & res));
    }

    void handle_CMPA()
    {
        assert(inst_->nea == 2);
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const uint32_t r = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        const uint32_t l = state_.A(inst_->ea[1] & ea_xn_mask);
        // And the performed on the full 32-bit value
        const auto res = l - r;
        update_flags(srm_ccr_no_x, res, (~l & r) | (~(l ^ r) & res));
    }

    void handle_DBcc()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) == ea_m_Dn && inst_->extra & extra_cond_flag);
        assert(inst_->size == opsize::w && inst_->ea[1] == ea_disp);

        if (state_.eval_cond(static_cast<conditional>(inst_->extra >> 4)))
            return;

        uint16_t val = static_cast<uint16_t>(read_ea(0));
        --val;
        write_ea(0, val);
        if (val != 0xffff) {
            state_.pc = ea_data_[1];
        }
    }

    void handle_EOR()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l ^ r;
        write_ea(1, res);
        update_flags(srm_ccr_no_x, res, 0);
    }

    void handle_EXG()
    {
        assert(inst_->size == opsize::l && inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) < 2 && (inst_->ea[1] >> ea_m_shift));
        const auto a = read_ea(0);
        const auto b = read_ea(1);
        write_ea(0, b);
        write_ea(1, a);
    }

    void handle_EXT()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift) == ea_m_Dn);
        auto r = state_.d[inst_->ea[0] & ea_xn_mask];
        uint32_t res;
        if (inst_->size == opsize::w) {
            res = sext(r, opsize::b);
        } else {
            assert(inst_->size == opsize::l);
            res = sext(r, opsize::w);
        }
        write_ea(0, res);
        update_flags(srm_ccr_no_x, res, 0);
    }

    void handle_JMP()
    {
        assert(inst_->nea == 1);
        state_.pc = ea_data_[0];
    }

    void handle_JSR()
    {
        assert(inst_->nea == 1);
        push_u32(state_.pc);
        state_.pc = ea_data_[0];
    }

    void handle_LEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift == ea_m_An) && inst_->size == opsize::l);
        write_ea(1, ea_data_[0]);
        // No flags affected
    }

    void handle_LSL()
    {
        uint32_t val, cnt;
        if (inst_->nea == 1) {
            val = read_ea(0);
            cnt = 1;
        } else {
            cnt = read_ea(0) & 63;
            val = read_ea(1);
        }
        bool carry;
        if (!cnt) {
            carry = false;
        } else if (cnt <= 32) {            
            carry = !!((val << (cnt - 1)) & opsize_msb_mask(inst_->size));
            val <<= cnt;
        } else {
            val = 0;
            carry = false;
        }

        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
    }

    void handle_LSR()
    {
        uint32_t val, cnt;
        if (inst_->nea == 1) {
            val = read_ea(0);
            cnt = 1;
        } else {
            cnt = read_ea(0) & 63;
            val = read_ea(1);
        }
        bool carry;
        if (!cnt) {
            carry = false;
        } else if (cnt <= 32) {
            carry = !!((val >> (cnt - 1)) & 1);
            val >>= cnt;
        } else {
            val = 0;
            carry = false;
        }

        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
    }

    void handle_MOVE()
    {
        assert(inst_->nea == 2);
        const uint32_t src = read_ea(0);
        write_ea(1, src);
        update_flags(srm_ccr_no_x, src, 0);
    }

    void handle_MOVEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        state_.A(inst_->ea[1] & ea_xn_mask) = s; // Always write full 32-bit result
        // No flags
    }

    void handle_MOVEM()
    {
        assert(inst_->nea == 2);
        assert(inst_->size == opsize::l); // TODO: Support word sized transfer

        if (inst_->ea[0] == ea_reglist) {
            uint16_t rl = static_cast<uint16_t>(ea_data_[0]);

            // Latch values
            uint32_t values[16];
            for (unsigned i = 0; i < 16; ++i) {
                if (i < 8)
                    values[i] = state_.d[i];
                else
                    values[i] = state_.A(i & 7);
            }

            for (unsigned bit = 16; bit--;) {
                if (!((rl >> bit) & 1))
                    continue;
                const auto val = values[15 - bit];

                if (inst_->ea[1] >> ea_m_shift == ea_m_A_ind_pre) {
                    auto& a = state_.A(inst_->ea[1] & ea_xn_mask);
                    a -= opsize_bytes(inst_->size);
                    write_mem(a, val);
                } else {
                    // Not checked...
                    write_ea(1, val);
                }
            }
        } else {
            assert(inst_->ea[1] == ea_reglist);
            uint16_t rl = static_cast<uint16_t>(ea_data_[1]);

            for (unsigned bit = 0; bit < 16; ++bit) {
                if (!((rl >> bit) & 1))
                    continue;
                uint32_t val;
                if (inst_->ea[0] >> ea_m_shift == ea_m_A_ind_post) {
                    auto& a = state_.A(inst_->ea[0] & ea_xn_mask);
                    val = read_mem(a);
                    a += opsize_bytes(inst_->size);
                } else {
                    // Not checked...
                    val = read_ea(0);
                }
                auto& r = bit < 8 ? state_.d[bit] : state_.A(bit & 7);
                assert(inst_->size == opsize::l);
                r = val;
            }
        }
    }

    void handle_MOVEQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift == ea_m_inst_data) && inst_->ea[0] <= ea_disp && (inst_->ea[1] >> ea_m_shift == ea_m_Dn));
        const uint32_t src = static_cast<int32_t>(static_cast<int8_t>(read_ea(0)));
        write_ea(1, src);
        update_flags(srm_ccr_no_x, src, 0);
    }

    void handle_MULU()
    {
        assert(inst_->size == opsize::w && inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto a = static_cast<uint16_t>(read_ea(0) & 0xffff);
        const auto b = static_cast<uint16_t>(read_ea(1) & 0xffff);
        const uint32_t res = static_cast<uint32_t>(a) * b;
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
    }

    void handle_NOT()
    {
        assert(inst_->nea == 1);
        auto n = ~read_ea(0);
        update_flags(srm_ccr_no_x, n, 0);
        write_ea(0, n);
    }

    void handle_OR()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l | r;
        write_ea(1, res);
        update_flags(srm_ccr_no_x, res, 0);
    }

    void handle_PEA()
    {
        assert(inst_->nea == 1 && inst_->size == opsize::l);
        push_u32(ea_data_[0]);
    }

    void handle_RTS()
    {
        assert(inst_->nea == 0);
        auto& a7 = state_.A(7);
        state_.pc = mem_.read_u32(a7);
        a7 += 4;
    }

    void handle_SUB()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r;
        write_ea(1, res);
        // All flags updated
        update_flags(srm_ccr, res, (~l & r) | (~(l ^ r) & res));
    }

    void handle_SUBA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        state_.A(inst_->ea[1] & ea_xn_mask) -= s; // And the operation performed on the full 32-bit value
        // No flags
    }

    void handle_SUBQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift == ea_m_inst_data) && inst_->ea[0] <= ea_disp);
        handle_SUB();
    }

    void handle_SWAP()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift == ea_m_Dn));
        auto& r = state_.d[inst_->ea[0] & ea_xn_mask];
        r = (r & 0xffff) << 16 | ((r >> 16) & 0xffff);
        update_flags(srm_ccr_no_x, r, 0);
    }

    void handle_TST()
    {
        assert(inst_->nea == 1);
        update_flags(srm_ccr_no_x, read_ea(0), 0);
    }
};

int main()
{
    try {
        //const char* const rom_file = "../../Misc/DiagROM/DiagROM";
        const char* const rom_file = "../../Misc/AmigaKickstart/Kickstart 1.3 A500.rom";
        //const char* const rom_file = "../../rom.bin";
        memory_handler mem { 1U<<20 };
        rom_area_handler rom { mem, read_file(rom_file) };
        cia_handler cias { mem, rom };
        custom_handler custom { mem };
        m68000 cpu { mem };

        for (;;) {
            try {
                cpu.step();
                //if (cpu.state().pc == 0xFC00E2) // After delay loop
                //    cpu.trace(true);
                //if (cpu.state().pc == 0x00fc08f6)
                //    cpu.trace(true);
                //if (cpu.instruction_count() == 7110-2)
                //    cpu.trace(true);
                //else if (cpu.instruction_count() == 7110+2)
                //    break;
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
