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

#if 0
/* interrupt control register bit numbers */
#define CIAICRB_TA 0
#define CIAICRB_TB 1
#define CIAICRB_ALRM 2
#define CIAICRB_SP 3
#define CIAICRB_FLG 4
#define CIAICRB_IR 7
#define CIAICRB_SETCLR 7

/* control register A bit numbers */
#define CIACRAB_START 0
#define CIACRAB_PBON 1
#define CIACRAB_OUTMODE 2
#define CIACRAB_RUNMODE 3
#define CIACRAB_LOAD 4
#define CIACRAB_INMODE 5
#define CIACRAB_SPMODE 6
#define CIACRAB_TODIN 7

/* control register B bit numbers */
#define CIACRBB_START 0
#define CIACRBB_PBON 1
#define CIACRBB_OUTMODE 2
#define CIACRBB_RUNMODE 3
#define CIACRBB_LOAD 4
#define CIACRBB_INMODE0 5
#define CIACRBB_INMODE1 6
#define CIACRBB_ALARM 7

/* interrupt control register masks */
#define CIAICRF_TA (1L << CIAICRB_TA)
#define CIAICRF_TB (1L << CIAICRB_TB)
#define CIAICRF_ALRM (1L << CIAICRB_ALRM)
#define CIAICRF_SP (1L << CIAICRB_SP)
#define CIAICRF_FLG (1L << CIAICRB_FLG)
#define CIAICRF_IR (1L << CIAICRB_IR)
#define CIAICRF_SETCLR (1L << CIAICRB_SETCLR)

/* control register A register masks */
#define CIACRAF_START (1L << CIACRAB_START)
#define CIACRAF_PBON (1L << CIACRAB_PBON)
#define CIACRAF_OUTMODE (1L << CIACRAB_OUTMODE)
#define CIACRAF_RUNMODE (1L << CIACRAB_RUNMODE)
#define CIACRAF_LOAD (1L << CIACRAB_LOAD)
#define CIACRAF_INMODE (1L << CIACRAB_INMODE)
#define CIACRAF_SPMODE (1L << CIACRAB_SPMODE)
#define CIACRAF_TODIN (1L << CIACRAB_TODIN)

/* control register B register masks */
#define CIACRBF_START (1L << CIACRBB_START)
#define CIACRBF_PBON (1L << CIACRBB_PBON)
#define CIACRBF_OUTMODE (1L << CIACRBB_OUTMODE)
#define CIACRBF_RUNMODE (1L << CIACRBB_RUNMODE)
#define CIACRBF_LOAD (1L << CIACRBB_LOAD)
#define CIACRBF_INMODE0 (1L << CIACRBB_INMODE0)
#define CIACRBF_INMODE1 (1L << CIACRBB_INMODE1)
#define CIACRBF_ALARM (1L << CIACRBB_ALARM)

/* control register B INMODE masks */
#define CIACRBF_IN_PHI2 0
#define CIACRBF_IN_CNT (CIACRBF_INMODE0)
#define CIACRBF_IN_TA (CIACRBF_INMODE1)
#define CIACRBF_IN_CNT_TA (CIACRBF_INMODE0 | CIACRBF_INMODE1)

/*
 * Port definitions -- what each bit in a cia peripheral register is tied to
 */

/* ciaa port A (0xbfe001) */
#define CIAB_GAMEPORT1 (7) /* gameport 1, pin 6 (fire button*) */
#define CIAB_GAMEPORT0 (6) /* gameport 0, pin 6 (fire button*) */
#define CIAB_DSKRDY (5) /* disk ready* */
#define CIAB_DSKTRACK0 (4) /* disk on track 00* */
#define CIAB_DSKPROT (3) /* disk write protect* */
#define CIAB_DSKCHANGE (2) /* disk change* */
#define CIAB_LED (1) /* led light control (0==>bright) */
#define CIAB_OVERLAY (0) /* memory overlay bit */

/* ciaa port B (0xbfe101) -- parallel port */

/* ciab port A (0xbfd000) -- serial and printer control */
#define CIAB_COMDTR (7) /* serial Data Terminal Ready* */
#define CIAB_COMRTS (6) /* serial Request to Send* */
#define CIAB_COMCD (5) /* serial Carrier Detect* */
#define CIAB_COMCTS (4) /* serial Clear to Send* */
#define CIAB_COMDSR (3) /* serial Data Set Ready* */
#define CIAB_PRTRSEL (2) /* printer SELECT */
#define CIAB_PRTRPOUT (1) /* printer paper out */
#define CIAB_PRTRBUSY (0) /* printer busy */

