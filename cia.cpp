#include "cia.h"
#include "ioutil.h"
#include <cassert>
#include <iostream>

//#define FLOPPY_DEBUG

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

// interrupt control register bit numbers
constexpr uint8_t CIAICRB_TA     = 0;
constexpr uint8_t CIAICRB_TB     = 1;
constexpr uint8_t CIAICRB_ALRM   = 2;
constexpr uint8_t CIAICRB_SP     = 3;
constexpr uint8_t CIAICRB_FLG    = 4;
constexpr uint8_t CIAICRB_IR     = 7; // Reading
constexpr uint8_t CIAICRB_SETCLR = 7; // Writing

// interrupt control register masks
constexpr uint8_t CIAICRF_TA     = 1 << CIAICRB_TA;
constexpr uint8_t CIAICRF_TB     = 1 << CIAICRB_TB;
constexpr uint8_t CIAICRF_ALRM   = 1 << CIAICRB_ALRM;
constexpr uint8_t CIAICRF_SP     = 1 << CIAICRB_SP;
constexpr uint8_t CIAICRF_FLG    = 1 << CIAICRB_FLG;
constexpr uint8_t CIAICRF_IR     = 1 << CIAICRB_IR;
constexpr uint8_t CIAICRF_SETCLR = 1 << CIAICRB_SETCLR;

// control register A bit numbers

constexpr uint8_t CIACRAB_START   = 0; // 1 = start Timer A, 0 - stop Timer A. This bit is automatically reset(= 0) when underflow occurs during one - shot mode.
constexpr uint8_t CIACRAB_PBON    = 1; // 1 = Timer A output on PB6, 0 = PB6 is normal operation.
constexpr uint8_t CIACRAB_OUTMODE = 2; // 1 = toggle, 0 = pulse.
constexpr uint8_t CIACRAB_RUNMODE = 3; // 1 = one-shot mode, 0 = continuous mode.
constexpr uint8_t CIACRAB_LOAD    = 4; // 1 = force load (this ia a strobe input, there ia no data storage; bit 4 will always read back a zero and writing a 0 has no effect.)
constexpr uint8_t CIACRAB_INMODE  = 5; // 1 = Timer A count positive CNT tranition, 0 = Timer A counts 02 pules.
constexpr uint8_t CIACRAB_SPMODE  = 6; // 1 = Serial port=output (CNT is the source of the shift clock)
constexpr uint8_t CIACRAB_TODIN   = 7; // 

// control register A register masks

constexpr uint8_t CIACRAF_START   = 1 << CIACRAB_START;
constexpr uint8_t CIACRAF_PBON    = 1 << CIACRAB_PBON;
constexpr uint8_t CIACRAF_OUTMODE = 1 << CIACRAB_OUTMODE;
constexpr uint8_t CIACRAF_RUNMODE = 1 << CIACRAB_RUNMODE;
constexpr uint8_t CIACRAF_LOAD    = 1 << CIACRAB_LOAD;
constexpr uint8_t CIACRAF_INMODE  = 1 << CIACRAB_INMODE;
constexpr uint8_t CIACRAF_SPMODE  = 1 << CIACRAB_SPMODE;
constexpr uint8_t CIACRAF_TODIN   = 1 << CIACRAB_TODIN;

// control register B bit numbers
constexpr uint8_t CIACRBB_START   = 0;
constexpr uint8_t CIACRBB_PBON    = 1;
constexpr uint8_t CIACRBB_OUTMODE = 2;
constexpr uint8_t CIACRBB_RUNMODE = 3;
constexpr uint8_t CIACRBB_LOAD    = 4;
constexpr uint8_t CIACRBB_INMODE0 = 5;
constexpr uint8_t CIACRBB_INMODE1 = 6;
constexpr uint8_t CIACRBB_ALARM   = 7;

