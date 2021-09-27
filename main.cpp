#include <iostream>
#include <stdint.h>
#include <stdexcept>
#include <cassert>
#include <fstream>
#include <chrono>
#include <mutex>
#include <condition_variable>

//#define TRACE_LOG

#ifdef TRACE_LOG
#include <sstream>
#endif

#include "ioutil.h"
#include "instruction.h"
#include "disasm.h"
#include "memory.h"
#include "cia.h"
#include "custom.h"
#include "cpu.h"
#include "disk_drive.h"
#include "gui.h"
#include "debug.h"
#include "wavedev.h"
#include "asm.h"

namespace {

std::vector<std::string> split_line(const std::string& line)
{
    std::vector<std::string> args;
    const size_t l = line.length();
    size_t i = 0;
    while (i < l && isspace(line[i]))
        ++i;
    if (i == l)
        return {};
    for (size_t j = i + 1; j < l;) {
        if (isspace(line[j])) {
            args.push_back(line.substr(i, j - i));
            while (isspace(line[j]) && j < l)
                ++j;
            i = j;
        } else {
            ++j;
        }
    }
    if (i < l)
        args.push_back(line.substr(i));
    return args;
}

std::pair<bool, uint32_t> from_hex(const std::string& s)
{
    if (s.empty() || s.length() > 8)
        return { false, 0 };
    uint32_t val = 0;
    for (const char c : s) {
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return { false, 0 };
        val = val << 4 | d;
    }
    return { true, val };
}

std::tuple<bool, uint32_t, uint32_t> get_addr_and_lines(const std::vector<std::string>& args, uint32_t def_addr, uint32_t def_lines)
{
    uint32_t addr = def_addr, lines = def_lines;

    if (args.size() > 1) {
        if (args.size() > 1) {
            auto fh = from_hex(args[1]);
            if (!fh.first) {
                std::cout << "Invalid address \"" << args[1] << "\"\n";
                return { false, def_addr, def_lines };
            }
            addr = fh.second;
            if (args.size() > 2) {
                if (fh = from_hex(args[2]); fh.first)
                    lines = fh.second;
                else {
                    std::cout << "Invalid lines \"" << args[2] << "\"\n";
                    return { false, def_addr, def_lines };
                }
            }
        }
    }

    return { true, addr, lines };
}

uint32_t disasm_stmts(memory_handler& mem, uint32_t start, uint32_t count)
{
    if (start & 1) {
        std::cout << "PC at odd address $" << hexfmt(start) << "\n";
        return 0;
    }
    uint32_t addr = start;
    while (count--) {
        uint16_t iw[max_instruction_words];
        const auto pc = addr;
        iw[0] = mem.read_u16(addr);
        addr += 2;
        for (uint16_t i = 1; i < instructions[iw[0]].ilen; ++i) {
            iw[i] = mem.read_u16(addr);
            addr += 2;
        }
        disasm(std::cout, pc, iw, max_instruction_words);
        std::cout << "\n";
    }
    return addr - start;
}

uint32_t copper_disasm(memory_handler& mem, uint32_t start, uint32_t count)
{
    if (start & 1) {
        std::cout << "Odd address $" << hexfmt(start) << "\n";
        return 0;
    }

    const auto indent = std::string(24, ' ');
    uint32_t addr = start;
    while (count--) {
        const uint16_t i1 = mem.read_u16(addr);
        const uint16_t i2 = mem.read_u16(addr+2);
        std::cout << hexfmt(addr) << ": " << hexfmt(i1) << " " << hexfmt(i2) << "\t;  ";
        if (i1 & 1) { // wait or skip
            const auto vp = (i1 >> 8) & 0xff;
            const auto hp = i1 & 0xfe;
            const auto ve = 0x80 | ((i2 >> 8) & 0x7f);
            const auto he = i2 & 0xfe;
            //} else if ((s_.vpos & ve) > (vp & ve) || ((s_.vpos & ve) == (vp & ve) && ((s_.hpos >> 1) & he) >= (hp & he))) {
            std::cout << (i2 & 1 ? "Skip if" : "Wait for") << " vpos >= $" << hexfmt(vp & ve, 2) << " and hpos >= $" << hexfmt(hp & he, 2) << "\n";
            std::cout << indent << ";  VP " << hexfmt(vp, 2) << ", VE " << hexfmt(ve & 0x7f, 2) << "; HP " << hexfmt(hp, 2) << ", HE " << hexfmt(he, 2) << "; BFD " << !!(i2 & 0x8000) << "\n";
        } else {
            std::cout << custom_regname(i1) << " := $" << hexfmt(i2) << "\n";
        }

        addr += 4;

        if (i1 == 0xffff && i2 == 0xfffe) {
            std::cout << indent << ";  End of copper list\n";
            break;
        }

    }
    return addr - start;
}

} // unnamed namespace


#if 0
void rom_tag_scan(const std::vector<uint8_t>& rom)
{
    const uint32_t rom_base = static_cast<uint32_t>(0x1000000 - rom.size());
    constexpr uint32_t romtag = 0x4AFC;

    /*
FC00B6  4AFC                        RTC_MATCHWORD   (start of ROMTAG marker)
FC00B8  00FC00B6                    RT_MATCHTAG     (pointer RTC_MATCHWORD)
FC00BC  00FC323A                    RT_ENDSKIP      (pointer to end of code)
FC00C0  00                          RT_FLAGS        (no flags)
FC00C1  21                          RT_VERSION      (version number)
FC00C2  09                          RT_TYPE         (NT_LIBRARY)
FC00C3  78                          RT_PRI          (priority = 126)
FC00C4  00FC00A8                    RT_NAME         (pointer to name)
FC00C8  00FC0018                    RT_IDSTRING     (pointer to ID string)
FC00CC  00FC00D2                    RT_INIT         (execution address)* 
    */
    
    for (uint32_t offset = 0; offset < rom.size() - 6; offset += 2) {
        if (get_u16(&rom[offset]) != romtag)
            continue;
        if (get_u32(&rom[offset + 2]) != offset + rom_base)
            continue;
        std::cout << "Found tag at $" << hexfmt(rom_base + offset) << "\n";
        const uint32_t endskip = get_u32(&rom[offset + 6]);
        const uint8_t flags = rom[offset + 10];
        const uint8_t version = rom[offset + 11];
        const uint8_t type = rom[offset + 12];
        const uint8_t priority = rom[offset + 13];
        const uint32_t name_addr = get_u32(&rom[offset + 14]);
        const uint32_t id_addr = get_u32(&rom[offset + 18]);
        const uint32_t init_addr = get_u32(&rom[offset + 22]);

        std::cout << "  End=$" << hexfmt(endskip) << "\n";
        std::cout << "  Flags=$" << hexfmt(flags) << " version=$" << hexfmt(version) << " type=$" << hexfmt(type) << " priority=$" << hexfmt(priority) << "\n";
        std::cout << "  Name='" << reinterpret_cast<const char*>(&rom[name_addr - rom_base]) << "'\n";
        std::cout << "  ID='" << reinterpret_cast<const char*>(&rom[name_addr - rom_base]) << "'\n";
        std::cout << "  Init=$" << hexfmt(init_addr) << "\n";
    }
}
#endif

