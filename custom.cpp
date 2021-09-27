#include "custom.h"
#include "ioutil.h"
#include "cia.h"
#include "debug.h"

#include <cassert>
#include <utility>
#include <iostream>

#define TODO_ASSERT(expr) do { if (!(expr)) throw std::runtime_error{("TODO: " #expr " in ") + std::string{__FILE__} + " line " + std::to_string(__LINE__) }; } while (0)

#define DBGOUT *debug_stream << "vpos=$" << hexfmt(s_.vpos) << " hpos=$" << hexfmt(s_.hpos) << " (clock $" << hexfmt(s_.hpos >> 1, 2) << ") "

    // Name, Offset, R(=0)/W(=1)
#define CUSTOM_REGS(X) \
    X(BLTDDAT  , 0x000 , 0) /* Blitter destination early read (dummy address)          */ \
    X(DMACONR  , 0x002 , 0) /* DMA control (and blitter status) read                   */ \
    X(VPOSR    , 0x004 , 0) /* Read vert most signif. bit (and frame flop)             */ \
    X(VHPOSR   , 0x006 , 0) /* Read vert and horiz. position of beam                   */ \
    X(JOY0DAT  , 0x00A , 0) /* Joystick-mouse 0 data (vert,horiz)                      */ \
    X(JOY1DAT  , 0x00C , 0) /* Joystick-mouse 1 data (vert,horiz)                      */ \
    X(POTGOR   , 0x016 , 0) /* Pot port data read(formerly POTINP)                     */ \
    X(SERDATR  , 0x018 , 0) /* Serial port data and status read                        */ \
    X(INTENAR  , 0x01C , 0) /* Interrupt enable bits read                              */ \
    X(INTREQR  , 0x01E , 0) /* Interrupt request bits read                             */ \
    X(DSKPTH   , 0x020 , 1) /* Disk pointer (high 3 bits)                              */ \
    X(DSKPTL   , 0x022 , 1) /* Disk pointer (low 15 bits)                              */ \
    X(DSKLEN   , 0x024 , 1) /* Disk length                                             */ \
    X(DSKDAT   , 0x026 , 1) /* Disk DMA data write                                     */ \
    X(REFPTR   , 0x028 , 1) /* Refresh pointer                                         */ \
    X(VPOSW    , 0x02A , 1) /* Write vert most signif. bit (and frame flop)            */ \
    X(VHPOSW   , 0x02C , 1) /* Write vert and horiz position of beam                   */ \
    X(COPCON   , 0x02E , 1) /* Coprocessor control register (CDANG)                    */ \
    X(SERDAT   , 0x030 , 1) /* Serial port data and stop bits write                    */ \
    X(SERPER   , 0x032 , 1) /* Serial port period and control                          */ \
    X(POTGO    , 0x034 , 1) /* Pot port data write and start                           */ \
    X(BLTCON0  , 0x040 , 1) /* Blitter control register 0                              */ \
    X(BLTCON1  , 0x042 , 1) /* Blitter control register 1                              */ \
    X(BLTAFWM  , 0x044 , 1) /* Blitter first word mask for source A                    */ \
    X(BLTALWM  , 0x046 , 1) /* Blitter last word mask for source A                     */ \
    X(BLTCPTH  , 0x048 , 1) /* Blitter pointer to source C (high 3 bits)               */ \
    X(BLTCPTL  , 0x04A , 1) /* Blitter pointer to source C (low 15 bits)               */ \
    X(BLTBPTH  , 0x04C , 1) /* Blitter pointer to source B (high 3 bits)               */ \
    X(BLTBPTL  , 0x04E , 1) /* Blitter pointer to source B (low 15 bits)               */ \
    X(BLTAPTH  , 0x050 , 1) /* Blitter pointer to source A (high 3 bits)               */ \
    X(BLTAPTL  , 0x052 , 1) /* Blitter pointer to source A (low 15 bits)               */ \
    X(BLTDPTH  , 0x054 , 1) /* Blitter pointer to destination D (high 3 bits)          */ \
    X(BLTDPTL  , 0x056 , 1) /* Blitter pointer to destination D (low 15 bits)          */ \
    X(BLTSIZE  , 0x058 , 1) /* Blitter start and size (window width,height)            */ \
    X(BLTCON0L , 0x05A , 1) /* Blitter control 0, lower 8 bits (minterms)              */ \
    X(BLTSIZV  , 0x05C , 1) /* Blitter V size (for 15 bit vertical size)               */ \
    X(BLTSIZH  , 0x05E , 1) /* Blitter H size and start (for 11 bit H size)            */ \
    X(BLTCMOD  , 0x060 , 1) /* Blitter modulo for source C                             */ \
    X(BLTBMOD  , 0x062 , 1) /* Blitter modulo for source B                             */ \
    X(BLTAMOD  , 0x064 , 1) /* Blitter modulo for source A                             */ \
    X(BLTDMOD  , 0x066 , 1) /* Blitter modulo for destination D                        */ \
    X(BLTCDAT  , 0x070 , 1) /* Blitter source C data register                          */ \
    X(BLTBDAT  , 0x072 , 1) /* Blitter source B data register                          */ \
    X(BLTADAT  , 0x074 , 1) /* Blitter source A data register                          */ \
    X(DSKSYNC  , 0x07E , 1) /* Disk sync register                                      */ \
    X(COP1LCH  , 0x080 , 1) /* Coprocessor first location register (high 3 bits)       */ \
    X(COP1LCL  , 0x082 , 1) /* Coprocessor first location register (low 15 bits)       */ \
    X(COP2LCH  , 0x084 , 1) /* Coprocessor second location register (high 3 bits)      */ \
    X(COP2LCL  , 0x086 , 1) /* Coprocessor second location register (low 15 bits)      */ \
    X(COPJMP1  , 0x088 , 1) /* Coprocessor restart at first location                   */ \
    X(COPJMP2  , 0x08A , 1) /* Coprocessor restart at second location                  */ \
    X(DIWSTRT  , 0x08E , 1) /* Display window start (upper left vert-horiz position)   */ \
    X(DIWSTOP  , 0x090 , 1) /* Display window stop (lower right vert.-horiz. position) */ \
    X(DDFSTRT  , 0x092 , 1) /* Display bitplane data fetch start (horiz. position)     */ \
    X(DDFSTOP  , 0x094 , 1) /* Display bitplane data fetch stop (horiz. position)      */ \
    X(DMACON   , 0x096 , 1) /* DMA control write (clear or set)                        */ \
    X(INTENA   , 0x09A , 1) /* Interrupt enable bits (clear or set bits)               */ \
    X(INTREQ   , 0x09C , 1) /* Interrupt request bits (clear or set bits)              */ \
    X(ADKCON   , 0x09E , 1) /* Audio, disk, UART control                               */ \
    X(AUD0LCH  , 0x0A0 , 1) /* Audio channel 0 location (high 3 bits)                  */ \
    X(AUD0LCL  , 0x0A2 , 1) /* Audio channel 0 location (low 15 bits)                  */ \
    X(AUD0LEN  , 0x0A4 , 1) /* Audio channel 0 length                                  */ \
    X(AUD0PER  , 0x0A6 , 1) /* Audio channel 0 period                                  */ \
    X(AUD0VOL  , 0x0A8 , 1) /* Audio channel 0 volume                                  */ \
    X(AUD0DAT  , 0x0AA , 1) /* Audio channel 0 data                                    */ \
    X(AUD1LCH  , 0x0B0 , 1) /* Audio channel 1 location (high 3 bits)                  */ \
    X(AUD1LCL  , 0x0B2 , 1) /* Audio channel 1 location (low 15 bits)                  */ \
    X(AUD1LEN  , 0x0B4 , 1) /* Audio channel 1 length                                  */ \
    X(AUD1PER  , 0x0B6 , 1) /* Audio channel 1 period                                  */ \
    X(AUD1VOL  , 0x0B8 , 1) /* Audio channel 1 volume                                  */ \
    X(AUD1DAT  , 0x0BA , 1) /* Audio channel 1 data                                    */ \
    X(AUD2LCH  , 0x0C0 , 1) /* Audio channel 2 location (high 3 bits)                  */ \
    X(AUD2LCL  , 0x0C2 , 1) /* Audio channel 2 location (low 15 bits)                  */ \
    X(AUD2LEN  , 0x0C4 , 1) /* Audio channel 2 length                                  */ \
    X(AUD2PER  , 0x0C6 , 1) /* Audio channel 2 period                                  */ \
    X(AUD2VOL  , 0x0C8 , 1) /* Audio channel 2 volume                                  */ \
    X(AUD2DAT  , 0x0CA , 1) /* Audio channel 2 data                                    */ \
    X(AUD3LCH  , 0x0D0 , 1) /* Audio channel 3 location (high 3 bits)                  */ \
    X(AUD3LCL  , 0x0D2 , 1) /* Audio channel 3 location (low 15 bits)                  */ \
    X(AUD3LEN  , 0x0D4 , 1) /* Audio channel 3 length                                  */ \
    X(AUD3PER  , 0x0D6 , 1) /* Audio channel 3 period                                  */ \
    X(AUD3VOL  , 0x0D8 , 1) /* Audio channel 3 volume                                  */ \
    X(BPL1PTH  , 0x0E0 , 1) /* Bitplane 1 pointer (high 3 bits)                        */ \
    X(BPL1PTL  , 0x0E2 , 1) /* Bitplane 1 pointer (low 15 bits)                        */ \
    X(BPL2PTH  , 0x0E4 , 1) /* Bitplane 2 pointer (high 3 bits)                        */ \
    X(BPL2PTL  , 0x0E6 , 1) /* Bitplane 2 pointer (low 15 bits)                        */ \
    X(BPL3PTH  , 0x0E8 , 1) /* Bitplane 3 pointer (high 3 bits)                        */ \
    X(BPL3PTL  , 0x0EA , 1) /* Bitplane 3 pointer (low 15 bits)                        */ \
    X(BPL4PTH  , 0x0EC , 1) /* Bitplane 4 pointer (high 3 bits)                        */ \
    X(BPL4PTL  , 0x0EE , 1) /* Bitplane 4 pointer (low 15 bits)                        */ \
    X(BPL5PTH  , 0x0F0 , 1) /* Bitplane 5 pointer (high 3 bits)                        */ \
    X(BPL5PTL  , 0x0F2 , 1) /* Bitplane 5 pointer (low 15 bits)                        */ \
    X(BPL6PTH  , 0x0F4 , 1) /* Bitplane 6 pointer (high 3 bits)                        */ \
    X(BPL6PTL  , 0x0F6 , 1) /* Bitplane 6 pointer (low 15 bits)                        */ \
    X(BPLCON0  , 0x100 , 1) /* Bitplane control register                               */ \
    X(BPLCON1  , 0x102 , 1) /* Bitplane control register                               */ \
    X(BPLCON2  , 0x104 , 1) /* Bitplane control register                               */ \
    X(BPLCON3  , 0x106 , 1) /* Bitplane control register  (ECS only)                   */ \
    X(BPLMOD1  , 0x108 , 1) /* Bitplane modulo (odd planes)                            */ \
    X(BPLMOD2  , 0x10A , 1) /* Bitplane modulo (even planes)                           */ \
    X(BPL1DAT  , 0x110 , 1) /* Bitplane 1 data (parallel-to-serial convert)            */ \
    X(BPL2DAT  , 0x112 , 1) /* Bitplane 2 data (parallel-to-serial convert)            */ \
    X(BPL3DAT  , 0x114 , 1) /* Bitplane 3 data (parallel-to-serial convert)            */ \
    X(BPL4DAT  , 0x116 , 1) /* Bitplane 4 data (parallel-to-serial convert)            */ \
    X(BPL5DAT  , 0x118 , 1) /* Bitplane 5 data (parallel-to-serial convert)            */ \
    X(BPL6DAT  , 0x11A , 1) /* Bitplane 6 data (parallel-to-serial convert)            */ \
    X(SPR0PTH  , 0x120 , 1) /* Sprite 0 pointer (high 3 bits)                          */ \
    X(SPR0PTL  , 0x122 , 1) /* Sprite 0 pointer (low 15 bits)                          */ \
    X(SPR1PTH  , 0x124 , 1) /* Sprite 1 pointer (high 3 bits)                          */ \
    X(SPR1PTL  , 0x126 , 1) /* Sprite 1 pointer (low 15 bits)                          */ \
    X(SPR2PTH  , 0x128 , 1) /* Sprite 2 pointer (high 3 bits)                          */ \
    X(SPR2PTL  , 0x12A , 1) /* Sprite 2 pointer (low 15 bits)                          */ \
    X(SPR3PTH  , 0x12C , 1) /* Sprite 3 pointer (high 3 bits)                          */ \
    X(SPR3PTL  , 0x12E , 1) /* Sprite 3 pointer (low 15 bits)                          */ \
    X(SPR4PTH  , 0x130 , 1) /* Sprite 4 pointer (high 3 bits)                          */ \
    X(SPR4PTL  , 0x132 , 1) /* Sprite 4 pointer (low 15 bits)                          */ \
    X(SPR5PTH  , 0x134 , 1) /* Sprite 5 pointer (high 3 bits)                          */ \
    X(SPR5PTL  , 0x136 , 1) /* Sprite 5 pointer (low 15 bits)                          */ \
    X(SPR6PTH  , 0x138 , 1) /* Sprite 6 pointer (high 3 bits)                          */ \
    X(SPR6PTL  , 0x13A , 1) /* Sprite 6 pointer (low 15 bits)                          */ \
    X(SPR7PTH  , 0x13C , 1) /* Sprite 7 pointer (high 3 bits)                          */ \
    X(SPR7PTL  , 0x13E , 1) /* Sprite 7 pointer (low 15 bits)                          */ \
    X(SPR0POS  , 0x140 , 1) /* Sprite 0 vert-horiz start position data                 */ \
    X(SPR0CTL  , 0x142 , 1) /* Sprite 0 vert stop position and control data            */ \
    X(SPR0DATA , 0x144 , 1) /* Sprite 0 image data register A                          */ \
    X(SPR0DATB , 0x146 , 1) /* Sprite 0 image data register B                          */ \
    X(SPR1POS  , 0x148 , 1) /* Sprite 1 vert-horiz start position data                 */ \
    X(SPR1CTL  , 0x14A , 1) /* Sprite 1 vert stop position and control data            */ \
    X(SPR1DATA , 0x14C , 1) /* Sprite 1 image data register A                          */ \
    X(SPR1DATB , 0x14E , 1) /* Sprite 1 image data register B                          */ \
    X(SPR2POS  , 0x150 , 1) /* Sprite 2 vert-horiz start position data                 */ \
    X(SPR2CTL  , 0x152 , 1) /* Sprite 2 vert stop position and control data            */ \
    X(SPR2DATA , 0x154 , 1) /* Sprite 2 image data register A                          */ \
    X(SPR2DATB , 0x156 , 1) /* Sprite 2 image data register B                          */ \
    X(SPR3POS  , 0x158 , 1) /* Sprite 3 vert-horiz start position data                 */ \
    X(SPR3CTL  , 0x15A , 1) /* Sprite 3 vert stop position and control data            */ \
    X(SPR3DATA , 0x15C , 1) /* Sprite 3 image data register A                          */ \
    X(SPR3DATB , 0x15E , 1) /* Sprite 3 image data register B                          */ \
    X(SPR4POS  , 0x160 , 1) /* Sprite 4 vert-horiz start position data                 */ \
    X(SPR4CTL  , 0x162 , 1) /* Sprite 4 vert stop position and control data            */ \
    X(SPR4DATA , 0x164 , 1) /* Sprite 4 image data register A                          */ \
    X(SPR4DATB , 0x166 , 1) /* Sprite 4 image data register B                          */ \
    X(SPR5POS  , 0x168 , 1) /* Sprite 5 vert-horiz start position data                 */ \
    X(SPR5CTL  , 0x16A , 1) /* Sprite 5 vert stop position and control data            */ \
    X(SPR5DATA , 0x16C , 1) /* Sprite 5 image data register A                          */ \
    X(SPR5DATB , 0x16E , 1) /* Sprite 5 image data register B                          */ \
    X(SPR6POS  , 0x170 , 1) /* Sprite 6 vert-horiz start position data                 */ \
    X(SPR6CTL  , 0x172 , 1) /* Sprite 6 vert stop position and control data            */ \
    X(SPR6DATA , 0x174 , 1) /* Sprite 6 image data register A                          */ \
    X(SPR6DATB , 0x176 , 1) /* Sprite 6 image data register B                          */ \
    X(SPR7POS  , 0x178 , 1) /* Sprite 7 vert-horiz start position data                 */ \
    X(SPR7CTL  , 0x17A , 1) /* Sprite 7 vert stop position and control data            */ \
    X(SPR7DATA , 0x17C , 1) /* Sprite 7 image data register A                          */ \
    X(SPR7DATB , 0x17E , 1) /* Sprite 7 image data register B                          */ \
    X(COLOR00  , 0x180 , 1) /* Color table 00                                          */ \
    X(COLOR01  , 0x182 , 1) /* Color table 01                                          */ \
    X(COLOR02  , 0x184 , 1) /* Color table 02                                          */ \
    X(COLOR03  , 0x186 , 1) /* Color table 03                                          */ \
    X(COLOR04  , 0x188 , 1) /* Color table 04                                          */ \
    X(COLOR05  , 0x18A , 1) /* Color table 05                                          */ \
    X(COLOR06  , 0x18C , 1) /* Color table 06                                          */ \
    X(COLOR07  , 0x18E , 1) /* Color table 07                                          */ \
    X(COLOR08  , 0x190 , 1) /* Color table 08                                          */ \
    X(COLOR09  , 0x192 , 1) /* Color table 09                                          */ \
    X(COLOR10  , 0x194 , 1) /* Color table 10                                          */ \
    X(COLOR11  , 0x196 , 1) /* Color table 11                                          */ \
    X(COLOR12  , 0x198 , 1) /* Color table 12                                          */ \
    X(COLOR13  , 0x19A , 1) /* Color table 13                                          */ \
    X(COLOR14  , 0x19C , 1) /* Color table 14                                          */ \
    X(COLOR15  , 0x19E , 1) /* Color table 15                                          */ \
    X(COLOR16  , 0x1A0 , 1) /* Color table 16                                          */ \
    X(COLOR17  , 0x1A2 , 1) /* Color table 17                                          */ \
    X(COLOR18  , 0x1A4 , 1) /* Color table 18                                          */ \
    X(COLOR19  , 0x1A6 , 1) /* Color table 19                                          */ \
    X(COLOR20  , 0x1A8 , 1) /* Color table 20                                          */ \
    X(COLOR21  , 0x1AA , 1) /* Color table 21                                          */ \
    X(COLOR22  , 0x1AC , 1) /* Color table 22                                          */ \
    X(COLOR23  , 0x1AE , 1) /* Color table 23                                          */ \
    X(COLOR24  , 0x1B0 , 1) /* Color table 24                                          */ \
    X(COLOR25  , 0x1B2 , 1) /* Color table 25                                          */ \
    X(COLOR26  , 0x1B4 , 1) /* Color table 26                                          */ \
    X(COLOR27  , 0x1B6 , 1) /* Color table 27                                          */ \
    X(COLOR28  , 0x1B8 , 1) /* Color table 28                                          */ \
    X(COLOR29  , 0x1BA , 1) /* Color table 29                                          */ \
    X(COLOR30  , 0x1BC , 1) /* Color table 30                                          */ \
    X(COLOR31  , 0x1BE , 1) /* Color table 31                                          */ \
    X(DIWHIGH  , 0x1E4 , 1) /* Display window - upper bits for start/stop (AGA)        */ \
    X(FMODE    , 0x1FC , 1) /* Fetch mode (AGA)                                        */ \