// control register B register masks
constexpr uint8_t CIACRBF_START   = 1 << CIACRBB_START;
constexpr uint8_t CIACRBF_PBON    = 1 << CIACRBB_PBON;
constexpr uint8_t CIACRBF_OUTMODE = 1 << CIACRBB_OUTMODE;
constexpr uint8_t CIACRBF_RUNMODE = 1 << CIACRBB_RUNMODE;
constexpr uint8_t CIACRBF_LOAD    = 1 << CIACRBB_LOAD;
constexpr uint8_t CIACRBF_INMODE0 = 1 << CIACRBB_INMODE0;
constexpr uint8_t CIACRBF_INMODE1 = 1 << CIACRBB_INMODE1;
constexpr uint8_t CIACRBF_ALARM   = 1 << CIACRBB_ALARM;

// ciaa port A (0xbfe001)
constexpr uint8_t CIAB_GAMEPORT1 = 7; // gameport 1, pin 6 (fire button*)
constexpr uint8_t CIAB_GAMEPORT0 = 6; // gameport 0, pin 6 (fire button*)
constexpr uint8_t CIAB_DSKRDY    = 5; // disk ready*
constexpr uint8_t CIAB_DSKTRACK0 = 4; // disk on track 00*
constexpr uint8_t CIAB_DSKPROT   = 3; // disk write protect*
constexpr uint8_t CIAB_DSKCHANGE = 2; // disk change*
constexpr uint8_t CIAB_LED       = 1; // led light control (0==>bright)
constexpr uint8_t CIAB_OVERLAY   = 0; // memory overlay bit

constexpr uint8_t CIAF_GAMEPORT1 = 1 << CIAB_GAMEPORT1;
constexpr uint8_t CIAF_GAMEPORT0 = 1 << CIAB_GAMEPORT0;
constexpr uint8_t CIAF_DSKRDY    = 1 << CIAB_DSKRDY;
constexpr uint8_t CIAF_DSKTRACK0 = 1 << CIAB_DSKTRACK0;
constexpr uint8_t CIAF_DSKPROT   = 1 << CIAB_DSKPROT;
constexpr uint8_t CIAF_DSKCHANGE = 1 << CIAB_DSKCHANGE;
constexpr uint8_t CIAF_LED       = 1 << CIAB_LED;
constexpr uint8_t CIAF_OVERLAY   = 1 << CIAB_OVERLAY;

// ciab port B (0xbfd100) -- disk control
constexpr uint8_t CIAB_DSKMOTOR = 7;  // disk motor*
constexpr uint8_t CIAB_DSKSEL3  = 6;  // disk select unit 3*
constexpr uint8_t CIAB_DSKSEL2  = 5;  // disk select unit 2*
constexpr uint8_t CIAB_DSKSEL1  = 4;  // disk select unit 1*
constexpr uint8_t CIAB_DSKSEL0  = 3;  // disk select unit 0*
constexpr uint8_t CIAB_DSKSIDE  = 2;  // disk side select*
constexpr uint8_t CIAB_DSKDIREC = 1;  // disk direction of seek*
constexpr uint8_t CIAB_DSKSTEP  = 0;  // disk step heads*

constexpr uint8_t CIAF_DSKMOTOR = 1 << CIAB_DSKMOTOR;
constexpr uint8_t CIAF_DSKSEL3  = 1 << CIAB_DSKSEL3;
constexpr uint8_t CIAF_DSKSEL2  = 1 << CIAB_DSKSEL2;
constexpr uint8_t CIAF_DSKSEL1  = 1 << CIAB_DSKSEL1;
constexpr uint8_t CIAF_DSKSEL0  = 1 << CIAB_DSKSEL0;
constexpr uint8_t CIAF_DSKSIDE  = 1 << CIAB_DSKSIDE;
constexpr uint8_t CIAF_DSKDIREC = 1 << CIAB_DSKDIREC;
constexpr uint8_t CIAF_DSKSTEP  = 1 << CIAB_DSKSTEP;


