#include "custom.h"
#include "ioutil.h"
#include "cia.h"

#include <iostream>
#include <cassert>
#include <utility>

#define TODO_ASSERT(expr) do { if (!(expr)) throw std::runtime_error{("TODO: " #expr " in ") + std::string{__FILE__} + " line " + std::to_string(__LINE__) }; } while (0)

//#define COPPER_DEBUG

    // Name, Offset, R(=0)/W(=1)
#define CUSTOM_REGS(X) \
    X(BLTDDAT , 0x000 , 0) /* Blitter destination early read (dummy address)          */ \
    X(DMACONR , 0x002 , 0) /* DMA control (and blitter status) read                   */ \
    X(VPOSR   , 0x004 , 0) /* Read vert most signif. bit (and frame flop)             */ \
    X(VHPOSR  , 0x006 , 0) /* Read vert and horiz. position of beam                   */ \
    X(JOY0DAT , 0x00A , 0) /* Joystick-mouse 0 data (vert,horiz)                      */ \
    X(JOY1DAT , 0x00C , 0) /* Joystick-mouse 1 data (vert,horiz)                      */ \
    X(POTGOR  , 0x016 , 0) /* Pot port data read(formerly POTINP)                     */ \
    X(SERDATR , 0x018 , 0) /* Serial port data and status read                        */ \
    X(INTENAR , 0x01C , 0) /* Interrupt enable bits read                              */ \
    X(INTREQR , 0x01E , 0) /* Interrupt request bits read                             */ \
    X(SERDAT  , 0x030 , 1) /*                                                         */ \
    X(SERPER  , 0x032 , 1) /* Serial port period and control                          */ \
    X(POTGO   , 0x034 , 1) /* Pot port data write and start                           */ \
    X(BLTCON0 , 0x040 , 1) /* Blitter control register 0                              */ \
    X(BLTCON1 , 0x042 , 1) /* Blitter control register 1                              */ \
    X(BLTAFWM , 0x044 , 1) /* Blitter first word mask for source A                    */ \
    X(BLTALWM , 0x046 , 1) /* Blitter last word mask for source A                     */ \
    X(BLTCPTH , 0x048 , 1) /* Blitter pointer to source C (high 3 bits)               */ \
    X(BLTCPTL , 0x04A , 1) /* Blitter pointer to source C (low 15 bits)               */ \
    X(BLTBPTH , 0x04C , 1) /* Blitter pointer to source B (high 3 bits)               */ \
    X(BLTBPTL , 0x04E , 1) /* Blitter pointer to source B (low 15 bits)               */ \
    X(BLTAPTH , 0x050 , 1) /* Blitter pointer to source A (high 3 bits)               */ \
    X(BLTAPTL , 0x052 , 1) /* Blitter pointer to source A (low 15 bits)               */ \
    X(BLTDPTH , 0x054 , 1) /* Blitter pointer to destination D (high 3 bits)          */ \
    X(BLTDPTL , 0x056 , 1) /* Blitter pointer to destination D (low 15 bits)          */ \
    X(BLTSIZE , 0x058 , 1) /* Blitter start and size (window width,height)            */ \
    X(BLTCON0L, 0x05A , 1) /* Blitter control 0, lower 8 bits (minterms)              */ \
    X(BLTSIZV , 0x05C , 1) /* Blitter V size (for 15 bit vertical size)               */ \
    X(BLTSIZH , 0x05E , 1) /* Blitter H size and start (for 11 bit H size)            */ \
    X(BLTCMOD , 0x060 , 1) /* Blitter modulo for source C                             */ \
    X(BLTBMOD , 0x062 , 1) /* Blitter modulo for source B                             */ \
    X(BLTAMOD , 0x064 , 1) /* Blitter modulo for source A                             */ \
    X(BLTDMOD , 0x066 , 1) /* Blitter modulo for destination D                        */ \
    X(BLTCDAT , 0x070 , 1) /* Blitter source C data register                          */ \
    X(BLTBDAT , 0x072 , 1) /* Blitter source B data register                          */ \
    X(BLTADAT , 0x074 , 1) /* Blitter source A data register                          */ \
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
    X(SPR0PTH , 0x120 , 1) /* Sprite 0 pointer (high 3 bits)                          */ \
    X(SPR0PTL , 0x122 , 1) /* Sprite 0 pointer (low 15 bits)                          */ \
    X(SPR1PTH , 0x124 , 1) /* Sprite 1 pointer (high 3 bits)                          */ \
    X(SPR1PTL , 0x126 , 1) /* Sprite 1 pointer (low 15 bits)                          */ \
    X(SPR2PTH , 0x128 , 1) /* Sprite 2 pointer (high 3 bits)                          */ \
    X(SPR2PTL , 0x12A , 1) /* Sprite 2 pointer (low 15 bits)                          */ \
    X(SPR3PTH , 0x12C , 1) /* Sprite 3 pointer (high 3 bits)                          */ \
    X(SPR3PTL , 0x12E , 1) /* Sprite 3 pointer (low 15 bits)                          */ \
    X(SPR4PTH , 0x130 , 1) /* Sprite 4 pointer (high 3 bits)                          */ \
    X(SPR4PTL , 0x132 , 1) /* Sprite 4 pointer (low 15 bits)                          */ \
    X(SPR5PTH , 0x134 , 1) /* Sprite 5 pointer (high 3 bits)                          */ \
    X(SPR5PTL , 0x136 , 1) /* Sprite 5 pointer (low 15 bits)                          */ \
    X(SPR6PTH , 0x138 , 1) /* Sprite 6 pointer (high 3 bits)                          */ \
    X(SPR6PTL , 0x13A , 1) /* Sprite 6 pointer (low 15 bits)                          */ \
    X(SPR7PTH , 0x13C , 1) /* Sprite 7 pointer (high 3 bits)                          */ \
    X(SPR7PTL , 0x13E , 1) /* Sprite 7 pointer (low 15 bits)                          */ \
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