// keep this line clear (for macro continuation)


namespace {

enum regnum {
#define REG_NUM(name, offset, _) name = offset,
    CUSTOM_REGS(REG_NUM)
#undef REG_NUM
};

constexpr uint16_t DMAB_SETCLR   = 15; // Set/clear control bit. Determines if bits written with a 1 get set or cleared.Bits written with a zero are unchanged.
constexpr uint16_t DMAB_BLTDONE  = 14; // Blitter busy status bit (read only) 
constexpr uint16_t DMAB_BLTNZERO = 13; // Blitter logic zero status bit  (read only).
constexpr uint16_t DMAB_BLITHOG  = 10; // Blitter DMA priority (over CPU micro) 
constexpr uint16_t DMAB_MASTER   =  9; // Enable all DMA below
constexpr uint16_t DMAB_RASTER   =  8; // Bitplane DMA enable
constexpr uint16_t DMAB_COPPER   =  7; // Copper DMA enable
constexpr uint16_t DMAB_BLITTER  =  6; // Blitter DMA enable
constexpr uint16_t DMAB_SPRITE   =  5; // Sprite DMA enable
constexpr uint16_t DMAB_DISK     =  4; // Disk DMA enable
constexpr uint16_t DMAB_AUD3     =  3; // Audio channel 3 DMA enable
constexpr uint16_t DMAB_AUD2     =  2; // Audio channel 2 DMA enable
constexpr uint16_t DMAB_AUD1     =  1; // Audio channel 1 DMA enable
constexpr uint16_t DMAB_AUD0     =  0; // Audio channel 0 DMA enable

constexpr uint16_t DMAF_SETCLR   = 1 << DMAB_SETCLR;
constexpr uint16_t DMAF_BLTDONE  = 1 << DMAB_BLTDONE;
constexpr uint16_t DMAF_BLTNZERO = 1 << DMAB_BLTNZERO;
constexpr uint16_t DMAF_BLITHOG  = 1 << DMAB_BLITHOG;
constexpr uint16_t DMAF_MASTER   = 1 << DMAB_MASTER;
constexpr uint16_t DMAF_RASTER   = 1 << DMAB_RASTER;
constexpr uint16_t DMAF_COPPER   = 1 << DMAB_COPPER;
constexpr uint16_t DMAF_BLITTER  = 1 << DMAB_BLITTER;
constexpr uint16_t DMAF_SPRITE   = 1 << DMAB_SPRITE;
constexpr uint16_t DMAF_DISK     = 1 << DMAB_DISK;
constexpr uint16_t DMAF_AUD3     = 1 << DMAB_AUD3;
constexpr uint16_t DMAF_AUD2     = 1 << DMAB_AUD2;
constexpr uint16_t DMAF_AUD1     = 1 << DMAB_AUD1;
constexpr uint16_t DMAF_AUD0     = 1 << DMAB_AUD0;


constexpr uint16_t INTB_SETCLR  = 15; // Set/Clear control bit. Determines if bits written with a 1 get set or cleared. Bits written with a zero are allways unchanged
constexpr uint16_t INTB_INTEN   = 14; // Master interrupt (enable only)
constexpr uint16_t INTB_EXTER   = 13; // External interrupt
constexpr uint16_t INTB_DSKSYNC = 12; // Disk re-SYNChronized
constexpr uint16_t INTB_RBF     = 11; // serial port Receive Buffer Full
constexpr uint16_t INTB_AUD3    = 10; // Audio channel 3 block finished
constexpr uint16_t INTB_AUD2    =  9; // Audio channel 2 block finished
constexpr uint16_t INTB_AUD1    =  8; // Audio channel 1 block finished
constexpr uint16_t INTB_AUD0    =  7; // Audio channel 0 block finished
constexpr uint16_t INTB_BLIT    =  6; // Blitter finished
constexpr uint16_t INTB_VERTB   =  5; // start of Vertical Blank
constexpr uint16_t INTB_COPER   =  4; // Coprocessor
constexpr uint16_t INTB_PORTS   =  3; // I/O Ports and timers
constexpr uint16_t INTB_SOFTINT =  2; // software interrupt request
constexpr uint16_t INTB_DSKBLK  =  1; // Disk Block done
constexpr uint16_t INTB_TBE     =  0; // serial port Transmit Buffer Empty

constexpr uint16_t INTF_SETCLR  = 1 << INTB_SETCLR;
constexpr uint16_t INTF_INTEN   = 1 << INTB_INTEN;
constexpr uint16_t INTF_EXTER   = 1 << INTB_EXTER;
constexpr uint16_t INTF_DSKSYNC = 1 << INTB_DSKSYNC;
constexpr uint16_t INTF_RBF     = 1 << INTB_RBF;
constexpr uint16_t INTF_AUD3    = 1 << INTB_AUD3;
constexpr uint16_t INTF_AUD2    = 1 << INTB_AUD2;
constexpr uint16_t INTF_AUD1    = 1 << INTB_AUD1;
constexpr uint16_t INTF_AUD0    = 1 << INTB_AUD0;
constexpr uint16_t INTF_BLIT    = 1 << INTB_BLIT;
constexpr uint16_t INTF_VERTB   = 1 << INTB_VERTB;
constexpr uint16_t INTF_COPER   = 1 << INTB_COPER;
constexpr uint16_t INTF_PORTS   = 1 << INTB_PORTS;
constexpr uint16_t INTF_SOFTINT = 1 << INTB_SOFTINT;
constexpr uint16_t INTF_DSKBLK  = 1 << INTB_DSKBLK;
constexpr uint16_t INTF_TBE     = 1 << INTB_TBE;

/*
                 BIT#     BPLCON0    BPLCON1    BPLCON2
                 ----     --------   --------   --------
                 15       HIRES       X           X
                 14       BPU2        X           X
                 13       BPU1        X           X
                 12       BPU0        X           X
                 11       HOMOD       X           X
                 10       DBLPF       X           X
                 09       COLOR       X           X
                 08       GAUD        X           X
                 07        X         PF2H3        X
                 06        X         PF2H2      PF2PRI
                 05        X         PF2H1      PF2P2
                 04        X         PF2H0      PF2P1
                 03       LPEN       PF1H3      PF2P0
                 02       LACE       PF1H2      PF1P2
                 01       ERSY       PF1H1      PF1P1
                 00        X         PF1H0      PF1P0 */
constexpr uint16_t BPLCON0B_HIRES = 15; // High-resolution (70 ns pixels)
constexpr uint16_t BPLCON0B_BPU2  = 14; // Bitplane use code 000-110 (NONE through 6 inclusive)
constexpr uint16_t BPLCON0B_BPU1  = 13; // 
constexpr uint16_t BPLCON0B_BPU0  = 12; // 
constexpr uint16_t BPLCON0B_HOMOD = 11; // Hold-and-modify mode (1 = Hold-and-modify mode (HAM); 0 = Extra Half Brite (EHB) if HAM=0 and BPU=6 and DBLPF=0 then bitplane 6 controls an intensity reduction in the other five bitplanes)
constexpr uint16_t BPLCON0B_DBLPF = 10; // Double playfield (PF1=odd PF2=even bitplanes)
constexpr uint16_t BPLCON0B_COLOR =  9; // Composite video COLOR enable
constexpr uint16_t BPLCON0B_GUAD  =  8; // Genlock audio enable (muxed on BKGND pin during vertical blanking
constexpr uint16_t BPLCON0B_LPEN  =  3; // Light pen enable (reset on power up)
constexpr uint16_t BPLCON0B_LACE  =  2; // Interlace enable (reset on power up)
constexpr uint16_t BPLCON0B_ERSY  =  1; // External resync (HSYNC, VSYNC pads become inputs) (reset on power up)

constexpr uint16_t BPLCON0F_HIRES = 1 << BPLCON0B_HIRES;
constexpr uint16_t BPLCON0F_BPU   = 7 << BPLCON0B_BPU0;
constexpr uint16_t BPLCON0F_HOMOD = 1 << BPLCON0B_HOMOD;
constexpr uint16_t BPLCON0F_DBLPF = 1 << BPLCON0B_DBLPF;
constexpr uint16_t BPLCON0F_COLOR = 1 << BPLCON0B_COLOR;
constexpr uint16_t BPLCON0F_GUAD  = 1 << BPLCON0B_GUAD;
constexpr uint16_t BPLCON0F_LPEN  = 1 << BPLCON0B_LPEN;
constexpr uint16_t BPLCON0F_LACE  = 1 << BPLCON0B_LACE;
constexpr uint16_t BPLCON0F_ERSY  = 1 << BPLCON0B_ERSY;


// BLTCON0
constexpr uint16_t BC0F_SRCA         = 0x0800;
constexpr uint16_t BC0F_SRCB         = 0x0400;
constexpr uint16_t BC0F_SRCC         = 0x0200;
constexpr uint16_t BC0F_DEST         = 0x0100;
constexpr uint16_t BC0F_ABC          = 0x0080;
constexpr uint16_t BC0F_ABNC         = 0x0040;
constexpr uint16_t BC0F_ANBC         = 0x0020;
constexpr uint16_t BC0F_ANBNC        = 0x0010;
constexpr uint16_t BC0F_NABC         = 0x0008;
constexpr uint16_t BC0F_NABNC        = 0x0004;
constexpr uint16_t BC0F_NANBC        = 0x0002;
constexpr uint16_t BC0F_NANBNC       = 0x0001;

// BLTCON1
constexpr uint16_t BC1F_FILL_XOR     = 0x0010; // Exclusive fill enable
constexpr uint16_t BC1F_FILL_OR      = 0x0008; // Inclusive fill enable
constexpr uint16_t BC1F_FILL_CARRYIN = 0x0004; // Fill carry input
constexpr uint16_t BC1F_BLITREVERSE  = 0x0002; // Descending (dec address)

constexpr uint16_t BC1F_SIGNFLAG     = 0x0040; // Sign flag (Line mode)
constexpr uint16_t BC1F_OVFLAG       = 0x0020; // Line/draw r/l word overflow flag (Line mode)
constexpr uint16_t BC1F_SUD          = 0x0010; // Sometimes up or down (=AUD) (Line mode)
constexpr uint16_t BC1F_SUL          = 0x0008; // Sometimes up or left (Line mode)
constexpr uint16_t BC1F_AUL          = 0x0004; // Always up or left (Line mode)
constexpr uint16_t BC1F_ONEDOT       = 0x0002; // one dot per horizontal line
constexpr uint16_t BC1F_LINEMODE     = 0x0001; // Line mode control bit

const uint8_t BC0_ASHIFTSHIFT = 12; // bits to right align ashift value
const uint8_t BC1_BSHIFTSHIFT = 12; // bits to right align bshift value

// 454 virtual Lores pixels
// 625 lines/frame (interlaced)
// https://retrocomputing.stackexchange.com/questions/44/how-to-obtain-256-arbitrary-colors-with-limitation-of-64-per-line-in-amiga-ecs

// Maximum overscan (http://coppershade.org/articles/AMIGA/Denise/Maximum_Overscan/)
// DIWSTRT = $1b51
// DIWSTOP = $37d1
// DDFSTRT = $0020
// DDFSTOP = $00d8
// BPLCON2 = $0000

// Color clocks per line
// 0..$E2 (227.5 actually, on NTSC they alternate between 227 and 228)
// 64us per line (52us visible), ~454 virtual lorespixels (~369 max visible)
// Each color clock produces 2 lores or 4 hires pixels

static constexpr uint16_t hpos_per_line    = 455; // 227.5 color clocks, lores pixels

static constexpr uint16_t lores_min_pixel  = 0x51; 
static constexpr uint16_t lores_max_pixel  = 0x1d1;
static constexpr uint16_t hires_min_pixel  = 2 * lores_min_pixel;
static constexpr uint16_t hires_max_pixel  = 2 * lores_max_pixel;

static constexpr uint16_t vpos_per_field  = 313;
//static constexpr uint16_t vpos_per_frame  = 625;
static constexpr uint16_t vblank_end_vpos = 28;
static constexpr uint16_t sprite_dma_start_vpos = 25; // http://eab.abime.net/showpost.php?p=1048395&postcount=200

static_assert(graphics_width == hires_max_pixel - hires_min_pixel);
static_assert(graphics_height == (vpos_per_field - vblank_end_vpos) * 2);

constexpr uint32_t rgb4_to_8(const uint16_t rgb4)
{
    const uint32_t r = (rgb4 >> 8) & 0xf;
    const uint32_t g = (rgb4 >> 4) & 0xf;
    const uint32_t b = rgb4 & 0xf;
    return r << 20 | r << 16 | g << 12 | g << 8 | b << 4 | b;
}

constexpr uint16_t blitter_func(uint8_t minterm, uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t val = 0;
    if (minterm & BC0F_ABC)    val |=   a &  b &  c;
    if (minterm & BC0F_ABNC)   val |=   a &  b &(~c);
    if (minterm & BC0F_ANBC)   val |=   a &(~b)&  c;
    if (minterm & BC0F_ANBNC)  val |=   a &(~b)&(~c);
    if (minterm & BC0F_NABC)   val |= (~a)&  b &  c;
    if (minterm & BC0F_NABNC)  val |= (~a)&  b &(~c);
    if (minterm & BC0F_NANBC)  val |= (~a)&(~b)&  c;
    if (minterm & BC0F_NANBNC) val |= (~a)&(~b)&(~c);
    return val;
}

template<std::integral T>
constexpr T rol(T val, unsigned amt)
{
    return val << amt | val >> (sizeof(T) * CHAR_BIT - amt);
}

template <std::integral T>
constexpr T ror(T val, unsigned amt)
{
    return val >> amt | val << (sizeof(T) * CHAR_BIT - amt);
}

enum class ddfstate {
    before_ddfstrt,
    active,
    ddfstop_passed,
    stopped,
};

enum class sprite_dma_state {
    fetch_ctl,
    fetch_data,
    stopped,
};

enum class sprite_vpos_state {
    vpos_disabled,
    vpos_waiting,
    vpos_active,
};

struct custom_state {
    // Internal state
    uint16_t hpos; // Resolution is in low-res pixels
    uint16_t vpos;
    uint16_t bpldat_shift[6];
    uint16_t bpldat_temp[6];
    bool bpl1dat_written;
    bool bpl1dat_written_this_line;
    uint8_t bpldata_avail;
    uint32_t ham_color;
    ddfstate ddfst;
    uint16_t ddfcycle;
    uint16_t ddfend;
    bool long_frame;