constexpr uint8_t CIAF_ALL_DSKSEL = CIAF_DSKSEL0 | CIAF_DSKSEL1 | CIAF_DSKSEL2 | CIAF_DSKSEL3;

#if 0

/* control register B INMODE masks */
#define CIACRBF_IN_PHI2 0
#define CIACRBF_IN_CNT (CIACRBF_INMODE0)
#define CIACRBF_IN_TA (CIACRBF_INMODE1)
#define CIACRBF_IN_CNT_TA (CIACRBF_INMODE0 | CIACRBF_INMODE1)

/*
 * Port definitions -- what each bit in a cia peripheral register is tied to
 */


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

#endif

}

// Handles both MOS Technology 8520 Complex Interface Adapter chips
// CIAA can generate INT2, CIAB can generate INT6
class cia_handler::impl : public memory_area_handler {
public:
    explicit impl(memory_handler& mem_handler, rom_area_handler& rom_handler, disk_drive* dfs[max_drives])
        : mem_handler_ { mem_handler }
        , rom_handler_ { rom_handler }
    {
        assert(dfs[0]);
        memcpy(drives_, dfs, sizeof(drives_));
        mem_handler_.register_handler(*this, 0xBF0000, 0x10000);
        rom_handler_.set_overlay(true);
        reset();
        // Since all ports are set to input, OVL in CIA pra is high -> OVL set
        assert(s_[0].port_value(0) & CIAF_OVERLAY);
    }