struct command_line_arguments {
    std::string rom;
    std::string df0;
    std::string df1;
    uint32_t chip_size;
    uint32_t slow_size;
    uint32_t fast_size;
    bool test_mode;
    bool nosound;
};

void usage(const std::string& msg)
{
    std::cerr << "Command line arguments: [-rom rom-file] [-df0 adf-file] [-chip size] [-slow size] [-fast size] [-testmode] [-nosound] [-help]\n";
    throw std::runtime_error { msg };
}

command_line_arguments parse_command_line_arguments(int argc, char* argv[])
{
    command_line_arguments args {};
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            auto get_string_arg = [&](const std::string& name, std::string& arg) {
                if (name.compare(&argv[i][1]) != 0)
                    return false;
                if (++i == argc)
                    usage("Missing argument for " + name);
                if (!arg.empty())
                    usage(name + " specified multiple times");
                arg = argv[i];
                return true;
            };
            auto get_size_arg = [&](const std::string& name, uint32_t& arg, uint32_t max_size) {
                std::string s = arg ? " " : "";// Non-empty if already specified so get_string_arg will warn
                if (!get_string_arg(name, s))
                    return false;
                
                char* ep = nullptr;
                uint32_t mult = 1;
                const auto num = strtoul(s.c_str(), &ep, 10);
                if (*ep == 'k' || *ep == 'K') {
                    ++ep;
                    mult = 1<<10;
                } else if (*ep == 'm' || *ep == 'M') {
                    ++ep;
                    mult = 1 << 20;
                }
                if (*ep)
                    usage("Invalid number for " + name);
                if (!num || num >= 16 << 20 || static_cast<uint64_t>(num) * mult > max_size || ((num * mult) & ((256 << 10) - 1)))
                    usage("Invalid or out of range number for mult");
                arg = num * mult;
                return true;
            };

            if (get_string_arg("df0", args.df0))
                continue;
            else if (get_string_arg("df1", args.df1))
                continue;
            else if (get_string_arg("rom", args.rom))
                continue;
            else if (get_size_arg("chip", args.chip_size, max_chip_size))
                continue;
            else if (get_size_arg("slow", args.slow_size, max_slow_size))
                continue;
            else if (get_size_arg("fast", args.fast_size, max_fast_size))
                continue;
            else if (!strcmp(&argv[i][1], "help"))
                usage("");
            else if (!strcmp(&argv[i][1], "testmode")) {
                args.test_mode = true;
                args.nosound = true;
                continue;
            } else if (!strcmp(&argv[i][1], "nosound")) {
                args.nosound = true;
                continue;
            }
        }
        usage("Unrecognized command line parameter: " + std::string { argv[i] });
    }
    if (args.rom.empty()) {
        args.rom = "rom.bin";
    }
    if (args.chip_size == 0 && args.slow_size == 0 && args.fast_size == 0) {
        args.chip_size = 512 << 10;
        args.slow_size = 512 << 10;
    } else if (args.chip_size == 0) {
        args.chip_size = 512 << 10;
    }
    if (args.chip_size < 256 << 10) {
        usage("Invalid configuration (chip RAM too small)");
    }
    return args;
}

class autoconf_device {
public:
    static constexpr uint8_t ERT_ZORROII        = 0xc0;
    static constexpr uint8_t ERTF_MEMLIST       = 1 << 5;
    static constexpr uint8_t ERTF_DIAGVALID     = 1 << 4;
    static constexpr uint8_t ERTF_CHAINEDCONFIG = 1 << 3;

    struct board_config {
        uint8_t type;
        uint32_t size;
        uint8_t product_number;
        uint16_t hw_manufacturer;
        uint32_t serial_no;
        uint16_t rom_vector_offset;
    };

    uint8_t read_config_byte(uint8_t offset) const
    {
        assert(offset < sizeof(conf_data_));
        assert(mode_ == mode::autoconf);
        return conf_data_[offset];
    }

    void write_config_byte(uint8_t offset, uint8_t val)
    {
        assert(mode_ == mode::autoconf);
        throw std::runtime_error { "TODO: Write config byte $" + hexstring(offset) + " val $" + hexstring(val) };
    }

    void shutup()
    {
        assert(mode_ == mode::autoconf);
        std::cout << "[AUTOCONF] " << desc() << " shutting up\n";
        mode_ = mode::shutup;
    }

    void activate(uint8_t base)
    {
        assert(mode_ == mode::autoconf);
        mode_ = mode::active;
        std::cout << "[AUTOCONF] " << desc() << " activating at $" << hexfmt(base << 16, 8) << "\n";
        mem_handler_.register_handler(area_handler_, base << 16, config_.size);
    }

protected:
    explicit autoconf_device(memory_handler& mem_handler, memory_area_handler& area_handler, const board_config& config)
        : mem_handler_ { mem_handler }
        , area_handler_ { area_handler }
        , config_ { config }
    {
        memset(conf_data_, 0, sizeof(conf_data_));
        assert((config.type & 0xc7) == 0);
        /* $00/$02 */ conf_data_[0] = ERT_ZORROII | config.type | board_size(config.size);
        /* $04/$06 */ conf_data_[1] = config.product_number;
        /* $10-$18 */ put_u16(&conf_data_[4], config.hw_manufacturer);
        /* $18-$28 */ put_u32(&conf_data_[6], config.serial_no);
        /* $28-$30 */ put_u16(&conf_data_[10], config.rom_vector_offset);
    }

private:
    enum class mode { autoconf, shutup, active } mode_ = mode::autoconf;
    memory_handler& mem_handler_;
    memory_area_handler& area_handler_;
    const board_config config_;
    uint8_t conf_data_[12];