    bool rmb_pressed;
    uint8_t cur_mouse_x;
    uint8_t cur_mouse_y;

    uint32_t dskpt;
    uint16_t dsklen;
    uint16_t dskwait; // HACK
    uint16_t dsklen_act;
    uint16_t dsksync;
    bool dsksync_passed;
    bool dskread;
    uint16_t mfm_pos;
    uint8_t mfm_track[MFM_TRACK_SIZE_WORDS * 2];

    uint32_t copper_pt;
    uint16_t copper_inst[2];
    uint8_t copper_inst_ofs;
    bool cdang;

    sprite_dma_state spr_dma_states[8];
    sprite_vpos_state spr_vpos_states[8];
    bool spr_armed[8]; // armed by writing to SPRxDATA, disarmed by writing to SPRxCTL
    uint16_t spr_hold_a[8];
    uint16_t spr_hold_b[8];
    uint8_t spr_hold_cnt[8];

    uint16_t bltw;
    uint16_t blth;
    uint16_t bltaold;
    uint16_t bltbold;
    uint16_t bltbhold;

    uint32_t coplc[2];
    uint16_t diwstrt;
    uint16_t diwstop;
    uint16_t ddfstrt;
    uint16_t ddfstop;
    uint16_t dmacon;
    uint16_t intena;
    uint16_t intreq;
    uint16_t adkcon;

