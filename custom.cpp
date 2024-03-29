#include "custom.h"
#include "ioutil.h"
#include "cia.h"
#include "debug.h"
#include "color_util.h"
#include "state_file.h"

#include <cassert>
#include <utility>
#include <cstring>
#include <climits>
#include <iostream>
#include <array>

#define TODO_ASSERT(expr) do { if (!(expr)) throw std::runtime_error{("TODO: " #expr " in ") + std::string{__FILE__} + " line " + std::to_string(__LINE__) }; } while (0)

#define DBGOUT *debug_stream << "PC=$" << hexfmt(current_pc_) << " vpos=$" << hexfmt(s_.vpos) << " hpos=$" << hexfmt(s_.hpos) << " (clock $" << hexfmt(s_.hpos >> 1, 2) << ") "

    // Name, Offset, R(=0)/W(=1)
#define CUSTOM_REGS(X) \
    X(BLTDDAT  , 0x000 , 0) /* Blitter destination early read (dummy address)          */ \
    X(DMACONR  , 0x002 , 0) /* DMA control (and blitter status) read                   */ \
    X(VPOSR    , 0x004 , 0) /* Read vert most signif. bit (and frame flop)             */ \
    X(VHPOSR   , 0x006 , 0) /* Read vert and horiz. position of beam                   */ \
    X(DSKDATR  , 0x008 , 0) /* Disk data early read (dummy address)                    */ \
    X(JOY0DAT  , 0x00A , 0) /* Joystick-mouse 0 data (vert,horiz)                      */ \
    X(JOY1DAT  , 0x00C , 0) /* Joystick-mouse 1 data (vert,horiz)                      */ \
    X(CLXDAT   , 0x00E , 0) /* Collision data reg. (read and clear)                    */ \
    X(ADKCONR  , 0x010 , 0) /* Audio,disk control register read                        */ \
    X(POT0DAT  , 0x012 , 0) /* Pot counter data left pair (vert, horiz)                */ \
    X(POT1DAT  , 0x014 , 0) /* Pot counter data right pair (vert, horiz)               */ \
    X(POTGOR   , 0x016 , 0) /* Pot port data read(formerly POTINP)                     */ \
    X(SERDATR  , 0x018 , 0) /* Serial port data and status read                        */ \
    X(DSKBYTR  , 0x01A , 0) /* Disk data byte and status read                          */ \
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
    X(JOYTEST  , 0x036 , 1) /* Write to all 4 joystick-mouse counters at once          */ \
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
    X(BLTCON0L , 0x05A , 1) /* Blitter control 0, lower 8 bits (minterms) (ECS only)   */ \
    X(BLTSIZV  , 0x05C , 1) /* Blitter V size (for 15 bit vertical size) (ECS only)    */ \
    X(BLTSIZH  , 0x05E , 1) /* Blitter H size and start (for 11 bit H size) (ECS only) */ \
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
    X(AUD3DAT  , 0x0DA , 1) /* Audio channel 3 data                                    */ \
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
    X(BPLCON4  , 0x10C , 1) /* Bitplane control (bitplane and sprite-masks) (AGA only) */ \
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
    X(BEAMCON0 , 0x1DC , 1) /* Beam counter control register (SHRES,UHRES,PAL) (AGA)   */ \
    X(DIWHIGH  , 0x1E4 , 1) /* Display window - upper bits for start/stop (AGA)        */ \
    X(FMODE    , 0x1FC , 1) /* Fetch mode (AGA)                                        */ \
// keep this line clear (for macro continuation)