    std::string desc() const
    {
        return hexstring(config_.product_number) + "/" + hexstring(config_.hw_manufacturer) + "/" + hexstring(config_.serial_no);
    }

    static const uint8_t board_size(uint32_t size)
    {
        switch (size) {
        case 64 << 10:
            return 0b001;
        case 128 << 10:
            return 0b010;
        case 256 << 10:
            return 0b011;
        case 512 << 10:
            return 0b100;
        case 1 << 20:
            return 0b101;
        case 2 << 20:
            return 0b110;
        case 4 << 20:
            return 0b111;
        case 8 << 20:
            return 0b000;
        default:
            throw std::runtime_error { "Unsupported autoconf board size $" + hexstring(size) };
        }
    }
};

class fastmem_handler : public autoconf_device, public ram_handler {
public:
    explicit fastmem_handler(memory_handler& mem_handler, uint32_t size)
        : autoconf_device { mem_handler, *this, make_config(size) }
        , ram_handler { size }
    {
    }

private:
    static constexpr board_config make_config(uint32_t size)
    {
        return board_config {
            .type = ERTF_MEMLIST,
            .size = size,
            .product_number = 0x12,
            .hw_manufacturer = 0x1234,
            .serial_no = 0x12345678,
            .rom_vector_offset = 0,
        };
    }
};


// For now: only handle one board (for fast memory)
class autoconf_handler : public memory_area_handler {
public:
    explicit autoconf_handler(memory_handler& mem_handler)
    {
        mem_handler.register_handler(*this, base, 0x10000);
    }

    void add_device(autoconf_device& dev)
    {
        devices_.push_back(&dev);
    }

private:
    static constexpr uint32_t base = 0xe80000;
    std::vector<autoconf_device*> devices_;
    uint8_t low_addr_hold_ = 0;
    bool has_low_addr_ = 0;

    void remove_device()
    {
        assert(!devices_.empty());
        has_low_addr_ = false;
        devices_.pop_back();
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        if (!(offset & 1)) {
            if (offset < 0x30) {
                if (devices_.empty()) {
                    return 0xff;
                }
                auto b = devices_.back()->read_config_byte(static_cast<uint8_t>(offset >> 2));
                if (offset & 2)
                    b <<= 4;
                else
                    b &= 0xf0;
                return offset < 4 ? b : static_cast<uint8_t>(~b);
            } else if (offset < 0x40) {
                return 0xff;
            } else if (offset == 0x40 || offset == 0x42) {
                // Interrupt pending register - Not inverted
                return 0;             
            }
        }

        std::cerr << "[AUTOCONF] Unhandled read offset $" << hexfmt(offset) << "\n";
        return 0xff;
    }

    uint16_t read_u16(uint32_t addr, uint32_t offset) override
    {
        return read_u8(addr, offset) << 8 | read_u8(addr, offset+1);
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        if (!devices_.empty()) {
            auto& dev = *devices_.back();
            if (offset == 0x48) {
                if (!has_low_addr_)
                    std::cerr << "[AUTOCONF] Warning high address written without low address val=$" << hexfmt(val) << "\n";
                dev.activate(static_cast<uint16_t>((val & 0xf0) | low_addr_hold_));
                remove_device();
                return;
            } else if (offset == 0x4a) {
                if (has_low_addr_)
                    std::cerr << "[AUTOCONF] Warning already has low address ($" << hexfmt(low_addr_hold_) << ") got $" << hexfmt(val) << "\n";
                has_low_addr_ = true;
                low_addr_hold_ = val >> 4;
                return;
            } else if (offset == 0x4c) {
                // shutup
                dev.shutup();
                remove_device();
                return;
            }
        } else {
            std::cerr << "[AUTCONF] Write without device\n";
        }

        std::cerr << "[AUTOCONF] Unhandled write offset $" << hexfmt(offset) << " val=$" << hexfmt(val) << "\n";
    }

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        std::cerr << "[AUTOCONF] Unhandled write offset $" << hexfmt(offset) << " val=$" << hexfmt(val) << "\n";
    }
};

class builder {
public:
    explicit builder()
    {
    }

    uint32_t ofs() const
    {
        return static_cast<uint32_t>(data_.size());
    }

    const std::vector<uint8_t>& data() const
    {
        return data_;
    }

    void u8(uint8_t val)
    {
        data_.push_back(val);
    }

    void u16(uint16_t val)
    {
        data_.push_back(static_cast<uint8_t>(val >> 8));
        data_.push_back(static_cast<uint8_t>(val));
    }

    void u32(uint32_t val)
    {
        data_.push_back(static_cast<uint8_t>(val >> 24));
        data_.push_back(static_cast<uint8_t>(val >> 16));
        data_.push_back(static_cast<uint8_t>(val >> 8));
        data_.push_back(static_cast<uint8_t>(val));
    }

    void str(const std::string& str)
    {
        data_.insert(data_.end(), str.begin(), str.end());
    }

    void even()
    {
        if (data_.size() & 1)
            data_.push_back(0);
    }

private:
    std::vector<uint8_t> data_;
};