/* ciab port B (0xbfd100) -- disk control */
#define CIAB_DSKMOTOR (7) /* disk motorr* */
#define CIAB_DSKSEL3 (6) /* disk select unit 3* */
#define CIAB_DSKSEL2 (5) /* disk select unit 2* */
#define CIAB_DSKSEL1 (4) /* disk select unit 1* */
#define CIAB_DSKSEL0 (3) /* disk select unit 0* */
#define CIAB_DSKSIDE (2) /* disk side select* */
#define CIAB_DSKDIREC (1) /* disk direction of seek* */
#define CIAB_DSKSTEP (0) /* disk step heads* */

/* ciaa port A (0xbfe001) */
#define CIAF_GAMEPORT1 (1L << 7)
#define CIAF_GAMEPORT0 (1L << 6)
#define CIAF_DSKRDY (1L << 5)
#define CIAF_DSKTRACK0 (1L << 4)
#define CIAF_DSKPROT (1L << 3)
#define CIAF_DSKCHANGE (1L << 2)
#define CIAF_LED (1L << 1)
#define CIAF_OVERLAY (1L << 0)

/* ciaa port B (0xbfe101) -- parallel port */

/* ciab port A (0xbfd000) -- serial and printer control */
#define CIAF_COMDTR (1L << 7)
#define CIAF_COMRTS (1L << 6)
#define CIAF_COMCD (1L << 5)
#define CIAF_COMCTS (1L << 4)
#define CIAF_COMDSR (1L << 3)
#define CIAF_PRTRSEL (1L << 2)
#define CIAF_PRTRPOUT (1L << 1)
#define CIAF_PRTRBUSY (1L << 0)

/* ciab port B (0xbfd100) -- disk control */
#define CIAF_DSKMOTOR (1L << 7)
#define CIAF_DSKSEL3 (1L << 6)
#define CIAF_DSKSEL2 (1L << 5)
#define CIAF_DSKSEL1 (1L << 4)
#define CIAF_DSKSEL0 (1L << 3)
#define CIAF_DSKSIDE (1L << 2)
#define CIAF_DSKDIREC (1L << 1)
#define CIAF_DSKSTEP (1L << 0)

#endif

}

// Handles both MOS Technology 8520 Complex Interface Adapter chips
// CIAA can generate INT2, CIAB can generate INT6
class cia_handler::impl : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler, rom_area_handler& rom_handler)
        : mem_handler_ { mem_handler }
        , rom_handler_ { rom_handler }
    {
        assert(get_port_output(0, 0) == 0xff);
        // Since all ports are set to input, OVL in CIA pra is high -> OVL set
        mem_handler_.register_handler(*this, 0xBF0000, 0x10000);
        rom_handler_.set_overlay(true);
        reset();
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

    void increment_tod_counter(uint8_t cia)
    {
        assert(cia < 2);
        ++counters_[cia];
    }

private:
    memory_handler& mem_handler_;
    rom_area_handler& rom_handler_;
    uint8_t regs_[2][16];
    uint32_t counters_[2];
    uint32_t counter_latches_[2];

    static constexpr uint32_t latch_active_mask = 0x8000'0000;

    void reset()
    {
        memset(regs_, 0, sizeof(regs_));
        memset(counters_, 0, sizeof(counters_));
        memset(counter_latches_, 0, sizeof(counter_latches_));
    }

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
        case todlo: {
            const uint8_t val = (counter_latches_[idx] & latch_active_mask ? counter_latches_[idx] : counters_[idx]) & 0xff;
            counter_latches_[idx] = 0; // Latch disabled
            return val;
        }
        case todmid:
            return ((counter_latches_[idx] & latch_active_mask ? counter_latches_[idx] : counters_[idx]) >> 8) & 0xff;
        case todhi:
            counter_latches_[idx] = latch_active_mask | (counters_[idx] & 0xffffff);
            return (counter_latches_[idx] >> 16) & 0xff;
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

void cia_handler::increment_tod_counter(uint8_t cia)
{
    impl_->increment_tod_counter(cia);
}