namespace {

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

#if 0
#define OCTANT8 24
#define OCTANT7 4
#define OCTANT6 12
#define OCTANT5 28
#define OCTANT4 20
#define OCTANT3 8
#define OCTANT2 0
#define OCTANT1 16
#endif

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

static constexpr uint16_t vpos_per_field  = 312;
//static constexpr uint16_t vpos_per_frame  = 625;
static constexpr uint16_t vblank_end_vpos = 28;

static_assert(graphics_width == hires_max_pixel - hires_min_pixel);
static_assert(graphics_height == (vpos_per_field - vblank_end_vpos) * 2);

constexpr uint32_t rgb4_to_8(const uint16_t rgb4)
{
    const uint32_t r = (rgb4 >> 8) & 0xf;
    const uint32_t g = (rgb4 >> 4) & 0xf;
    const uint32_t b = rgb4 & 0xf;
    return r << 20 | r << 16 | g << 12 | g << 8 | b << 4 | b;
}

struct custom_state {
    uint16_t hpos; // Resolution is in low-res pixels
    uint16_t vpos;
    uint16_t bpldat_shift[6];
    uint8_t bpldat_shift_pixels;

    uint32_t copper_pt;
    uint16_t copper_inst[2];
    uint8_t copper_inst_ofs;

    uint32_t coplc[2];
    uint16_t diwstrt;
    uint16_t diwstop;
    uint16_t ddfstrt;
    uint16_t ddfstop;
    uint16_t dmacon;
    uint16_t intena;
    uint16_t intreq;
    uint32_t bplpt[6];
    uint16_t bplcon0;
    int16_t bplmod1; // odd planes
    int16_t bplmod2; // even planes
    uint16_t bpldat[6];
    uint16_t color[32]; // $180..$1C0

    // blitter
    uint16_t bltcon0;
    uint16_t bltcon1;
    uint16_t bltafwm;
    uint16_t bltalwm;
    uint32_t bltpt[4];
    uint16_t bltdat[4];
    int16_t  bltmod[4];
    uint16_t bltw;
    uint16_t blth;
};

}