class test_board : public memory_area_handler, public autoconf_device {
public:
    explicit test_board(memory_handler& mem)
        : autoconf_device { mem, *this, config }
    {
        const char* rom_code = R"(

RomStart=0

VERSION=0
REVISION=1
RTC_MATCHWORD=$4afc
RTF_AUTOINIT=$80	; rt_Init points to data structure
RTF_COLDSTART=$01
NT_DEVICE=3

INITBYTE=$e000
INITWORD=$d000
INITLONG=$c000

LIB_FLAGS=$0E
LIB_NEGSIZE=$10
LIB_POSSIZE=$12
LIB_VERSION=$14
LIB_REVISION=$16
LIB_IDSTRING=$18
LIB_SUM=$1C
LIB_OPENCNT=$20
LIB_SIZE=$22

LIBF_SUMMING=$01	; we are currently checksumming
LIBF_CHANGED=$02	; we have just changed the lib
LIBF_SUMUSED=$04	; set if we should bother to sum
LIBF_DELEXP=$08     ; delayed expunge

LN_SUCC=$00
LN_PRED=$04
LN_TYPE=$08
LN_PRI=$09
LN_NAME=$0A
LN_SIZE=$0E

dev_SysLib=$22  ; LIB_SIZE
dev_SegList=$26
dev_Base=$2A

_LVOOpenLibrary=-552
_LVOCloseLibrary=-414

DiagStart:
            dc.b $90                 ; da_Config = DAC_WORDWIDE|DAC_CONFIGTIME
            dc.b $00                 ; da_Flags = 0
            dc.w EndCopy-DiagStart   ; da_Size (in bytes)
            dc.w DiagEntry-DiagStart ; da_DiagPoint (0 = none)
            dc.w BootEntry-DiagStart ; da_BootPoint
            dc.w DevName-DiagStart   ; da_Name (offset of ID string)
            dc.w $0000               ; da_Reserved01
            dc.w $0000               ; da_Reserved02

RomTag:
            dc.w    RTC_MATCHWORD      ; UWORD RT_MATCHWORD
rt_Match:   dc.l    Romtag-DiagStart   ; APTR  RT_MATCHTAG
rt_End:     dc.l    EndCopy-DiagStart  ; APTR  RT_ENDSKIP
            dc.b    RTF_AUTOINIT+RTF_COLDSTART ; UBYTE RT_FLAGS
            dc.b    VERSION            ; UBYTE RT_VERSION
            dc.b    NT_DEVICE          ; UBYTE RT_TYPE
            dc.b    20                 ; BYTE  RT_PRI
rt_Name:    dc.l    DevName-DiagStart  ; APTR  RT_NAME
rt_Id:      dc.l    IdString-DiagStart ; APTR  RT_IDSTRING
rt_Init:    dc.l    Init-DiagStart     ; APTR  RT_INIT


DevName:    dc.b 'hello.device', 0
IdString:   dc.b 'hello ',VERSION+48,'.',REVISION+48, 0
    even

Init:
            dc.l    $100 ; data space size
            dc.l    funcTable-DiagStart
            dc.l    dataTable-DiagStart
            dc.l    initRoutine-RomStart

funcTable:
            dc.l   Open-RomStart
            dc.l   Close-RomStart
            dc.l   Expunge-RomStart
            dc.l   Null-RomStart	    ;Reserved for future use!
            dc.l   BeginIO-RomStart
            dc.l   AbortIO-RomStart
            dc.l   -1

dataTable:
            DC.W INITBYTE, LN_TYPE
            DC.B NT_DEVICE, 0
            DC.W INITLONG, LN_NAME
dt_Name:    DC.L DevName-DiagStart
            DC.W INITBYTE, LIB_FLAGS
            DC.B LIBF_SUMUSED+LIBF_CHANGED, 0
            DC.W INITWORD, LIB_VERSION, VERSION
            DC.W INITWORD, LIB_REVISION, REVISION
            DC.W INITLONG, LIB_IDSTRING
dt_Id:      DC.L IdString-DiagStart
            DC.W 0   ; terminate list

BootEntry:
            moveq   #$66,d0
            bra BootEntry
            rts

DiagEntry:
            ;lea     patchTable-RomStart(a0), a1
            move.l  #patchTable, a1
            sub.l   #RomStart, a1
            add.l   a0, a1
            move.l  a2, d1
dloop:
            move.w  (a1)+, d0
            bmi.b   bpatches
            add.l   d1, 0(a2,d0.w)
            bra.b   dloop
bpatches:
            move.l  a0, d1
bloop:
            move.w  (a1)+, d0
            bmi.b   endpatches
            add.l   d1, 0(a2,d0.w)
            bra.b   bloop
endpatches:
            moveq   #1,d0 ; success
            rts
EndCopy:

patchTable:
; Word offsets into Diag area where pointers need Diag copy address added
            dc.w   rt_Match-DiagStart
            dc.w   rt_End-DiagStart
            dc.w   rt_Name-DiagStart
            dc.w   rt_Id-DiagStart
            dc.w   rt_Init-DiagStart
            dc.w   Init-DiagStart+$4
            dc.w   Init-DiagStart+$8
            dc.w   dt_Name-DiagStart
            dc.w   dt_Id-DiagStart
            dc.w   -1
; Word offsets into Diag area where pointers need boardbase+ROMOFFS added
            dc.w   Init-DiagStart+$c
            dc.w   funcTable-DiagStart+$00
            dc.w   funcTable-DiagStart+$04
            dc.w   funcTable-DiagStart+$08
            dc.w   funcTable-DiagStart+$0C
            dc.w   funcTable-DiagStart+$10
            dc.w   funcTable-DiagStart+$14
            dc.w   -1

; d0 = device pointer, a0 = segment list
; a6 = exec base
initRoutine:
            movem.l  d1-d7/a0-a6,-(sp)
            move.l   d0, a5   ; a5=device pointer
            move.l   a6, dev_SysLib(a5)
            move.l   a0, dev_SegList(a5)

            ; Open expansion library
            lea     ExLibName(pc), a1
            moveq   #0, d0
            jsr     _LVOOpenLibrary(a6)
            tst.l   d0
            beq     irError
            move.l  d0, a4    ; a4=expansion library
        
            ; Close expansion library
            move.l  a4, a1
            jsr     _LVOCloseLibrary(a6)

            move.l   a5, d0
            bra.b    irExit
irError:
            moveq    #0, d0
irExit:
            movem.l  (sp)+, d1-d7/a0-a6
            rts

ExLibName:  dc.b 'expansion.library', 0
            even

Open:
            moveq   #1, d0
            bra.b   Open

Close:
            moveq   #2, d0
            bra.b   Close

Expunge:
            moveq   #3, d0
            bra.b   Expunge

Null:
            moveq   #4, d0
            bra.b   Null

BeginIO:
            moveq   #5, d0
            bra.b   BeginIO

AbortIO:
            moveq   #6, d0
            bra.b   AbortIO

)";

        rom_ = assemble(config.rom_vector_offset, rom_code);
    }

private:
    static constexpr board_config config {
        .type = ERTF_DIAGVALID,
        .size = 64 << 10,
        .product_number = 0x88,
        .hw_manufacturer = 1337,
        .serial_no = 1,
        .rom_vector_offset = 16,
    };
    std::vector<uint8_t> rom_;


    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset < config.rom_vector_offset + rom_.size()) {
            return rom_[offset - config.rom_vector_offset];
        }

        std::cerr << "Test board: Read U8 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset + 1 < config.rom_vector_offset + rom_.size()) {
            offset -= config.rom_vector_offset;
            return static_cast<uint16_t>(rom_[offset] << 8 | rom_[offset+1]);
        }
        std::cerr << "Test board: Read U16 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        std::cerr << "Test board: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
    }

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        std::cerr << "Test board: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
    }
};