    uint32_t bplpt[6];
    uint16_t bplcon0;
    uint16_t bplcon1;
    uint16_t bplcon2;
    int16_t bplmod1; // odd planes
    int16_t bplmod2; // even planes
    uint16_t bpldat[6];

    // sprite
    uint32_t sprpt[8];
    uint16_t sprpos[8];
    uint16_t sprctl[8];
    uint16_t sprdata[8];
    uint16_t sprdatb[8];

    uint16_t color[32]; // $180..$1C0

    // blitter
    uint16_t bltcon0;
    uint16_t bltcon1;
    uint16_t bltafwm;
    uint16_t bltalwm;
    uint32_t bltpt[4];
    uint16_t bltdat[4];
    int16_t  bltmod[4];
    uint16_t bltsize;

    uint16_t sprite_vpos_start(uint8_t spr)
    {
        assert(spr < 8);
        return (sprpos[spr] >> 8) | (sprctl[spr] & 4) << 6;
    }

    uint16_t sprite_vpos_end(uint8_t spr)
    {
        assert(spr < 8);
        return (sprctl[spr] >> 8) | (sprctl[spr] & 2) << 7;
    }

    uint16_t sprite_hpos_start(uint8_t spr)
    {
        assert(spr < 8);
        return ((sprpos[spr] & 0xff) << 1) | (sprctl[spr] & 1);
    }
};

}

