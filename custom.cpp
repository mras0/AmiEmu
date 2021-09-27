#include "custom.h"
#include "ioutil.h"

#include <iostream>

class custom_handler::impl : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler)
        : mem_handler_ { mem_handler }
    {
        mem_handler_.register_handler(*this, 0xDFF000, 0x1000);
    }

    void step()
    {
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        switch (offset) {
        case POTGOR:
            std::cerr << "[CUSTOM] Ignoring read from " << regname(offset) << "\n";
            return 0xff;
        }
        std::cerr << "Unhandled read from custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")\n";
        return 0xff;
    }
    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        if (offset == SERDATR) {
            // Don't spam in DiagROM
            return 0xff;
        }

        std::cerr << "Unhandled read from custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")\n";
        return 0xffff;
    }
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")"
                  << " val $" << hexfmt(val) << "\n";
    }
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        switch (offset) {
        case SERDAT:
            std::cerr << "[CUSTOM] Serial output ($" << hexfmt(val) << ") '" << (isprint(val&0xff)?static_cast<char>(val&0xff):' ') << "'\n"; 
            return;
        case INTREQ:
            // Don't spam
            return;

        case SERPER:
        case POTGO:
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

        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")" << " val $" << hexfmt(val) << "\n";
    }

private:
    memory_handler& mem_handler_;

    //uint16_t hpos = 0; // 1/160 of screen width (280 ns)
    //uint16_t vpos = 0;

    // Name, Offset, R(=0)/W(=1)
#define CUSTOM_REGS(X) \
    X(BLTDDAT , 0x000 , 0) /* Blitter destination early read (dummy address)          */ \
    X(DMACONR , 0x002 , 0) /* DMA control (and blitter status) read                   */ \
    X(VPOSR   , 0x004 , 0) /* Read vert most signif. bit (and frame flop)             */ \
    X(VHPOSR  , 0x006 , 0) /* Read vert and horiz. position of beam                   */ \
    X(POTGOR  , 0x016 , 0) /* Pot port data read(formerly POTINP)                     */ \
    X(SERDATR , 0x018 , 0) /* Serial port data and status read                        */ \
    X(SERDAT  , 0x030 , 1) /*                                                         */ \
    X(SERPER  , 0x032 , 1) /* Serial port period and control                          */ \
    X(POTGO   , 0x034 , 1) /* Pot port data write and start                           */ \
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

custom_handler::custom_handler(memory_handler& mem_handler)
    : impl_ { std::make_unique<impl>(mem_handler) }
{
}

custom_handler::~custom_handler() = default;

void custom_handler::step()
{
    impl_->step();
}