int main(int argc, char* argv[])
{
    constexpr uint32_t testmode_stable_frames = 5*50;
    constexpr uint32_t testmode_min_instructions = 5'000'000;

    try {
        const auto cmdline_args = parse_command_line_arguments(argc, argv);
        disk_drive df0 {"DF0:"}, df1 {"DF1:"};
        disk_drive* drives[max_drives] = { &df0, &df1 };
        std::unique_ptr<ram_handler> slow_ram;
        std::unique_ptr<fastmem_handler> fast_ram;
        memory_handler mem { cmdline_args.chip_size };
        rom_area_handler rom { mem, read_file(cmdline_args.rom) };
        cia_handler cias { mem, rom, drives };
        custom_handler custom { mem, cias, slow_base + cmdline_args.slow_size };
        autoconf_handler autoconf { mem };
        std::vector<uint32_t> testmode_data;
        uint32_t testmode_cnt = 0;

        if (cmdline_args.slow_size) {
            slow_ram = std::make_unique<ram_handler>(cmdline_args.slow_size);
            mem.register_handler(*slow_ram, slow_base, cmdline_args.slow_size);
        }
        if (cmdline_args.fast_size) {
            fast_ram = std::make_unique<fastmem_handler>(mem, cmdline_args.fast_size);
            autoconf.add_device(*fast_ram);
        }

        test_board b { mem };
        autoconf.add_device(b);

        m68000 cpu { mem };

        if (!cmdline_args.df0.empty())
            df0.insert_disk(read_file(cmdline_args.df0));
        if (!cmdline_args.df1.empty())
            df1.insert_disk(read_file(cmdline_args.df1));

        std::cout << "Memory configuration: Chip: " << (cmdline_args.chip_size >> 10) << " KB, Slow: " << (cmdline_args.slow_size >> 10) << " KB, Fast: " << (cmdline_args.fast_size >> 10) << " KB\n";

        if (cmdline_args.test_mode) {
            std::cout << "Testmode! Min. instructions=" << testmode_min_instructions << " frames=" << testmode_stable_frames << "\n";
            testmode_data.resize(graphics_width * graphics_height);
        }

        //rom_tag_scan(rom.rom());

        // Serial data handler
        std::vector<uint8_t> serdata;
        char escape_sequence[32];
        uint8_t escape_sequence_pos = 0;
        uint8_t crlf = 0;
        custom.set_serial_data_handler([&]([[maybe_unused]] uint8_t numbits, uint8_t data) {
            assert(numbits == 8);
            if (data == 0x1b) {
                assert(escape_sequence_pos == 0);
                escape_sequence[escape_sequence_pos++] = 0x1b;
                return;
            } else if (escape_sequence_pos) {
                if (escape_sequence_pos >= sizeof(escape_sequence)) {
                    // Overflow
                    assert(0);
                    escape_sequence_pos = 0;
                    return;
                }
                escape_sequence[escape_sequence_pos++] = data;
                
                if (isalpha(data)) {
                    if (data == 'm') {
                        // ESC CSI 'n' m -> SGR (Select Graphic Rendition)
                        escape_sequence_pos = 0;
                        return;
                    } else if (data == 'H') {
                        // ESC CSI 'n' ; 'm' H -> Moves the cursor to row n, column m
                        escape_sequence_pos = 0;
                        return;
                    }
                    std::cout << "TODO: check escape sequence 'ESC" << std::string(escape_sequence + 1, escape_sequence_pos - 1) << "'\n";
                    escape_sequence_pos = 0;
                }
                return;
            }
            //std::cout << "Serdata: $" << hexfmt(data) << ": " << (char)(isprint(data) ? data : ' ') << "\n";
            if (data == '\r') {
                if (crlf & 1) {
                    // LF CR
                    serdata.push_back('\r');
                    serdata.push_back('\n');
                    crlf = 0;
                    return;
                }
                crlf |= 2;
                return;
            } else if (data == '\n') {
                if (crlf & 2) {
                    // CR LF
                    serdata.push_back('\r');
                    serdata.push_back('\n');
                    crlf = 0;
                    return;
                }
                crlf |= 1;
                return;
            }
            if (crlf) {
                // Plain CR or LF
                assert(crlf == 1 || crlf == 2);
                serdata.push_back('\r');
                serdata.push_back('\n');
                crlf = 0;
            }
            serdata.push_back(data ? data : ' ');
            });


        gui g { graphics_width, graphics_height, std::array<std::string, 4>{ cmdline_args.df0, cmdline_args.df1, "", "" } };
        auto serdata_flush = [&g, &serdata]() {
            if (!serdata.empty()) {
                g.serial_data(serdata);
                serdata.clear();
            }
        };

        df0.set_disk_activity_handler([](uint8_t track, bool write) {
            std::cout << "DF0: " << (write ? "Write" : "Read") << " track $" << hexfmt(track) << "\n";
        });
        df1.set_disk_activity_handler([](uint8_t track, bool write) {
            std::cout << "DF1: " << (write ? "Write" : "Read") << " track $" << hexfmt(track) << "\n";
        });

#ifdef TRACE_LOG
        std::ostringstream oss;
        constexpr size_t pc_log_size = 0x1000;
        std::string pc_log[pc_log_size];
        size_t pc_log_pos = 0;
        constexpr size_t trace_start_inst = 200'000'000;
#endif

        const unsigned steps_per_update = 1000000;
        unsigned steps_to_update = 0;
        std::vector<gui::event> events;
        std::vector<uint8_t> pending_disk;
        uint8_t pending_disk_drive = 0xff;
        uint32_t disk_chosen_countdown = 0;
        constexpr uint32_t invalid_pc = ~0U;
        enum {wait_none, wait_next_inst, wait_exact_pc, wait_non_rom_pc, wait_vpos} wait_mode = wait_none;
        uint32_t wait_arg = 0;
        std::vector<uint32_t> breakpoints;
        bool debug_mode = false;
        custom_handler::step_result custom_step {};
        m68000::step_result cpu_step {};
        std::unique_ptr<std::ofstream> trace_file;
        bool new_frame = false;
        bool cpu_active = false;
        uint32_t cycles_todo = 0;
        uint32_t idle_count = 0;
        std::mutex audio_mutex_;
        std::condition_variable audio_buffer_ready_cv;
        std::condition_variable audio_buffer_played_cv;
        int16_t audio_buffer[2][audio_buffer_size];
        bool audio_buffer_ready[2] = { false, false };
        int audio_next_to_play = 0;
        int audio_next_to_fill = 0;

        cpu.set_cycle_handler([&](uint8_t cycles) {
            assert(cpu_active);
            cycles_todo += cycles;
        });

        auto active_debugger = [&]() {
            g.set_active(false);
            debug_mode = true;
        };

        std::unique_ptr<wavedev> audio;

        //#define WRITE_SOUND
        #ifdef WRITE_SOUND
        std::ofstream sound_out { "c:/temp/sound.raw" };
        #endif

        auto cstep = [&](bool cpu_waiting) {
            const bool cpu_was_active = cpu_active;
            cpu_active = false;
            custom_step = custom.step(cpu_waiting);
            cpu_active = cpu_was_active;
            if (wait_mode == wait_vpos && wait_arg == (static_cast<uint32_t>(custom_step.vpos) << 9 | custom_step.hpos)) {
                active_debugger();
                wait_mode = wait_none;
            } else if (!(custom_step.vpos | custom_step.hpos)) {
                new_frame = true;
                
                if (audio)
                {
                    std::unique_lock<std::mutex> lock { audio_mutex_ };
                    if (audio_buffer_ready[audio_next_to_fill]) {
                        audio_buffer_played_cv.wait(lock, [&]() { return !audio_buffer_ready[audio_next_to_fill]; });
                    }
                    memcpy(audio_buffer[audio_next_to_fill], custom_step.audio, sizeof(audio_buffer[audio_next_to_fill]));
                    audio_buffer_ready[audio_next_to_fill] = true;
                    audio_next_to_fill = !audio_next_to_fill;
                }
                audio_buffer_ready_cv.notify_one();
                #ifdef WRITE_SOUND
                for (int i = 0; i < audio_samples_per_frame; ++i) {
                    sound_out.write((const char*)&custom_step.audio[i * 2], 2);
                }
                #endif
                #if 0
                static auto last_time = std::chrono::high_resolution_clock::now();
                static int frame_cnt = 0;
                if (++frame_cnt == 100) {
                    auto now = std::chrono::high_resolution_clock::now();
                    const auto t = std::chrono::duration_cast<std::chrono::microseconds>(now - last_time).count();
                    std::cout << "FPS: " << frame_cnt * 1e6 / t << "\n"; 
                    last_time = now;
                    frame_cnt = 0;
                }
                #endif
            }
        };

        auto do_all_custom_cylces = [&]() {
            while (cycles_todo) {
                cstep(false);
                --cycles_todo;
            }
        };

        if (!cmdline_args.nosound) {
            audio = std::make_unique<wavedev>(audio_sample_rate, audio_buffer_size, [&](int16_t* buf, size_t sz) {
                static_assert(2 * audio_samples_per_frame == audio_buffer_size);
                assert(sz == audio_samples_per_frame);
                {
                    std::unique_lock<std::mutex> lock { audio_mutex_ };
                    if (!audio_buffer_ready[audio_next_to_play]) {
                        std::cerr << "Audio buffer not ready!\n";
                        audio_buffer_ready_cv.wait(lock, [&]() { return audio_buffer_ready[audio_next_to_play]; });
                    }
                    memcpy(buf, audio_buffer[audio_next_to_play], sz * 2 * sizeof(int16_t));
                    audio_buffer_ready[audio_next_to_play] = false;
                    audio_next_to_play = !audio_next_to_play;
                }
                audio_buffer_played_cv.notify_one();
            });
        }

        // Make sure audio stops
        struct at_exit {
            explicit at_exit(std::function<void()> ae)
                : ae_ { ae }
            {
                assert(ae_);
            }

            ~at_exit()
            {
                ae_();
            }

        private:
            std::function<void()> ae_;
        } kill_audio([&]() {
            std::unique_lock<std::mutex> lock { audio_mutex_ };
            audio_buffer_ready[0] = true;
            audio_buffer_ready[1] = true;
            audio_buffer_ready_cv.notify_all();
        });



        g.set_on_pause_callback([&](bool pause) {
            if (audio)
                audio->set_paused(pause);
        });

        // Temp:
        //debug_flags |= debug_flag_audio;

        constexpr uint32_t min_rom_addr = 0x00e0'0000;
        mem.set_memory_interceptor([&](uint32_t addr, uint32_t /*data*/, uint8_t size, bool /*write*/) {
            if (!cpu_active)
                return;
            assert(size == 1 || size == 2); (void)size;
            if (addr >= min_rom_addr || (addr >= fast_base && addr < fast_base + max_fast_size)) {
                cycles_todo += 4;
                return;
            }
            do_all_custom_cylces();
            while (!custom_step.free_chip_cycle) {
                cstep(true);
            } 
            cycles_todo = 4;
        });

        //cpu.trace(&std::cout);
        breakpoints.push_back(0x00e900fa);

        for (bool quit = false; !quit;) {
            try {
                if (!events.empty()) {
                    auto evt = events[0];
                    events.erase(events.begin());
                    switch (evt.type) {
                    case gui::event_type::quit:
                        quit = true;
                        break;
                    case gui::event_type::keyboard:
                        cias.keyboard_event(evt.keyboard.pressed, evt.keyboard.scancode);
                        break;
                    case gui::event_type::mouse_button:
                        if (evt.mouse_button.left)
                            cias.set_lbutton_state(evt.mouse_button.pressed);
                        else
                            custom.set_rbutton_state(evt.mouse_button.pressed);
                        break;
                    case gui::event_type::mouse_move:
                        custom.mouse_move(evt.mouse_move.dx, evt.mouse_move.dy);
                        break;
                    case gui::event_type::disk_inserted:
                        assert(evt.disk_inserted.drive < max_drives && drives[evt.disk_inserted.drive]);
                        std::cout << drives[evt.disk_inserted.drive]->name() << " Ejecting\n";
                        drives[evt.disk_inserted.drive]->insert_disk(std::vector<uint8_t>()); // Eject any existing disk
                        if (evt.disk_inserted.filename[0]) {
                            disk_chosen_countdown = 25; // Give SW (e.g. Defender of the Crown) time to recognize that the disk has changed
                            std::cout << "Reading " << evt.disk_inserted.filename << "\n";
                            pending_disk = read_file(evt.disk_inserted.filename);
                            pending_disk_drive = evt.disk_inserted.drive;
                        }
                        break;
                    case gui::event_type::debug_mode:
                        debug_mode = true;
                        break;
                    default:
                        assert(0);
                    }
                }

                if (debug_mode) {
                    const auto& s = cpu.state();
                    g.set_debug_memory(mem.ram(), custom.get_regs());
                    g.set_debug_windows_visible(true);
                    g.update_image(custom_step.frame);
                    cpu.show_state(std::cout);
                    disasm_stmts(mem, cpu_step.current_pc, 1);
                    uint32_t disasm_pc = s.pc, hexdump_addr = 0, cop_addr = custom.copper_ptr(0);
                    for (;;) {
                        std::string line;
                        if (!g.debug_prompt(line)) {
                            debug_mode = false;
                            quit = true;
                            break;
                        }
                        const auto args = split_line(line);
                        if (args.empty())
                            continue;
                        assert(!args[0].empty());
                        if (args[0] == "c") {
                            custom.show_debug_state(std::cout);
                        } else if (args[0] == "d") {
                            auto [valid, pc, lines] = get_addr_and_lines(args, disasm_pc, 10);
                            if (valid) {
                                disasm_pc = pc;
                                disasm_pc += disasm_stmts(mem, disasm_pc, lines);
                            }
                        } else if (args[0] == "e") {
                            custom.show_registers(std::cout);
                        } else if (args[0] == "f") {
                            if (args.size() > 1) {
                                auto [valid, pc] = from_hex(args[1]);
                                if (valid && !(pc & 1)) {
                                    if (auto it = std::find(breakpoints.begin(), breakpoints.end(), pc); it != breakpoints.end()) {
                                        breakpoints.erase(it);
                                        std::cout << "Breakpoint removed\n";
                                    } else {
                                        breakpoints.push_back(pc);
                                    }
                                } else {
                                    std::cerr << "Invalid address\n";
                                }
                            } else {
                                wait_mode = wait_non_rom_pc;
                                break;
                            }
                        } else if (args[0] == "fd") {
                            breakpoints.clear();
                            std::cout << "All breakpoints deleted\n";
                        } else if (args[0] == "g") {
                            break;
                        } else if (args[0] == "m") {
                            auto [valid, addr, lines] = get_addr_and_lines(args, hexdump_addr, 20);
                            addr &= ~1;
                            if (valid) {
                                std::vector<uint8_t> data(lines * 16);
                                for (size_t i = 0; i < lines * 8; ++i)
                                    put_u16(&data[2 * i], mem.read_u16(static_cast<uint32_t>(addr + 2 * i)));
                                hexdump16(std::cout, addr, data.data(), data.size());
                                hexdump_addr = addr + lines * 16;
                            }
                        } else if (args[0][0] == 'o') {
                            if (args[0] == "o") {
                                auto [valid, addr, lines] = get_addr_and_lines(args, cop_addr, 20);
                                if (valid) {
                                    cop_addr = addr & ~1;
                                    cop_addr += copper_disasm(mem, cop_addr, lines);
                                }
                            } else if (args[0] == "o1") {
                                cop_addr = custom.copper_ptr(1);
                                cop_addr += copper_disasm(mem, cop_addr, 20);
                            } else if (args[0] == "o2") {
                                cop_addr = custom.copper_ptr(2);
                                cop_addr += copper_disasm(mem, cop_addr, 20);
                            } else {
                                goto unknown_command;
                            }
                        } else if (args[0] == "q") {
                            quit = true;
                            break;
                        } else if (args[0] == "r") {
                            cpu.show_state(std::cout);
                        } else if (args[0] == "t") {
                            wait_mode = wait_next_inst;
                            break;
                        } else if (args[0] == "trace_file") {
                            if (args.size() > 1) {
                                auto f = std::make_unique<std::ofstream>(args[1].c_str());
                                if (*f) {
                                    trace_file.reset(f.release());
                                    debug_stream = trace_file.get();
                                    std::cout << "Tracing to \"" << args[1] << "\"\n";
                                } else {
                                    std::cout << "Error creating \"" << args[1] << "\"\n";
                                }
                            } else {
                                std::cout << "Tracing to screen\n";
                                trace_file.reset();
                                debug_stream = &std::cout;
                            }
                        } else if (args[0] == "trace_flags") {
                            if (args.size() > 1) {
                                auto fh = from_hex(args[1]);
                                if (fh.first) {
                                    debug_flags = fh.second;
                                } else {
                                    debug_flags = 0;
                                    for (size_t i = 1; i < args.size(); ++i) {
                                        if (args[i] == "copper")
                                            debug_flags |= debug_flag_copper;
                                        else if (args[i] == "bpl")
                                            debug_flags |= debug_flag_bpl;
                                        else if (args[i] == "sprite")
                                            debug_flags |= debug_flag_sprite;
                                        else if (args[i] == "disk")
                                            debug_flags |= debug_flag_disk;
                                        else if (args[i] == "blitter")
                                            debug_flags |= debug_flag_blitter;
                                        else if (args[i] == "audio")
                                            debug_flags |= debug_flag_audio;
                                        else
                                            std::cerr << "Unknown trace flag \"" << args[i] << "\"\n";
                                    }
                                }
                            }
                            std::cout << "debug flags: $" << hexfmt(debug_flags) << "\n";
                        } else if (args[0] == "W") {
                            if (args.size() > 2) {
                                auto [avalid, address] = from_hex(args[1]);
                                if (avalid) {
                                    for (uint32_t i = 2; i < args.size(); ++i) {
                                        const auto& a = args[i];
                                        uint8_t size = 0;
                                        if (a.length() == 2)
                                            size = 1;
                                        else if (a.length() == 4)
                                            size = 2;
                                        else if (a.length() == 8)
                                            size = 4;
                                        else {
                                            std::cerr << "Invalid length for value \"" << a << "\n";
                                            break;
                                        }
                                        auto [dvalid, data] = from_hex(args[i]);
                                        if (!dvalid) {
                                            std::cerr << "Invalid value \"" << a << "\n";
                                            break;
                                        }
                                        if (size > 1 && (address & 1)) {
                                            std::cerr << "Won't write to odd address " << hexfmt(address) << "\n";
                                            break;
                                        }
                                        switch (size) {
                                        case 1:
                                            mem.write_u8(address, static_cast<uint8_t>(data));
                                            break;
                                        case 2:
                                            mem.write_u16(address, static_cast<uint16_t>(data));
                                            break;
                                        case 4:
                                            mem.write_u32(address, data);
                                            break;
                                        }
                                        std::cout << "Wrote $" << hexfmt(data, size * 2) << " to $" << hexfmt(address) << "\n";
                                        address += size;
                                    }
                                } else {
                                    std::cerr << "Invalid adddress\n";
                                }
                            } else {
                                std::cerr << "Missing arguments address, value(s)\n";
                            }
                        } else if (args[0] == "write_mem") {
                            if (args.size() > 1) {
                                std::ofstream f(args[1], std::ofstream::binary);
                                if (f) {
                                    f.write(reinterpret_cast<const char*>(mem.ram().data()), mem.ram().size());
                                } else {
                                    std::cout << "Error creating \"" << args[1] << "\"\n";
                                }
                            } else {
                                std::cout << "Missing argument (file)\n";
                            }

                        } else if (args[0] == "z") {
                            wait_mode = wait_exact_pc;
                            wait_arg = s.pc + instructions[mem.read_u16(s.pc)].ilen * 2;
                            break;
                        } else if (args[0] == "zf") {
                            // Wait for next frame
                            wait_mode = wait_vpos;
                            wait_arg = 0;
                            break;
                        } else if (args[0] == "zv") {
                            // Wait for video position
                            if (args.size() > 1) {
                                uint32_t waitpos = 0;
                                auto vpos = from_hex(args[1]);
                                if (vpos.first && vpos.second <= 313) {
                                    waitpos = vpos.second << 9;
                                    if (args.size() > 2) {
                                        auto hpos = from_hex(args[2]);
                                        if (hpos.first && hpos.second < 455) {
                                            waitpos |= hpos.second;
                                        } else {
                                            std::cout << "Invalid hpos\n";
                                            waitpos = ~0U;
                                        }
                                    }
                                    if (waitpos != ~0U) {
                                        wait_mode = wait_vpos;
                                        wait_arg = waitpos;
                                        break;
                                    }
                                } else {
                                    std::cout << "Invalid vpos\n";
                                }
                            } else {
                                std::cout << "Missing argument(s) vpos [hpos]\n";
                            }
                        } else {
unknown_command:
                            std::cout << "Unknown command \"" << args[0] << "\"\n";
                        }
                    }
                    debug_mode = false;
                    g.set_debug_windows_visible(false);
                    g.set_active(true);
                }

                cpu_active = true;
                cpu_step = cpu.step(custom_step.ipl);
                cpu_active = false;
                   
                if (wait_mode == wait_next_inst || (wait_mode == wait_exact_pc && cpu_step.current_pc == wait_arg) || (wait_mode == wait_non_rom_pc && cpu_step.current_pc < min_rom_addr)) {
                    active_debugger();
                    wait_mode = wait_none;
                } else if (auto it = std::find(breakpoints.begin(), breakpoints.end(), cpu_step.current_pc); it != breakpoints.end()) {
                    active_debugger();
                    std::cout << "Breakpoint hit\n";
                }

                if (cpu_step.stopped) {
                    do {
                        ++idle_count;
                        cstep(false);
                    } while (custom_step.ipl == 0 && !new_frame && !debug_mode);
                } else {
                    do_all_custom_cylces();
                    idle_count = 0;
                }

#ifdef TRACE_LOG
                if (cpu.instruction_count() == trace_start_inst) {
                    std::cout << "Starting trace\n";
                    cpu.trace(&oss);
                } else if (cpu.instruction_count() > trace_start_inst) {
                    pc_log[pc_log_pos] = oss.str();
                    oss.str("");
                    oss.clear();
                    pc_log_pos = (pc_log_pos + 1) % pc_log_size;
                }
#endif

                if (new_frame) {
                    if (disk_chosen_countdown) {
                        if (--disk_chosen_countdown == 0) {
                            std::cout << drives[pending_disk_drive]->name() << " Inserting disk\n";
                            drives[pending_disk_drive]->insert_disk(std::move(pending_disk));
                            pending_disk_drive = 0xff;
                        }
                    }
                    if (cmdline_args.test_mode) {
                        if (!memcmp(custom_step.frame, testmode_data.data(), testmode_data.size() * 4)) {
                            if (cpu.state().instruction_count >= testmode_min_instructions && idle_count > 100'000 && ++testmode_cnt >= testmode_stable_frames) {
                                const auto ts = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                                std::string filename = "c:/temp/test" + ts + ".ppm";
                                {
                                    std::ofstream out { filename, std::ofstream::binary };
                                    out << "P6\n" << graphics_width << " " << graphics_height << "\n255\n";
                                    for (unsigned i = 0; i < graphics_width * graphics_height; ++i) {
                                        const auto c = custom_step.frame[i];
                                        char rgb[3] = { static_cast<char>((c >> 16) & 0xff), static_cast<char>((c >> 8) & 0xff), static_cast<char>((c & 0xff)) };
                                        out.write(rgb, 3);
                                    }
                                }

                                auto pngname = "c:/temp/test" + ts + ".png";
                                auto cmd = "c:/Tools/ImageMagick/convert " + filename + " " + pngname;
                                system(cmd.c_str());
                                _unlink(filename.c_str());

                                std::cout << "Testmode done. Wrote " << pngname << " (DF0: " << cmdline_args.df0 << ")\n";
                                return 0;
                            }
                        } else {
                            memcpy(&testmode_data[0], custom_step.frame, testmode_data.size() * 4);
                        }
                    }
                    new_frame = false;
                    goto update;
                }
                if (!steps_to_update--) {
                update:
                    g.update_image(custom_step.frame);
                    g.led_state(cias.power_led_on());
                    serdata_flush();
                    auto new_events = g.update();
                    events.insert(events.end(), new_events.begin(), new_events.end());
                    steps_to_update = steps_per_update;
                }
            } catch (const std::exception& e) {
#ifdef TRACE_LOG
                for (size_t i = 0; i < sizeof(pc_log) / sizeof(*pc_log); ++i) {
                    std::cerr << pc_log[(pc_log_pos + i) % pc_log_size] << "\n";
                }
#endif
                cpu.show_state(std::cerr);
                std::cerr << "\n" << e.what() << "\n\n";

#ifndef TRACE_LOG
                serdata_flush();
                active_debugger();
                if (cmdline_args.test_mode)
                    throw;
#else
                throw;
#endif
            }
        }

        std::cout << "Exited\n";
        cpu.show_state(std::cout);

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