class custom_handler::impl : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler, cia_handler& cia)
        : mem_ { mem_handler }
        , cia_ { cia }
        , chip_ram_mask_ { static_cast<uint32_t>(mem_.ram().size()) - 2 } // -1 for normal mask, -2 for word aligned mask
    {
        mem_.register_handler(*this, 0xDFF000, 0x1000);    
        reset();
    }

    void reset()
    {
        memset(gfx_buf_, 0, sizeof(gfx_buf_));
        memset(&s_, 0, sizeof(s_));
        s_.long_frame = true;
        pause_copper();
    }

    void pause_copper()
    {
        // Indefinte wait
        s_.copper_inst[0] = 0xffff;
        s_.copper_inst[1] = 0xfffe;
        s_.copper_inst_ofs = 2; // Don't fetch new instructions until vblank
    }

    void set_serial_data_handler(const serial_data_handler& handler)
    {
        serial_data_handler_ = handler;
    }

    void set_rbutton_state(bool pressed)
    {
        s_.rmb_pressed = pressed;
    }

    void mouse_move(int dx, int dy)
    {
        s_.cur_mouse_x = static_cast<uint8_t>(s_.cur_mouse_x + dx);
        s_.cur_mouse_y = static_cast<uint8_t>(s_.cur_mouse_y + dy);
    }

    void show_debug_state(std::ostream& os)
    {
        cia_.show_debug_state(os);
        os << "DSK PT: " << hexfmt(s_.dskpt) << " LEN: " << hexfmt(s_.dsklen_act) << " ADK: " << hexfmt(s_.adkcon) << " SYNC: " << hexfmt(s_.dsksync) << "\n";
        os << "DMACON: " << hexfmt(s_.dmacon) << " INTENA: " << hexfmt(s_.intena) << " INTREQ: " << hexfmt(s_.intreq) << " VPOS: " << hexfmt(s_.vpos) << " HPOS: " << hexfmt(s_.hpos) << "\n";
        os << "INT: " << hexfmt(s_.intena & s_.intreq, 4) << " IPL: " << hexfmt(current_ipl()) << "\n";
        os << "COP1LC: " << hexfmt(s_.coplc[0]) << " COP2LC: " << hexfmt(s_.coplc[1]) << " COPPTR: " << hexfmt(s_.copper_pt) << "\n";
        os << "DIWSTRT: " << hexfmt(s_.diwstrt) << " DIWSTOP: " << hexfmt(s_.diwstop) << " DDFSTRT: " << hexfmt(s_.ddfstrt) << " DDFSTOP: " << hexfmt(s_.ddfstop) << "\n";
        os << "BPLCON 0: " << hexfmt(s_.bplcon0) << " 1: " << hexfmt(s_.bplcon1) << " 2: " << hexfmt(s_.bplcon2) << "\n";
    }

    void show_registers(std::ostream& os)
    {
        for (uint16_t i = 0; i < 0x100; i += 2) {
            os << hexfmt(i, 3) << " " << custom_regname(i) << "\t" << hexfmt(internal_read(i)) << "\t" << hexfmt(0x100 | i, 3) << " " << custom_regname(0x100 | i) << "\t" << hexfmt(internal_read(0x100 | i)) << "\n";
        }
    }

    uint32_t copper_ptr(uint8_t idx) // 0=current
    {
        switch (idx) {
        case 0:
            return s_.copper_pt & chip_ram_mask_;
        case 1:
            return s_.coplc[0] & chip_ram_mask_;
        case 2:
            return s_.coplc[1] & chip_ram_mask_;
        default:
            TODO_ASSERT(!"Invalid index");
            return 0;
        }
    }

    void do_blitter_line()
    {
        assert(s_.bltdat[0] == 0x8000);
        assert(s_.bltcon0 & (BC0F_SRCA | BC0F_SRCC | BC0F_DEST));
        assert(!(s_.bltcon0 & BC0F_SRCB));
        assert(s_.bltw == 2);

        uint8_t ashift = s_.bltcon0 >> BC0_ASHIFTSHIFT;
        bool sign = !!(s_.bltcon1 & BC1F_SIGNFLAG);
        bool dot_this_line = false;

        auto incx = [&]() {
            if (++ashift == 16) {
                ashift = 0;
                s_.bltpt[2] += 2;
            }
        };
        auto decx = [&]() {
            if (ashift-- == 0) {
                ashift = 15;
                s_.bltpt[2] -= 2;
            }
        };
        auto incy = [&]() {
            s_.bltpt[2] += s_.bltmod[2];
            dot_this_line = false;
        };
        auto decy = [&]() {
            s_.bltpt[2] -= s_.bltmod[2];
            dot_this_line = false;
        };

        for (uint16_t cnt = 0; cnt < s_.blth; ++cnt) {
            const uint32_t addr = s_.bltpt[2];
            const bool draw = !(s_.bltcon1 & BC1F_ONEDOT) || !dot_this_line;
            s_.bltdat[2] = chip_read(addr);
            s_.bltdat[3] = blitter_func(s_.bltcon0 & 0xff, (s_.bltdat[0] & s_.bltafwm) >> ashift, (s_.bltdat[1] & 1) ? 0xFFFF : 0, s_.bltdat[2]);
            s_.bltpt[0] += sign ? s_.bltmod[1] : s_.bltmod[0];
            dot_this_line = true;

            if (!sign) {
                if (s_.bltcon1 & BC1F_SUD) {
                    if (s_.bltcon1 & BC1F_SUL)
                        decy();
                    else
                        incy();
                } else {
                    if (s_.bltcon1 & BC1F_SUL)
                        decx();
                    else
                        incx();
                }
            }
            if (s_.bltcon1 & BC1F_SUD) {
                if (s_.bltcon1 & BC1F_AUL)
                    decx();
                else
                    incx();
            } else {
                if (s_.bltcon1 & BC1F_AUL)
                    decy();
                else
                    incy();
            } 

            sign = static_cast<int16_t>(s_.bltpt[0]) <= 0;
            s_.bltdat[1] = rol(s_.bltdat[1], 1);
            // First pixel is written to D
            if (draw)
                chip_write(cnt ? addr : s_.bltpt[3], s_.bltdat[3]);
        }
        s_.bplpt[3] = s_.bplpt[2];
    }

    void do_blit()
    {
        // For now just do immediate blit

        if (DEBUG_BLITTER) {
            DBGOUT << "Blit $" << hexfmt(s_.bltw) << "x$" << hexfmt(s_.blth) << " bltcon0=$" << hexfmt(s_.bltcon0) << " bltcon1=$" << hexfmt(s_.bltcon1) << " bltafwm=$" << hexfmt(s_.bltafwm) << " bltalwm=$" << hexfmt(s_.bltalwm) << "\n";
            for (int i = 0; i < 4; ++i) {
                const char name[5] = { 'B', 'L', 'T', static_cast<char>('A' + i), 0 };
                *debug_stream << "\t" << name << "PT=$" << hexfmt(s_.bltpt[i]) << " " << name << "DAT=$" << hexfmt(s_.bltdat[i]) << " " << name << "MOD=$" << hexfmt(s_.bltmod[i]) << " (" << (int)s_.bltmod[i] << ")\n";
            }
            if (s_.bltcon1 & BC1F_LINEMODE)
                *debug_stream << "\tLine mode\n";
            else if (s_.bltcon1 & (BC1F_FILL_OR | BC1F_FILL_XOR))
                *debug_stream << "\t" << (s_.bltcon1 & BC1F_FILL_XOR ? "Xor" : "Or") << " fill mode\n";
            else
                *debug_stream << "\tNormal mode\n";
        }

        uint16_t any = 0;

        if (s_.bltcon1 & BC1F_LINEMODE) {
            do_blitter_line();
            any = 1; // ?
        } else {
            const bool reverse = !!(s_.bltcon1 & BC1F_BLITREVERSE);
            const uint8_t ashift = s_.bltcon0 >> BC0_ASHIFTSHIFT;
            const uint8_t bshift = s_.bltcon1 >> BC1_BSHIFTSHIFT;

            auto incr_ptr = [reverse](uint32_t& pt, int16_t n = 2) {
                if (reverse)
                    pt -= n;
                else
                    pt += n;
            };
            auto do_dma = [&](uint8_t index) {
                s_.bltdat[index] = chip_read(s_.bltpt[index]);
                incr_ptr(s_.bltpt[index]);
            };

            for (uint16_t y = 0; y < s_.blth; ++y) {
                bool inpoly = !!(s_.bltcon1 & BC1F_FILL_CARRYIN);
                for (uint16_t x = 0; x < s_.bltw; ++x) {

                    // A
                    if (s_.bltcon0 & BC0F_SRCA)
                        do_dma(0);
                    uint16_t a = s_.bltdat[0], ahold;
                    if (x == 0)
                        a &= s_.bltafwm;
                    if (x == s_.bltw - 1)
                        a &= s_.bltalwm;
                    if (reverse)
                        ahold = ((uint32_t)a << 16 | s_.bltaold) >> (16 - ashift);
                    else
                        ahold = ((uint32_t)s_.bltaold << 16 | a) >> ashift;
                    s_.bltaold = a;

                    // B
                    if (s_.bltcon0 & BC0F_SRCB) {
                        do_dma(1);
                        if (reverse)
                            s_.bltbhold = ((uint32_t)s_.bltdat[1] << 16 | s_.bltbold) >> (16 - bshift);
                        else
                            s_.bltbhold = ((uint32_t)s_.bltbold << 16 | s_.bltdat[1]) >> bshift;
                        s_.bltbold = s_.bltdat[1];
                    }

                    // C
                    if (s_.bltcon0 & BC0F_SRCC)
                        do_dma(2);

                    uint16_t val = blitter_func(static_cast<uint8_t>(s_.bltcon0), ahold, s_.bltbhold, s_.bltdat[2]);
                    if (s_.bltcon1 & (BC1F_FILL_OR|BC1F_FILL_XOR)) {
                        for (uint8_t bit = 0; bit < 16; ++bit) {
                            const uint16_t mask = 1U << bit;
                            const bool match = !!(val & mask);
                            if (inpoly) {
                                if (s_.bltcon1 & BC1F_FILL_XOR)
                                    val ^= mask;
                                else
                                    val |= mask;
                            }
                            if (match)
                                inpoly = !inpoly;
                        }
                    }
                    any |= val;
                    if (s_.bltcon0 & BC0F_DEST) {
                        chip_write(s_.bltpt[3], val);
                        incr_ptr(s_.bltpt[3]);
                    }
                }
                if (s_.bltcon0 & BC0F_SRCA) incr_ptr(s_.bltpt[0], s_.bltmod[0]);
                if (s_.bltcon0 & BC0F_SRCB) incr_ptr(s_.bltpt[1], s_.bltmod[1]);
                if (s_.bltcon0 & BC0F_SRCC) incr_ptr(s_.bltpt[2], s_.bltmod[2]);
                if (s_.bltcon0 & BC0F_DEST) incr_ptr(s_.bltpt[3], s_.bltmod[3]);
            }
        }
        s_.intreq |= INTF_BLIT;
        s_.dmacon &= ~(DMAF_BLTDONE|DMAF_BLTNZERO);
        if (!any)
            s_.dmacon |= DMAF_BLTNZERO;
        s_.bltw = s_.blth = 0;
    }

    void do_copper()
    {
        // TODO: Comparisons are different for copper $ffdf wait should work for asm as well...
        // Each copper command takes 8 cycles (4 raster pos increments) to execute
        // TODO: Wait actually uses an extra cycle (?)

        if (s_.copper_inst[0] == 0xFFFF && s_.copper_inst[1] == 0xFFFE) {
            return;
        }

        if (s_.copper_inst[0] & 1) {
            // Wait/skip
            const auto vp = (s_.copper_inst[0] >> 8) & 0xff;
            const auto hp = s_.copper_inst[0] & 0xfe;
            const auto ve = 0x80 | ((s_.copper_inst[1] >> 8) & 0x7f);
            const auto he = s_.copper_inst[1] & 0xfe;

            if (!(s_.copper_inst[1] & 0x8000) && (s_.dmacon & DMAF_BLTDONE)) {
                // Blitter wait
            } else if ((s_.vpos & ve) > (vp & ve) || ((s_.vpos & ve) == (vp & ve) && ((s_.hpos >> 1) & he) >= (hp & he))) {
                if (DEBUG_COPPER)
                    DBGOUT << "Wait done $" << hexfmt(s_.copper_inst[0]) << ", " << hexfmt(s_.copper_inst[1]) << " from $" << hexfmt(s_.copper_pt - 4) << "\n";
                s_.copper_inst_ofs = 0; // Fetch next instruction
                if (s_.copper_inst[1] & 1) {
                    // SKIP instruction. Actually reads next instruction, but does nothing?
                    if (DEBUG_COPPER)
                        DBGOUT << "Warning: SKIP processed\n";
                    TODO_ASSERT(!"Skip not implemented");
                }
            }

        } else {
            const auto reg = s_.copper_inst[0] & 0x1ff;
            if (reg >= 0x80 || (s_.cdang && reg >= 0x40)) {
                if (DEBUG_COPPER)
                    DBGOUT << "Writing to " << custom_regname(reg) << " value=$" << hexfmt(s_.copper_inst[1]) << "\n";
                write_u16(0xdff000 + reg, reg, s_.copper_inst[1]);
                s_.copper_inst_ofs = 0; // Fetch next instruction
            } else {
                if (DEBUG_COPPER)
                    DBGOUT << "Writing to " << custom_regname(reg) << " value=$" << hexfmt(s_.copper_inst[1]) << " - Illegal. Pausing copper.\n";
                pause_copper();
            }
        }
    }

    void copjmp(int idx)
    {
        assert(idx == 0 || idx == 1);
        s_.copper_inst_ofs = 0;
        s_.copper_pt = s_.coplc[idx];
        if (DEBUG_COPPER)
            DBGOUT << "Copper jump to COP" << (idx + 1) << "LC: $" << hexfmt(s_.coplc[idx]) << "\n";
    }

    bool do_disk_dma()
    {
        TODO_ASSERT(!(s_.dsklen_act & 0x4000)); // Write not supported
        if (s_.dskwait) {
            --s_.dskwait;
            if (DEBUG_DISK && !s_.dskwait)
                DBGOUT << "Disk wait done\n";
            return false;
        }
        
        if (!s_.dskread) {
            if (DEBUG_DISK)
                DBGOUT << "Reading track\n";
            assert(s_.mfm_pos == 0);
            cia_.active_drive().read_mfm_track(s_.mfm_track);
            s_.dskread = true;
            s_.dskwait = 0;
            return false;
        }

        if (!s_.dsksync_passed && (s_.adkcon & 0x400)) {
            assert(s_.mfm_pos == 0);
            for (uint16_t i = 0; i < MFM_TRACK_SIZE_WORDS; ++i) {
                if (get_u16(&s_.mfm_track[i * 2]) == s_.dsksync) {
                    if (DEBUG_DISK)
                        DBGOUT << "Disk sync word ($" << hexfmt(s_.dsksync) << ") matches at word pos $" << hexfmt(i) << "\n";
                    s_.mfm_pos = i + 1;
                    s_.intreq |= INTF_DSKSYNC;
                    s_.dsksync_passed = true;
                    s_.dskwait = 10; // HACK: Some demos (e.g. desert dream clear intreq after starting the read, so delay a bit)
                    return false;
                }
            }
            TODO_ASSERT(!"Sync word not found?");
        }


        const uint16_t nwords = s_.dsklen_act & 0x3FFF;
        TODO_ASSERT(nwords > 0);

        if (DEBUG_DISK)
            DBGOUT << "Reading $" << hexfmt(nwords) << " words to $" << hexfmt(s_.dskpt) << " mfm offset=$" << hexfmt(s_.mfm_pos) << "\n";
    
        for (uint16_t i = 0; i < nwords; ++i) {
            uint16_t val = 0xaaaa;
            if (s_.mfm_pos < MFM_TRACK_SIZE_WORDS) {
                val = get_u16(&s_.mfm_track[s_.mfm_pos*2]);
                ++s_.mfm_pos;
            }
            chip_write(s_.dskpt, val);
            s_.dskpt += 2;
        }
        s_.intreq |= INTF_DSKBLK;
        s_.dsklen_act = 0;
        return true;
    }

    void step()
    {
        // Step frequency: Base CPU frequency (7.09 for PAL) => 1 lores virtual pixel / 2 hires pixels

        // First readble hpos that's interpreted as being on a new line is $005 (since it's resolution is half that of lowres pixels -> 20) 
        const unsigned virt_pixel = s_.hpos * 2 + 20;
        const unsigned disp_pixel = virt_pixel - hires_min_pixel;
        const bool vert_disp = s_.vpos >= s_.diwstrt >> 8 && s_.vpos < ((~s_.diwstop & 0x8000) >> 7 | s_.diwstop >> 8); // VSTOP MSB is complemented and used as the 9th bit
        const bool horiz_disp = s_.hpos >= (s_.diwstrt & 0xff) && s_.hpos < (0x100 | (s_.diwstop & 0xff));
        const uint16_t colclock = s_.hpos >> 1;

        for (uint8_t spr = 0; spr < 8; ++spr) {
            if (s_.spr_vpos_states[spr] == sprite_vpos_state::vpos_waiting && s_.sprite_vpos_start(spr) == s_.vpos) {
                if (DEBUG_SPRITE)
                    DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " Now active\n";
                s_.spr_vpos_states[spr] = sprite_vpos_state::vpos_active;
                s_.spr_dma_states[spr] = sprite_dma_state::fetch_data;
            } else if (s_.spr_vpos_states[spr] == sprite_vpos_state::vpos_active && s_.sprite_vpos_end(spr) == s_.vpos) {
                if (DEBUG_SPRITE)
                    DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " Now done\n";
                s_.spr_vpos_states[spr] = sprite_vpos_state::vpos_disabled;
                s_.spr_dma_states[spr] = sprite_dma_state::fetch_ctl;
            } else if (s_.spr_vpos_states[spr] == sprite_vpos_state::vpos_active && s_.spr_armed[spr] && s_.sprite_hpos_start(spr) == s_.hpos) {
                if (DEBUG_SPRITE)
                    DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " Armed and HPOS matches!\n";
                s_.spr_hold_a[spr] = s_.sprdata[spr];
                s_.spr_hold_b[spr] = s_.sprdatb[spr];
                s_.spr_hold_cnt[spr] = 16;
            }
        }

        static int rem_pixelsO = 0, rem_pixelsE = 0;

        if (!(s_.hpos & 1) && s_.bpl1dat_written) {
            if (DEBUG_BPL) {
                DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Making BPL data available";
                if (s_.bpldata_avail) {
                    *debug_stream << " Warning: data not used flags=" << hexfmt(s_.bpldata_avail);
                }
                *debug_stream << "\n";
            }
            static_assert(sizeof(s_.bpldat_temp) == sizeof(s_.bpldat));
            memcpy(s_.bpldat_temp, s_.bpldat, sizeof(s_.bpldat_temp));

            s_.bpldata_avail = 3;
            s_.bpl1dat_written = false;
        }

        if (s_.vpos >= vblank_end_vpos && disp_pixel < graphics_width) {
            uint32_t* row = &gfx_buf_[(s_.vpos - vblank_end_vpos) * 2 * graphics_width + disp_pixel + (s_.long_frame ? 0 : graphics_width)];
            assert(&row[1] < &gfx_buf_[sizeof(gfx_buf_) / sizeof(*gfx_buf_)]);

            if (vert_disp && horiz_disp) {
                const uint8_t nbpls = (s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0;

                if (DEBUG_BPL && (s_.dmacon & (DMAF_MASTER | DMAF_RASTER)) == (DMAF_MASTER | DMAF_RASTER) && nbpls && s_.hpos == (s_.diwstrt & 0xff))
                    DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Display starting\n";


                uint8_t spriteidx[8];
                uint8_t active_sprite = 0;
                uint8_t active_sprite_group = 8;

                for (uint8_t spr = 0; spr < 8; ++spr) {
                    uint8_t idx = 0;
                    if (s_.spr_hold_cnt[spr]) {
                        if (s_.spr_hold_a[spr] & 0x8000)
                            idx |= 1;
                        if (s_.spr_hold_b[spr] & 0x8000)
                            idx |= 2;
                        s_.spr_hold_a[spr] <<= 1;
                        s_.spr_hold_b[spr] <<= 1;
                        --s_.spr_hold_cnt[spr];
                    }
                    spriteidx[spr] = idx;
                }

                // Sprites are only visible after write to BPL1DAT
                if (s_.bpl1dat_written_this_line) {
                    // Sprite 0 has highest priority, 7 lowest
                    for (uint8_t spr = 0; spr < 8; ++spr) {
                        if (!(spr & 1) && (s_.sprctl[spr + 1] & 0x80)) {
                            // Attached sprite
                            const uint8_t idx = spriteidx[spr] | spriteidx[spr + 1] << 2;
                            if (idx) {
                                active_sprite = 16 + idx;
                                active_sprite_group = spr >> 1;
                                break;
                            }
                            ++spr; // Skip next sprite
                        } else if (spriteidx[spr]) {
                            active_sprite = 16 + (spr >> 1) * 4 + spriteidx[spr];
                            active_sprite_group = spr >> 1;
                            break;
                        }
                    }
                }

                auto one_pixel = [&]() {
                    uint8_t pf1 = 0, pf2 = 0;

                    if (s_.bplcon0 & BPLCON0F_DBLPF) {
                        for (int i = 0; i < nbpls; ++i) {
                            if (s_.bpldat_shift[i] & 0x8000) {
                                if (i & 1)
                                    pf2 |= 1 << (i >> 1);
                                else
                                    pf1 |= 1 << (i >> 1);
                            }
                            s_.bpldat_shift[i] <<= 1;
                        }
                    } else {
                        for (int i = 0; i < nbpls; ++i) {
                            if (s_.bpldat_shift[i] & 0x8000)
                                pf1 |= 1 << i;
                            s_.bpldat_shift[i] <<= 1;
                        }
                    }

                    if (DEBUG_BPL) {
                        rem_pixelsO--;
                        rem_pixelsE--;
                        if ((rem_pixelsO | rem_pixelsE) < 0 && nbpls && (s_.dmacon & (DMAF_MASTER | DMAF_RASTER)) == (DMAF_MASTER | DMAF_RASTER)) {
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Warning: out of pixels O=" << rem_pixelsO << " E=" << rem_pixelsE << "\n";
                        }
                    }

                    const uint8_t pf2p = (s_.bplcon2 >> 3) & 7;
                    const uint8_t pf1p = s_.bplcon2 & 7;

                    // TODO: Sprites again
                    if (s_.bplcon0 & BPLCON0F_DBLPF) {
                        const bool pf1vis = active_sprite_group >= pf1p;
                        const bool pf2vis = active_sprite_group >= pf2p;

                        uint8_t idx;
                        if (s_.bplcon2 & 0x40) { // PF2PRI
                            if (pf2vis && pf2)
                                idx = pf2 + 8;
                            else if (!pf1vis || !pf1)
                                idx = active_sprite;
                            else
                                idx = pf1;
                        } else {
                            if (pf1vis && pf1)
                                idx = pf1;
                            else if (!pf2vis || !pf2)
                                idx = active_sprite;
                            else
                                idx = pf2 + 8;
                        }
                        return rgb4_to_8(s_.color[idx]);
                    } else if (s_.bplcon0 & BPLCON0F_HOMOD) {
                        // TODO: HAM5
                        const int ibits = ((nbpls + 1) & ~1) - 2;
                        const int val = (pf1 & 0xf) << (8 - ibits);
                        auto& col = s_.ham_color;
                        switch (pf1 >> ibits) {
                        case 0: // Palette entry
                            col = rgb4_to_8(s_.color[pf1 & 0xf]);
                            break;
                        case 1: // Modify B
                            col = (col & 0xffff00) | val;
                            break;
                        case 2: // Modify R
                            col = (col & 0x00ffff) | val << 16;
                            break;
                        case 3: // Modify G
                            col = (col & 0xff00ff) | val << 8;
                            break;
                        default:
                            assert(false);
                        }
                        //if (active_sprite_group < pf2p)
                        if (active_sprite) // How is priority handled here?
                            return rgb4_to_8(s_.color[active_sprite]);
                        return col;
                    } else if (active_sprite_group < pf2p || !pf1) { // no active sprite -> active_sprite=0, so same as pf1
                        return rgb4_to_8(s_.color[active_sprite]);
                    } else if (nbpls < 6) {
                        return rgb4_to_8(s_.color[pf1]);
                    } else {
                        // EHB bpls=6 && !HAM && !DPU
                        const auto col = rgb4_to_8(s_.color[pf1 & 0x1f]);
                        return pf1 & 0x20 ? (col & 0xfefefe) >> 1 : col;
                    }
                };

                if (s_.bplcon0 & BPLCON0F_HIRES) {
                    row[0] = one_pixel();
                    row[1] = one_pixel();
                } else {
                    row[0] = row[1] = one_pixel();
                }
            } else {
                row[0] = row[1] = rgb4_to_8(s_.color[0]);
            }

            if (!(s_.bplcon0 & BPLCON0F_LACE) && s_.long_frame) {
                assert(&row[graphics_width + 1] < &gfx_buf_[sizeof(gfx_buf_) / sizeof(*gfx_buf_)]);
                row[0 + graphics_width] = row[0];
                row[1 + graphics_width] = row[1];
            }
        }

        if (s_.bpldata_avail) {
            const uint8_t mask = s_.bplcon0 & BPLCON0F_HIRES ? 7 : 15;

            if ((s_.bpldata_avail & 1) && (s_.bplcon1 & mask) == (s_.hpos & mask)) {
                for (int i = 0; i < 6; i += 2)
                    s_.bpldat_shift[i] = s_.bpldat_temp[i];
                s_.bpldata_avail &= ~1;
                if (DEBUG_BPL) {
                    DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Loaded odd pixels (shift=$" << hexfmt(s_.bplcon1 & mask, 1) << ")";
                    if (rem_pixelsO > 0)
                        *debug_stream << " Warning discarded " << rem_pixelsO;
                    *debug_stream << "\n";
                    rem_pixelsO = 16;
                }
            }
            if ((s_.bpldata_avail & 2) && ((s_.bplcon1 >> 4) & mask) == (s_.hpos & mask)) {
                for (int i = 1; i < 6; i += 2)
                    s_.bpldat_shift[i] = s_.bpldat_temp[i];
                s_.bpldata_avail &= ~2;
                if (DEBUG_BPL) {
                    DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Loaded even pixels (shift=$" << hexfmt((s_.bplcon1 >> 4) & mask, 1) << ")";
                    if (rem_pixelsE > 0)
                        *debug_stream << " Warning discarded " << rem_pixelsE;
                    *debug_stream << "\n";
                    rem_pixelsE = 16;
                }
            }
        }

        if (s_.copper_inst_ofs == 2) {
            do_copper();
        }

        if (!(s_.hpos & 1) && (s_.dmacon & DMAF_MASTER)) {
            auto do_dma = [this](uint32_t& pt) {
                const auto val = chip_read(pt);
                pt += 2;
                return val;
            };

            do {
                // Only 226 slots are usable
                if (colclock == 0xE3)
                    break;

                // Refresh
                if (colclock == 0xE2 || colclock == 1 || colclock == 3 || colclock == 5)
                    break;

                // Disk
                if ((colclock == 7 || colclock == 9 || colclock == 11) && (s_.dmacon & DMAF_DISK) && (s_.dsklen & 0x8000) && s_.dsklen_act) {
                    if (do_disk_dma())
                        break;
                }

                // TODO: Audio

                // Display
                const uint16_t act_ddfstop = std::min<uint16_t>(0xD8, s_.ddfstop);
                const bool bpl_dma_active = (s_.dmacon & DMAF_RASTER) && vert_disp && (s_.bplcon0 & BPLCON0F_BPU);

                static int num_bpl1_writes = 0;

                if (bpl_dma_active) {
                    if (s_.ddfst == ddfstate::before_ddfstrt && colclock == std::max<uint16_t>(0x18, s_.ddfstrt)) {
                        if (DEBUG_BPL) {
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " DDFSTRT=$" << hexfmt(s_.ddfstrt) << " passed\n";
                            num_bpl1_writes = 0;
                        }
                        s_.ddfst = ddfstate::active;
                        s_.ddfcycle = 0;
                        s_.ddfend = 0;
                    } else if (s_.ddfst == ddfstate::active && colclock == std::min<uint16_t>(0xD8, s_.ddfstop)) {
                        if (DEBUG_BPL)
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " DDFSTOP=$" << hexfmt(s_.ddfstop) << " passed\n";
                        // Need to do one final 8-cycles DMA fetch
                        s_.ddfst = ddfstate::ddfstop_passed;
                    } else if (s_.ddfst == ddfstate::ddfstop_passed && s_.ddfcycle == s_.ddfend) {
                        if (DEBUG_BPL)
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " BPL DMA done (fetch cycle $" << hexfmt(s_.ddfcycle) << ", bpl1 writes: " << num_bpl1_writes << ")\n";
                        s_.ddfst = ddfstate::stopped;
                    }
                }

                if (bpl_dma_active && (s_.ddfst == ddfstate::active || s_.ddfst == ddfstate::ddfstop_passed)) {
                    constexpr uint8_t lores_bpl_sched[8] = { 0, 4, 6, 2, 0, 3, 5, 1 };
                    constexpr uint8_t hires_bpl_sched[8] = { 4, 3, 2, 1, 4, 3, 2, 1 };
                    const int bpl = (s_.bplcon0 & BPLCON0F_HIRES ? hires_bpl_sched : lores_bpl_sched)[s_.ddfcycle & 7] - 1;
                    if (s_.ddfst == ddfstate::ddfstop_passed && !s_.ddfend && (s_.ddfcycle & 7) == 0) {
                        if (DEBUG_BPL)
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Doing final DMA cycles (fetch cycle $" << hexfmt(s_.ddfcycle) << ", bpl1 writes: " << num_bpl1_writes << ")\n";
                        s_.ddfend = s_.ddfcycle + 8;
                    }
                    ++s_.ddfcycle;

                    if (bpl >= 0 && bpl < ((s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0)) {
                        if (DEBUG_BPL)
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " BPL " << bpl << " DMA (fetch cycle $" << hexfmt(s_.ddfcycle) << ")\n";
                        s_.bpldat[bpl] = do_dma(s_.bplpt[bpl]);
                        if (bpl == 0) {
                            if (DEBUG_BPL) {
                                DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Data available -- bpldat1_written = " << s_.bpl1dat_written << " bpldata_avail = " << hexfmt(s_.bpldata_avail) << (s_.bpl1dat_written ? " Warning!" : "") << "\n";
                                ++num_bpl1_writes;
                            }

                            s_.bpl1dat_written = true;
                            s_.bpl1dat_written_this_line = true;
                        }
                        break;
                    }
                }

                // Sprite
                if ((s_.dmacon & DMAF_SPRITE) && s_.vpos >= sprite_dma_start_vpos && (colclock & 1) && colclock >= 0x15 && colclock < 0x15+8*4) {
                    const uint8_t spr = static_cast<uint8_t>((colclock - 0x15) / 4);
                    if (s_.spr_dma_states[spr] != sprite_dma_state::stopped) {
                        const bool first_word = !(colclock & 2);
                        const uint16_t reg = SPR0POS + 8 * spr + 2 * (s_.spr_dma_states[spr] == sprite_dma_state::fetch_ctl ? 1 - first_word : 3 - first_word);
                        const uint16_t val = do_dma(s_.sprpt[spr]);
                        if (DEBUG_SPRITE)
                            DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " vpos_state=" << (int)s_.spr_vpos_states[spr] << " first_word=" << first_word << " writing $" << hexfmt(val) << " to " << custom_regname(reg) << "\n";
                        write_u16(0xdff000 | reg, reg, val);
                        break;
                    }
                }

                // Copper (uses only odd-numbered cycles)
                if ((s_.dmacon & (DMAF_MASTER | DMAF_COPPER)) == (DMAF_MASTER | DMAF_COPPER) && s_.copper_inst_ofs < 2 && !(colclock & 1)) {
                    if (colclock == 0xe0) {
                        if (DEBUG_COPPER)
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Wasting cycle (HACK)\n";
                        break; // $E0 not usable by copper?
                    }

                    s_.copper_inst[s_.copper_inst_ofs++] = do_dma(s_.copper_pt);
                    if (DEBUG_COPPER) {
                        DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " Read copper instruction word $" << hexfmt(s_.copper_inst[s_.copper_inst_ofs - 1]) << " from $" << hexfmt(s_.copper_pt - 2) << "\n";
                        if (s_.copper_inst_ofs == 2 && s_.copper_inst[0] == 0xFFFF && s_.copper_inst[1] == 0xFFFE) {
                            DBGOUT << "virt_pixel=" << hexfmt(virt_pixel) << " End of copper list.\n";
                        }
                    }
                    break;
                }
                
                // Blitter
                if (s_.blth) {
                    do_blit();
                }
            } while (0);
        }

        // CIA tick rate is 1/10th of (base) CPU speed
        if (s_.hpos % 10 == 0) {
            cia_.step();
            const auto irq_mask = cia_.active_irq_mask();
            if (irq_mask & 1)
                s_.intreq |= INTF_PORTS;
            if (irq_mask & 2)
                s_.intreq |= INTF_EXTER;
        }

        if (++s_.hpos == hpos_per_line) {
            s_.hpos = 0;
            // XXX
            memset(s_.bpldat_shift, 0, sizeof(s_.bpldat_shift));
            memset(s_.bpldat_temp, 0, sizeof(s_.bpldat_temp));
            s_.bpl1dat_written = false;
            s_.bpl1dat_written_this_line = false;
            s_.bpldata_avail = 0;
            rem_pixelsO = rem_pixelsE = 0;
            s_.ham_color = rgb4_to_8(s_.color[0]);
            s_.ddfst = ddfstate::before_ddfstrt;
            memset(s_.spr_hold_cnt, 0, sizeof(s_.spr_hold_cnt));

            cia_.increment_tod_counter(1);
            if ((s_.dmacon & (DMAF_MASTER|DMAF_RASTER)) == (DMAF_MASTER|DMAF_RASTER) && vert_disp) {
                for (int bpl = 0; bpl < ((s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0); ++bpl) {
                    s_.bplpt[bpl] += bpl & 1 ? s_.bplmod2 : s_.bplmod1;
                }
            }
            if (++s_.vpos == vpos_per_field) {
                memset(s_.spr_dma_states, 0, sizeof(s_.spr_dma_states));
                memset(s_.spr_vpos_states, 0, sizeof(s_.spr_vpos_states));
                memset(s_.spr_armed, 0, sizeof(s_.spr_armed));
                s_.copper_pt = s_.coplc[0];
                s_.copper_inst_ofs = 0;
                s_.vpos = 0;
                s_.long_frame = (s_.bplcon0 & BPLCON0F_LACE ? !s_.long_frame : true);
                s_.intreq |= INTF_VERTB;
                cia_.increment_tod_counter(0);
                if (!(s_.dmacon & DMAF_COPPER))
                    pause_copper();
            }
        }
    }

    uint8_t read_u8(uint32_t addr, uint32_t offset) override
    {
        const auto v = read_u16(addr & ~1, offset & ~1);
        return (offset & 1 ? v : v >> 8) & 0xff;
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        switch (offset) {
        case DMACONR: // $002
            return s_.dmacon;
        case JOY0DAT: // $00A
            return s_.cur_mouse_y << 8 | s_.cur_mouse_x;
        case JOY1DAT: // $00C
            return 0;
        case VPOSR: // $004
            // Hack: Just return a fixed V/HPOS when external sync is enabled (needed for KS1.2)
            if (s_.bplcon0 & BPLCON0F_ERSY)
                return 0x8040;

            return (s_.long_frame ? 0x8000 : 0) | ((s_.vpos >> 8) & 1);
        case VHPOSR:  // $006
            // Hack: Just return a fixed V/HPOS when external sync is enabled (needed for KS1.2)
            if (s_.bplcon0 & BPLCON0F_ERSY)
                return 0x80;
            return (s_.vpos & 0xff) << 8 | ((s_.hpos >> 1) & 0xff);
        case POTGOR:  // $016
            // Don't spam in DiagROM
            return 0xFF00 & ~(s_.rmb_pressed ? 0x400 : 0);
        case SERDATR: // $018
            return (3<<12); // Just return transmit buffer empty
        case INTENAR: // $01C
            return s_.intena;
        case INTREQR: // $01E
            return s_.intreq;
        case COPJMP1: // $088
            copjmp(0);
            return 0;
        case COPJMP2: // $08A
            copjmp(1);
            return 0;
        }

        std::cerr << "Unhandled read from custom register $" << hexfmt(offset, 3) << " (" << custom_regname(offset) << ")\n";
        return 0xffff;
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        offset &= ~1;
        write_u16(0xdff000 | offset, offset, val | val << 8);
    }

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        //std::cerr << "Write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")" << " val $" << hexfmt(val) << "\n";
        auto write_partial = [offset, &val](uint32_t& r) {
            if (offset & 2) {
                //assert(!(val & 1)); // Blitter pointers (and modulo) ignore bit0
                r = (r & 0xffff0000) | val;
            }
            else
                r = val << 16 | (r & 0xffff);
        };
        auto setclr = [](uint16_t& r, uint16_t val) {
            const bool set = !!(val & 0x8000);
            val &= 0x7fff;
            r &= ~val;
            if (set)
                r |= val;
            else
                r &= ~val;

        };

        if (offset >= COP1LCH && offset <= COP2LCL) {
            write_partial(s_.coplc[(offset - COP1LCH) / 4]);
            return;
        }
        if (offset >= AUD0LCH && offset <= AUD3VOL) {
            // Ignore audio
            return;
        }
        if (offset >= BPL1PTH && offset <= BPL6PTL) {
            write_partial(s_.bplpt[(offset - BPL1PTH) / 4]);
            return;
        }
        if (offset >= BPL1DAT && offset <= BPL6DAT) {
            const int bpl = (offset - BPL1DAT) / 2;
            // XXX: Also set bpldat_reload?
            s_.bpldat_temp[bpl] = s_.bpldat[bpl] = val;
            if (bpl == 0) {
                s_.bpl1dat_written = true;
                s_.bpl1dat_written_this_line = true;
            }
            return;
        }
        if (offset >= SPR0PTH && offset <= SPR7PTL) {
            write_partial(s_.sprpt[(offset - SPR0PTH) / 4]);
            return;
        }
        if (offset >= SPR0POS && offset <= SPR7DATB) {
            const auto spr = static_cast<uint8_t>((offset - SPR0POS) / 8);
            switch ((offset >> 1) & 3) {
            case 0: // SPRxPOS
                s_.sprpos[spr] = val;
                return;
            case 1: // SPRxCTL
                s_.sprctl[spr] = val;
                s_.spr_armed[spr] = false;
                s_.spr_dma_states[spr] = sprite_dma_state::stopped;
                if (s_.sprite_vpos_start(spr) < s_.sprite_vpos_end(spr) && s_.sprite_vpos_start(spr) >= s_.vpos) {
                    if (DEBUG_SPRITE)
                        DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " Setting active waiting for VPOS=$" << hexfmt(s_.sprite_vpos_start(spr)) << " end=$" << hexfmt(s_.sprite_vpos_end(spr)) << "\n";
                    s_.spr_vpos_states[spr] = sprite_vpos_state::vpos_waiting;
                } else {
                    if (DEBUG_SPRITE)
                        DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " Disabling start=$" << hexfmt(s_.sprite_vpos_start(spr)) << " >= end=$" << hexfmt(s_.sprite_vpos_end(spr)) << "\n";
                    s_.spr_vpos_states[spr] = sprite_vpos_state::vpos_disabled;
                }
                return;
            case 2: // SPRxDATA (low word)
                s_.sprdata[spr] = val;
                if (DEBUG_SPRITE)
                    DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)s_.spr_dma_states[spr] << " Arming\n";
                s_.spr_armed[spr] = true;
                return;
            case 3: // SPRxDATB (high word)
                s_.sprdatb[spr] = val;
                return;
            }
        }

        if (offset >= COLOR00 && offset <= COLOR31) {
            s_.color[(offset-COLOR00)/2] = val;
            return;
        }

        switch (offset) {
        case BLTDDAT: // $000
            return; // Dummy address
        case DSKPTH: // $020:
            s_.dskpt = val << 16 | (s_.dskpt & 0xffff);
            if (DEBUG_DISK)
                DBGOUT << "Write to DSKPTH val=$" << hexfmt(val) << " dskpt=$" << hexfmt(s_.dskpt) << "\n";
            return;
        case DSKPTL: // $022:
            s_.dskpt = (s_.dskpt & 0xffff0000) | (val & 0xfffe);
            if (DEBUG_DISK)
                DBGOUT << "Write to DSKPTL val=$" << hexfmt(val) << " dskpt=$" << hexfmt(s_.dskpt) << "\n";
            return;
        case DSKLEN: // $024
            s_.dsklen_act = s_.dsklen == val ? val : 0;
            s_.dsklen = val;
            s_.dskwait = 0;
            s_.mfm_pos = 0;
            s_.dsksync_passed = false;
            s_.dskread = false;
            if (DEBUG_DISK)
                DBGOUT << "Write to DSKLEN: $" << hexfmt(val) << (s_.dsklen_act && (s_.dsklen_act & 0x8000) ? " - Active!" : "") << "\n";
            return;
        case VPOSW:  // $02A
            return;  // Ignore for now
        case COPCON: // $02E
            s_.cdang = !!(val & 2);
            if (DEBUG_COPPER)
                DBGOUT << "CDANG " << (s_.cdang ? "enabled" : "disabled") << "\n";
            return;
        case SERDAT: // $030
            if (serial_data_handler_) {
                uint8_t numbits = 9;
                while (numbits && !(val & (1 << numbits)))
                    --numbits;
                serial_data_handler_(numbits, val & ((1 << numbits) - 1));
            }
            return;
        case SERPER: // $032
            return;  // Ignore for now
        case POTGO:  // $034 
            return;  // Ignore for now
        case BLTCON0:
            s_.bltcon0 = val;
            return;
        case BLTCON1:
            s_.bltcon1 = val;
            return;
        case BLTAFWM:
            s_.bltafwm = val;
            return;
        case BLTALWM:
            s_.bltalwm = val;
            return;
        case BLTCPTH:
        case BLTCPTL:
            write_partial(s_.bltpt[2]);
            s_.bltpt[2] &= ~1U;
            return;
        case BLTBPTH:
        case BLTBPTL:
            write_partial(s_.bltpt[1]);
            s_.bltpt[1] &= ~1U;
            return;
        case BLTAPTH:
        case BLTAPTL:
            write_partial(s_.bltpt[0]);
            s_.bltpt[0] &= ~1U;
            return;
        case BLTDPTH:
        case BLTDPTL:
            write_partial(s_.bltpt[3]);
            s_.bltpt[3] &= ~1U;
            return;
        case BLTSIZE:        
            assert(!s_.bltw && !s_.blth && !(s_.dmacon & DMAF_BLTDONE));
            s_.bltsize = val;
            s_.bltw = val & 0x3f ? val & 0x3f : 0x40;
            s_.blth = val >> 6 ? val >> 6 : 0x400;
            s_.bltaold = 0;
            s_.bltbold = 0;
            s_.dmacon |= DMAF_BLTDONE;
            return;
        case BLTCMOD:
            s_.bltmod[2] = val & ~1U;
            return;
        case BLTBMOD:
            s_.bltmod[1] = val & ~1U;
            return;
        case BLTAMOD:
            s_.bltmod[0] = val & ~1U;
            return;
        case BLTDMOD:
            s_.bltmod[3] = val & ~1U;
            return;
        case BLTCDAT:
            s_.bltdat[2] = val;
            return;
        case BLTBDAT:
            // B value is shifted immediately
            if (s_.bltcon1 & BC1F_BLITREVERSE)
                s_.bltbhold = (((uint32_t)val << 16) | s_.bltbold) >> (16 - (s_.bltcon1 >> BC1_BSHIFTSHIFT));
            else
                s_.bltbhold = (((uint32_t)s_.bltbold << 16) | val) >> (s_.bltcon1 >> BC1_BSHIFTSHIFT);
            s_.bltdat[1] = val;
            s_.bltbold = val;
            return;
        case BLTADAT:
            s_.bltdat[0] = val;
            return;
        case DSKSYNC: // $07E:
            s_.dsksync = val;
            return;
        case COPJMP1: // $088
            copjmp(0);
            return;
        case COPJMP2: // $08A
            copjmp(1);
            return;
        case DIWSTRT: // $08E
            s_.diwstrt = val;
            return;
        case DIWSTOP: // $090
            s_.diwstop = val;
            return;
        case DDFSTRT: // $092
            s_.ddfstrt = val & 0xfc;
            return;
        case DDFSTOP: // $094
            s_.ddfstop = val & 0xfc;
            return;
        case DMACON:  // $096
            setclr(s_.dmacon, val & ~(1 << 14 | 1 << 13 | 1 << 12 | 1 << 11));
            if (DEBUG_DMA)
                DBGOUT << "Write to DMACON val=$" << hexfmt(val) << " dmacon=$" << hexfmt(s_.dmacon) << "\n";
            return;
        case INTENA:  // $09A
            setclr(s_.intena, val);
            if (s_.intena & (INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3))
                std::cerr << "Warning: INTENA contains AUDx interrupts: $" << hexfmt(s_.intena) << "\n";
            return;
        case INTREQ:  // $09C
            val &= ~INTF_INTEN;
            setclr(s_.intreq, val);
            return;
        case ADKCON:  // $09E
            setclr(s_.adkcon, val);
            if (DEBUG_DISK)
                DBGOUT << "Write to ADKCON val=$" << hexfmt(val) << " adkcon=$" << hexfmt(s_.adkcon) << "\n";
            return;
        case BPLCON0: // $100
            s_.bplcon0 = val;
            return;
        case BPLCON1: // $102
            s_.bplcon1 = val;
            return;
        case BPLCON2: // $104
            s_.bplcon2 = val;
            return;
        case BPLCON3: // $106
            return;
        case BPLMOD1: // $108
            s_.bplmod1 = val;
            return;
        case BPLMOD2: // $10A
            s_.bplmod2 = val;
            return;
        case DIWHIGH: // $1E4
        case FMODE:   // $1FC
        case 0x1fe:   // NO-OP
            return;
        }
        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << custom_regname(offset) << ")"
                  << " val $" << hexfmt(val) << "\n";
    }

    uint8_t current_ipl() const
    {
        if (!(s_.intena & INTF_INTEN))
            return 0;
        const auto active = s_.intena & s_.intreq;
        if (!active)
            return 0;
        // Find highest priority active interrupt
        constexpr uint8_t ipl[INTB_EXTER+1] = {
            1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6
        };
        for (uint8_t i = INTB_EXTER+1; i--;) {
            if ((active & (1 << i))) {
                return ipl[i];
            }
        }
        assert(0);
        return 0;
    }

    const uint32_t* new_frame()
    {
        if (s_.hpos == 0 && s_.vpos == 0) {
            return gfx_buf_;
        }
        return nullptr;
    }

private:
    memory_handler& mem_;
    cia_handler& cia_;
    serial_data_handler serial_data_handler_;

    uint32_t gfx_buf_[graphics_width * graphics_height];
    custom_state s_;
    uint32_t chip_ram_mask_;

    uint16_t chip_read(uint32_t addr)
    {
        return mem_.read_u16(addr & chip_ram_mask_);
    }

    void chip_write(uint32_t addr, uint16_t val)
    {
        mem_.write_u16(addr & chip_ram_mask_, val);
    }

    uint16_t internal_read(uint16_t reg);
};