class custom_handler::impl : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler, cia_handler& cia)
        : mem_ { mem_handler }
        , cia_ { cia }
    {
        mem_.register_handler(*this, 0xDFF000, 0x1000);    
        reset();
    }

    void reset()
    {
        memset(gfx_buf_, 0, sizeof(gfx_buf_));
        memset(&s_, 0, sizeof(s_));
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

    void log_blitter_state()
    {
        std::cout << "Blit $" << hexfmt(s_.bltw) << "x$" << hexfmt(s_.blth) << " bltcon0=$" << hexfmt(s_.bltcon0) << " bltcon1=$" << hexfmt(s_.bltcon1) << " bltafwm=$" << hexfmt(s_.bltafwm) << " bltalwm=$" << hexfmt(s_.bltalwm) << "\n";
        for (int i = 0; i < 4; ++i) {
            const char name[5] = { 'B', 'L', 'T', static_cast<char>('A' + i), 0 };
            std::cout << "  " << name << "PT=$" << hexfmt(s_.bltpt[i]) << " " << name << "DAT=$" << hexfmt(s_.bltdat[i]) << " " << name << "MOD=" << (int)s_.bltmod[i] << "\n";
        }
    }

    void do_blit()
    {
        // For now just do immediate blit

        uint16_t any = 0;

        if (s_.bltcon1 & BC1F_LINEMODE) {
            std::cout << "TODO: Line mode\n";
            log_blitter_state();
        } else {
            TODO_ASSERT(!(s_.bltcon1 & (BC1F_FILL_OR | BC1F_FILL_XOR | BC1F_FILL_CARRYIN)));

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
                s_.bltdat[index] = mem_.read_u16(s_.bltpt[index]);
                incr_ptr(s_.bltpt[index]);
            };
            auto shift_val = [&](uint16_t& oldval, uint16_t newval, uint8_t shift) {
                const uint16_t res = reverse
                    ? ((oldval << 16) | (newval)) << shift
                    : ((oldval << 16) | (newval)) >> shift;
                oldval = newval;
                return res;
            };

            uint16_t areg = 0;
            uint16_t breg = 0;

            for (uint16_t y = 0; y < s_.blth; ++y) {
                for (uint16_t x = 0; x < s_.bltw; ++x) {
                    if (s_.bltcon0 & BC0F_SRCA) do_dma(0);
                    if (s_.bltcon0 & BC0F_SRCB) do_dma(1);
                    if (s_.bltcon0 & BC0F_SRCC) do_dma(2);
                    uint16_t amask = 0xffff;
                    if (x == 0)
                        amask &= s_.bltafwm;
                    if (x == s_.bltw - 1)
                        amask &= s_.bltalwm;
                    const uint16_t a = shift_val(areg, s_.bltdat[0] & amask, ashift);
                    const uint16_t b = shift_val(breg, s_.bltdat[1], bshift);
                    const uint16_t c = s_.bltdat[2];
                    uint16_t val = 0;
                    if (s_.bltcon0 & BC0F_ABC)    val |=   a &  b &  c;
                    if (s_.bltcon0 & BC0F_ABNC)   val |=   a &  b &(~c);
                    if (s_.bltcon0 & BC0F_ANBC)   val |=   a &(~b)&  c;
                    if (s_.bltcon0 & BC0F_ANBNC)  val |=   a &(~b)&(~c);
                    if (s_.bltcon0 & BC0F_NABC)   val |= (~a)&  b &  c;
                    if (s_.bltcon0 & BC0F_NABNC)  val |= (~a)&  b &(~c);
                    if (s_.bltcon0 & BC0F_NANBC)  val |= (~a)&(~b)&  c;
                    if (s_.bltcon0 & BC0F_NANBNC) val |= (~a)&(~b)&(~c);

                    any |= val;
                    if (s_.bltcon0 & BC0F_DEST) {
                        mem_.write_u16(s_.bltpt[3], val);
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

    void step()
    {
        // Step frequency: Base CPU frequency (7.09 for PAL) => 1 lores virtual pixel / 2 hires pixels

        // First readble hpos that's interpreted as being on a new line is $005 (since it's resolution is half that of lowres pixels -> 20) 
        const unsigned virt_pixel = s_.hpos * 2 + 20;
        const unsigned disp_pixel = virt_pixel - hires_min_pixel;
        const bool vert_disp = s_.vpos >= s_.diwstrt >> 8 && s_.vpos < (0x100 | s_.diwstop >> 8);
        const bool horiz_disp = s_.hpos >= (s_.diwstrt & 0xff) && s_.hpos < (0x100 | (s_.diwstop & 0xff));

        if (s_.copper_inst_ofs == 2) {
            if (s_.copper_inst[0] & 1) {
                // Wait/skip
                const auto vp = (s_.copper_inst[0] >> 8) & 0xff;
                const auto hp = s_.copper_inst[0] & 0xfe;
                const auto ve = 0x80 | ((s_.copper_inst[1] >> 8) & 0x7f);
                const auto he = s_.copper_inst[1] & 0xfe;

#ifdef COPPER_DEBUG
                static bool blitter_wait_warn = false;
                if (s_.vpos < 2)
                    blitter_wait_warn = true;
#endif

                if (!(s_.copper_inst[1] & 0x8000) && !(s_.dmacon & DMAF_BLTDONE)) {
                    // Blitter wait
#ifdef COPPER_DEBUG
                    if (blitter_wait_warn) {
                        std::cout << "Waiting for blitter\n";
                        blitter_wait_warn = false;
                    }
#endif
                } else if ((s_.vpos & ve) >= (vp & ve) && ((s_.hpos >> 1) & he) >= (hp & he)) {
                    //std::cout << "Wait done $" << hexfmt(s_.copper_inst[0]) << ", $" << hexfmt(s_.copper_inst[1]) << ": vp=$" << hexfmt(vp) << ", hp=$" << hexfmt(hp) << ", ve=$" << hexfmt(ve) << ", he=$" << hexfmt(he) << "\n";
                    s_.copper_inst_ofs = 0; // Fetch next instruction
                    if (s_.copper_inst[1] & 1) {
                        // SKIP instruction. Actually reads next instruction, but does nothing?
#ifdef COPPER_DEBUG
                        std::cout << "Warning: SKIP processed\n";
#endif
                        assert(0);
                    }

                }

                //std::cout << "Wait $" << hexfmt(s_.copper_inst[0]) << ", $" << hexfmt(s_.copper_inst[1]) << ": vp=$" << hexfmt(vp) << ", hp=$" << hexfmt(hp) << ", ve=$" << hexfmt(ve) << ", he=$" << hexfmt(he) << "\n";
            } else if ((s_.hpos & 1)) {
                // The copper is activate DMA on odd memory cycles
                // TODO: Check which register is accessed ($20+ is ok, $10+ ok only with copper danger)
                // TODO: Does this consume a DMA slot? (it steals it from the CPU at least)
                const auto reg = s_.copper_inst[0] & 0x1ff;
                if (reg != 0 || s_.copper_inst[1] != 0) // Seems to be used as a kind of NOP in the kickstart copper list?
                    write_u16(0xdff000 + reg, reg, s_.copper_inst[1]);
                s_.copper_inst_ofs = 0; // Fetch next instruction
            }
        }

        if (s_.vpos >= vblank_end_vpos && disp_pixel < graphics_width) {
            uint32_t* row = &gfx_buf_[(s_.vpos - vblank_end_vpos) * 2 * graphics_width + disp_pixel];
            assert(&row[graphics_width + 1] < &gfx_buf_[sizeof(gfx_buf_) / sizeof(*gfx_buf_)]);

            if (vert_disp && horiz_disp) {
                const uint8_t nbpls = (s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0;

                if (!s_.bpldat_shift_pixels) {
                    //if (s_.dmacon & DMAF_RASTER) std::cout << "hpos=$" << hexfmt(s_.hpos) << " (clock $" << hexfmt(s_.hpos >> 1, 4) << ")" << " reloading\n";
                    for (int i = 0; i < nbpls; ++i)
                        s_.bpldat_shift[i] = s_.bpldat[i];
                    s_.bpldat_shift_pixels = 16;
                }

                auto one_pixel = [&]() {
                    uint8_t index = 0;
                    for (int i = 0; i < nbpls; ++i) {
                        if (s_.bpldat_shift[i] & 0x8000)
                            index |= 1 << i;
                        s_.bpldat_shift[i] <<= 1;
                    }

                    assert(s_.bpldat_shift_pixels);
                    --s_.bpldat_shift_pixels;
                    return rgb4_to_8(s_.color[index]);
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

            if (!(s_.bplcon0 & BPLCON0F_LACE)) {
                row[0 + graphics_width] = row[0];
                row[1 + graphics_width] = row[1];
            }
        }

        if (!(s_.hpos & 1) && (s_.dmacon & DMAF_MASTER)) {
            const uint16_t colclock = s_.hpos >> 1;

            auto do_dma = [&mem = this->mem_](uint32_t& pt) {
                const auto val = mem.read_u16(pt);
                pt += 2;
                return val;
            };

            do {
                // Refresh
                if (colclock < 8 && !(colclock & 0))
                    break;

                // TODO: Disk, Audio

                // Display
                if ((s_.dmacon & DMAF_RASTER) && vert_disp
                    // Note: hack - comparing colclock-7 with ddfstop since ddfstop/d8 is the latest point a new 8-word transfer can be started
                    && colclock >= std::max<uint16_t>(0x18, s_.ddfstrt) && (colclock-7) <= std::min<uint16_t>(0xD8, s_.ddfstop)) {
                    constexpr uint8_t lores_bpl_sched[8] = { 0, 4, 6, 2, 0, 3, 5, 1 };
                    constexpr uint8_t hires_bpl_sched[8] = { 4, 3, 2, 1, 4, 3, 2, 1 };
                    const int bpl = (s_.bplcon0 & BPLCON0F_HIRES ? hires_bpl_sched : lores_bpl_sched)[colclock & 7] - 1;

                    if (bpl >= 0 && bpl < ((s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0)) {
                        //std::cout << "hpos=$" << hexfmt(s_.hpos) << " (clock $" << hexfmt(colclock) << ")" << " BPL " << bpl << " DMA shift_pixels=" << (int)s_.bpldat_shift_pixels << "\n";
                        s_.bpldat[bpl] = do_dma(s_.bplpt[bpl]);
                        break;
                    }
                }

                // TODO: Sprite

                // Copper
                if ((s_.dmacon & DMAF_COPPER) && s_.copper_inst_ofs < 2) {
                    s_.copper_inst[s_.copper_inst_ofs++] = do_dma(s_.copper_pt);

#ifdef COPPER_DEBUG
                    if (s_.copper_inst_ofs == 2) std::cout << "Read copper instruction $" << hexfmt(s_.copper_inst[0]) << ", " << hexfmt(s_.copper_inst[1]) << " from $" << hexfmt(s_.copper_pt-4) << "\n";
#endif
                    if (s_.copper_inst[0] == 0 && s_.copper_inst[1] == 0) {
#ifdef COPPER_DEBUG
                        std::cout << "Hack, halting copper after reading 0,0\n";
#endif
                        pause_copper();
                    }
                }
                
                // TODO: Blitter
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
            s_.bpldat_shift_pixels = 0; // Any remaining pixels are lost, force re-read of bpldat
            cia_.increment_tod_counter(1);
            if ((s_.dmacon & DMAF_RASTER) && vert_disp) {
                for (int bpl = 0; bpl < ((s_.bplcon0 & BPLCON0F_BPU) >> BPLCON0B_BPU0); ++bpl) {
                    assert(!(bpl & 1 ? s_.bplmod2 : s_.bplmod1)); // Untested
                    s_.bplpt[bpl] += bpl & 1 ? s_.bplmod2 : s_.bplmod1;                    
                }
            }
            if (++s_.vpos == vpos_per_field) {
                s_.copper_pt = s_.coplc[0];
                s_.copper_inst_ofs = 0;
                s_.vpos = 0;
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
            return 0;
        case JOY1DAT: // $00C
            return 0;
        case VPOSR: // $004
            // TODO: Bit15 LOF (long frame)
            // Hack: Just return a fixed V/HPOS when external sync is enabled (needed for KS1.2)
            if (s_.bplcon0 & BPLCON0F_ERSY)
                return 0x8040;

            return 0x8000 | ((s_.vpos >> 8)&1);
        case VHPOSR:  // $006
            // Hack: Just return a fixed V/HPOS when external sync is enabled (needed for KS1.2)
            if (s_.bplcon0 & BPLCON0F_ERSY)
                return 0x80;
            return (s_.vpos & 0xff) << 8 | ((s_.hpos >> 1) & 0xff);
        case POTGOR:  // $016
            // Don't spam in DiagROM
            return 0xFF00;
        case SERDATR: // $018
            return (3<<12); // Just return transmit buffer empty
        case INTENAR: // $01C
            return s_.intena;
        case INTREQR: // $01E
            return s_.intreq;
        }

        std::cerr << "Unhandled read from custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")\n";
        return 0xffff;
    }
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        auto write_partial = [offset, val](uint16_t& r) {
            if (offset & 1)
                r = (r & 0xff00) | val;
            else
                r = val << 8 | (r & 0xff);
        };

        if (offset >= COLOR00 && offset <= COLOR31 + 1) {
            write_partial(s_.color[(offset - COLOR00) / 2]);
            return;
        }

        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")"
                  << " val $" << hexfmt(val) << "\n";
    }
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        //std::cerr << "Write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")" << " val $" << hexfmt(val) << "\n";
        auto write_partial = [offset, &val](uint32_t& r) {
            if (offset & 2) {
                assert(!(val & 1));
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
            std::cerr << "Update register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")" << " val $" << hexfmt(val) << "\n";
            if (offset == COP2LCL && !val) {
                std::cerr << "HACK: Ignoring write of 0 to COP2LCL\n";
                return;
            }
            write_partial(s_.coplc[(offset - COP1LCH) / 4]);
            return;
        }
        if (offset >= BPL1PTH && offset <= BPL6PTL) {
            write_partial(s_.bplpt[(offset - BPL1PTH) / 4]);
            return;
        }
        if (offset >= BPL1DAT && offset <= BPL6DAT) {
            s_.bpldat[(offset - BPL1DAT) / 2] = val;
            return;
        }
        if (offset >= SPR0PTH && offset <= SPR7PTL) {
            //write_partial(s_.sprpt[(offset - SPR0PTH) / 4]);
            return;
        }
        if (offset >= COLOR00 && offset <= COLOR31) {
            s_.color[(offset-COLOR00)/2] = val;
            return;
        }

        switch (offset) {
        case SERDAT: // $018
            if (serial_data_handler_) {
                uint8_t numbits = 9;
                while (numbits && !(val & (1 << numbits)))
                    --numbits;
                serial_data_handler_(numbits, val & ((1 << numbits) - 1));
            }
            return;
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
        case BLTCPTL:
            val &= 0xfffe;
            [[fallthrough]];
        case BLTCPTH:
            write_partial(s_.bltpt[2]);
            return;
        case BLTBPTL:
            val &= 0xfffe;
            [[fallthrough]];
        case BLTBPTH:
            write_partial(s_.bltpt[1]);
            return;
        case BLTAPTL:
            val &= 0xfffe;
            [[fallthrough]];
        case BLTAPTH:
            write_partial(s_.bltpt[0]);
            return;
        case BLTDPTL:
            val &= 0xfffe;
            [[fallthrough]];
        case BLTDPTH:
            write_partial(s_.bltpt[3]);
            return;
        case BLTSIZE:        
            assert(!s_.bltw && !s_.blth && !(s_.dmacon & DMAF_BLTDONE));
            s_.bltw = val & 0x3f ? val & 0x3f : 0x40;
            s_.blth = val >> 6 ? val >> 6 : 0x400;
            return;
        case BLTCMOD:
            s_.bltmod[2] = val & 0xfffe;
            return;
        case BLTBMOD:
            s_.bltmod[1] = val & 0xfffe;
            return;
        case BLTAMOD:
            s_.bltmod[0] = val & 0xfffe;
            return;
        case BLTDMOD:
            s_.bltmod[3] = val & 0xfffe;
            return;
        case BLTCDAT:
            s_.bltdat[2] = val;
            return;
        case BLTBDAT:
            s_.bltdat[1] = val;
            return;
        case BLTADAT:
            s_.bltdat[0] = val;
            return;
        case COPJMP1: // $088
            TODO_ASSERT(0);
            return;
        case COPJMP2: // $08A
            s_.copper_inst_ofs = 0;
            s_.copper_pt = s_.coplc[1];
            TODO_ASSERT(s_.copper_pt);
            //pause_copper();
            break;
        case DIWSTRT: // $08E
            s_.diwstrt = val;
            return;
        case DIWSTOP: // $090
            s_.diwstop = val;
            return;
        case DDFSTRT: // $092
            s_.ddfstrt = val;
            return;
        case DDFSTOP: // $094
            s_.ddfstop = val;
            return;
        case DMACON:  // $096
            val &= ~(1 << 14 | 1 << 13 | 1 << 12 | 1 << 11); // Mask out read only/unused bits
            setclr(s_.dmacon, val);
            if ((s_.dmacon & DMAF_COPPER) && s_.copper_pt == 0) {
                std::cout << "HACK: Loading copper pointer early from $" << hexfmt(s_.coplc[0]) << "\n";
                TODO_ASSERT(s_.coplc[0]);
                s_.copper_pt = s_.coplc[0];
            }
            return;
        case INTENA:  // $09A
            setclr(s_.intena, val);
            return;
        case INTREQ:  // $09C
            val &= ~INTF_INTEN;
            setclr(s_.intreq, val);
            return;
        case BPLCON0: // $100
            if (val & BPLCON0F_LACE) // TODO: Handle LOF in vposr
                std::cout << "Warning: Enabling interlace mode\n";
            s_.bplcon0 = val;
            return;
        case BPLCON1: // $102
            return;
        case BPLCON2: // $104
            break;
        case BPLCON3: // $106
            return;
        case BPLMOD1: // $108
            TODO_ASSERT(val == 0);
            s_.bplmod1 = val;
            return;
        case BPLMOD2: // $10A
            TODO_ASSERT(val == 0);
            s_.bplmod2 = val;
            return;
        case 0x1fe: // NO-OP
            return;
        }
        std::cerr << "Unhandled write to custom register $" << hexfmt(offset, 3) << " (" << regname(offset) << ")"
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
};

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