    void step()
    {
        for (int i = 0; i < 2; ++i) {
            auto& s = s_[i];
            for (int t = 0; t < 2; ++t) {
                if (s.cr[t] & CIACRAF_START) {
                    if (!(s.timer_val[t]--)) {
                        if (s.cr[t] & CIACRAF_RUNMODE) {
                            s.cr[t] &= ~CIACRAF_START;
                        } else {
                            s.timer_val[t] = s.timer_latch[t];
                        }
                        s.trigger_int(t ? CIAICRB_TB : CIAICRB_TA);
                    }
                }
            }
        }

        if (kbd_buffer_head_ != kbd_buffer_tail_ && kbd_ack_ && !(s_[0].cr[0] & CIACRAF_SPMODE)) {
            kbd_ack_ = false;
            s_[0].sdrdata = kbd_buffer_[kbd_buffer_tail_++ % sizeof(kbd_buffer_)];
            s_[0].trigger_int(CIAICRB_SP);
        }
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

    uint8_t active_irq_mask() const
    {
        return (!!(s_[0].icrdata & CIAICRF_IR)) | (!!(s_[1].icrdata & CIAICRF_IR)) << 1;
    }

    void increment_tod_counter(uint8_t cia)
    {
        assert(cia < 2);
        auto& s = s_[cia];
        ++s.counter;
        if (/*(s.icrmask & CIAICRF_ALRM) && */ (s.counter & 0xffffff) == s.alarm) {
            std::cerr << "[CIA] Todo: trigger alarm intterupt? for alarm=$" << hexfmt(s.alarm) << "\n";
            //s.trigger_int(CIAICRB_ALRM);
            assert(0);
        }
    }

    void keyboard_event(bool pressed, uint8_t raw)
    {
        assert(raw <= 0x7f);
        // Bit0: 1=down/0=up, Bit1..7: ~scancore (i.e. bitwise not)
        if (static_cast<uint8_t>(kbd_buffer_head_ - kbd_buffer_tail_) >= sizeof(kbd_buffer_))
            throw std::runtime_error { "Keyboard buffer overflow" }; // FIXME
        kbd_buffer_[(kbd_buffer_head_++) % sizeof(kbd_buffer_)] = (pressed & 1) | (~raw) << 1;
    }

    bool power_led_on() const
    {
        return !(s_[0].port_value(0) & CIAF_LED);
    }

    disk_drive& active_drive()
    {
        const uint8_t bpb = s_[1].port_value(1);
        int drive = max_drives;
        for (int dsk = 0; dsk < max_drives; ++dsk) {
            if (bpb & (1 << (CIAB_DSKSEL0 + dsk)))
                continue;
            if (drive != max_drives)
                throw std::runtime_error { "Multiple drives selected" };
            drive = dsk;
        }
        if (drive >= max_drives || !drives_[drive])
            throw std::runtime_error { "Invalid drive selected" };
        return *drives_[drive];
    }

private:
    memory_handler& mem_handler_;
    rom_area_handler& rom_handler_;
    disk_drive* drives_[max_drives];
    struct state {
        uint8_t ports[2];
        uint8_t ddr[2];
        uint8_t cr[2];
        uint8_t icrmask;
        uint8_t icrdata;
        uint8_t sdrdata;

        uint32_t counter;
        uint32_t counter_latch;
        uint32_t alarm;

        uint16_t timer_latch[2];
        uint16_t timer_val[2];

        uint8_t port_input[2];

        uint8_t port_value(uint8_t port) const
        {
            assert(port < 2);
            return (ports[port] & ddr[port]) | (port_input[port] & ~ddr[port]);
        }

        void trigger_int(uint8_t icrbit)
        {
            assert(icrbit <= CIAICRB_FLG);
            uint8_t mask = 1 << icrbit;
            if (icrmask & mask)
                mask |= CIAICRF_IR;
            icrdata |= mask;
        }

    } s_[2];
    uint8_t kbd_buffer_[8];
    uint8_t kbd_buffer_head_;
    uint8_t kbd_buffer_tail_;
    bool kbd_ack_;

    static constexpr uint32_t latch_active_mask = 0x8000'0000;

    void reset()
    {
        memset(s_, 0, sizeof(s_));
        // Set output pin values to high (since they're connected to pull-ups)
        s_[0].port_input[0] = 0xFF;
        s_[1].port_input[1] = 0xFF;
        s_[1].port_input[0] = 0xFF;
        s_[1].port_input[1] = 0xFF;
        kbd_buffer_head_ = kbd_buffer_tail_ = 0;
        kbd_ack_ = true;
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
        auto& s = s_[idx];
        switch (reg) {
        case pra:
            if (idx == 0) {
                auto& pi = s_[0].port_input[0];
                static_assert(DSKF_ALL == 0xF << CIAB_DSKCHANGE);
                uint8_t disk_state = DSKF_ALL;
                const uint8_t ciab_prb_output = s_[1].port_value(1);
                for (uint8_t dsk = 0; dsk < max_drives; ++dsk) {
                    // Drive selected?
                    if (ciab_prb_output & (1 << (dsk + CIAB_DSKSEL0)))
                        continue;
                    // Present?
                    if (!drives_[dsk])
                        continue;
                    disk_state &= drives_[dsk]->cia_state();
                }
                pi = (pi & ~DSKF_ALL) | (disk_state & DSKF_ALL);
            }
            [[fallthrough]];
        case prb:
            //std::cerr << "[CIA] TODO: Not handling input pins for CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << "\n";
            return s.port_value(reg - pra);
        case ddra:
        case ddrb:
            return s.ddr[reg - ddra];
        case talo:
            return s.timer_val[0] & 0xff;
        case tahi:
            return (s.timer_val[0] >> 8) & 0xff;
        case tblo:
            return s.timer_val[1] & 0xff;
        case tbhi:
            return (s.timer_val[1] >> 8) & 0xff;
        case todlo: {
            const uint8_t val = (s.counter_latch & latch_active_mask ? s.counter_latch : s.counter) & 0xff;
            s.counter_latch = 0; // Latch disabled
            return val;
        }
        case todmid:
            return ((s.counter_latch & latch_active_mask ? s.counter_latch : s.counter) >> 8) & 0xff;
        case todhi:
            s.counter_latch = latch_active_mask | (s.counter & 0xffffff);
            return (s.counter_latch >> 16) & 0xff;
        case sdr:
            return s.sdrdata;
        case icr: {
            const uint8_t val = s.icrdata;
            s.icrdata = 0; // All bits are cleared on read
            return val;
        }
        case cra:
        case crb:
            return s.cr[reg - cra];
        default:
            std::cerr << "[CIA] TODO: Handle read from CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << "\n";
        }
        return 0xFF;
    }

    void handle_write(uint8_t idx, uint8_t reg, uint8_t val)
    {
        assert(idx < 2 && reg < 16);
        auto& s = s_[idx];
        const uint8_t port_a_before = s.port_value(0);
        const uint8_t port_b_before = s.port_value(1);

        switch (reg) {
        case pra:
        case prb:
            s.ports[reg - pra] = val;
            if (idx == 1 && reg == prb)
                recalc_disk(port_b_before);
            break;
        case ddra:
        case ddrb:
            s.ddr[reg - ddra] = val;
            if (idx == 1 && reg == ddrb)
                recalc_disk(port_b_before);
            break;
        case talo:
            s.timer_latch[0] = (s.timer_latch[0] & 0xff00) | val;
            break;
        case tahi:
            s.timer_latch[0] = (s.timer_latch[0] & 0xff) | val << 8;
            if (s.cr[0] & CIACRAF_RUNMODE) {
                // In one-shot mode, a write to timer-high (register 5 for timer A, register 7 for Timer B) will transfer the timer latch to the counter and initiate counting regardless of the start bit.
                s.timer_val[0] = s.timer_latch[0]; // Start timer
                s.cr[0] |= CIACRAF_START;
            }
            break;
        case tblo:
            s.timer_latch[1] = (s.timer_latch[1] & 0xff00) | val;
            break;
        case tbhi:
            s.timer_latch[1] = (s.timer_latch[1] & 0xff) | val << 8;
            if (s.cr[1] & CIACRBF_RUNMODE) {
                s.timer_val[1] = s.timer_latch[1]; // Start timer
                s.cr[1] |= CIACRBF_START;
            }
            break;
        case todlo:
            if (s.cr[1] & CIACRBF_ALARM)
                s.alarm = (s.alarm & 0xffffff00) | val;
            else
                s.counter = (s.counter & 0xffffff00) | val;
            break;
        case todmid:
            if (s.cr[1] & CIACRBF_ALARM)
                s.alarm = (s.alarm & 0xffff00ff) | val << 8;
            else
                s.counter = (s.counter & 0xffff00ff) | val << 8;
            break;
        case todhi:
            if (s.cr[1] & CIACRBF_ALARM)
                s.alarm = (s.alarm & 0xff00ffff) | val << 16;
            else
                s.counter = (s.counter & 0xff00ffff) | val << 16;
            break;
        case sdr:
            if (!(s.cr[0] & CIACRAF_SPMODE))
                throw std::runtime_error { "SR not in output mode?" };
            assert(s.cr[0] & CIACRAF_SPMODE); // SDR must be in output mode
            kbd_ack_ = true;
            s.sdrdata = val;
            break; // Ignore value written (probably keyboard handshake)
        case icr: {
            const bool set = !!(val & CIAICRF_SETCLR);
            auto& r = s.icrmask;
            val &= 0x7f;
            r &= ~val;
            if (set)
                r |= val;
            else
                r &= ~val;
            break;
        }
        case cra:
            assert(!(val & (CIACRAF_PBON | CIACRAF_OUTMODE | CIACRAF_INMODE))); // Not tested
            if (val & CIACRAF_LOAD) {
                val &= ~CIACRAF_LOAD;
                s.timer_val[0] = s.timer_latch[0];
                if (val & CIACRAF_RUNMODE)
                    val |= CIACRAF_START;
            }
            s.cr[reg - cra] = val;
            break;
        case crb:
            assert(!(val & (CIACRBF_PBON | CIACRBF_OUTMODE | CIACRBF_INMODE0 | CIACRBF_INMODE1))); // Not tested
            if (val & CIACRAF_LOAD) {
                val &= ~CIACRAF_LOAD;
                s.timer_val[1] = s.timer_latch[1];
                if (val & CIACRAF_RUNMODE)
                    val |= CIACRAF_START;
            }
            s.cr[reg - cra] = val;
            break;
        default:
            std::cerr << "[CIA] Ignoring write to CIA" << static_cast<char>('A' + idx) << " " << regnames[reg] << " val $" << hexfmt(val) << "\n";
            return;
        }

        if (idx == 0) {
            const uint8_t port_a_after = s.port_value(0);
            const uint8_t port_a_diff = port_a_before ^ port_a_after;
            // OVL changed
            if (port_a_diff & CIAF_OVERLAY)
                rom_handler_.set_overlay(!!(port_a_after & 1));
        }
    }

private:
    void recalc_disk(uint8_t before) {
        const uint8_t after = s_[1].port_value(1);
        const uint8_t diff = after ^ before;
        if (!diff)
            return;
        #if 0
        for (int dsk = 0; dsk < 4; ++dsk) {
            const uint8_t this_drive_change = diff & ~CIAF_ALL_DSKSEL;
            if ((diff & (1<<(CIAB_DSKSEL0+dsk))) && this_drive_change) {
                std::cout << "Change for drive " << dsk << ": " << hexfmt(this_drive_change) << "\n";
            }
        }
        #endif
        for (uint8_t dsk = 0; dsk < 4; ++dsk) {
            if (!drives_[dsk])
                continue;
            auto& d = *drives_[dsk];
            const uint8_t selmask = 1 << (CIAB_DSKSEL0 + dsk);
            if (after & selmask)
                continue;
            if (before & selmask) {
#ifdef FLOPPY_DEBUG
                std::cout << "DF" << (int)dsk << " motor " << (after & CIAF_DSKMOTOR ? "off" : "on") << "\n";
#endif
                d.set_motor(!(after & CIAF_DSKMOTOR));
            }
            if (diff & (CIAF_DSKSIDE | CIAF_DSKDIREC)) {
                // seekdir out -> towards 0
#ifdef FLOPPY_DEBUG
                std::cout << "DF" << (int)dsk << " side=" << (after & CIAF_DSKSIDE ? "lower" : "upper") << " seekdir=" << (after & CIAF_DSKDIREC ? "out" : "in") << "\n";
#endif
                d.set_side_dir(!!(after & CIAF_DSKSIDE), !!(after & CIAF_DSKDIREC));
            }
            if (!(before & CIAF_DSKSTEP) && (after & CIAF_DSKSTEP)) {
                // TODO: Maybe check if it actually goes high again?
#ifdef FLOPPY_DEBUG
                std::cout << "DF" << (int)dsk << " step pulse\n";
#endif
                d.dir_step();
            }

        }
    }
};

cia_handler::cia_handler(memory_handler& mem_handler, rom_area_handler& rom_handler, disk_drive* dfs[max_drives])
    : impl_ { std::make_unique<impl>(mem_handler, rom_handler, dfs) }
{
}

cia_handler::~cia_handler() = default;

void cia_handler::step()
{
    impl_->step();
}

uint8_t cia_handler::active_irq_mask() const
{
    return impl_->active_irq_mask();
}

void cia_handler::increment_tod_counter(uint8_t cia)
{
    impl_->increment_tod_counter(cia);
}

void cia_handler::keyboard_event(bool pressed, uint8_t raw)
{
    impl_->keyboard_event(pressed, raw);
}

bool cia_handler::power_led_on() const
{
    return impl_->power_led_on();
}

disk_drive& cia_handler::active_drive()
{
    return impl_->active_drive();
}