uint16_t custom_handler::impl::internal_read(uint16_t reg)
{
    auto lo = [](uint32_t l) { return static_cast<uint16_t>(l); };
    auto hi = [](uint32_t l) { return static_cast<uint16_t>(l >> 16); };

    switch (reg) {
    case BLTDDAT:
        return s_.bltdat[3];
    case DMACONR:
    case VPOSR:
    case VHPOSR:
    case JOY0DAT:
    case JOY1DAT:
    case POTGOR:
    case SERDATR:
    case INTENAR:
    case INTREQR:
        return read_u16(reg | 0xdff000, reg);
    case DSKPTH:
        return hi(s_.dskpt);
    case DSKPTL:
        return lo(s_.dskpt);
    case DSKLEN:
        return s_.dsklen;
    //case DSKDAT:
    //case REFPTR:
    //case VPOSW:
    //case VHPOSW:
    //case COPCON:
    //case SERDAT:
    //case SERPER:
    //case POTGO:
    case BLTCON0:
        return s_.bltcon0;
    case BLTCON1:
        return s_.bltcon1;
    case BLTAFWM:
        return s_.bltafwm;
    case BLTALWM:
        return s_.bltalwm;
    case BLTCPTH:
        return hi(s_.bltpt[2]);
    case BLTCPTL:
        return lo(s_.bltpt[2]);
    case BLTBPTH:
        return hi(s_.bltpt[1]);
    case BLTBPTL:
        return lo(s_.bltpt[1]);
    case BLTAPTH:
        return hi(s_.bltpt[0]);
    case BLTAPTL:
        return lo(s_.bltpt[0]);
    case BLTDPTH:
        return hi(s_.bltpt[3]);
    case BLTDPTL:
        return lo(s_.bltpt[3]);
    case BLTSIZE:
        return s_.bltsize;
    //case BLTCON0L:
    //case BLTSIZV:
    //case BLTSIZH:
    case BLTCMOD:
        return s_.bltmod[2];
    case BLTBMOD:
        return s_.bltmod[1];
    case BLTAMOD:
        return s_.bltmod[0];
    case BLTDMOD:
        return s_.bltmod[3];
    case BLTCDAT:
        return s_.bltdat[2];
    case BLTBDAT:
        return s_.bltdat[1];
    case BLTADAT:
        return s_.bltdat[0];
    case DSKSYNC:
        return s_.dsksync;
    case COP1LCH:
        return hi(s_.coplc[0]);
    case COP1LCL:
        return lo(s_.coplc[0]);
    case COP2LCH:
        return hi(s_.coplc[1]);
    case COP2LCL:
        return lo(s_.coplc[1]);
    //case COPJMP1:
    //case COPJMP2:
    case DIWSTRT:
        return s_.diwstrt;
    case DIWSTOP:
        return s_.diwstop;
    case DDFSTRT:
        return s_.ddfstrt;
    case DDFSTOP:
        return s_.ddfstop;
    case DMACON:
        return s_.dmacon;
    case INTENA:
        return s_.intena;
    case INTREQ:
        return s_.intreq;
    case ADKCON:
        return s_.adkcon;
    //case AUD0LCH:
    //case AUD0LCL:
    //case AUD0LEN:
    //case AUD0PER:
    //case AUD0VOL:
    //case AUD0DAT:
    //case AUD1LCH:
    //case AUD1LCL:
    //case AUD1LEN:
    //case AUD1PER:
    //case AUD1VOL:
    //case AUD1DAT:
    //case AUD2LCH:
    //case AUD2LCL:
    //case AUD2LEN:
    //case AUD2PER:
    //case AUD2VOL:
    //case AUD2DAT:
    //case AUD3LCH:
    //case AUD3LCL:
    //case AUD3LEN:
    //case AUD3PER:
    //case AUD3VOL:
    case BPL1PTH:
    case BPL1PTL:
    case BPL2PTH:
    case BPL2PTL:
    case BPL3PTH:
    case BPL3PTL:
    case BPL4PTH:
    case BPL4PTL:
    case BPL5PTH:
    case BPL5PTL:
    case BPL6PTH:
    case BPL6PTL:
        if (reg & 2)
            return lo(s_.bplpt[(reg - BPL1PTH) / 4]);
        else
            return hi(s_.bplpt[(reg - BPL1PTH) / 4]);
    case BPLCON0:
        return s_.bplcon0;
    case BPLCON1:
        return s_.bplcon1;
    case BPLCON2:
        return s_.bplcon2;
    //case BPLCON3:
    case BPLMOD1:
        return s_.bplmod1;
    case BPLMOD2:
        return s_.bplmod2;
    case BPL1DAT:
    case BPL2DAT:
    case BPL3DAT:
    case BPL4DAT:
    case BPL5DAT:
    case BPL6DAT:
        return s_.bpldat[(reg - BPL1DAT) / 2];
    case SPR0PTH:
    case SPR0PTL:
    case SPR1PTH:
    case SPR1PTL:
    case SPR2PTH:
    case SPR2PTL:
    case SPR3PTH:
    case SPR3PTL:
    case SPR4PTH:
    case SPR4PTL:
    case SPR5PTH:
    case SPR5PTL:
    case SPR6PTH:
    case SPR6PTL:
    case SPR7PTH:
    case SPR7PTL:
        if (reg & 2)
            return lo(s_.sprpt[(reg - SPR0PTH) / 4]);
        else
            return hi(s_.sprpt[(reg - SPR0PTH) / 4]);
    case SPR0POS:
    case SPR0CTL:
    case SPR0DATA:
    case SPR0DATB:
    case SPR1POS:
    case SPR1CTL:
    case SPR1DATA:
    case SPR1DATB:
    case SPR2POS:
    case SPR2CTL:
    case SPR2DATA:
    case SPR2DATB:
    case SPR3POS:
    case SPR3CTL:
    case SPR3DATA:
    case SPR3DATB:
    case SPR4POS:
    case SPR4CTL:
    case SPR4DATA:
    case SPR4DATB:
    case SPR5POS:
    case SPR5CTL:
    case SPR5DATA:
    case SPR5DATB:
    case SPR6POS:
    case SPR6CTL:
    case SPR6DATA:
    case SPR6DATB:
    case SPR7POS:
    case SPR7CTL:
    case SPR7DATA:
    case SPR7DATB:
        reg -= SPR0POS;
        switch ((reg >> 1) & 3) {
        case 0:
            return s_.sprpos[reg / 8];
        case 1:
            return s_.sprctl[reg / 8];
        case 2:
            return s_.sprdata[reg / 8];
        case 3:
            return s_.sprdatb[reg / 8];
        }
    case COLOR00:
    case COLOR01:
    case COLOR02:
    case COLOR03:
    case COLOR04:
    case COLOR05:
    case COLOR06:
    case COLOR07:
    case COLOR08:
    case COLOR09:
    case COLOR10:
    case COLOR11:
    case COLOR12:
    case COLOR13:
    case COLOR14:
    case COLOR15:
    case COLOR16:
    case COLOR17:
    case COLOR18:
    case COLOR19:
    case COLOR20:
    case COLOR21:
    case COLOR22:
    case COLOR23:
    case COLOR24:
    case COLOR25:
    case COLOR26:
    case COLOR27:
    case COLOR28:
    case COLOR29:
    case COLOR30:
    case COLOR31:
        return s_.color[(reg - COLOR00) / 2];
    //case DIWHIGH:
    //case FMODE:
    default:
        return 0;
    }
}