namespace {

enum regnum {
#define REG_NUM(name, offset, _) name = offset,
    CUSTOM_REGS(REG_NUM)
#undef REG_NUM
};

//constexpr uint16_t DMAB_SETCLR   = 15; // Set/clear control bit. Determines if bits written with a 1 get set or cleared.Bits written with a zero are unchanged.
constexpr uint16_t DMAB_BLTBUSY  = 14; // Blitter busy status bit (read only)
constexpr uint16_t DMAB_BLTNZERO = 13; // Blitter logic zero status bit  (read only).
constexpr uint16_t DMAB_BLITHOG  = 10; // Blitter DMA priority (over CPU micro) 
constexpr uint16_t DMAB_MASTER   =  9; // Enable all DMA below
constexpr uint16_t DMAB_RASTER   =  8; // Bitplane DMA enable
constexpr uint16_t DMAB_COPPER   =  7; // Copper DMA enable
constexpr uint16_t DMAB_BLITTER  =  6; // Blitter DMA enable
constexpr uint16_t DMAB_SPRITE   =  5; // Sprite DMA enable
constexpr uint16_t DMAB_DISK     =  4; // Disk DMA enable
//constexpr uint16_t DMAB_AUD3     =  3; // Audio channel 3 DMA enable
//constexpr uint16_t DMAB_AUD2     =  2; // Audio channel 2 DMA enable
//constexpr uint16_t DMAB_AUD1     =  1; // Audio channel 1 DMA enable
constexpr uint16_t DMAB_AUD0     =  0; // Audio channel 0 DMA enable

//constexpr uint16_t DMAF_SETCLR   = 1 << DMAB_SETCLR;
constexpr uint16_t DMAF_BLTBUSY  = 1 << DMAB_BLTBUSY;
constexpr uint16_t DMAF_BLTNZERO = 1 << DMAB_BLTNZERO;
constexpr uint16_t DMAF_BLITHOG  = 1 << DMAB_BLITHOG;
constexpr uint16_t DMAF_MASTER   = 1 << DMAB_MASTER;
constexpr uint16_t DMAF_RASTER   = 1 << DMAB_RASTER;
constexpr uint16_t DMAF_COPPER   = 1 << DMAB_COPPER;
constexpr uint16_t DMAF_BLITTER  = 1 << DMAB_BLITTER;
constexpr uint16_t DMAF_SPRITE   = 1 << DMAB_SPRITE;
constexpr uint16_t DMAF_DISK     = 1 << DMAB_DISK;
//constexpr uint16_t DMAF_AUD3     = 1 << DMAB_AUD3;
//constexpr uint16_t DMAF_AUD2     = 1 << DMAB_AUD2;
//constexpr uint16_t DMAF_AUD1     = 1 << DMAB_AUD1;
//constexpr uint16_t DMAF_AUD0     = 1 << DMAB_AUD0;


constexpr uint16_t INTB_SETCLR  = 15; // Set/Clear control bit. Determines if bits written with a 1 get set or cleared. Bits written with a zero are allways unchanged
constexpr uint16_t INTB_INTEN   = 14; // Master interrupt (enable only)
constexpr uint16_t INTB_EXTER   = 13; // External interrupt
constexpr uint16_t INTB_DSKSYNC = 12; // Disk re-SYNChronized
//constexpr uint16_t INTB_RBF     = 11; // serial port Receive Buffer Full
//constexpr uint16_t INTB_AUD3    = 10; // Audio channel 3 block finished
//constexpr uint16_t INTB_AUD2    =  9; // Audio channel 2 block finished
//constexpr uint16_t INTB_AUD1    =  8; // Audio channel 1 block finished
constexpr uint16_t INTB_AUD0    =  7; // Audio channel 0 block finished
constexpr uint16_t INTB_BLIT    =  6; // Blitter finished
constexpr uint16_t INTB_VERTB   =  5; // start of Vertical Blank
//constexpr uint16_t INTB_COPER   =  4; // Coprocessor
constexpr uint16_t INTB_PORTS   =  3; // I/O Ports and timers
//constexpr uint16_t INTB_SOFTINT =  2; // software interrupt request
constexpr uint16_t INTB_DSKBLK  =  1; // Disk Block done
//constexpr uint16_t INTB_TBE     =  0; // serial port Transmit Buffer Empty

constexpr uint16_t INTF_SETCLR  = 1 << INTB_SETCLR;
constexpr uint16_t INTF_INTEN   = 1 << INTB_INTEN;
constexpr uint16_t INTF_EXTER   = 1 << INTB_EXTER;
constexpr uint16_t INTF_DSKSYNC = 1 << INTB_DSKSYNC;
//constexpr uint16_t INTF_RBF     = 1 << INTB_RBF;
//constexpr uint16_t INTF_AUD3    = 1 << INTB_AUD3;
//constexpr uint16_t INTF_AUD2    = 1 << INTB_AUD2;
//constexpr uint16_t INTF_AUD1    = 1 << INTB_AUD1;
//constexpr uint16_t INTF_AUD0    = 1 << INTB_AUD0;
constexpr uint16_t INTF_BLIT    = 1 << INTB_BLIT;
constexpr uint16_t INTF_VERTB   = 1 << INTB_VERTB;
//constexpr uint16_t INTF_COPER   = 1 << INTB_COPER;
constexpr uint16_t INTF_PORTS   = 1 << INTB_PORTS;
//constexpr uint16_t INTF_SOFTINT = 1 << INTB_SOFTINT;
constexpr uint16_t INTF_DSKBLK  = 1 << INTB_DSKBLK;
//constexpr uint16_t INTF_TBE     = 1 << INTB_TBE;

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
//constexpr uint16_t BPLCON0B_BPU2  = 14; // Bitplane use code 000-110 (NONE through 6 inclusive)
//constexpr uint16_t BPLCON0B_BPU1  = 13; // 
constexpr uint16_t BPLCON0B_BPU0  = 12; // 
constexpr uint16_t BPLCON0B_HOMOD = 11; // Hold-and-modify mode (1 = Hold-and-modify mode (HAM); 0 = Extra Half Brite (EHB) if HAM=0 and BPU=6 and DBLPF=0 then bitplane 6 controls an intensity reduction in the other five bitplanes)
constexpr uint16_t BPLCON0B_DBLPF = 10; // Double playfield (PF1=odd PF2=even bitplanes)
//constexpr uint16_t BPLCON0B_COLOR =  9; // Composite video COLOR enable
//constexpr uint16_t BPLCON0B_GUAD  =  8; // Genlock audio enable (muxed on BKGND pin during vertical blanking
//constexpr uint16_t BPLCON0B_LPEN  =  3; // Light pen enable (reset on power up)
constexpr uint16_t BPLCON0B_LACE  =  2; // Interlace enable (reset on power up)
constexpr uint16_t BPLCON0B_ERSY  =  1; // External resync (HSYNC, VSYNC pads become inputs) (reset on power up)

constexpr uint16_t BPLCON0F_HIRES = 1 << BPLCON0B_HIRES;
constexpr uint16_t BPLCON0F_BPU   = 7 << BPLCON0B_BPU0;
constexpr uint16_t BPLCON0F_HOMOD = 1 << BPLCON0B_HOMOD;
constexpr uint16_t BPLCON0F_DBLPF = 1 << BPLCON0B_DBLPF;
//constexpr uint16_t BPLCON0F_COLOR = 1 << BPLCON0B_COLOR;
//constexpr uint16_t BPLCON0F_GUAD  = 1 << BPLCON0B_GUAD;
//constexpr uint16_t BPLCON0F_LPEN  = 1 << BPLCON0B_LPEN;
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
//constexpr uint16_t BC1F_OVFLAG       = 0x0020; // Line/draw r/l word overflow flag (Line mode)
constexpr uint16_t BC1F_SUD          = 0x0010; // Sometimes up or down (=AUD) (Line mode)
constexpr uint16_t BC1F_SUL          = 0x0008; // Sometimes up or left (Line mode)
constexpr uint16_t BC1F_AUL          = 0x0004; // Always up or left (Line mode)
constexpr uint16_t BC1F_ONEDOT       = 0x0002; // one dot per horizontal line
constexpr uint16_t BC1F_LINEMODE     = 0x0001; // Line mode control bit

const uint8_t BC0_ASHIFTSHIFT = 12; // bits to right align ashift value
const uint8_t BC1_BSHIFTSHIFT = 12; // bits to right align bshift value

const char* const minterm_desc[] = {
    "0",
    "abc",
    "abC",
    "ab",
    "aBc",
    "ac",
    "abC+aBc",
    "ab+aBc",
    "aBC",
    "abc+aBC",
    "aC",
    "ab+aBC",
    "aB",
    "ac+aBC",
    "aC+aBc",
    "a",
    "Abc",
    "bc",
    "abC+Abc",
    "ab+Abc",
    "aBc+Abc",
    "ac+Abc",
    "abC+aBc+Abc",
    "ab+aBc+Abc",
    "aBC+Abc",
    "bc+aBC",
    "aC+Abc",
    "ab+aBC+Abc",
    "aB+Abc",
    "ac+aBC+Abc",
    "aC+aBc+Abc",
    "a+bc",
    "AbC",
    "abc+AbC",
    "bC",
    "ab+AbC",
    "aBc+AbC",
    "ac+AbC",
    "bC+aBc",
    "ab+aBc+AbC",
    "aBC+AbC",
    "abc+aBC+AbC",
    "aC+AbC",
    "ab+aBC+AbC",
    "aB+AbC",
    "ac+aBC+AbC",
    "aC+aBc+AbC",
    "a+bC",
    "Ab",
    "bc+AbC",
    "bC+Abc",
    "b",
    "aBc+Ab",
    "ac+Ab",
    "bC+aBc+Abc",
    "b+ac",
    "aBC+Ab",
    "bc+aBC+AbC",
    "aC+Ab",
    "b+aC",
    "aB+Ab",
    "ac+aBC+Ab",
    "aC+aBc+Ab",
    "a+b",
    "ABc",
    "abc+ABc",
    "abC+ABc",
    "ab+ABc",
    "Bc",
    "ac+ABc",
    "abC+Bc",
    "ab+Bc",
    "aBC+ABc",
    "abc+aBC+ABc",
    "aC+ABc",
    "ab+aBC+ABc",
    "aB+ABc",
    "ac+aBC+ABc",
    "aC+Bc",
    "a+Bc",
    "Ac",
    "bc+ABc",
    "abC+Ac",
    "ab+Ac",
    "Bc+Abc",
    "c",
    "abC+Bc+Abc",
    "ab+Bc+Abc",
    "aBC+Ac",
    "bc+aBC+ABc",
    "aC+Ac",
    "ab+aBC+Ac",
    "aB+Ac",
    "c+aB",
    "aC+Bc+Abc",
    "a+c",
    "AbC+ABc",
    "abc+AbC+ABc",
    "bC+ABc",
    "ab+AbC+ABc",
    "Bc+AbC",
    "ac+AbC+ABc",
    "bC+Bc",
    "ab+Bc+AbC",
    "aBC+AbC+ABc",
    "abc+aBC+AbC+ABc",
    "aC+AbC+ABc",
    "ab+aBC+AbC+ABc",
    "aB+AbC+ABc",
    "ac+aBC+AbC+ABc",
    "aC+Bc+AbC",
    "a+bC+Bc",
    "Ab+ABc",
    "bc+AbC+ABc",
    "bC+Ac",
    "b+Ac",
    "Bc+Ab",
    "ac+Ab+ABc",
    "bC+Bc+Abc",
    "b+c",
    "aBC+Ab+ABc",
    "bc+aBC+AbC+ABc",
    "aC+Ab+ABc",
    "b+aC+Ac",
    "aB+Ab+ABc",
    "ac+aBC+Ab+ABc",
    "aC+Bc+Ab",
    "a+b+c",
    "ABC",
    "abc+ABC",
    "abC+ABC",
    "ab+ABC",
    "aBc+ABC",
    "ac+ABC",
    "abC+aBc+ABC",
    "ab+aBc+ABC",
    "BC",
    "abc+BC",
    "aC+ABC",
    "ab+BC",
    "aB+ABC",
    "ac+BC",
    "aC+aBc+ABC",
    "a+BC",
    "Abc+ABC",
    "bc+ABC",
    "abC+Abc+ABC",
    "ab+Abc+ABC",
    "aBc+Abc+ABC",
    "ac+Abc+ABC",
    "abC+aBc+Abc+ABC",
    "ab+aBc+Abc+ABC",
    "BC+Abc",
    "bc+BC",
    "aC+Abc+ABC",
    "ab+BC+Abc",
    "aB+Abc+ABC",
    "ac+BC+Abc",
    "aC+aBc+Abc+ABC",
    "a+bc+BC",
    "AC",
    "abc+AC",
    "bC+ABC",
    "ab+AC",
    "aBc+AC",
    "ac+AC",
    "bC+aBc+ABC",
    "ab+aBc+AC",
    "BC+AbC",
    "abc+BC+AbC",
    "C",
    "ab+BC+AbC",
    "aB+AC",
    "ac+BC+AbC",
    "C+aB",
    "a+C",
    "Ab+ABC",
    "bc+AC",
    "bC+Abc+ABC",
    "b+AC",
    "aBc+Ab+ABC",
    "ac+Ab+ABC",
    "bC+aBc+Abc+ABC",
    "b+ac+AC",
    "BC+Ab",
    "bc+BC+AbC",
    "aC+Ab+ABC",
    "b+C",
    "aB+Ab+ABC",
    "ac+BC+Ab",
    "aC+aBc+Ab+ABC",
    "a+b+C",
    "AB",
    "abc+AB",
    "abC+AB",
    "ab+AB",
    "Bc+ABC",
    "ac+AB",
    "abC+Bc+ABC",
    "ab+Bc+ABC",
    "BC+ABc",
    "abc+BC+ABc",
    "aC+AB",
    "ab+BC+ABc",
    "B",
    "ac+BC+ABc",
    "aC+Bc+ABC",
    "a+B",
    "Ac+ABC",
    "bc+AB",
    "abC+Ac+ABC",
    "ab+Ac+ABC",
    "Bc+Abc+ABC",
    "c+AB",
    "abC+Bc+Abc+ABC",
    "ab+Bc+Abc+ABC",
    "BC+Ac",
    "bc+BC+ABc",
    "aC+Ac+ABC",
    "ab+BC+Ac",
    "aB+Ac+ABC",
    "c+B",
    "aC+Bc+Abc+ABC",
    "a+c+B",
    "AC+ABc",
    "abc+AC+ABc",
    "bC+AB",
    "ab+AC+ABc",
    "Bc+AC",
    "ac+AC+ABc",
    "bC+Bc+ABC",
    "ab+Bc+AC",
    "BC+AbC+ABc",
    "abc+BC+AbC+ABc",
    "C+AB",
    "ab+BC+AbC+ABc",
    "aB+AC+ABc",
    "ac+BC+AbC+ABc",
    "C+B",
    "a+C+B",
    "A",
    "bc+AC+ABc",
    "bC+Ac+ABC",
    "b+A",
    "Bc+Ab+ABC",
    "c+A",
    "bC+Bc+Abc+ABC",
    "b+c+A",
    "BC+Ab+ABc",
    "bc+BC+AbC+ABc",
    "C+A",
    "b+C+A",
    "B+A",
    "ac+BC+Ab+ABc",
    "aC+Bc+Ab+ABC",
    "1",
};

#define A 0
#define B 1
#define C 2
#define D 3
#define I 0xff
// First byte: Period
constexpr uint8_t blitcycles[16][5] = {
    { 1, I          }, // 0          none
    { 2, D, I       }, // 1            D      D0  - D1  - D2
    { 2, C, I       }, // 2          C        C0  - C1  - C2
    { 3, C, D, I    }, // 3          C D      C0  -  - C1 D0  - C2 D1  - D2
    { 3, B, I, I    }, // 4        B          B0  -  - B1  -  - B2
    { 3, B, D, I    }, // 5        B   D      B0  -  - B1 D0  - B2 D1  - D2
    { 3, B, C, I    }, // 6        B C        B0 C0  - B1 C1  - B2 C2
    { 4, B, C, D, I }, // 7        B C D      B0 C0  -  - B1 C1 D0  - B2 C2 D1  - D2
    { 2, A, I       }, // 8      A            A0  - A1  - A2
    { 2, A, D       }, // 9      A     D      A0  - A1 D0 A2 D1  - D2
    { 2, A, C       }, // A      A   C        A0 C0 A1 C1 A2 C2
    { 3, A, C, D    }, // B      A   C D      A0 C0  - A1 C1 D0 A2 C2 D1  - D2
    { 3, A, B, I    }, // C      A B          A0 B0  - A1 B1  - A2 B2
    { 3, A, B, D    }, // D      A B   D      A0 B0  - A1 B1 D0 A2 B2 D1  - D2
    { 3, A, B, C    }, // E      A B C        A0 B0 C0 A1 B1 C1 A2 B2 C2
    { 4, A, B, C, D }, // F      A B C D      A0 B0 C0  - A1 B1 C1 D0 A2 B2 C2 D1 D2
};
// See Amiga_Hardware_Manual_Errata.pdf (Note: Only 1,5,9 and D are different from the nomral cycle diagrams)
constexpr uint8_t blitcycles_fill[16][5] = {
    { 1, I          }, // 0          none
    { 3, I, D, I    }, // 1            D       - D0  -  - D1  -  - D2
    { 2, C, I       }, // 2          C        C0  - C1  - C2
    { 3, C, D, I    }, // 3          C D      C0  -  - C1 D0  - C2 D1  - D2
    { 3, B, I, I    }, // 4        B          B0  -  - B1  -  - B2
    { 4, I, B, D, I }, // 5        B   D       - B0  -  -  - B1 D0  -  - B2 D1  - D2
    { 3, B, C, I    }, // 6        B C        B0 C0  - B1 C1  - B2 C2
    { 4, B, C, D, I }, // 7        B C D      B0 C0  -  - B1 C1 D0  - B2 C2 D1  - D2
    { 2, A, I       }, // 8      A            A0  - A1  - A2
    { 3, A, D, I    }, // 9      A     D      A0  -  - A1 D0  - A2 D1  - D2
    { 2, A, C       }, // A      A   C        A0 C0 A1 C1 A2 C2
    { 3, A, C, D    }, // B      A   C D      A0 C0  - A1 C1 D0 A2 C2 D1  - D2
    { 3, A, B, I    }, // C      A B          A0 B0  - A1 B1  - A2 B2
    { 4, A, B, D, I }, // D      A B   D      A0 B0  -  - A1 B1 D0  - A2 B2 D1  - D2
    { 3, A, B, C    }, // E      A B C        A0 B0 C0 A1 B1 C1 A2 B2 C2
    { 4, A, B, C, D }, // F      A B C D      A0 B0 C0  - A1 B1 C1 D0 A2 B2 C2 D1 D2
};
#undef A
#undef B
#undef C
#undef D
#undef I

// 454 virtual Lores pixels
// 625 lines/frame (interlaced)
// https://retrocomputing.stackexchange.com/questions/44/how-to-obtain-256-arbitrary-colors-with-limitation-of-64-per-line-in-amiga-ecs

// Maximum overscan (http://coppershade.org/articles/AMIGA/Denise/Maximum_Overscan/)
// DIWSTRT = $1b51
// DIWSTOP = $37d1
// DDFSTRT = $0020
// DDFSTOP = $00d8
// BPLCON2 = $0000

static constexpr uint16_t lores_min_pixel  = 0x51; 
static constexpr uint16_t lores_max_pixel  = 0x1d1;
static constexpr uint16_t hires_min_pixel  = 2 * lores_min_pixel;
static constexpr uint16_t hires_max_pixel  = 2 * lores_max_pixel;

// Extra pixels are shown on the same line even after "real" HPOS has rolled around
// This is needed for max overscan support
static constexpr uint16_t disp_extra_hpos = 11;
static_assert(hpos_per_line + disp_extra_hpos >= lores_max_pixel);

//static constexpr uint16_t vpos_per_frame  = 625;
static constexpr uint16_t vblank_end_vpos = 0x1a;
static constexpr uint16_t sprite_dma_start_vpos = 25; // http://eab.abime.net/showpost.php?p=1048395&postcount=200

static_assert(graphics_width == hires_max_pixel - hires_min_pixel);
static_assert(graphics_height == (/*vpos_per_field*/312 - vblank_end_vpos) * 2);

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

template<typename T>
constexpr T rol(T val, unsigned amt)
{
    return val << amt | val >> (sizeof(T) * CHAR_BIT - amt);
}

template<typename T>
constexpr T ror(T val, unsigned amt)
{
    return val >> amt | val << (sizeof(T) * CHAR_BIT - amt);
}

constexpr uint16_t interleave8(uint8_t x, uint8_t y)
{
    // https://graphics.stanford.edu/~seander/bithacks.html#Interleave64bitOps
    return (((x * 0x0101010101010101ULL & 0x8040201008040201ULL) * 0x0102040810204081ULL >> 49) & 0x5555) | (((y * 0x0101010101010101ULL & 0x8040201008040201ULL) * 0x0102040810204081ULL >> 48) & 0xAAAA);
}

constexpr uint32_t interleave16(uint16_t x, uint16_t y)
{
    const auto lo = interleave8(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    const auto hi = interleave8(static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(y >> 8));
    return hi << 16 | lo;
}

constexpr float pi = 3.1415926535897932385f;

//template <float cutoff_frequeny>
struct simple_lowpass_filter {
public:
    static constexpr float cutoff_frequeny = 4400.0f;

    float operator()(const float in)
    {
        const float out = last_ + alpha_ * (in - last_);
        last_ = out;
        return out;
    }

private:
    static constexpr float x_ = 2.0f * pi * cutoff_frequeny / audio_sample_rate;
    static constexpr float alpha_ = x_ / (x_ + 1);
    float last_/* = 0*/;
};

enum class ddfstate {
    before_ddfstrt,
    active,
    ddfstop_passed,
    stopped,
};

enum class copper_state {
    halted,
    vblank,
    read_inst,
    need_free_cycle,
    wait,
    skip,
    jmp_delay1,
    jmp_delay2,
};

constexpr uint8_t spr_active_disp_mask = 1 << 0;  // Any sprites displayed this line?
constexpr uint8_t spr_active_check_mask = 1 << 1; // Any more sprite checks necessary this frame?

struct custom_state {
    // Internal state
    uint16_t hpos; // Resolution is in low-res pixels
    uint16_t vpos;
    uint8_t  eclock_cycle;
    uint16_t bpldat_shift[6];
    uint16_t bpldat_temp[6];
    bool bpl1dat_written;
    bool bpl1dat_written_this_line;
    uint8_t bpldata_avail;
    uint16_t bplcon1_denise;
    uint32_t ham_color;
    ddfstate ddfst;
    uint16_t ddfcycle;
    uint16_t ddfend;
    bool long_frame;
    bool last_long_frame;
    uint16_t bplmod1_pending;
    uint16_t bplmod2_pending;
    uint8_t bplmod1_countdown;
    uint8_t bplmod2_countdown;

    bool rmb_pressed[2];
    uint8_t cur_mouse_x;
    uint8_t cur_mouse_y;
    uint16_t joydat;

    uint32_t dskpt;
    uint16_t dsklen;
    uint16_t dskwait; // HACK
    uint16_t dsklen_act;
    uint16_t dsksync;
    bool dsksync_passed;
    bool dskread;
    uint16_t dskbyt;
    uint16_t dskpos;
    uint32_t mfm_pos;
    uint8_t mfm_track[MFM_TRACK_SIZE_WORDS * 2];

    uint32_t copper_pt;
    uint16_t copper_inst[2];
    uint8_t copper_inst_ofs;
    bool copper_skip_next;
    bool cdang;
    copper_state copstate;

    uint8_t spr_dma_active_mask;
    uint8_t spr_armed_mask; // armed by writing to SPRxDATA, disarmed by writing to SPRxCTL
    uint8_t spr_active_mask;
    uint32_t spr_hold[8];

    uint16_t bltw;
    uint16_t blth;
    uint16_t bltaold;
    uint16_t bltbold;
    uint16_t bltbhold;
    uint8_t bltx;
    uint8_t bltcycle;
    bool bltdwrite;
    bool bltinpoly;
    uint32_t bltdpt;
    uint8_t blitline_ashift;
    uint16_t blitline_a;
    uint16_t blitline_b;
    bool blitline_sign;
    bool blitline_dot_this_line;
    bool blitfirst;
    uint8_t bltblockingcpu;
    uint8_t blitdelay;
    enum { blit_stopped, blit_starting, blit_running, blit_final } blitstate;

    uint32_t coplc[2];
    uint16_t diwstrt;
    uint16_t diwstop;
    uint16_t ddfstrt;
    uint16_t ddfstop;
    uint16_t dmacon;
    uint16_t intena;
    uint16_t intreq;
    uint16_t adkcon;
    uint8_t int_delay[INTB_EXTER+1];
    uint8_t ipl_current;
    uint8_t ipl_pending;
    uint8_t ipl_delay;

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

    // audio
    enum class audio_channel_state {
        inactive      = 0b000,
        dma_starting  = 0b001, 
        dma_has_first = 0b101,
        dma_samp1     = 0b010,
        dma_samp2     = 0b011,
    };
    struct audio_channel {
        uint32_t lc;
        uint16_t len;
        uint16_t per;
        uint16_t vol;
        uint16_t dat;

        audio_channel_state state;
        uint32_t ptr;
        uint16_t actlen;
        uint16_t actdat;
        uint16_t percnt;
        bool dmareq;
        //http://eab.abime.net/showthread.php?t=86880
        //A500: 0.1 uF, 360 Ohm -> 4,4 kHz
        //A600: 3900 pF, 1.5k Ohm -> 27 kHz
        //A1200 r1: 3900pF, 1.5kOhm -> 27 kHz
        //A1200 r2: 6800pF, 680 Ohm -> 34 kHz
        simple_lowpass_filter/*<4400.0f>*/ filter;
        int32_t output;

        void load_per()
        {
            percnt = per;
            if (per && per < 113)
                percnt = 113;
        }

        float filtered_output(float scale)
        {
            return filter(static_cast<float>(output) * scale);
        }

    } audio_channels[4];

    void update_blitter_line()
    {
        auto incx = [&]() {
            if (++blitline_ashift == 16) {
                blitline_ashift = 0;
                bltpt[2] += 2;
            }
        };
        auto decx = [&]() {
            if (blitline_ashift-- == 0) {
                blitline_ashift = 15;
                bltpt[2] -= 2;
            }
        };
        auto incy = [&]() {
            bltpt[2] += bltmod[2];
            blitline_dot_this_line = false;
        };
        auto decy = [&]() {
            bltpt[2] -= bltmod[2];
            blitline_dot_this_line = false;
        };

        if (!blitline_sign) {
            if (bltcon1 & BC1F_SUD) {
                if (bltcon1 & BC1F_SUL)
                    decy();
                else
                    incy();
            } else {
                if (bltcon1 & BC1F_SUL)
                    decx();
                else
                    incx();
            }
        }
        if (bltcon1 & BC1F_SUD) {
            if (bltcon1 & BC1F_AUL)
                decx();
            else
                incx();
        } else {
            if (bltcon1 & BC1F_AUL)
                decy();
            else
                incy();
        } 
    }

    uint16_t get_mfm_word() const
    {
        const uint32_t b = mfm_pos / 8;
        const uint32_t track_size = MFM_TRACK_SIZE_WORDS * 2;
        const uint32_t dat = mfm_track[b % track_size] << 16 | mfm_track[(b + 1) % track_size] << 8 | mfm_track[(b + 2) % track_size];
        return static_cast<uint16_t>(dat >> (8 - mfm_pos % 8));
    }

};

template <uint16_t bplcon0>
uint32_t one_pixel(custom_state& s_, uint8_t active_sprite, uint8_t active_sprite_group, uint8_t pixel, const uint32_t* col32)
{
    const uint8_t nbpls = std::min<uint8_t>(6, (bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0);
    assert(pixel < 64);
    pixel &= (1 << nbpls) - 1;

#if 0
        if (DEBUG_BPL) {
            rem_pixels_odd_--;
            rem_pixels_even_--;
            if ((rem_pixels_odd_ | rem_pixels_even_) < 0 && nbpls && (s_.dmacon & (DMAF_MASTER | DMAF_RASTER)) == (DMAF_MASTER | DMAF_RASTER)) {
                DBGOUT << "Warning: out of pixels O=" << rem_pixels_odd_ << " E=" << rem_pixels_even_ << "\n";
            }
        }
#endif

    const uint8_t pf2p = (s_.bplcon2 >> 3) & 7;

    if constexpr (!!(bplcon0 & BPLCON0F_DBLPF)) {
        const uint8_t pf1p = s_.bplcon2 & 7;
        const uint8_t pf1 = (pixel & 16) >> 2 | (pixel & 4) >> 1 | (pixel & 1);
        const uint8_t pf2 = (pixel & 32) >> 3 | (pixel & 8) >> 2 | (pixel & 2) >> 1;
        const bool pf1vis = active_sprite_group >= pf1p;
        const bool pf2vis = active_sprite_group >= pf2p;
        uint8_t idx;
        // Illegal PF priority => drawn transparent
        const uint8_t pf1_idx = pf1p >= 5 ? 0 : pf1;
        const uint8_t pf2_idx = pf2p >= 5 ? 0 : pf2 + 8;

        if (s_.bplcon2 & 0x40) { // PF2PRI
            if (pf2vis && pf2)
                idx = pf2_idx;
            else if (!pf1vis || !pf1)
                idx = active_sprite;
            else
                idx = pf1_idx;
        } else {
            if (pf1vis && pf1)
                idx = pf1_idx;
            else if (!pf2vis || !pf2)
                idx = active_sprite;
            else
                idx = pf2_idx;
        }
        return col32[idx];
    } else if constexpr ((bplcon0 & BPLCON0F_HOMOD) && nbpls >= 5) { // HAM is only active if 5 or 6 bitplanes
        const uint32_t val = pixel & 0xf;
        auto& col = s_.ham_color;
        switch (pixel >> 4) {
        case 0: // Palette entry
            col = col32[val];
            break;
        case 1: // Modify B
            col = (col & 0xffff00) | val << 4 | val;
            break;
        case 2: // Modify R
            col = (col & 0x00ffff) | val << 20 | val << 16;
            break;
        case 3: // Modify G
            col = (col & 0xff00ff) | val << 12 | val << 8;
            break;
        default:
            assert(false);
        }
        // if (active_sprite_group < pf2p)
        if (active_sprite) // How is priority handled here?
            return col32[active_sprite];
        return col;
    } else if (active_sprite_group < pf2p || !pixel) { // no active sprite -> active_sprite=0, so same as pf1
        return col32[active_sprite];
    } else if constexpr (nbpls < 6) {
        return col32[pixel];
    } else {
        // EHB bpls=6 && !HAM && !DPU
        const auto col = col32[pixel & 0x1f];
        return pixel & 0x20 ? (col & 0xfefefe) >> 1 : col;
    }
}

template <std::size_t... I>
constexpr auto make_one_pixel_func_array(std::index_sequence<I...>)
{
    return std::array<decltype(&one_pixel<0>), sizeof...(I)> { &one_pixel<I<<10>... };
}

constexpr auto one_pixel_funcs = make_one_pixel_func_array(std::make_index_sequence<32> {});


} // unnamed namespace

class custom_handler::impl final : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler, cia_handler& cia, uint32_t slow_end, uint32_t floppy_speed)
        : mem_ { mem_handler }
        , cia_ { cia }
        , chip_ram_mask_ { static_cast<uint32_t>(mem_.ram().size()) - 2 } // -1 for normal mask, -2 for word aligned mask
        , floppy_speed_ { floppy_speed }
    {
        mem_.register_handler(*this, custom_base_addr, custom_mem_size);
        for (uint32_t addr = slow_end; addr < 0xDC'0000; addr += 256 << 10) {
            assert((addr & ((256 << 10) - 1)) == 0);
            // Mirror custom registers (due to partial decoding), this is necessary for e.g. KS1.2
            mem_.register_handler(*this, addr + (256 << 10) - 0x1000, 0x1000);
        }
        reset();
    }