custom_handler::custom_handler(memory_handler& mem_handler, cia_handler& cia)
    : impl_ { std::make_unique<impl>(mem_handler, cia) }
{
}

custom_handler::~custom_handler() = default;

void custom_handler::step()
{
    impl_->step();
}

uint8_t custom_handler::current_ipl() const
{
    return impl_->current_ipl();
}

const uint32_t* custom_handler::new_frame()
{
    return impl_->new_frame();
}

void custom_handler::set_serial_data_handler(const serial_data_handler& handler)
{
    impl_->set_serial_data_handler(handler);
}

void custom_handler::set_rbutton_state(bool pressed)
{
    impl_->set_rbutton_state(pressed);
}

void custom_handler::mouse_move(int dx, int dy)
{
    impl_->mouse_move(dx, dy);
}

void custom_handler::show_debug_state(std::ostream& os)
{
    impl_->show_debug_state(os);
}

void custom_handler::show_registers(std::ostream& os)
{
    impl_->show_registers(os);
}

uint32_t custom_handler::copper_ptr(uint8_t idx)
{
    return impl_->copper_ptr(idx);
}

std::string custom_regname(uint32_t offset)
{
    offset &= 0x1ff;
    switch (offset) {
#define CHECK_NAME(name, offset, _) \
    case offset:                    \
        return #name;
        CUSTOM_REGS(CHECK_NAME)
#undef CHECK_NAME
    }
    return "$" + hexstring(offset, 3);
}