    void reset() override
    {
        std::memset(gfx_buf_, 0, sizeof(gfx_buf_));
        std::memset(audio_buf_, 0, sizeof(audio_buf_));
        std::memset(&s_, 0, sizeof(s_));
        std::memset(col32_, 0, sizeof(col32_));
        s_.long_frame = true;
        s_.copstate = copper_state::halted;
        one_pixel_ = one_pixel_funcs[0];
    }

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, "Custom", 1 };
        sf.handle_blob(&s_, sizeof(s_));
        if (sf.loading()) {
            for (int i = 0; i < 32; ++i)
                col32_[i] = rgb4_to_8(s_.color[i]);
            for (int spr = 0; spr < 8; ++spr)
                sprite_state_[spr].recalc(s_.sprpos[spr], s_.sprctl[spr]);
            one_pixel_ = one_pixel_funcs[(s_.bplcon0 >> 10) & 31];
        }
    }


    void set_serial_data_handler(const serial_data_handler& handler)
    {
        serial_data_handler_ = handler;
    }

    void set_rbutton_state(bool pressed)
    {
        s_.rmb_pressed[0] = pressed;
    }

    void mouse_move(int dx, int dy)
    {
        s_.cur_mouse_x = static_cast<uint8_t>(s_.cur_mouse_x + dx);
        s_.cur_mouse_y = static_cast<uint8_t>(s_.cur_mouse_y + dy);
    }

    void set_joystate(uint16_t dat, bool button_state)
    {
        s_.joydat = dat;
        s_.rmb_pressed[1] = button_state;
    }

    void show_debug_state(std::ostream& os)
    {
        cia_.show_debug_state(os);
        os << "DSK PT: " << hexfmt(s_.dskpt) << " LEN: " << hexfmt(s_.dsklen_act) << " ADK: " << hexfmt(s_.adkcon) << " SYNC: " << hexfmt(s_.dsksync) << "\n";
        os << "DMACON: " << hexfmt(s_.dmacon) << " INTENA: " << hexfmt(s_.intena) << " INTREQ: " << hexfmt(s_.intreq) << " VPOS: " << hexfmt(s_.vpos) << " HPOS: " << hexfmt(s_.hpos) << " (DMA: " << hexfmt(s_.hpos>>1,2) << ")\n";
        os << "INT: " << hexfmt(s_.intena & s_.intreq, 4) << " IPL: " << hexfmt(current_ipl()) << "\n";
        os << "COP1LC: " << hexfmt(s_.coplc[0]) << " COP2LC: " << hexfmt(s_.coplc[1]) << " COPPTR: " << hexfmt(s_.copper_pt) << "\n";
        os << "DIWSTRT: " << hexfmt(s_.diwstrt) << " DIWSTOP: " << hexfmt(s_.diwstop) << " DDFSTRT: " << hexfmt(s_.ddfstrt) << " DDFSTOP: " << hexfmt(s_.ddfstop) << "\n";
        os << "BPLCON 0: " << hexfmt(s_.bplcon0) << " 1: " << hexfmt(s_.bplcon1) << " 2: " << hexfmt(s_.bplcon2) << " LOF=" << s_.long_frame << "\n";
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

    void do_immedite_blit_line()
    {
        for (uint16_t cnt = 0; cnt < s_.blth; ++cnt) {
            // First pixel is written to D
            s_.bltdpt = cnt ? s_.bltpt[2] : s_.bltpt[3];
            s_.bltdwrite = !(s_.bltcon1 & BC1F_ONEDOT) || !s_.blitline_dot_this_line;
            s_.bltdat[2] = chip_read(s_.bltpt[2]);
            s_.bltbhold = s_.blitline_b & 1 ? 0xFFFF : 0;
            s_.bltdat[3] = blitter_func(s_.bltcon0 & 0xff, (s_.blitline_a & s_.bltafwm) >> s_.blitline_ashift, s_.bltbhold, s_.bltdat[2]);
            s_.bltpt[0] += s_.blitline_sign ? s_.bltmod[1] : s_.bltmod[0];
            s_.blitline_dot_this_line = true;

            s_.update_blitter_line();

            s_.blitline_sign = static_cast<int16_t>(s_.bltpt[0]) < 0;
            s_.blitline_b = rol(s_.blitline_b, 1);
            if (s_.bltdwrite)
                chip_write(s_.bltdpt, s_.bltdat[3]);
        }
    }

    bool do_immedite_blit()
    {
        if (!s_.blth)
            return false;

        // For now just do immediate blit
        uint16_t any = 0;

        if (s_.bltcon1 & BC1F_LINEMODE) {
            do_immedite_blit_line();
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

            bool dwrite = false;
            uint32_t dpt = 0;
            uint16_t dval = 0;
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

                    if (dwrite)
                        chip_write(dpt, dval);

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
                        dwrite = true;
                        dpt = s_.bltpt[3];
                        dval = val;
                        incr_ptr(s_.bltpt[3]);
                    }
                }
                if (s_.bltcon0 & BC0F_SRCA) incr_ptr(s_.bltpt[0], s_.bltmod[0]);
                if (s_.bltcon0 & BC0F_SRCB) incr_ptr(s_.bltpt[1], s_.bltmod[1]);
                if (s_.bltcon0 & BC0F_SRCC) incr_ptr(s_.bltpt[2], s_.bltmod[2]);
                if (s_.bltcon0 & BC0F_DEST) incr_ptr(s_.bltpt[3], s_.bltmod[3]);
            }
            if (dwrite)
                chip_write(dpt, dval);
        }
        if (!any)
            s_.dmacon |= DMAF_BLTNZERO;
        s_.bltw = s_.blth = 0;
        s_.blitstate = custom_state::blit_final;
        blitfinished();
        blitdone();
        return false;
    }

    bool do_blitter_line()
    {
        const uint8_t period = 4; // -C-D
        const bool no_dma = s_.bltblockingcpu >= 3;
        bool dma = false;

        switch (s_.bltcycle) {
        case 0:
        case 2:
            // Idle cycles
            break;
        case 1: // C
            if (s_.blitstate == custom_state::blit_final)
                break;
            if (no_dma)
                return false;
            // First pixel is written to D
            s_.bltdpt = s_.blitfirst ? s_.bltpt[3] : s_.bltpt[2];
            s_.blitfirst = false;
            s_.bltdwrite = !(s_.bltcon1 & BC1F_ONEDOT) || !s_.blitline_dot_this_line;
            s_.bltdat[2] = chip_read(s_.bltpt[2]);
            s_.bltbhold = s_.blitline_b & 1 ? 0xFFFF : 0;
            s_.bltdat[3] = blitter_func(s_.bltcon0 & 0xff, (s_.blitline_a & s_.bltafwm) >> s_.blitline_ashift, s_.bltbhold, s_.bltdat[2]);
            s_.bltpt[0] += s_.blitline_sign ? s_.bltmod[1] : s_.bltmod[0];
            s_.blitline_dot_this_line = true;

            s_.update_blitter_line();

            s_.blitline_sign = static_cast<int16_t>(s_.bltpt[0]) < 0;
            s_.blitline_b = rol(s_.blitline_b, 1);

            dma = true;
            break;
        case 3: // D
            if (s_.bltdwrite) {
                if (no_dma)
                    return false;
                chip_write(s_.bltdpt, s_.bltdat[3]);
                dma = true;
            }
            break;
        }

        if (++s_.bltcycle == period) {
            s_.bltcycle = 0;

            if (s_.blitstate == custom_state::blit_final) {
                blitdone();
                blitfinished();
            } else if (--s_.blth == 0) {
                assert(s_.blitstate == custom_state::blit_running);
                s_.blitstate = custom_state::blit_final;
            }
        }

        return dma;
    }

    // 1 cycle for write to BLTSIZE to take effect and 2 blitter idle cycles before starting
    // https://github.com/dirkwhoffmann/vAmiga/issues/466#issuecomment-928186357
    void check_blit_start_delay()
    {
        if (s_.blitstate != custom_state::blit_starting)
            return;
        assert(s_.blitdelay);
        if (--s_.blitdelay == 0) {
            if (DEBUG_BLITTER)
                DBGOUT << "BLTSIZE write taking effect\n";
            s_.blitstate = custom_state::blit_running;
            s_.blitdelay = 2;
        }
    }

    void check_blit_idle_any_cycle()
    {
        if (!(s_.dmacon & (DMAF_MASTER|DMAF_BLITTER)) || s_.blitstate == custom_state::blit_stopped)
            return;
        if (s_.blitstate == custom_state::blit_starting) {
            check_blit_start_delay();
        } else if (s_.blitstate == custom_state::blit_final && !(s_.bltcon1 & BC1F_LINEMODE)) {
            // Idle cycles at end don't need free bus
            // (Don't handle line mode here)
            const auto cd = (s_.bltcon1 & (BC1F_FILL_OR | BC1F_FILL_XOR) ? blitcycles_fill : blitcycles)[(s_.bltcon0 >> 8) & 0xf];
            const uint8_t period = cd[0];
            assert(s_.bltcycle < period);
            if (cd[1 + s_.bltcycle] != 3) { // Not a D-cycle?
                if (s_.bltcycle + 1 < period)
                    ++s_.bltcycle;
                else
                    blitdone();
            }

        }
    }

    bool do_blitter()
    {
#if 0
        if (1)
            return do_immedite_blit();
#endif

        if (s_.blitstate == custom_state::blit_stopped)
            return false;

        assert((s_.dmacon & DMAF_BLTBUSY) || s_.blitstate == custom_state::blit_final);

        if (s_.blitstate == custom_state::blit_starting) {
            check_blit_start_delay();
            return false;
        }

        // Blitter idle cycles before starting
        if (s_.blitdelay) {            
            --s_.blitdelay;
            if (DEBUG_BLITTER && !s_.blitdelay)
                DBGOUT << "Blitter starting idle cycles done\n";
            return false;
        }

        if (s_.bltcon1 & BC1F_LINEMODE) {
            return do_blitter_line();
        }

        const bool fillmode = !!(s_.bltcon1 & (BC1F_FILL_OR | BC1F_FILL_XOR));
        const uint8_t usef = (s_.bltcon0 >> 8) & 0xf;
        const auto cd = fillmode ? blitcycles_fill : blitcycles;
        const uint8_t period = cd[usef][0];
        assert(s_.bltcycle < period);
        const uint8_t cycle = cd[usef][1 + s_.bltcycle];
        bool no_dma = s_.bltblockingcpu >= 3;

        const bool reverse = !!(s_.bltcon1 & BC1F_BLITREVERSE);
        bool dma = false;

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

        switch (cycle) {
        case 0: { // A
            if (s_.blitstate == custom_state::blit_final)
                break;
            if (no_dma)
                return false;
            do_dma(0);
            dma = true;
            break;
        }
        case 1: { // B
            if (s_.blitstate == custom_state::blit_final)
                break;
            if (no_dma)
                return false;
            do_dma(1);
            dma = true;
            const uint8_t bshift = s_.bltcon1 >> BC1_BSHIFTSHIFT;
            if (reverse)
                s_.bltbhold = ((uint32_t)s_.bltdat[1] << 16 | s_.bltbold) >> (16 - bshift);
            else
                s_.bltbhold = ((uint32_t)s_.bltbold << 16 | s_.bltdat[1]) >> bshift;
            s_.bltbold = s_.bltdat[1];
            break;
        }
        case 2: { // C
            if (s_.blitstate == custom_state::blit_final)
                break;
            if (no_dma)
                return false;
            do_dma(2);
            dma = true;
            break;
        }
        case 3: { // D
            if (s_.bltdwrite) {
                if (no_dma)
                    return false;
                chip_write(s_.bltdpt, s_.bltdat[3]);
                dma = true;
            }
            break;
        }
        case 0xff: // Idle
            break;
        default:
            assert(0);
        }

        if (++s_.bltcycle == period) {
            s_.bltcycle = 0;

            if (s_.blitstate == custom_state::blit_final) {
                blitdone();
            } else {
                // A
                uint16_t a = s_.bltdat[0], ahold;
                if (s_.bltx == 0)
                    a &= s_.bltafwm;
                if (s_.bltx == s_.bltw - 1)
                    a &= s_.bltalwm;
                const uint8_t ashift = s_.bltcon0 >> BC0_ASHIFTSHIFT;
                if (reverse)
                    ahold = ((uint32_t)a << 16 | s_.bltaold) >> (16 - ashift);
                else
                    ahold = ((uint32_t)s_.bltaold << 16 | a) >> ashift;
                s_.bltaold = a;
                // B already shifted
                // Nothing for C

                uint16_t val = blitter_func(static_cast<uint8_t>(s_.bltcon0), ahold, s_.bltbhold, s_.bltdat[2]);
                if (fillmode) {
                    for (uint8_t bit = 0; bit < 16; ++bit) {
                        const uint16_t mask = 1U << bit;
                        const bool match = !!(val & mask);
                        if (s_.bltinpoly) {
                            if (s_.bltcon1 & BC1F_FILL_XOR)
                                val ^= mask;
                            else
                                val |= mask;
                        }
                        if (match)
                            s_.bltinpoly = !s_.bltinpoly;
                    }
                }
                if (val)
                    s_.dmacon &= ~DMAF_BLTNZERO;

                if (s_.bltcon0 & BC0F_DEST) {
                    s_.bltdwrite = true;
                    s_.bltdat[3] = val;
                    s_.bltdpt = s_.bltpt[3];
                    incr_ptr(s_.bltpt[3]);
                }

                if (++s_.bltx == s_.bltw) {
                    s_.bltx = 0;
                    s_.bltinpoly = !!(s_.bltcon1 & BC1F_FILL_CARRYIN);
                    if (--s_.blth == 0) {
                        assert(s_.blitstate == custom_state::blit_running);
                        s_.blitstate = custom_state::blit_final;
                        blitfinished();
                    }
                    if (s_.bltcon0 & BC0F_SRCA)
                        incr_ptr(s_.bltpt[0], s_.bltmod[0]);
                    if (s_.bltcon0 & BC0F_SRCB)
                        incr_ptr(s_.bltpt[1], s_.bltmod[1]);
                    if (s_.bltcon0 & BC0F_SRCC)
                        incr_ptr(s_.bltpt[2], s_.bltmod[2]);
                    if (s_.bltcon0 & BC0F_DEST)
                        incr_ptr(s_.bltpt[3], s_.bltmod[3]);
                }
            }
        }

        return dma;
    }

    void blitstart(uint16_t val)
    {
        s_.bltsize = val;
        s_.bltw = val & 0x3f ? val & 0x3f : 0x40;
        s_.blth = val >> 6 ? val >> 6 : 0x400;
        s_.bltaold = 0;
        s_.bltbold = 0;
        s_.dmacon |= DMAF_BLTBUSY | DMAF_BLTNZERO;
        s_.bltx = 0;
        s_.bltcycle = 0;
        s_.bltdwrite = false;
        s_.bltinpoly = !!(s_.bltcon1 & BC1F_FILL_CARRYIN);      
        s_.bltdpt = 0;
        s_.blitstate = custom_state::blit_starting;
        s_.blitdelay = 2; // One CCK?
        s_.bltblockingcpu = 0;
        s_.blitfirst = true;

        if (s_.bltcon1 & BC1F_LINEMODE) {
            // http://eab.abime.net/showpost.php?p=206412&postcount=6
            // D can be disabled
            // C MUST be enabled (otherwise nothing is drawn)
            // If A is disabled BLTAPT isn't updated
            if ((s_.bltcon0 & (BC0F_SRCA | BC0F_SRCB | BC0F_SRCC)) != (BC0F_SRCA | BC0F_SRCC))
                DBGOUT << "Warning: Blitter line unexpected BLTCON0 $" << hexfmt(s_.bltcon0) << "\n";
            if (s_.bltw != 2)
                DBGOUT << "Warning: Blitter line unexpected width $" << hexfmt(s_.bltw) << "\n";

            // Do not reset blitline_ashift/blitline_sign here (vAmigaTS timing1l)
            s_.blitline_dot_this_line = false;
            s_.blitline_a = s_.bltdat[0];
            s_.blitline_b = ror(s_.bltdat[1], s_.bltcon1 >> BC1_BSHIFTSHIFT);
        }

        if (DEBUG_BLITTER) {
            DBGOUT << "Blit $" << hexfmt(s_.bltw) << "x$" << hexfmt(s_.blth) << " bltcon0=$" << hexfmt(s_.bltcon0) << " bltcon1=$" << hexfmt(s_.bltcon1) << " bltafwm=$" << hexfmt(s_.bltafwm) << " bltalwm=$" << hexfmt(s_.bltalwm) << "\n";
            for (int i = 0; i < 4; ++i) {
                const char name[5] = { 'B', 'L', 'T', static_cast<char>('A' + i), 0 };
                *debug_stream << "\t" << name << "PT=$" << hexfmt(s_.bltpt[i]) << " " << name << "DAT=$" << hexfmt(s_.bltdat[i]) << " " << name << "MOD=$" << hexfmt(s_.bltmod[i]) << " (" << (int)s_.bltmod[i] << ")\n";
            }
            if (s_.bltcon1 & BC1F_LINEMODE)
                *debug_stream << "\tLine mode";
            else if (s_.bltcon1 & (BC1F_FILL_OR | BC1F_FILL_XOR))
                *debug_stream << "\t" << (s_.bltcon1 & BC1F_FILL_XOR ? "Xor" : "Or") << " fill mode";
            else
                *debug_stream << "\tNormal mode";
            *debug_stream << " minterm: D = " << minterm_desc[s_.bltcon0 & 0xff] << "\t";
            if (s_.bltcon0 & BC0F_SRCA)
                *debug_stream << " SRCA";
            if (s_.bltcon0 & BC0F_SRCB)
                *debug_stream << " SRCB";
            if (s_.bltcon0 & BC0F_SRCC)
                *debug_stream << " SRCC";
            if (s_.bltcon0 & BC0F_DEST)
                *debug_stream << " DEST";
            *debug_stream << "\n";
        }
    }

    void blitfinished()
    {
        if (DEBUG_BLITTER)
            DBGOUT << "Blitter finished INTREQ=$" << hexfmt(s_.intreq) << " DMACON=$" << hexfmt(s_.dmacon) << " state=" << (int)s_.blitstate << "\n";
        interrupt_with_delay(INTB_BLIT, 8+2); // 4 CCK delay +2 not correct (should probably just be +1 since ipl_delay is decremented in main loop)
        s_.dmacon &= ~DMAF_BLTBUSY;
    }

    void blitdone()
    {
        assert(s_.blitstate == custom_state::blit_final);
        assert(s_.blth == 0);
        if (DEBUG_BLITTER)
            DBGOUT << "Blitter done INTREQ=$" << hexfmt(s_.intreq) << " DMACON=$" << hexfmt(s_.dmacon) << " state=" << (int)s_.blitstate << "\n";
        s_.bltw = s_.blth = 0;
        s_.blitstate = custom_state::blit_stopped;
        if (s_.bltcon1 & BC1F_LINEMODE)
            s_.bltpt[3] = s_.bltpt[2];
    }

    bool do_copper()
    {
        switch (s_.copstate) {
        case copper_state::halted:
            return false;
        case copper_state::vblank:
            // copper doesn't load COP1LC until DMA has been started after vblank ("ham" test)
            s_.copper_pt = s_.coplc[0];
            s_.copper_inst_ofs = 0;
            s_.copper_skip_next = false;
            s_.copstate = copper_state::read_inst;
            if (DEBUG_COPPER)
                DBGOUT << "Copper starting IP=$" << hexfmt(s_.copper_pt) << "\n";
            return true; // seems like copper does use this DMA slot
        case copper_state::read_inst:
            assert(s_.copper_inst_ofs < 2);
            s_.copper_inst[s_.copper_inst_ofs++] = chip_read(s_.copper_pt);
            s_.copper_pt += 2;
            if (DEBUG_COPPER)
                DBGOUT << "Read copper instruction word $" << hexfmt(s_.copper_inst[s_.copper_inst_ofs - 1]) << " from $" << hexfmt(s_.copper_pt - 2) << "\n";
            if (s_.copper_inst_ofs != 2)
                return true;
            if (s_.copper_inst[0] & 1) {
                // Wait or skip instruction
                if (s_.copper_inst[0] == 0xFFFF && s_.copper_inst[1] == 0xFFFE) {
                    if (DEBUG_COPPER)
                        DBGOUT << "End of copper list.\n";
                    s_.copstate = copper_state::halted;
                } else {
                    s_.copstate = copper_state::need_free_cycle;
                }
            } else {
                // Move instruction: TODO: Registers managed by other chips have delay?
                const auto reg = s_.copper_inst[0] & 0x1ff;
                if (DEBUG_COPPER)
                    DBGOUT << "Copper " << (s_.copper_skip_next ? "skipping write" : "writing") << " to " << custom_regname(reg) << " value=$" << hexfmt(s_.copper_inst[1]) << "\n";
                if (reg >= 0x80 || (s_.cdang && reg >= 0x40)) {
                    if (!s_.copper_skip_next)
                        write_u16(0xdff000 + reg, reg, s_.copper_inst[1]);
                    s_.copper_inst_ofs = 0; // Fetch next instruction
                    s_.copper_skip_next = false;
                } else {
                    // Copper stops on write to dangerous/invalid register (even if supposed to be skipped)
                    if (DEBUG_COPPER)
                        DBGOUT << "Illegal register access to " << custom_regname(reg) << " value=$" << hexfmt(s_.copper_inst[1]) << " - Illegal. Pausing copper.\n";
                    s_.copstate = copper_state::halted;
                }
            }
            return true;
        case copper_state::need_free_cycle:
            if (DEBUG_COPPER)
                DBGOUT << "Free cycle -> wait for blitter/y\n";
            s_.copstate = s_.copper_inst[1] & 1 ? copper_state::skip : copper_state::wait;
            return false;
        case copper_state::jmp_delay1:
            if (DEBUG_COPPER)
                DBGOUT << "jmp_delay1 done: Allocating cycle\n";
            s_.copstate = copper_state::jmp_delay2;
            return true;
        case copper_state::jmp_delay2:
            if (DEBUG_COPPER)
                DBGOUT << "jmp_delay2 done: Allocating cycle\n";
            s_.copstate = copper_state::read_inst;
            return true;
        default:
            break;
        }

        // Wait or skip

        // Copper compare is ahead to compensate for wake-up delay
        auto chp = s_.hpos >> 1;
        auto cvp = s_.vpos;
        chp += 2;
        if (chp >= hpos_per_line / 2) {
            chp -= hpos_per_line / 2;
            ++cvp;
        }

        const auto vp = (s_.copper_inst[0] >> 8) & 0xff;
        const auto hp = s_.copper_inst[0] & 0xfe;
        const auto ve = 0x80 | ((s_.copper_inst[1] >> 8) & 0x7f);
        const auto he = s_.copper_inst[1] & 0xfe;
        const bool reached = ((s_.copper_inst[1] & 0x8000) || !(s_.dmacon & DMAF_BLTBUSY)) && ((cvp & ve) > (vp & ve) || ((cvp & ve) == (vp & ve) && (chp & he) >= (hp & he)));

        if (s_.copstate == copper_state::skip) {
            s_.copper_inst_ofs = 0; // Fetch next instruction
            s_.copper_skip_next = reached;
            s_.copstate = copper_state::read_inst;
            if (DEBUG_COPPER)
                    DBGOUT << "Skip processed, reached=" << reached << " VP=$" << hexfmt(vp, 2) << " VE=$" << hexfmt(ve, 2) << " HP=" << hexfmt(hp, 2) << " HE=" << hexfmt(he) << " BFD=" << (s_.copper_inst[1] & 0x8000 ? 1 : 0) << "\n";
        } else if (reached) {
            assert(s_.copstate == copper_state::wait);
            s_.copper_inst_ofs = 0; // Fetch next instruction
            s_.copper_skip_next = false;
            s_.copstate = copper_state::read_inst;
            if (DEBUG_COPPER)
                DBGOUT << "Wait done VP=$" << hexfmt(vp, 2) << " VE=$" << hexfmt(ve, 2) << " HP=$" << hexfmt(hp, 2) << " HE=$" << hexfmt(he) << " BFD=" << (s_.copper_inst[1] & 0x8000 ? 1 : 0) << "\n";
        }

        return false;
    }

    void copjmp(int idx)
    {
        assert(idx == 0 || idx == 1);
        s_.copper_inst_ofs = 0;
        s_.copper_pt = s_.coplc[idx];
        s_.copstate = copper_state::jmp_delay1;
        if (DEBUG_COPPER)
            DBGOUT << "Copper jump to COP" << (idx + 1) << "LC: $" << hexfmt(s_.coplc[idx]) << "\n";
    }

    bool do_disk_dma()
    {
        if (s_.dskwait) {
            --s_.dskwait;
            if (DEBUG_DISK && !s_.dskwait)
                DBGOUT << "Disk wait done\n";
            return false;
        }
        
        const uint16_t nwords = s_.dsklen_act & 0x3FFF;
        const bool write = !!(s_.dsklen_act & 0x4000);
        TODO_ASSERT(nwords > 0);

        if (write) {
            // Pretty hacky...
            if (s_.dskpos == 0) {
                if (DEBUG_DISK)
                    DBGOUT << "Disk writing starting nwords=$" << hexfmt(nwords) << "\n";
                if (nwords * 2 < sizeof(s_.mfm_track))
                    std::cerr << "[DISK] WARNING writing less than full track nwords=$" << hexfmt(nwords) << "\n";
            }
            for (uint32_t i = 0; i < floppy_speed_ && s_.dskpos < nwords; ++i) {
                const auto data = chip_read(s_.dskpt);
                s_.dskpt += 2;
                put_u16(&s_.mfm_track[(s_.dskpos % MFM_TRACK_SIZE_WORDS) * 2], data);
                ++s_.dskpos;
            }
            if (s_.dskpos < nwords)
                return true;
            cia_.active_drive().write_mfm_track(s_.mfm_track);
        } else {
            // Read
            if (!s_.dskread) {
                if (DEBUG_DISK)
                    DBGOUT << "Reading track\n";
                assert(s_.dskpos == 0);
                cia_.active_drive().read_mfm_track(s_.mfm_track);
                s_.dskread = true;
                s_.dskwait = 0;

                // TODO: More correct drive emulation. (Probably with real speed, but we still want "turbo" mode supported)
                //
                // Note: This is tricky, so be sure to retest if making changes
                // Zool expects to be able to start a new read with no gap between sectors (i.e. don't advance mfmpos any more here)
                // Rink-a-dink (using a Rob Northen loader) expects to be able to do a short read of $20 words (the header) and then the subsequent sector knowing its number
                // RSI Mega demo 1 is also picky - Reads $18c9 words (less than a full track) and doesn't like it if it can't get a good "fit"
                // Other loaders that have been troublesome:  batman, obliterator, james pond 1, speedball 2, desert dream
                // Flower/Anarchy doesn't work if floppy speed is > 100%
                if (nwords >= MFM_TRACK_SIZE_WORDS - MFM_GAP_SIZE_WORDS)
                    s_.mfm_pos = 0; // If the loader is able to handle a full track (minus the gap) then give it a clean fit to work around any issues
                else
                    s_.mfm_pos %= MFM_TRACK_SIZE_WORDS * 16; // Otherwise let it think that it's processing data as fast as possible (no gaps)
                return false;
            }

            if (!s_.dsksync_passed && (s_.adkcon & 0x400)) {
                for (uint32_t i = 0; i < MFM_TRACK_SIZE_WORDS * 16 + 16; ++i) {
                    if (s_.get_mfm_word() == s_.dsksync) {
                        if (DEBUG_DISK)
                            DBGOUT << "Disk sync word ($" << hexfmt(s_.dsksync) << ") matches at word pos $" << hexfmt(s_.mfm_pos) << "\n";
                        s_.mfm_pos += 16;
                        // XXX: FIXME: Shouldn't be done here
                        s_.intreq |= INTF_DSKSYNC;
                        s_.dsksync_passed = true;
                        s_.dskbyt = 1 << 15 | 1 << 12 | (s_.dsksync & 0xff); // XXX
                        s_.dskwait = 10; // HACK: Some demos (e.g. desert dream clear intreq after starting the read, so delay a bit)
                        return false;
                    }
                    ++s_.mfm_pos;
                }
                TODO_ASSERT(!"Sync word not found?");
            }

            if (DEBUG_DISK && !s_.dskpos)
                DBGOUT << "Disk reading $" << hexfmt(nwords) << " words to $" << hexfmt(s_.dskpt) << " mfm pos=$" << hexfmt(s_.mfm_pos) << " dsksync_passed=" << s_.dsksync_passed << "\n";

            // 400% floppy speed corrupts display at start of "flower"/Anarchy Germany (https://www.pouet.net/prod.php?which=3037)
            for (uint32_t i = 0; i < floppy_speed_ && s_.dskpos < nwords; ++i) {
                const auto data = s_.get_mfm_word();
                chip_write(s_.dskpt, data);
                s_.dskbyt = 1 << 15 | (data == s_.dsksync ? 1 << 12 : 0) | (data & 0xff); // XXX
                s_.dskpt += 2;
                s_.mfm_pos += 16;
                ++s_.dskpos;
            }
        }
        if (s_.dskpos == nwords) {
            if (DEBUG_DISK)
                DBGOUT << "Disk operation done $" << hexfmt(nwords) << " words " << (write ? "written" : "read") << "\n";
            // XXX: FIXME: Shouldn't be done here
            s_.intreq |= INTF_DSKBLK;
            s_.dsklen_act = 0;
        }
        return true;
    }

    void do_audio()
    {
        const bool dma_master = !!(s_.dmacon & DMAF_MASTER);
        for (uint8_t idx = 0; idx < 4; ++idx) {
            auto& ch = s_.audio_channels[idx];
            const bool active = dma_master && !!(s_.dmacon & (1 << idx));

            if (ch.state != custom_state::audio_channel_state::inactive && !active) {
                if (DEBUG_AUDIO)
                    DBGOUT << "Audio channel " << (int)idx << " stopping\n";
                ch.state = custom_state::audio_channel_state::inactive;
                ch.dmareq = false;
                continue;
            }

            int16_t dat = 0;
            switch (ch.state) {
            case custom_state::audio_channel_state::inactive:
                if (active) {
                    ch.state = custom_state::audio_channel_state::dma_starting;
                    ch.ptr = ch.lc;
                    ch.actlen = ch.len;
                    ch.load_per();
                    ch.dmareq = true;
                    if (DEBUG_AUDIO)
                        DBGOUT << "Audio channel " << (int)idx << " starting\n";
                }
                continue;
            case custom_state::audio_channel_state::dma_samp1:
                ch.actdat = ch.dat;
                if (ch.percnt == 1) {
                    if (DEBUG_AUDIO)
                        DBGOUT << "Audio channel " << (int)idx << " finished playing sample one - DMA request\n";
                    ch.state = custom_state::audio_channel_state::dma_samp2;
                    ch.load_per();
                    // This isn't exactly right, but gives much better quality
                    ch.dmareq = true;
                } else {
                    ch.percnt--; // period 0 => period 65536
                }
                dat = static_cast<int8_t>(ch.actdat >> 8);
                break;
            case custom_state::audio_channel_state::dma_samp2:
                if (ch.percnt == 1) {
                    if (DEBUG_AUDIO)
                        DBGOUT << "Audio channel " << (int)idx << " finished playing sample two -> 1, per=$" << hexfmt(ch.per) << "\n";
                    ch.state = custom_state::audio_channel_state::dma_samp1;
                    ch.load_per();
                } else {
                    ch.percnt--; // period 0 => period 65536
                }
                dat = static_cast<int8_t>(ch.actdat & 0xff);
                break;
            default:
                continue;
            }

            ch.output += dat * ch.vol;
        }

        // Mix 2 samples per line (but not at hpos = 0 to make life easier for main audio handling)
        if (s_.hpos == 2 || (s_.hpos >> 1) == 2 + hpos_per_line / 4) {
            auto buf = &audio_buf_[s_.vpos * 4 + 2 * (s_.hpos > 2 ? 1 : 0)];
            const float a = 1.0f / (1 + hpos_per_line / 4);
            buf[0] = static_cast<int16_t>(2 * (s_.audio_channels[0].filtered_output(a) + s_.audio_channels[3].filtered_output(a)));
            buf[1] = static_cast<int16_t>(2 * (s_.audio_channels[1].filtered_output(a) + s_.audio_channels[2].filtered_output(a)));
            s_.audio_channels[0].output = 0;
            s_.audio_channels[1].output = 0;
            s_.audio_channels[2].output = 0;
            s_.audio_channels[3].output = 0;
        }

    }

    bool audio_dma(uint8_t idx)
    {
        assert(!!(s_.dmacon & DMAF_MASTER));
        assert(idx < 4);
        auto& ch = s_.audio_channels[idx];
        if (!(s_.dmacon & (1 << idx)) || !ch.dmareq)
            return false;
        ch.dat = chip_read(ch.ptr);
        ch.ptr += 2;
        ch.dmareq = false;
        switch (ch.state) {
        case custom_state::audio_channel_state::dma_starting:
            if (DEBUG_AUDIO)
                DBGOUT << "Audio channel " << (int)idx << " DMA started actlen=$" << hexfmt(ch.actlen) << "\n";
            if (ch.actlen != 1)
                ch.actlen--;
            // XXX: FIXME: Shouldn't be done here
            s_.intreq |= 1 << (idx + INTB_AUD0);
            ch.state = custom_state::audio_channel_state::dma_samp1;
            break;
        case custom_state::audio_channel_state::dma_samp1:
            // TODO: This shouldn't happen, but can if the sample is playing quickly... Deliver it 14 cycles later to match real HW (or something)
        case custom_state::audio_channel_state::dma_samp2:
            if (ch.actlen == 1) {
                if (DEBUG_AUDIO)
                    DBGOUT << "Audio channel " << (int)idx << " finished playing (restarting)\n";
                ch.ptr = ch.lc;
                ch.actlen = ch.len;
                // XXX: FIXME: Shouldn't be done here
                s_.intreq |= 1 << (idx + INTB_AUD0);
            } else {
                if (DEBUG_AUDIO)
                    DBGOUT << "Audio channel " << (int)idx << " DMA actlen=$" << hexfmt(ch.actlen) << "\n";
                --ch.actlen;
            }
            break;
        default:
            DBGOUT << "Audio channel " << (int)idx << " in unknown state " << (int)ch.state << " in audio_dma actlen=$" << hexfmt(ch.actlen) << " period=$" << hexfmt(ch.per) << "\n";
            throw std::runtime_error { "FIXME" };
        }
        return true;
    }

    void do_sprites(const uint16_t display_hpos)
    {
        if (!(s_.spr_active_mask | s_.spr_armed_mask | s_.spr_dma_active_mask))
            return;

        // Sprite vpos check is 4 CCKs ahead
        const uint16_t spr_vpos_check = s_.vpos + (s_.hpos + 8 >= hpos_per_line);

        // Actually only need to do end check
        if (spr_vpos_check <= sprite_dma_start_vpos && !s_.spr_dma_active_mask) {
            return;
        }

        s_.spr_active_mask &= ~(spr_active_disp_mask | spr_active_check_mask);

        for (int spr = 0; spr < 8; ++spr) {
            const uint8_t mask = 1 << spr;
            auto& ss = sprite_state_[spr];

            if (s_.spr_dma_active_mask & mask) {
                // end check is always active (vAmigaTS spritedma10)
                if (spr_vpos_check == ss.vend) {
                    if (DEBUG_SPRITE)
                        DBGOUT << "Sprite " << (int)spr << " DMA state=" << !!(s_.spr_dma_active_mask & mask) << " Turning DMA off\n";
                    s_.spr_dma_active_mask &= ~mask;
                }
            } else {
                if (ss.vstart == spr_vpos_check) {
                    if (DEBUG_SPRITE)
                        DBGOUT << "Sprite " << (int)spr << " DMA state=" << !!(s_.spr_dma_active_mask & mask) << " Turning DMA on\n";
                    s_.spr_dma_active_mask |= mask;
                } else if (ss.vstart > spr_vpos_check && ss.vstart < vpos_per_field) {
                    s_.spr_active_mask |= spr_active_check_mask;
                }
            }
            if (s_.spr_hold[spr]) {
                if ((ss.idx = s_.spr_hold[spr] >> 30) != 0)
                    s_.spr_active_mask |= spr_active_disp_mask;
                s_.spr_hold[spr] <<= 2;
            } else {
                ss.idx = 0;
            }

            // Need to check AFTER shifting out pixels to both avoid gaps in clouds in Brian the Lion and support
            // sprite overlay in Platon-HAM-Eager-Final which contains copper writes to SPR0POS that align exactly with sprite start
            if ((s_.spr_armed_mask & mask) && ss.hpos == display_hpos) {
                if (DEBUG_SPRITE)
                    DBGOUT << "Sprite " << (int)spr << " DMA state=" << !!(s_.spr_dma_active_mask & mask) << " Armed and HPOS ($" << hexfmt(ss.hpos) << ") matches!\n";
                s_.spr_hold[spr] = interleave16(s_.sprdata[spr], s_.sprdatb[spr]);
                // Note: sprite stays armed here (e.g. vAmigaTS manual1)
            }
        }
    }

    void do_pixels(bool vert_disp, uint16_t display_vpos, uint16_t display_hpos, uint8_t pixel)
    {
        const unsigned disp_pixel = display_hpos * 2 - hires_min_pixel;
        if (disp_pixel >= graphics_width)
            return;
        uint32_t* row = &gfx_buf_[(display_vpos - vblank_end_vpos) * 2 * graphics_width + disp_pixel + (s_.long_frame ? 0 : graphics_width)];
        assert(&row[1] < &gfx_buf_[sizeof(gfx_buf_) / sizeof(*gfx_buf_)]);

        if (vert_disp && display_hpos >= (s_.diwstrt & 0xff) && display_hpos < (0x100 | (s_.diwstop & 0xff))) {
            if (DEBUG_BPL && (s_.dmacon & (DMAF_MASTER | DMAF_RASTER)) == (DMAF_MASTER | DMAF_RASTER) && (s_.bplcon0 & BPLCON0F_BPU) && s_.hpos == (s_.diwstrt & 0xff))
                DBGOUT << "Display starting\n";

            uint8_t active_sprite = 0;
            uint8_t active_sprite_group = 8;

            // Sprites are only visible after write to BPL1DAT
            if ((s_.spr_active_mask & spr_active_disp_mask) && s_.bpl1dat_written_this_line) {
                // Sprite 0 has highest priority, 7 lowest
                for (uint8_t spr = 0; spr < 8; ++spr) {
                    if (!(spr & 1) && (s_.sprctl[spr + 1] & 0x80)) {
                        // Attached sprite
                        const uint8_t idx = sprite_state_[spr].idx | sprite_state_[spr + 1].idx << 2;
                        if (idx) {
                            active_sprite = 16 + idx;
                            active_sprite_group = spr >> 1;
                            break;
                        }
                        ++spr; // Skip next sprite
                    } else if (sprite_state_[spr].idx) {
                        active_sprite = 16 + (spr >> 1) * 4 + sprite_state_[spr].idx;
                        active_sprite_group = spr >> 1;
                        break;
                    }
                }
            }

            if (s_.bplcon0 & BPLCON0F_HIRES) {
                row[0] = one_pixel_(s_, active_sprite, active_sprite_group, ((pixel >> 4) & 8) | ((pixel >> 3) & 4) | ((pixel >> 2) & 2) | ((pixel >> 1) & 1), col32_);
                row[1] = one_pixel_(s_, active_sprite, active_sprite_group, ((pixel >> 3) & 8) | ((pixel >> 2) & 4) | ((pixel >> 1) & 2) | (pixel & 1), col32_);
            } else {
                row[0] = row[1] = one_pixel_(s_, active_sprite, active_sprite_group, pixel, col32_);
            }
        } else {
            row[0] = row[1] = col32_[0];

            if (DEBUG_BPL && s_.bpl1dat_written_this_line) {
                rem_pixels_odd_--;
                rem_pixels_even_--;
            }
        }
    }

    void scandouble()
    {
        const uint32_t* src = &gfx_buf_[(!s_.long_frame) * graphics_width];
        uint32_t* dst = &gfx_buf_[s_.long_frame * graphics_width];
        for (uint32_t y = 0; y < graphics_height; y += 2) {
            #if 0
            // Normal scan doubling
            memcpy(dst, src, graphics_width * sizeof(uint32_t));
            #elif 1
            // Blend/bleed (shift > 1 = bleed)
            const uint32_t* src2 = s_.long_frame ? &src[y + 2 < graphics_height ? 2 * graphics_width : 0] : &src[y > 2 ? static_cast<int>(graphics_width)*-2 : 0];
            constexpr uint8_t shift = 1;
            constexpr uint8_t mask8 = 0xff - ((1 << shift) - 1);
            constexpr uint32_t mask24 = mask8 << 16 | mask8 << 8 | mask8;
            for (uint32_t x = 0; x < graphics_width; ++x) {
                dst[x] = ((src[x] & mask24) + (src2[x] & mask24)) >> shift;
            }
            #else
            // No scandoubling
            #endif
            src += 2 * graphics_width;
            dst += 2 * graphics_width;
        }
    }

    step_result step(bool cpu_wants_access, uint32_t current_pc)
    {
        // Step frequency: Base CPU frequency (7.09 for PAL) => 1 lores virtual pixel / 2 hires pixels

        uint16_t display_hpos = s_.hpos;
        uint16_t display_vpos = s_.vpos;
        if (display_vpos > vblank_end_vpos && display_hpos < disp_extra_hpos) {
            --display_vpos;
            display_hpos += hpos_per_line;
        }

        const bool vert_disp = display_vpos >= s_.diwstrt >> 8 && display_vpos < ((~s_.diwstop & 0x8000) >> 7 | s_.diwstop >> 8); // VSTOP MSB is complemented and used as the 9th bit
        const uint16_t colclock = s_.hpos >> 1;

        step_result res{};
        res.frame = gfx_buf_;
        res.audio = audio_buf_;
        res.bus = bus_use::none;

        current_pc_ = current_pc;

        const bool cck_tick = !(s_.hpos & 1);

        do_sprites(display_hpos);

        if (cck_tick && s_.bpl1dat_written) {
            if (DEBUG_BPL) {
                DBGOUT << "Making BPL data available";
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

        // Shift out pixel data here (but don't draw until after the copper has had a chance to update the color register)
        uint8_t pixel_temp = 0;
        if (s_.bplcon0 & BPLCON0F_HIRES) {
            for (int i = 4; i--;) {
                pixel_temp = pixel_temp << 2 | s_.bpldat_shift[i] >> 14;
                s_.bpldat_shift[i] <<= 2;
            }
        } else {
            for (int i = 6; i--;) {
                pixel_temp = pixel_temp << 1 | s_.bpldat_shift[i] >> 15;
                s_.bpldat_shift[i] <<= 1;
            }
        }

        if (s_.bpldata_avail) {
            const uint8_t mask = s_.bplcon0 & BPLCON0F_HIRES ? 7 : 15;
            const uint8_t delay1 = s_.bplcon1_denise & mask;
            const uint8_t delay2 = (s_.bplcon1_denise >> 4) & mask;
            // http://eab.abime.net/showthread.php?t=108873 only active planes are copied
            const uint8_t nbpls = std::min<uint8_t>(6, (s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0);

            if ((s_.bpldata_avail & 1) && delay1 == (s_.hpos & mask)) {
                for (int i = 0; i < nbpls; i += 2)
                    s_.bpldat_shift[i] = s_.bpldat_temp[i];
                s_.bpldata_avail &= ~1;

                if (DEBUG_BPL) {
                    DBGOUT << "Loaded odd pixels (delay=$" << hexfmt(delay1, 1) << ")";
                    if (rem_pixels_odd_ > 0)
                        *debug_stream << " Warning discarded " << rem_pixels_odd_;
                    *debug_stream << "\n";
                    rem_pixels_odd_ = 16;
                }
            }
            if ((s_.bpldata_avail & 2) && delay2 == (s_.hpos & mask)) {
                for (int i = 1; i < nbpls; i += 2)
                    s_.bpldat_shift[i] = s_.bpldat_temp[i];
                s_.bpldata_avail &= ~2;

                if (DEBUG_BPL) {
                    DBGOUT << "Loaded even pixels (delay=$" << hexfmt(delay2, 1) << ")";
                    if (rem_pixels_even_ > 0)
                        *debug_stream << " Warning discarded " << rem_pixels_even_;
                    *debug_stream << "\n";
                    rem_pixels_even_ = 16;
                }
            }
        }
        // Seems like there's a slight delay before updates to BPL1CON relating to bitplane delays take effect
        // Ex. desert dream rotating logo cube, Brian The Lion start menu and vAmigaTS DENISE/BPLCON1/Timing4
        if (DEBUG_BPL && s_.bplcon1_denise != s_.bplcon1)
            DBGOUT << "DDFSTRT=$" << hexfmt(s_.ddfstrt) << " BPLCON1 update to " << hexfmt(s_.bplcon1) << " taking effect (previous: $" << hexfmt(s_.bplcon1_denise) << ")\n";
        s_.bplcon1_denise = s_.bplcon1;

        if (cck_tick) {
            do_audio();

            if (cpu_wants_access && !(s_.dmacon & DMAF_BLITHOG) && s_.bltblockingcpu < 3)
                ++s_.bltblockingcpu;
        }

        bool blitter_had_chance_to_run = false;
        if (cck_tick && (s_.dmacon & DMAF_MASTER)) {
            auto do_dma = [this](uint32_t& pt) {
                const auto val = chip_read(pt);
                pt += 2;
                return val;
            };

            dma_addr_ = 0;
            dma_val_ = 0;

            do {
                // Refresh
                if (colclock == 0xE2 || colclock == 1 || colclock == 3 || colclock == 5) {
                    res.bus = bus_use::refresh;
                    break;
                }

                // Disk
                if ((colclock == 7 || colclock == 9 || colclock == 11) && (s_.dmacon & DMAF_DISK) && (s_.dsklen & 0x8000) && s_.dsklen_act) {
                    if (do_disk_dma()) {
                        res.bus = bus_use::disk;
                        break;
                    }
                }

                // Audio
                if (colclock == 13 && audio_dma(0)) {
                    res.bus = bus_use::audio;
                    break;
                }
                if (colclock == 15 && audio_dma(1)) {
                    res.bus = bus_use::audio;
                    break;
                }
                if (colclock == 17 && audio_dma(2)) {
                    res.bus = bus_use::audio;
                    break;
                }
                if (colclock == 19 && audio_dma(3)) {
                    res.bus = bus_use::audio;
                    break;
                }

                // Display
                const bool bpl_dma_active = (s_.dmacon & DMAF_RASTER) && vert_disp && (s_.bplcon0 & BPLCON0F_BPU);
                static int num_bpl1_writes = 0;

                if (bpl_dma_active) {
                    if (s_.ddfst == ddfstate::before_ddfstrt && colclock == std::max<uint16_t>(0x18, s_.ddfstrt)) {
                        if (DEBUG_BPL) {
                            DBGOUT << "DDFSTRT=$" << hexfmt(s_.ddfstrt) << " passed\n";
                            num_bpl1_writes = 0;
                        }
                        s_.ddfst = ddfstate::active;
                        s_.ddfcycle = 0;
                        s_.ddfend = 0;
                    } else if (s_.ddfst == ddfstate::active && colclock == std::min<uint16_t>(0xD8, s_.ddfstop)) {
                        if (DEBUG_BPL)
                            DBGOUT << "DDFSTOP=$" << hexfmt(s_.ddfstop) << " passed\n";
                        // Need to do one final 8-cycles DMA fetch
                        s_.ddfst = ddfstate::ddfstop_passed;
                    } else if (s_.ddfst == ddfstate::ddfstop_passed && s_.ddfcycle == s_.ddfend) {
                        if (DEBUG_BPL)
                            DBGOUT << "BPL DMA done (fetch cycle $" << hexfmt(s_.ddfcycle) << ", bpl1 writes: " << num_bpl1_writes << ")\n";
                        s_.ddfst = ddfstate::stopped;
                    }
                }

                if (bpl_dma_active && (s_.ddfst == ddfstate::active || s_.ddfst == ddfstate::ddfstop_passed)) {
                    constexpr uint8_t lores_bpl_sched[8] = { 0, 4, 6, 2, 0, 3, 5, 1 };
                    constexpr uint8_t hires_bpl_sched[8] = { 4, 3, 2, 1, 4, 3, 2, 1 };
                    const int bpl = (s_.bplcon0 & BPLCON0F_HIRES ? hires_bpl_sched : lores_bpl_sched)[s_.ddfcycle & 7] - 1;
                    if (s_.ddfst == ddfstate::ddfstop_passed && !s_.ddfend && (s_.ddfcycle & 7) == 0) {
                        if (DEBUG_BPL)
                            DBGOUT << "Doing final DMA cycles (fetch cycle $" << hexfmt(s_.ddfcycle) << ", bpl1 writes: " << num_bpl1_writes << ")\n";
                        s_.ddfend = s_.ddfcycle + 8;
                    }
                    ++s_.ddfcycle;

                    // OCS/ECS only "7 bitplane" effect (4 dma channels used for DMA, 6 for display)
                    int nbpls = ((s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0);
                    if (nbpls == 7)
                        nbpls = 4;

                    if (bpl >= 0 && bpl < nbpls) {
                        if (DEBUG_BPL)
                            DBGOUT << "BPL " << bpl << " DMA (fetch cycle $" << hexfmt(s_.ddfcycle) << ")\n";
                        s_.bpldat[bpl] = do_dma(s_.bplpt[bpl]);
                        if (bpl == 0) {
                            if (DEBUG_BPL) {
                                DBGOUT << "Data available -- bpldat1_written = " << s_.bpl1dat_written << " bpldata_avail = " << hexfmt(s_.bpldata_avail) << (s_.bpl1dat_written ? " Warning!" : "") << "\n";
                                ++num_bpl1_writes;
                            }

                            s_.bpl1dat_written = true;
                            s_.bpl1dat_written_this_line = true;
                        }

                        if (s_.ddfend && (!(s_.bplcon0 & BPLCON0F_HIRES) || ((s_.ddfcycle-1) & 7) > 3)) {
                            // Final DMA cycle adds bitplane modulo
                            if (DEBUG_BPL)
                                DBGOUT << "Data available -- Adding modulo for bpl=" << (int)bpl << ": " << (bpl & 1 ? s_.bplmod2 : s_.bplmod1) << "\n";
                            s_.bplpt[bpl] += bpl & 1 ? s_.bplmod2 : s_.bplmod1;
                        }

                        res.bus = bus_use::bitplane;
                        break;
                    }
                }

                // Sprite
                if (s_.vpos >= sprite_dma_start_vpos && (colclock & 1) && colclock >= 0x15 && colclock < 0x15+8*4) {
                    const uint8_t spr = static_cast<uint8_t>((colclock - 0x15) / 4);
                    const bool fetch_ctl = s_.vpos == sprite_dma_start_vpos || s_.vpos == sprite_state_[spr].vend;

                    if ((s_.dmacon & DMAF_SPRITE) && (fetch_ctl || (s_.spr_dma_active_mask & (1 << spr)))) {
                        const bool first_word = !(colclock & 2);
                        const uint16_t reg = SPR0POS + 8 * spr + 2 * (fetch_ctl ? 1 - first_word : 3 - first_word);
                        const uint16_t val = do_dma(s_.sprpt[spr]);
                        if (DEBUG_SPRITE)
                            DBGOUT << "Sprite " << (int)spr << " DMA state=" << (int)((s_.spr_dma_active_mask >> spr) & 1) << " first_word=" << first_word << " writing $" << hexfmt(val) << " to " << custom_regname(reg) << "\n";
                        write_u16(0xdff000 | reg, reg, val);
                        res.bus = bus_use::sprite;
                        s_.spr_active_mask |= spr_active_check_mask;
                        break;
                    }
                }

                // Copper (uses only odd-numbered cycles)
                if (s_.dmacon & DMAF_COPPER) {
                    // $E0 not usable by copper, but $E1 is???
                    // http://eab.abime.net/showpost.php?p=600609&postcount=47
                    // But seems like it gets allocated anyway?
                    if ((!(colclock & 1) && colclock != 0xe0) || colclock == 0xe1) {
                        if (do_copper()) {
                            res.bus = bus_use::copper;
                            break;
                        }
                    }
                }
                
                // Blitter
                blitter_had_chance_to_run = true;
                if (do_blitter()) {
                    res.bus = bus_use::blitter;
                    break;
                }

                res.free_chip_cycle = true;
                s_.bltblockingcpu = 0;
            } while (0);

            assert(res.free_chip_cycle == (res.bus == bus_use::none));
            res.dma_addr = dma_addr_;
            res.dma_val = dma_val_;

        } else if (cck_tick) {
            res.free_chip_cycle = true;
            s_.bltblockingcpu = 0;
        }

        // Output pixels after copper had a chance to update color registers
        if (display_vpos >= vblank_end_vpos && display_vpos != vpos_per_field-1)
            do_pixels(vert_disp, display_vpos, display_hpos, pixel_temp);

        if (cck_tick && (s_.bplmod1_countdown | s_.bplmod2_countdown)) {
            if (s_.bplmod1_countdown && --s_.bplmod1_countdown == 0) {
                if (DEBUG_BPL)
                    DBGOUT << "BPLMOD1 change taking effect new=$" << hexfmt(s_.bplmod1_pending) << " old=$" << hexfmt(s_.bplmod1) << "\n";
                s_.bplmod1 = s_.bplmod1_pending;
            }
            if (s_.bplmod2_countdown && --s_.bplmod2_countdown == 0) {
                if (DEBUG_BPL)
                    DBGOUT << "BPLMOD2 change taking effect new=$" << hexfmt(s_.bplmod2_pending) << " old=$" << hexfmt(s_.bplmod2) << "\n";
                s_.bplmod2 = s_.bplmod2_pending;
            }
        }

        if (s_.copstate == copper_state::jmp_delay1 && res.bus != bus_use::copper && !(s_.hpos & 3)) {
            // vAmigaTS jump1b test
            // http://eab.abime.net/showpost.php?p=832397&postcount=118
            if (DEBUG_COPPER)
                DBGOUT << "jmp_delay1 done: Advancing even though cycles isn't available\n";
            s_.copstate = copper_state::jmp_delay2;
        }

        // Check for blitter delays/idle cycles that don't need the bus
        if (!blitter_had_chance_to_run) {
            check_blit_idle_any_cycle();
        }

        // Delayed interrupts
        for (int i = 0; i < static_cast<int>(sizeof(s_.int_delay) / sizeof(*s_.int_delay)); ++i) {
            if (s_.int_delay[i] && --s_.int_delay[i] == 0) {
                if (debug_flags)
                    DBGOUT << "Triggering interrupt " << i << "\n";
                s_.intreq |= 1 << i;
            }
        }

        // Hack: IPL change delay
        // TODO: Debug info...
        const auto ipl = calc_ipl();
        if (s_.ipl_delay) {
            if (--s_.ipl_delay == 0)
                s_.ipl_current = s_.ipl_pending;
        } else if (ipl != s_.ipl_current) {
            s_.ipl_pending = ipl;
            s_.ipl_delay = 2;
        }

        // Capture before incrementing
        res.vpos = s_.vpos;
        res.hpos = s_.hpos;
        res.eclock_cycle = s_.eclock_cycle;

        // CIA tick rate (EClock) is 1/10th of (base) CPU speed = 1/5th of CCK (to keep in sync with DMA)
        if (++s_.eclock_cycle == 10) {
            cia_.step();
            const auto irq_mask = cia_.active_irq_mask();
            constexpr uint8_t cia_int_delay = 16; // XXX: FIXME: Need correct number
            if ((irq_mask & 1) && !(s_.intreq & INTF_PORTS))
                interrupt_with_delay(INTB_PORTS, cia_int_delay);
            if ((irq_mask & 2) && !(s_.intreq & INTF_EXTER))
                interrupt_with_delay(INTB_EXTER, cia_int_delay);
            s_.eclock_cycle = 0;
        }

        if (++s_.hpos == hpos_per_line) {
            s_.hpos = 0;
            s_.ddfst = ddfstate::before_ddfstrt;

            cia_.increment_tod_counter(1);
            if (++s_.vpos == vpos_per_field) {
                s_.spr_dma_active_mask = 0;
                s_.spr_active_mask = 0;
                //Note: sprite armed flag is not reset here (vAmigaTS manual1)
                s_.copstate = copper_state::vblank;
                s_.vpos = 0;
                if (s_.bplcon0 & BPLCON0F_LACE)
                    s_.long_frame = !s_.long_frame;
                else if (s_.last_long_frame == s_.long_frame)
                    scandouble();
                s_.last_long_frame = s_.long_frame;
                // XXX: FIXME: Shouldn't be done here
                s_.intreq |= INTF_VERTB;
                cia_.increment_tod_counter(0);
            }
        }
        if (s_.hpos == disp_extra_hpos) {
            // Don't clear display state until after "display end of line" passed
            //memset(s_.bpldat_shift, 0, sizeof(s_.bpldat_shift));
            //memset(s_.bpldat_temp, 0, sizeof(s_.bpldat_temp));
            //s_.bpldata_avail = 0;
            s_.bpl1dat_written = false;
            s_.bpl1dat_written_this_line = false;
            rem_pixels_odd_ = rem_pixels_even_ = 0;
            s_.ham_color = col32_[0];
            //memset(s_.spr_hold_cnt, 0, sizeof(s_.spr_hold_cnt));
        }

        return res;
    }

    void interrupt_with_delay(uint8_t index, uint8_t delay)
    {
        assert(index < sizeof(s_.int_delay)/sizeof(*s_.int_delay));
        if (s_.int_delay[index])
            return;
        if (debug_flags)
            DBGOUT << "Interrupt with delay " << static_cast<int>(index) << " delay " << static_cast<int>(delay) << "\n";
        s_.int_delay[index] = delay;
    }

    uint8_t read_u8(uint32_t addr, uint32_t offset) override
    {
        const auto v = read_u16(addr & ~1, offset & ~1);
        return (offset & 1 ? v : v >> 8) & 0xff;
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        offset &= 0xffe;

        // V(H)POSR is shifted compared to the actual value (this is needed for the winuae cputest cycle accuracy tests)
        // WinUAE uses a shift of 3, but this doesn't work with vAmigaTS tests (e.g. copbpl1) where an unstable image is created when trying to sync horizonatally
        // This is probably due to when the value is "sample" (actually read) in the interaction between the CPU and custom chip stepping
        constexpr uint16_t hpos_cycle_shift = 2; //3;
        constexpr uint16_t hpos_max = (hpos_per_line >> 1);

        switch (offset) {
        case DMACONR: // $002
            return s_.dmacon;
        case VPOSR: { // $004
            // Hack: Just return a fixed V/HPOS when external sync is enabled (needed for KS1.2)
            if (s_.bplcon0 & BPLCON0F_ERSY)
                return 0x8040;

            auto v = s_.vpos;
            auto h = (s_.hpos >> 1) & 0xff;
            if (h + hpos_cycle_shift >= hpos_max + 1) {
                if (++v >= vpos_per_field)
                    v = 0;
            }

            return (s_.long_frame ? 0x8000 : 0) | ((v >> 8) & 1);
        }
        case VHPOSR: { // $006
            // Hack: Just return a fixed V/HPOS when external sync is enabled (needed for KS1.2)
            if (s_.bplcon0 & BPLCON0F_ERSY)
                return 0x80;
            auto v = s_.vpos;
            auto h = (s_.hpos >> 1) & 0xff;
            h += hpos_cycle_shift;
            if (h >= hpos_max) {
                h -= hpos_max;
                // VPOS changes at shifted hpos=1 NOT 0
                if (h >= 1)
                    if (++v >= vpos_per_field)
                        v = 0;
            }
            // cputest cycle exact requires this
            h = (h + 1) % hpos_max;
            return static_cast<uint16_t>((v & 0xff) << 8 | h);
        }
        case DSKDATR: // $008
            break;
        case JOY0DAT: // $00A
            return s_.cur_mouse_y << 8 | s_.cur_mouse_x;
        case JOY1DAT: // $00C
            return s_.joydat;
        case CLXDAT:  // $00E
            break;
        case ADKCONR: // $010
            return s_.adkcon;
        case POT0DAT: // $012
            break;
        case POT1DAT: // $014
            break;
        case POTGOR:  // $016
            return 0xFF00 & ~(s_.rmb_pressed[0] ? 0x400 : 0) & ~(s_.rmb_pressed[1] ? 0x4000 : 0);
        case SERDATR: // $018
            return (3<<12); // Just return transmit buffer empty
        case DSKBYTR: { // $01A
            // Not properly emulated yet, but whdload doesn't like $ffff always being returned
            uint16_t val = s_.dskbyt;
            if ((s_.dmacon & DMAF_DISK) && (s_.dsklen & 0x8000) && s_.dsklen_act)
                val |= 1 << 14;
            s_.dskbyt &= ~(1 << 15); // clear DSKBYT
            return val;
        }
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
        default:
            // Don't warn out w/o registers to avoid spam when CLR.W/CLR.L is used
            return 0xffff;
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
        offset &= 0xffe;

        auto write_partial = [offset, &val](uint32_t& r) {
            if (offset & 2) {
                //assert(!(val & 1)); // Blitter pointers (and modulo) ignore bit0
                r = (r & 0xffff0000) | (val & 0xfffe);
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
        if (offset >= AUD0LCH && offset <= AUD3DAT) {
            const int idx = (offset - AUD0LCH) / 16;
            auto& ch = s_.audio_channels[idx];
            if (DEBUG_AUDIO)
                DBGOUT << "Audio write to " << custom_regname(offset) << " val $" << hexfmt(val) << "\n";
            switch ((offset & 0xf) >> 1) {
            case 0: // LCH
                ch.lc = val << 16 | (ch.lc & 0xffff);
                return;
            case 1: // LCL
                ch.lc = (ch.lc & 0xffff0000) | (val & 0xfffe);
                return;
            case 2: // LEN
                ch.len = val;
                return;
            case 3: // PER
                ch.per = val;
                return;
            case 4: // VOL
                ch.vol = val & 0x7f;
                if (ch.vol > 64)
                    ch.vol = 64;
                return;
            case 5: // DAT
                DBGOUT << "Audio unspported write to " << custom_regname(offset) << " val $" << hexfmt(val) << "\n";
                ch.dat = val;
                // The below isn't enough (should actually play the sample, etc.) but gets us past the first requester in AIBB
                if (!(s_.dmacon & (1 << (DMAB_AUD0 + idx))))
                    s_.intreq |= 1 << (INTB_AUD0 + idx);
                return;
            }
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
            if (DEBUG_SPRITE)
                DBGOUT << "Write to " << custom_regname(offset) << " value $" << hexfmt(val) << " dma=" << (int)((s_.spr_dma_active_mask >> spr) & 1) << " armed=" << (int)((s_.spr_armed_mask >> spr) & 1) << "\n";
            switch ((offset >> 1) & 3) {
            case 0: // SPRxPOS
                s_.sprpos[spr] = val;
                break;
            case 1: // SPRxCTL
                if (DEBUG_SPRITE && !((s_.spr_armed_mask >> spr) & 1))
                    DBGOUT << "Sprite " << (int)spr << " Disarming\n";
                s_.sprctl[spr] = val;
                s_.spr_armed_mask &= ~(1 << spr);
                break;
            case 2: // SPRxDATA (low word)
                if (DEBUG_SPRITE && !((s_.spr_armed_mask >> spr) & 1))
                    DBGOUT << "Sprite " << (int)spr << " Arming\n";
                s_.sprdata[spr] = val;
                s_.spr_armed_mask |= 1 << spr;
                return;
            case 3: // SPRxDATB (high word)
                s_.sprdatb[spr] = val;
                return;
            }

            auto& ss = sprite_state_[spr];
            ss.recalc(s_.sprpos[spr], s_.sprctl[spr]);
            if (DEBUG_SPRITE)
                DBGOUT << "Sprite " << (int)spr << " vstart=$" << hexfmt(ss.vstart) << " vend=$" << hexfmt(ss.vend) << " hstart=$" << hexfmt(ss.hpos) << " dma=" << (int)((s_.spr_dma_active_mask >> spr) & 1) << "\n";

            return;
        }

        if (offset >= COLOR00 && offset <= COLOR31) {
            const auto idx = (offset - COLOR00) / 2;
            s_.color[idx] = val;
            col32_[idx] = rgb4_to_8(val);
            return;
        }

        // Warn if blitter registers are accessed when not idle
        if (offset >= BLTCON0 && offset <= BLTADAT && s_.blitstate != custom_state::blit_stopped) {
            // Avoid spamming in demos (expect for BLTSIZE) unless blitter debugging is active
            if (DEBUG_BLITTER || offset == BLTSIZE)
                DBGOUT << "Warning: Blitter register access (" << custom_regname(offset) << " val $" << hexfmt(val) << ") while busy. bltx=$" << hexfmt(s_.bltx) << " blth=$" << hexfmt(s_.blth) << " busy=" << !!(s_.dmacon & DMAF_BLTBUSY) << " state=" << (int)s_.blitstate << "\n";
        }

        switch (offset) {
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
            s_.dskpos = 0;
            s_.dsksync_passed = false;
            s_.dskread = false;
            if (DEBUG_DISK)
                DBGOUT << "Write to DSKLEN: $" << hexfmt(val) << (s_.dsklen_act && (s_.dsklen_act & 0x8000) ? " - Active!" : "") << "\n";
            return;
        case VPOSW:  // $02A
            // Only support clearing/setting LOF (3d demo II)
            if (DEBUG_BPL)
                DBGOUT << "Write to VPOSW $" << hexfmt(val) << "\n";
            s_.long_frame = !!(val & 0x8000);
            return;
        case VHPOSW: // $02C
            // Ignore: (Dynablaster)
            if (DEBUG_BPL)
                DBGOUT << "Write to VHPOSW $" << hexfmt(val) << "\n";
            return;
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
        case BLTCON0:
            s_.bltcon0 = val;
            s_.blitline_ashift = s_.bltcon0 >> BC0_ASHIFTSHIFT;
            return;
        case BLTCON1:
            s_.bltcon1 = val;
            s_.blitline_sign = !!(s_.bltcon1 & BC1F_SIGNFLAG);
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
            blitstart(val);
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
        case DSKSYNC: // $07E
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
            return;
        case INTREQ:  // $09C
            if (DEBUG_BLITTER && (val & INTF_BLIT))
                DBGOUT << (val & INTF_SETCLR ? "Setting" : "Clearing") << " INTF_BLIT in INTREQ (val=$" << hexfmt(val) << ")\n";
            val &= ~INTF_INTEN;
            setclr(s_.intreq, val);
            return;
        case ADKCON:  // $09E
            setclr(s_.adkcon, val);
            if (DEBUG_DISK || DEBUG_AUDIO)
                DBGOUT << "Write to ADKCON val=$" << hexfmt(val) << " adkcon=$" << hexfmt(s_.adkcon) << "\n";
            return;
        case BPLCON0: // $100
            s_.bplcon0 = val;
            one_pixel_ = one_pixel_funcs[(s_.bplcon0 >> 10) & 31];
            return;
        case BPLCON1: // $102
            s_.bplcon1 = val;
            return;
        case BPLCON2: // $104
            s_.bplcon2 = val;
            return;
        case BPLMOD1: // $108
            val &= 0xfffe;
            // BPLxMOD changes (by copper atleast) only take effect 2 CCKs later
            // vAmigaTS Agnus/BPLMOD/bplmod1-3
            if (val != s_.bplmod1 || val != s_.bplmod1_pending) {
                if (s_.bplmod1_countdown)
                    DBGOUT << "Untested: Countdown already in progress while changing BPL1MOD\n";
                s_.bplmod1_pending = val;
                s_.bplmod1_countdown = 2;
            }
            return;
        case BPLMOD2: // $10A
            val &= 0xfffe;
            // See comment for BPL1MOD
            if (val != s_.bplmod2 || val != s_.bplmod2_pending) {
                if (s_.bplmod2_countdown)
                    DBGOUT << "Untested: Countdown already in progress while changing BPL2MOD\n";
                s_.bplmod2_pending = val;
                s_.bplmod2_countdown = 2;
            }
            return;

        //
        // Ignored registers
        //

        // Don't spam if RMW instructions used for R/O registers
        case BLTDDAT:  // $000
        case DMACONR:  // $002
        case VPOSR:    // $004
        case VHPOSR:   // $006
        case INTREQR:  // $01E

        // These should be implemented at some point
        case SERPER:   // $032
        case POTGO:    // $034
        case JOYTEST:  // $036

        case BLTCON0L: // $05A (avoid spamming warnings in Cryptoburners - The Hunt for 7th October)
        case 0x068:    // ??
        case 0x076:    // ?
        case 0x078:    // SPRHDAT (AGA)
        case 0x07A:    // BPLHDAT (AGA)
        case 0x0F8:    // BPL7PTH
        case 0x0FA:    // BPL7PTL
        case 0x0FC:    // BPL8PTH
        case 0x0FE:    // BPL8PTL
        case BPLCON3:  // $106
        case BPLCON4:  // $10C
        case 0x11C:    // BPL7DAT
        case 0x11E:    // BPL8DAT
        case 0x1C0:    // HTOTAL (ignored unless VARBEAMEN=1)
        case 0x1C8:    // VTOTAL
        case 0x1CC:    // VBSTRT
        case 0x1CE:    // VBSTOP
        case BEAMCON0: // $1DC
        case DIWHIGH:  // $1E4
        case FMODE:    // $1FC
        case 0x1fe:    // NO-OP
            if (debug_flags) // Don't ignore if debugging is enabled
                break;
            return;
        }

        static uint8_t warned[0x100];
        if (!debug_flags && offset < 0x200) {
            if (warned[offset >> 1] == 0xff)
                return;
            if (++warned[offset >> 1] == 0xff)
                std::cerr << "Disabling warnings for writes to " << custom_regname(offset) << "\n";
        }
        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << custom_regname(offset) << ")"
                  << " val $" << hexfmt(val) << "\n";
    }

    uint8_t current_ipl() const
    {
        return s_.ipl_current;
    }

    uint8_t calc_ipl() const
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

    std::vector<uint16_t> get_regs()
    {
        std::vector<uint16_t> regs(0x100);
        for (uint16_t i = 0; i < 0x100; ++i)
            regs[i] = internal_read(i * 2);
        return regs;
    }

private:
    memory_handler& mem_;
    cia_handler& cia_;
    serial_data_handler serial_data_handler_;

    uint32_t gfx_buf_[graphics_width * graphics_height];
    int16_t audio_buf_[audio_buffer_size];
    custom_state s_;
    uint32_t chip_ram_mask_;
    uint32_t current_pc_; // For debug output
    uint32_t floppy_speed_;
    decltype(&one_pixel<0>) one_pixel_;
    uint32_t col32_[32];
    struct sprite_state {
        uint8_t idx;
        uint16_t hpos;
        uint16_t vstart;
        uint16_t vend;
        void recalc(uint16_t pos, uint16_t ctl)
        {
            hpos = ((pos & 0xff) << 1) | (ctl & 1);
            vstart = (pos >> 8) | (ctl & 4) << 6;
            vend = (ctl >> 8) | (ctl & 2) << 7;
        }
    } sprite_state_[8];
    // bitplane debugging (don't need to be saved)
    int rem_pixels_odd_;
    int rem_pixels_even_;
    // dma usage tracking (don't need to be saved)
    uint32_t dma_addr_ = 0;
    uint16_t dma_val_ = 0;

    uint16_t chip_read(uint32_t addr)
    {
        addr &= chip_ram_mask_;
        dma_addr_ = addr;
        dma_val_ = mem_.read_u16(addr);
        return dma_val_;
    }

    void chip_write(uint32_t addr, uint16_t val)
    {
        addr &= chip_ram_mask_;
        dma_addr_ = addr;
        dma_val_ = val;
        mem_.write_u16(addr, val);
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
    case DSKBYTR: {
        const auto val = read_u16(0xdff000 + DSKBYTR, DSKBYTR);
        // HACK: Restore DSKBYT bit
        if (val & 0x8000)
            s_.dskbyt |= 0x8000;
        return val;
    }
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
    case AUD0LCH:
    case AUD0LCL:
    case AUD0LEN:
    case AUD0PER:
    case AUD0VOL:
    case AUD0DAT:
    case AUD1LCH:
    case AUD1LCL:
    case AUD1LEN:
    case AUD1PER:
    case AUD1VOL:
    case AUD1DAT:
    case AUD2LCH:
    case AUD2LCL:
    case AUD2LEN:
    case AUD2PER:
    case AUD2VOL:
    case AUD2DAT:
    case AUD3LCH:
    case AUD3LCL:
    case AUD3LEN:
    case AUD3PER:
    case AUD3VOL: {
        auto& ch = s_.audio_channels[(reg - AUD0LCH) / 16];
        switch ((reg & 0xf) >> 1) {
        case 0:
            return hi(ch.lc);
        case 1:
            return lo(ch.lc);
        case 2:
            return ch.len;
        case 3:
            return ch.per;
        case 4:
            return ch.vol;
        case 5:
            return ch.dat;
        }
        assert(0);
        return 0;
    }
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
        assert(0);
        return 0;
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

custom_handler::custom_handler(memory_handler& mem_handler, cia_handler& cia, uint32_t slow_end, uint32_t floppy_speed)
    : impl_ { std::make_unique<impl>(mem_handler, cia, slow_end, floppy_speed) }
{
}

custom_handler::~custom_handler() = default;

custom_handler::step_result custom_handler::step(bool cpu_wants_access, uint32_t current_pc)
{
    return impl_->step(cpu_wants_access, current_pc);
}

uint8_t custom_handler::current_ipl()
{
    return impl_->current_ipl();
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

void custom_handler::set_joystate(uint16_t dat, bool button_state)
{
    impl_->set_joystate(dat, button_state);
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

std::vector<uint16_t> custom_handler::get_regs()
{
    return impl_->get_regs();
}

void custom_handler::handle_state(state_file& sf)
{
    impl_->handle_state(sf);
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
