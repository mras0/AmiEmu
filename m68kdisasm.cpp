#include <cassert>
#include <iostream>
#include <fstream>

#include "disasm.h"
#include "ioutil.h"
#include "instruction.h"
#include "memory.h"

void disasm_stmts(const std::vector<uint8_t>& data, uint32_t offset, uint32_t end, uint32_t pcoffset = 0)
{
    auto get_word = [&]() -> uint16_t {
        if (offset + 2 > data.size())
            return 0;
        const auto w = get_u16(&data[offset]);
        offset += 2;
        return w;
    };
    while (offset < end) {
        uint16_t iw[max_instruction_words];
        const auto pc = pcoffset + offset;
        iw[0] = get_word();
        for (uint16_t i = 1; i < instructions[iw[0]].ilen; ++i)
            iw[i] = get_word();
        disasm(std::cout, pc, iw, max_instruction_words);
        std::cout << "\n";
    }
}

uint32_t hex_or_die(const char* s)
{
    auto [valid, val] = from_hex(s);
    if (!valid)
        throw std::runtime_error { "Invalid hex number: " + std::string { s } };
    return val;
}

constexpr const char* const cia_regname[16] = {
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
    "todhi ",
    "unused",
    "sdr",
    "icr",
    "cra",
    "crb"
};


constexpr const char* const custom_regname[0x100] = {
    "BLTDDAT",
    "DMACONR",
    "VPOSR",
    "VHPOSR",
    "DSKDATR",
    "JOY0DAT",
    "JOY1DAT",
    "CLXDAT",
    "ADKCONR",
    "POT0DAT",
    "POT1DAT",
    "POTGOR",
    "SERDATR",
    "DSKBYTR",
    "INTENAR",
    "INTREQR",
    "DSKPTH",
    "DSKPTL",
    "DSKLEN",
    "DSKDAT",
    "REFPTR",
    "VPOSW",
    "VHPOSW",
    "COPCON",
    "SERDAT",
    "SERPER",
    "POTGO",
    "JOYTEST",
    "STREQU",
    "STRVBL",
    "STRHOR",
    "STRLONG",
    "BLTCON0",
    "BLTCON1",
    "BLTAFWM",
    "BLTALWM",
    "BLTCPTH",
    "BLTCPTL",
    "BLTBPTH",
    "BLTBPTL",
    "BLTAPTH",
    "BLTAPTL",
    "BLTDPTH",
    "BLTDPTL",
    "BLTSIZE",
    "BLTCON0L",
    "BLTSIZV",
    "BLTSIZH",
    "BLTCMOD",
    "BLTBMOD",
    "BLTAMOD",
    "BLTDMOD",
    "RESERVED_068",
    "RESERVED_06a",
    "RESERVED_06c",
    "RESERVED_06e",
    "BLTCDAT",
    "BLTBDAT",
    "BLTADAT",
    "RESERVED_076",
    "SPRHDAT",
    "BPLHDAT",
    "LISAID",
    "DSKSYNC",
    "COP1LCH",
    "COP1LCL",
    "COP2LCH",
    "COP2LCL",
    "COPJMP1",
    "COPJMP2",
    "COPINS",
    "DIWSTRT",
    "DIWSTOP",
    "DDFSTRT",
    "DDFSTOP",
    "DMACON",
    "CLXCON",
    "INTENA",
    "INTREQ",
    "ADKCON",
    "AUD0LCH",
    "AUD0LCL",
    "AUD0LEN",
    "AUD0PER",
    "AUD0VOL",
    "AUD0DAT",
    "RESERVED_0ac",
    "RESERVED_0ae",
    "AUD1LCH",
    "AUD1LCL",
    "AUD1LEN",
    "AUD1PER",
    "AUD1VOL",
    "AUD1DAT",
    "RESERVED_0bc",
    "RESERVED_0be",
    "AUD2LCH",
    "AUD2LCL",
    "AUD2LEN",
    "AUD2PER",
    "AUD2VOL",
    "AUD2DAT",
    "RESERVED_0cc",
    "RESERVED_0ce",
    "AUD3LCH",
    "AUD3LCL",
    "AUD3LEN",
    "AUD3PER",
    "AUD3VOL",
    "AUD3DAT",
    "RESERVED_0dc",
    "RESERVED_0de",
    "BPL1PTH",
    "BPL1PTL",
    "BPL2PTH",
    "BPL2PTL",
    "BPL3PTH",
    "BPL3PTL",
    "BPL4PTH",
    "BPL4PTL",
    "BPL5PTH",
    "BPL5PTL",
    "BPL6PTH",
    "BPL6PTL",
    "BPL7PTH",
    "BPL7PTL",
    "BPL8PTH",
    "BPL8PTL",
    "BPLCON0",
    "BPLCON1",
    "BPLCON2",
    "BPLCON3",
    "BPL1MOD",
    "BPL2MOD",
    "BPLCON4",
    "CLXCON2",
    "BPL1DAT",
    "BPL2DAT",
    "BPL3DAT",
    "BPL4DAT",
    "BPL5DAT",
    "BPL6DAT",
    "BPL7DAT",
    "BPL8DAT",
    "SPR0PTH",
    "SPR0PTL",
    "SPR1PTH",
    "SPR1PTL",
    "SPR2PTH",
    "SPR2PTL",
    "SPR3PTH",
    "SPR3PTL",
    "SPR4PTH",
    "SPR4PTL",
    "SPR5PTH",
    "SPR5PTL",
    "SPR6PTH",
    "SPR6PTL",
    "SPR7PTH",
    "SPR7PTL",
    "SPR0POS",
    "SPR0CTL",
    "SPR0DATA",
    "SPR0DATB",
    "SPR1POS",
    "SPR1CTL",
    "SPR1DATA",
    "SPR1DATB",
    "SPR2POS",
    "SPR2CTL",
    "SPR2DATA",
    "SPR2DATB",
    "SPR3POS",
    "SPR3CTL",
    "SPR3DATA",
    "SPR3DATB",
    "SPR4POS",
    "SPR4CTL",
    "SPR4DATA",
    "SPR4DATB",
    "SPR5POS",
    "SPR5CTL",
    "SPR5DATA",
    "SPR5DATB",
    "SPR6POS",
    "SPR6CTL",
    "SPR6DATA",
    "SPR6DATB",
    "SPR7POS",
    "SPR7CTL",
    "SPR7DATA",
    "SPR7DATB",
    "COLOR00",
    "COLOR01",
    "COLOR02",
    "COLOR03",
    "COLOR04",
    "COLOR05",
    "COLOR06",
    "COLOR07",
    "COLOR08",
    "COLOR09",
    "COLOR10",
    "COLOR11",
    "COLOR12",
    "COLOR13",
    "COLOR14",
    "COLOR15",
    "COLOR16",
    "COLOR17",
    "COLOR18",
    "COLOR19",
    "COLOR20",
    "COLOR21",
    "COLOR22",
    "COLOR23",
    "COLOR24",
    "COLOR25",
    "COLOR26",
    "COLOR27",
    "COLOR28",
    "COLOR29",
    "COLOR30",
    "COLOR31",
    "HTOTAL",
    "HSSTOP",
    "HBSTRT",
    "HBSTOP",
    "VTOTAL",
    "VSSTOP",
    "VBSTRT",
    "VBSTOP",
    "SPRHSTRT",
    "SPRHSTOP",
    "BPLHSTRT",
    "BPLHSTOP",
    "HHPOSW",
    "HHPOSR",
    "BEAMCON0",
    "HSSTRT",
    "VSSTRT",
    "HCENTER",
    "DIWHIGH",
    "BPLHMOD",
    "SPRHPTH",
    "SPRHPTL",
    "BPLHPTH",
    "BPLHPTL",
    "RESERVED_1f0",
    "RESERVED_1f2",
    "RESERVED_1f4",
    "RESERVED_1f6",
    "RESERVED_1f8",
    "RESERVED_1fa",
    "FMODE",
    "CUSTOM_NOOP",
};


#include <queue>
#include <sstream>
#include <map>
#include <iomanip>

class simval {
public:
    simval()
        : raw_ { 0 }
        , state_ { STATE_UNKNOWN }
    {
    }

    explicit simval(uint32_t val)
        : raw_ { val }
        , state_ { STATE_KNOWN }
    {
    }

    bool known() const
    {
        return state_ == STATE_KNOWN;
    }

    uint32_t raw() const
    {
        assert(known());
        return raw_;
    }

    friend std::ostream& operator<<(std::ostream& os, const simval& v)
    {
        switch (v.state_) {
        case STATE_KNOWN:
            return os << ("$" + hexstring(v.raw_));
        case STATE_UNKNOWN:
            return os << "UNKNOWN";
        default:
            assert(0);
            return os << "INVALID STATE";
        }
    }


private:
    uint32_t raw_;
    enum { STATE_UNKNOWN, STATE_KNOWN } state_;
};

struct simregs {
    simval d[8];
    simval a[8];
};

enum class base_data_type {
    unknown_,
    char_,
    byte_,
    word_,
    long_,
    code_,
    ptr_,
};

class type {
public:
    explicit type(base_data_type t)
        : t_ { t }
    {
    }
    explicit type(const type& base, uint32_t len)
        : t_ { base_data_type::ptr_ }
        , ptr_ { &base }
        , len_ { len }
    {
    }
    type(const type&) = delete;
    type& operator=(const type&) = delete;

    base_data_type base() const
    {
        return t_;
    }

    const type* ptr() const
    {
        return ptr_;
    }

    uint32_t len() const
    {
        return len_;
    }

private:
    base_data_type t_;
    const type* ptr_ = nullptr;
    uint32_t len_ = 0;
};

std::ostream& operator<<(std::ostream& os, const type& t)
{
    if (t.ptr()) {
        os << *t.ptr();
        if (t.len())
            os << "[" << t.len() << "]";
        else
            os << "*";
    } else {
        switch (t.base()) {
        case base_data_type::unknown_:
            return os << "UNKNOWN";
        case base_data_type::char_:
            return os << "CHAR";
        case base_data_type::byte_:
            return os << "BYTE";
        case base_data_type::word_:
            return os << "WORD";
        case base_data_type::long_:
            return os << "LONG";
        case base_data_type::code_:
            return os << "CODE";
        }
        assert(false);
    }
    return os;
}

const type unknown_type { base_data_type::unknown_ };
const type char_type { base_data_type::char_ };
const type byte_type { base_data_type::byte_ };
const type word_type { base_data_type::word_ };
const type long_type { base_data_type::long_ };
const type code_type { base_data_type::code_ };
const type unknown_ptr { unknown_type, 0 };
const type char_ptr { byte_type, 0 };
const type byte_ptr { byte_type, 0 };
const type code_ptr { code_type, 0 };

const type& make_array_type(const type& base, uint32_t len)
{
    assert(len);
    static std::vector<std::unique_ptr<type>> types;
    for (const auto& t : types) {
        if (t->ptr() == &base && t->len() == len) {
            return *t.get();
        }
    }
    types.push_back(std::make_unique<type>(base, len));
    return *types.back().get();
}

const type& make_pointer_type(const type& base)
{
    if (&base == &unknown_type)
        return unknown_ptr;
    else if (&base == &char_type)
        return char_ptr;
    else if (&base == &byte_type)
        return byte_ptr;
    else if (&base == &code_type)
        return code_ptr;

    std::ostringstream oss;
    oss << "TODO: make_pointer_type for " << base;
    throw std::runtime_error { oss.str() };
}

const type& type_from_size(opsize s)
{
    switch (s) {
    case opsize::none:
    default:
        return unknown_type;
    case opsize::b:
        return byte_type;
    case opsize::w:
        return word_type;
    case opsize::l:
        return long_type;
    }
}

const uint32_t sizeof_type(const type& t)
{
    if (t.ptr())
        return t.len() ? t.len() * sizeof_type(*t.ptr()) : 4;
    switch (t.base()) {
    //case base_data_type::unknown_:
    case base_data_type::char_:
    case base_data_type::byte_:
        return 1;
    case base_data_type::word_:
        return 2;
    case base_data_type::long_:
        return 4;
    //case base_data_type::code_:
    //case base_data_type::ptr_:
    }
    std::ostringstream oss;
    oss << "Unhandled type in sizeof_type: " << t;
    throw std::runtime_error { oss.str() };
}

const std::unordered_map<std::string, const type*> typenames{
    { "UNKNOWN", &unknown_type },
    { "CHAR", &char_type },
    { "BYTE", &byte_type },
    { "WORD", &word_type },
    { "LONG", &long_type },
    { "CODE", &code_type },
};

class analyzer {
public:
    explicit analyzer()
        : written_(max_mem)
        , data_(max_mem)        
        , regs_ {}
    {
    }

    void read_info_file(const std::string& filename)
    {
        std::ifstream in { filename };
        if (!in || !in.is_open())
            throw std::runtime_error { "Could not open " + filename };

        uint32_t linenum = 1;
        for (std::string line; std::getline(in, line); ++linenum) {
            size_t start = 0, end = line.length();
            while (start < line.length() && isspace(line[start]))
                ++start;
            for (size_t p = start; p < end; ++p) {
                if (line[p] == '#') {
                    end = p;
                    break;
                }
            }
            while (end > start && isspace(line[end - 1]))
                --end;
            if (start >= end)
                continue;
            std::vector<std::string> parts;
            std::istringstream iss { line.substr(start, end - start) };
            for (std::string part; std::getline(iss, part, ' ');) {
                if (!part.empty())
                    parts.push_back(part);
            }
            if (parts.size() != 3)
                throw std::runtime_error { "Error in " + filename + " line " + std::to_string(linenum) + ": " + line };
            const auto [ok, addr] = from_hex(parts[0]);
            if (!ok)
                throw std::runtime_error { "Invalid address in " + filename + " line " + std::to_string(linenum) + ": " + line };

            std::string basetype;
            size_t p = 0;
            while (p < parts[1].length() && parts[1][p] != '*' && parts[1][p] != '[')
                basetype.push_back(parts[1][p++]);

            const type* t = nullptr;
            if (auto it = typenames.find(basetype); it != typenames.end())
                t = it->second;
            if (!t)
                throw std::runtime_error { "Invalid base type in " + filename + " line " + std::to_string(linenum) + ": " + line + " \"" + basetype + "\"" };

            for (;  p < parts[1].length(); ++p) {
                if (parts[1][p] == '*')
                    t = &make_pointer_type(*t);
                else if (parts[1][p] == '[' && ++p < parts[1].length()) {
                    uint32_t len = 0;
                    while (p < parts[1].length() && isdigit(parts[1][p]))
                        len = len * 10 + (parts[1][p++] - '0');
                    if (p >= parts[1].length() || parts[1][p] != ']')
                        goto typeerr;
                    ++p;
                    t = &make_array_type(*t, len);
                } else {
                typeerr:
                    throw std::runtime_error { "Unsupported type in " + filename + " line " + std::to_string(linenum) + ": " + line + " \"" + parts[1] + "\"" };
                }
            }

            add_label(addr, parts[2], *t);
            if (t == &code_type)
                add_root(addr, simregs {}, true);
        }
    }

    void write_data(uint32_t addr, const uint8_t* data, uint32_t length)
    {
        if (static_cast<size_t>(addr) + length > data_.size())
            throw std::runtime_error { "Out of range" };
        memcpy(&data_[addr], data, length);
        for (uint32_t i = 0; i < length; ++i)
            written_[i + addr] = true;
        insert_area(addr, addr + length);
    }

    void add_root(uint32_t addr, const simregs& regs, bool force = false)
    {
        // Don't visit non-written areas
        if (!force && (!written_[addr] || addr < 0x400))
            return;

        if (visited_.find(addr) == visited_.end()) {
            roots_.push({ addr, regs });
            visited_[addr] = regs; // Mark visitied now to avoid re-insertion
        }
        add_auto_label(addr, code_type);
    }

    void add_label(uint32_t addr, const std::string& name, const type& t)
    {
        auto it = labels_.find(addr);
        if (it == labels_.end())
            labels_[addr] = { name, &t };
        else if (it->second.t != &t) {
            if (it->second.t == &unknown_type)
                it->second.t = &t;
            else
                std::cerr << "Type conflict for " << name << " was " << *it->second.t << " now " << t << "\n";
        }
    
    }

    void add_auto_label(uint32_t addr, const type& t)
    {
        if (addr < 0x400 && (addr & 3) == 0) {
            // Interrupt vector
            const auto vec = addr >> 2;
            std::string lab = "Interrupt" + hexstring(vec, 2) + "Vec";
            switch (vec) {
            case 0: // Reset ssp
            case 1: // Reset PC
                return;
            case 2:
                lab = "BusErrorVec";
                break;
            case 3:
                lab = "BusErrorVec";
                break;
            case 4:
                lab = "IllegalInstructionVec";
                break;
            case 5:
                lab = "ZeroDivideVec";
                break;
            case 6:
                lab = "ChkExceptionVec";
                break;
            case 7:
                lab = "TrapVExceptionVec";
                break;
            case 8:
                lab = "PrivililegeViolationVec";
                break;
            case 9:
                lab = "TraceExceptionVec";
                break;
            case 10:
                lab = "Line1010ExceptionVec";
                break;
            case 11:
                lab = "Line1111ExceptionVec";
                break;
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
                lab = "Level" + std::to_string(vec - 24) + "Vec";
                break;
            default:
                if (vec >= 32 && vec < 48)
                    lab = "Trap" + hexstring(vec - 32, 1) + "Vec";
            }
            add_label(addr, lab, code_ptr);
            return;
        }

        add_label(addr, "lab_" + hexstring(addr), t);
    }

    void run()
    {
        while (!roots_.empty()) {
            const auto r = roots_.front();
            roots_.pop();
            regs_ = r.second;
            trace(r.first);
        }

        for (const auto& area : areas_) {
            uint32_t pos = area.beg;

            while (pos < area.end) {
                auto it = visited_.find(pos);
                if (it == visited_.end()) {
                    const auto next_visited = visited_.upper_bound(pos);
                    const auto next_visited_pos = std::min(area.end, next_visited == visited_.end() ? ~0U : next_visited->first);
                    handle_data_area(pos, next_visited_pos);
                    pos = next_visited_pos;
                    continue;
                }

                maybe_print_label(pos);
                uint16_t iwords[max_instruction_words];
                read_instruction(iwords, pos);
                const auto& inst = instructions[iwords[0]];

                regs_ = it->second;

                std::ostringstream extra;

                std::cout << "\t" << inst.name;
                for (int i = 0; i < inst.nea; ++i) {
                    const auto ea = inst.ea[i];
                    std::cout << (i ? ", " : "\t");

                    auto do_known = [&](std::string lab = "") {
                        if (!ea_val_[i].known())
                            return;
                        if (lab.empty())
                            lab = ea_string(ea);
                        if (ea_val_[i].known()) {
                            extra << "\t" << lab << "=$" << hexfmt(ea_val_[i].raw());
                        }
                    };

                    switch (ea >> ea_m_shift) {
                    case ea_m_Dn:
                        std::cout << "D" << (ea & ea_xn_mask);
                        do_known();
                        break;
                    case ea_m_An:
                        std::cout << "A" << (ea & ea_xn_mask);
                        do_known();
                        break;
                    case ea_m_A_ind:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")";
                        do_known("A" + std::to_string(ea & ea_xn_mask));
                        break;
                    case ea_m_A_ind_post:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")+";
                        do_known("A" + std::to_string(ea & ea_xn_mask));
                        break;
                    case ea_m_A_ind_pre:
                        std::cout << "-(A" << (ea & ea_xn_mask) << ")";
                        do_known("A" + std::to_string(ea & ea_xn_mask));
                        break;
                    case ea_m_A_ind_disp16: {
                        auto n = static_cast<int16_t>(ea_data_[i]);
                        const auto& aval = regs_.a[ea & 7];
                        std::ostringstream desc;
                        desc << "$";
                        if (n < 0) {
                            desc << "-";
                            n = -n;
                        }
                        desc << hexfmt(static_cast<uint16_t>(n));
                        desc << "(A" << (ea & 7) << ")";
                        if (aval.known()) {
                            const auto addr = aval.raw() + static_cast<int16_t>(ea_data_[i]);
                            // Custom reg
                            if (addr >= 0xDE0000 && addr < 0xE00000) {
                                const auto ra = addr & ~0x1ff;
                                std::cout << custom_regname[(addr >> 1) & 0xff];
                                if (int ofs = (addr & 1) - (aval.raw() & 0x1ff); ofs != 0)
                                    std::cout << (ofs > 0 ? "+" : "-") << (ofs > 0 ? ofs : -ofs);
                                std::cout << "(A" << (ea & 7) << ")";
                                break;
                            } else {
                                extra << "\t" << desc.str() << " = $" << hexfmt(addr);
                            }
                        }

                        std::cout << desc.str();
                        break;
                    }
                    case ea_m_A_ind_index: {
                        const auto extw = ea_data_[i];
                        // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                        auto disp = static_cast<int8_t>(extw & 255);
                        std::ostringstream desc;
                        desc << "$";
                        if (disp < 0) {
                            desc << "-";
                            disp = -disp;
                        }
                        desc << hexfmt(static_cast<uint8_t>(disp)) << "(A" << (ea & 7) << ",";
                        desc << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                        desc << ")";
                        // TODO: Handle know values..
                        std::cout << desc.str();
                        break;
                    }
                    case ea_m_Other:
                        switch (ea & ea_xn_mask) {
                        case ea_other_abs_w:
                        case ea_other_abs_l:
                            print_addr(ea_addr_[i].raw());
                            break;
                        case ea_other_pc_disp16:
                            print_addr(ea_addr_[i].raw());
                            std::cout << "(PC)";
                            break;
                        case ea_other_pc_index: {
                            const auto extw = ea_data_[i];
                            // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                            auto disp = static_cast<int8_t>(extw & 255);
                            std::cout << "$";
                            if (disp < 0) {
                                std::cout << "-";
                                disp = -disp;
                            }
                            std::cout << hexfmt(static_cast<uint8_t>(disp)) << "(PC,";
                            std::cout << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                            std::cout << ")";
                            // TODO: Handle know values..
                            break;
                        }
                        case ea_other_imm:
                            std::cout << "#$" << hexfmt(ea_data_[i], opsize_bytes(inst.size)*2);
                            break;
                        default:
                            throw std::runtime_error { "TODO: " + ea_string(ea) };
                        }
                        break;
                    default:
                        if (ea == ea_sr) {
                            std::cout << "SR";
                        } else if (ea == ea_ccr) {
                            std::cout << "CCR";
                        } else if (ea == ea_usp) {
                            std::cout << "USP";
                        } else if (ea == ea_reglist) {
                            assert(inst.nea == 2);
                            std::cout << reg_list_string(static_cast<uint16_t>(ea_data_[i]), i == 0 && (inst.ea[1] >> 3) == ea_m_A_ind_pre);
                        } else if (ea == ea_bitnum) {
                            std::cout << "#" << ea_data_[i];
                        } else if (ea == ea_disp) {
                            print_addr(ea_addr_[i].raw());
                        } else {
                            if (inst.size == opsize::l && inst.data > 0x400 && labels_.find(inst.data) != labels_.end()) {
                                std::cout << "#";
                                print_addr(inst.data);
                            } else {
                                std::cout << "#$" << hexfmt(inst.data);
                            }
                        }
                        break;
                    }
                }
                if (!extra.str().empty()) {
                    std::cout << "\t;" << extra.str();
                }
                std::cout << "\n";
                pos += inst.ilen * 2;
            }
        }

    }

private:
    static constexpr uint32_t max_mem = 1 << 24;
    struct area {
        uint32_t beg;
        uint32_t end;
    };
    struct label_info {
        std::string name;
        const type* t;
    };
    std::vector<bool> written_;
    std::vector<uint8_t> data_;
    std::queue<std::pair<uint32_t, simregs>> roots_;
    std::map<uint32_t, simregs> visited_;
    std::map<uint32_t, label_info> labels_;
    simregs regs_;
    std::vector<area> areas_;

    uint32_t ea_data_[2];
    simval ea_addr_[2];
    simval ea_val_[2];

    void insert_area(uint32_t beg, uint32_t end)
    {
        auto it = areas_.begin();
        for (; it != areas_.end(); ++it) {
            //return a0 <= b1 && b0 <= a1;
            if (beg <= it->end && it->beg <= end)
                throw std::runtime_error { "Area overlap" };
            if (beg < it->beg)
                break;
        }
        areas_.insert(it, { beg, end });
    }

    const type* maybe_print_label(uint32_t pos) const
    {
        if (auto it = labels_.find(pos); it != labels_.end()) {
            std::cout << std::setw(32) << std::left << it->second.name << "\t; $" << hexfmt(it->first) << " " << *it->second.t << "\n";
            return it->second.t;
        }
        return nullptr;
    }

    uint32_t handle_array_range(uint32_t pos, uint32_t end, uint32_t elemsize)
    {
        assert(elemsize == 1 || elemsize == 2 || elemsize == 4);
        assert((end - pos) % elemsize == 0);
        const char suffix = elemsize == 1 ? 'b' : elemsize == 2 ? 'w' : 'l';
        const auto elem_per_line = (80 - 16) / (3 + 2 * elemsize);        

        while (pos < end) {
            const auto here = std::min(elem_per_line, (end - pos) / elemsize);
            // Check for a run of similar elements
            uint32_t runlen = 0;
            for (uint32_t i = 1; pos + i * elemsize <= end; ++i) {
                if (memcmp(&data_[pos], &data_[pos + i * elemsize], elemsize))
                    break;
                ++runlen;
            }

            if (runlen > elem_per_line) {
                const uint32_t val = elemsize == 1 ? data_[pos] : elemsize == 2 ? get_u16(&data_[pos]) : get_u32(&data_[pos]);
                std::cout << "\tds." << suffix << "\t$" << hexfmt(runlen, runlen < 256 ? 2 : runlen < 65536 ? 4 : 8) << ", $" << hexfmt(val, 2 * elemsize) << "\n";
                pos += runlen * elemsize;
                continue;
            }

            std::cout << "\tdc." << suffix << "\t";
            for (uint32_t i = 0; i < here && pos < end; ++i) {
                if (i)
                    std::cout << ", ";
                std::cout << "$";
                if (elemsize == 1)
                    std::cout << hexfmt(data_[pos]);
                else if (elemsize == 2)
                    std::cout << hexfmt(get_u16(&data_[pos]));
                else
                    std::cout << hexfmt(get_u32(&data_[pos]));
                pos += elemsize;
            }
            std::cout << "\n";
        }
        return pos;
    }

    void handle_data_area(uint32_t pos, uint32_t end)
    {
        if (labels_.find(pos) == labels_.end())
            std::cout << "; $" << hexfmt(pos) << "\n";
        while (pos < end) {
            const auto next_label = labels_.upper_bound(pos);
            const auto next_pos = std::min(end, next_label == labels_.end() ? ~0U : next_label->first);

            if (auto t = maybe_print_label(pos); t) {
                switch (t->base()) {
                case base_data_type::unknown_:
                case base_data_type::code_:
                    break;
                case base_data_type::char_:
                case base_data_type::byte_:
                    std::cout << "\tdc.b\t$" << hexfmt(data_[pos]) << "\n";
                    ++pos;
                    continue;
                case base_data_type::word_:
                    std::cout << "\tdc.w\t$" << hexfmt(get_u16(&data_[pos])) << "\n";
                    pos += 2;
                    continue;
                case base_data_type::ptr_:
                    if (t->len()) {
                        if (t->ptr()->base() == base_data_type::char_) {
                            bool in = false;
                            uint32_t linepos = 0;
                            auto end_quote = [&]() {
                                if (in) {
                                    std::cout << '\'';
                                    in = false;
                                    linepos++;
                                }
                            };
                            auto maybe_sep = [&]() {
                                if (linepos > 16) {
                                    std::cout << ", ";
                                    linepos += 3;
                                }
                            };
                            for (uint32_t i = 0; i < t->len() && pos < next_pos; ++i, ++pos) {
                                if (linepos == 0) {
                                    std::cout << "\tdc.b\t";
                                    linepos = 16;
                                    in = false;
                                }
                                const uint8_t c = data_[pos];
                                if (c >= ' ' && c < 128) {
                                    if (!in) {
                                        maybe_sep();
                                        std::cout << '\'';
                                        in = true;
                                        ++linepos;
                                    }
                                    std::cout << static_cast<char>(c);
                                    ++linepos;
                                } else {
                                    end_quote();
                                    maybe_sep();
                                    std::cout << "$" << hexfmt(c);
                                    linepos += 3;
                                }
                                if (linepos+in >= 80) {
                                    end_quote();
                                    std::cout << "\n";
                                    linepos = 0;
                                }
                            }
                            end_quote();
                            if (linepos)
                                std::cout << "\n";
                            continue;
                        }
                        const auto elem_size = sizeof_type(*t->ptr());
                        pos = handle_array_range(pos, std::min(pos + elem_size * t->len(), next_pos), elem_size);
                        continue;
                    } else {
                        std::cout << "\tdc.l\t";
                        if (!print_addr_maybe(get_u32(&data_[pos])))
                            std::cout << "$" << hexfmt(get_u32(&data_[pos]));
                        std::cout << "\n";
                        pos += 4;
                        continue;
                    }
                    break;
                case base_data_type::long_:
                    std::cout << "\tdc.l\t$" << hexfmt(get_u32(&data_[pos])) << "\n";
                    pos += 4;
                    continue;
                }
            }

            // ALIGN
            if (pos & 1) {
                std::cout << "\tdc.b\t$" << hexfmt(data_[pos]) << "\n";
                ++pos;
            }

            pos = handle_array_range(pos, next_pos, 2);
        }
    }

    bool print_addr_maybe(uint32_t addr)
    {
        if (auto it = labels_.find(addr); it != labels_.end()) {
            std::cout << it->second.name;
            return true;
        }

        if (addr == 4) {
            std::cout << "AbsExecBase";
            return true;
        }

        if (addr >= 0xA00000 && addr < 0xC00000) {
            // CIA-A is selected when A12=0, CIA-B is selcted when A13=0
            switch ((addr >> 12) & 3) {
            case 0: // Both!
                std::cout << "ciaboth";
                break;
            case 1: // CIAB
                std::cout << "ciab";
                break;
            case 2: // CIAA
                std::cout << "ciaa";
                break;
            case 3: // Niether!
                return false;
            }
            std::cout << cia_regname[(addr >> 8) & 0xf];
            return true;
        }

        if (addr == 0xdff000) {
            std::cout << "CustomBase";
            return true;
        }
        if (addr >= 0xDE0000 && addr < 0xE00000) {
            std::string regname = custom_regname[(addr >> 1) & 0xff];
            for (auto& r : regname) {
                // tolower
                if (r >= 'A' && r <= 'Z')
                    r += 32;
            }
            std::cout << regname;
            if (addr & 1)
                std::cout << "+1";
            return true;
        }

        return false;
    }

    void print_addr(uint32_t addr)
    {
        if (!print_addr_maybe(addr))
            std::cout << "$" << hexfmt(addr);
    }

    uint16_t read_iword(uint32_t addr)
    {
        if ((addr & 1) || addr < 0x400 || addr > max_mem - 2)
            throw std::runtime_error { "Reading instruction word from invalid address $" + hexstring(addr) };
        return get_u16(&data_[addr]);
    }

    void read_instruction(uint16_t* iwords, const uint32_t addr)
    {
        iwords[0] = read_iword(addr);
        const auto& inst = instructions[iwords[0]];
        for (uint8_t i = 1; i < inst.ilen; ++i) {
            iwords[i] = read_iword(addr + i * 2);
        }

        unsigned eaw = 1;

        // Reglist is always first
        uint16_t reglist = 0;
        if (inst.nea == 2 && (inst.ea[0] == ea_reglist || inst.ea[1] == ea_reglist))
            reglist = iwords[eaw++];

        ea_val_[0] = simval {};
        ea_addr_[0] = simval {};
        ea_data_[0] = 0;
        ea_val_[1] = simval {};
        ea_addr_[0] = simval {};
        ea_data_[1] = 0;

        for (int i = 0; i < inst.nea; ++i) {
            const auto ea = inst.ea[i];
            switch (ea >> ea_m_shift) {
            case ea_m_Dn:
                ea_val_[i] = regs_.d[ea & ea_xn_mask];
                break;
            case ea_m_An:
                ea_val_[i] = regs_.a[ea & ea_xn_mask];
                break;
            case ea_m_A_ind:
                ea_addr_[i] = regs_.a[ea & ea_xn_mask];
                // TODO: ea_val_
                break;
            case ea_m_A_ind_post:
                // TODO: ea_val_, ea_addr_
                // TODO: increment
                break;
            case ea_m_A_ind_pre:
                // TODO: decrement
                // TODO: ea_val_, ea_addr_
                break;
            case ea_m_A_ind_disp16: {
                assert(eaw < inst.ilen);
                int16_t n = iwords[eaw++];
                ea_data_[i] = static_cast<uint32_t>(n);
                // TODO: ea_val_
                break;
            }
            case ea_m_A_ind_index: {
                assert(eaw < inst.ilen);
                const auto extw = iwords[eaw++];
                ea_data_[i] = static_cast<uint32_t>(extw);
                // TODO: ea_val_
                break;
            }
            case ea_m_Other:
                switch (ea & ea_xn_mask) {
                case ea_other_abs_w:
                    ea_data_[i] = static_cast<uint32_t>(static_cast<int16_t>(iwords[eaw++]));
                    ea_addr_[i] = simval { ea_data_[i] };
                    // TODO: ea_val_
                    break;
                case ea_other_abs_l:
                    ea_data_[i] = iwords[eaw] << 16 | iwords[eaw + 1];
                    ea_addr_[i] = simval { ea_data_[i] };
                    eaw += 2;
                    // TODO: ea_val_
                    break;
                case ea_other_pc_disp16: {
                    assert(eaw < inst.ilen);
                    int16_t n = iwords[eaw++];
                    ea_data_[i] = static_cast<uint32_t>(n);
                    ea_addr_[i] = simval { addr + (eaw - 1) * 2 + n };
                    // TODO: ea_val_
                    break;
                }
                case ea_other_pc_index: {
                    assert(eaw < inst.ilen);
                    const auto extw = iwords[eaw++];
                    ea_data_[i] = static_cast<uint32_t>(extw);
                    // TODO: ea_val_
                    // TODO: ea_addr_
                    break;
                }
                case ea_other_imm:
                    if (inst.size == opsize::l) {
                        assert(eaw + 1 < inst.ilen);
                        ea_data_[i] = iwords[eaw] << 16 | iwords[eaw + 1];
                        eaw += 2;
                    } else {
                        assert(eaw < inst.ilen);
                        if (inst.size == opsize::b)
                            ea_data_[i] = static_cast<uint8_t>(iwords[eaw++]);
                        else
                            ea_data_[i] = iwords[eaw++];
                    }
                    ea_val_[i] = simval { ea_data_[i] };
                    break;
                default:
                    throw std::runtime_error { "TODO: " + ea_string(ea) };
                }
                break;
            default:
                if (ea == ea_sr || ea == ea_ccr || ea == ea_usp) {
                    // TODO
                    break;
                } else if (ea == ea_reglist) {
                    assert(inst.nea == 2);
                    ea_data_[i] = reglist;
                    break;
                } else if (ea == ea_bitnum) {
                    assert(eaw < inst.ilen);
                    uint16_t b = iwords[eaw++];
                    if (inst.size == opsize::b)
                        b &= 7;
                    else
                        b &= 31;
                    ea_data_[i] = b;
                    break;
                }

                if (inst.extra & extra_disp_flag) {
                    assert(ea == ea_disp);
                    assert(eaw < inst.ilen);
                    const auto disp = static_cast<int16_t>(iwords[eaw++]);
                    ea_data_[i] = static_cast<uint32_t>(disp);
                    ea_addr_[i] = simval { addr + 2 + disp };
                } else if (ea == ea_disp) {
                    ea_data_[i] = static_cast<int8_t>(inst.data);
                    ea_addr_[i] = simval { addr + 2 + static_cast<int8_t>(inst.data) };
                } else {
                }
                break;
            }
        }
        assert(eaw == inst.ilen);
    }

    void print_sim_regs()
    {
        for (uint32_t i = 0; i < 16; ++i) {
            if (i == 8)
                std::cout << "\n";
            else if (i)
                std::cout << " ";
            std::cout << (i & 8 ? "A" : "D") << (i & 7) << "=" << std::setw(9) << std::left << (i < 8 ? regs_.d[i & 7] : regs_.a[i & 7]);
        }
        std::cout << "\n";
    }

    void trace(uint32_t addr)
    {
        for (;;) {
            const auto start = addr;

            uint16_t iwords[max_instruction_words];
            read_instruction(iwords, addr);
            const auto& inst = instructions[iwords[0]];
            addr += 2 * inst.ilen;

            #if 0
            print_sim_regs();
            disasm(std::cout, start, iwords, inst.ilen);
            std::cout << "\n";
            #endif

            switch (inst.type) {
            case inst_type::Bcc:
            case inst_type::BSR:
            case inst_type::BRA: {
                assert(inst.nea == 1 && inst.ea[0] == ea_disp);
                add_root(ea_addr_[0].raw(), regs_);
                if (inst.type == inst_type::BRA)
                    goto finish;
                break;
            }
            case inst_type::DBcc:
                assert(inst.nea == 2 && inst.ea[1] == ea_disp && inst.ilen == 2);
                add_root(ea_addr_[1].raw(), regs_);
                break;

            case inst_type::JSR:
            case inst_type::JMP:
                assert(inst.nea == 1);
                if (ea_addr_[0].known())
                    add_root(ea_addr_[0].raw(), regs_);
                if (inst.type == inst_type::JMP)
                    goto finish;
                break;

            case inst_type::ILLEGAL:
            case inst_type::RTS:
            case inst_type::RTE:
            case inst_type::RTR:
            finish:
                visited_[start] = regs_;
                return;

            default:
                for (int i = 0; i < inst.nea; ++i) {
                    if (!ea_addr_[i].known())
                        continue;
                    const auto ea_addr = ea_addr_[i].raw() & 0xffffff;
                    if (ea_addr >= written_.size() || !written_[ea_addr])
                        continue;

                    const auto& t = inst.type == inst_type::LEA ? unknown_type : type_from_size(inst.size);

                    switch (inst.ea[i] >> ea_m_shift) {
                    case ea_m_Dn:
                        break;
                    case ea_m_An:
                    case ea_m_A_ind:
                    case ea_m_A_ind_post:
                    case ea_m_A_ind_pre:
                    case ea_m_A_ind_disp16:
                    case ea_m_A_ind_index:
                        add_auto_label(ea_addr, t);
                        break;
                    case ea_m_Other:
                        switch (inst.ea[i] & ea_xn_mask) {
                        case ea_other_abs_w:
                        case ea_other_abs_l:
                        case ea_other_pc_disp16:
                        case ea_other_pc_index:                            
                            add_auto_label(ea_addr, t);
                            break;
                        case ea_other_imm:
                            // Maybe better heuristics here?
                            if (inst.size == opsize::l)
                                add_auto_label(ea_addr, t);
                            break;
                        default:
                            throw std::runtime_error { "Unsupported EA " + ea_string(inst.ea[i]) };
                        }
                    default:
                        if (inst.ea[i] == ea_disp)
                            throw std::runtime_error { "Unexpected EA " + ea_string(inst.ea[i]) + " for " + inst.name };
                    }
                }
                sim_inst(inst);
            }

            if (visited_.find(start) == visited_.end()) {
                // TODO: Combine register values (or something)
                visited_[start] = regs_;
            }
        }
    }

    void update_mem(opsize size, uint32_t addr, const simval& val)
    {
        if (!val.known())
            return;
        
        if (size == opsize::l && !(addr & 3) && addr < 0x400) {
            add_auto_label(addr, code_ptr);
            add_root(val.raw(), simregs {});
            return;
        }

        std::cerr << "Update $" << hexfmt(addr) << "." << (size == opsize::l ? "L" : size == opsize::w ? "W" : "B") << " to $" << hexfmt(val.raw(), 2*opsize_bytes(size)) << "\n";
        //throw std::runtime_error { "TODO" };
    }

    void update_ea(opsize size, uint8_t idx, uint8_t ea, const simval& val)
    {
        const auto& ea_val = ea_val_[idx];

        // XXX: TODO
        if (size != opsize::l)
            return;

        switch (ea >> ea_m_shift) {
        case ea_m_Dn:
            regs_.d[ea & ea_xn_mask] = val;
            return;
        case ea_m_An:
            regs_.a[ea & ea_xn_mask] = val;
            return;
            // TOOD
        //case ea_m_A_ind:
        //case ea_m_A_ind_post:
        //case ea_m_A_ind_pre:
        //case ea_m_A_ind_disp16:
        //case ea_m_A_ind_index:
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
            case ea_other_pc_disp16:
            case ea_other_pc_index:
                update_mem(size, ea_val.raw(), val);
                break;
            }
        }
    }

    void sim_inst(const instruction& inst)
    {
        switch (inst.type) {
        case inst_type::LEA:
            update_ea(opsize::l, 1, inst.ea[1], ea_addr_[0]);
            break;
        case inst_type::MOVE:
        case inst_type::MOVEA: // TODO: sign extend if opsize == w
            update_ea(inst.size, 1, inst.ea[1], ea_val_[0]);
            break;
        }
    }
};


constexpr uint32_t HUNK_UNIT    = 999;
constexpr uint32_t HUNK_NAME    = 1000;
constexpr uint32_t HUNK_CODE    = 1001;
constexpr uint32_t HUNK_DATA    = 1002;
constexpr uint32_t HUNK_BSS     = 1003;
constexpr uint32_t HUNK_RELOC32 = 1004;
constexpr uint32_t HUNK_RELOC16 = 1005;
constexpr uint32_t HUNK_RELOC8  = 1006;
constexpr uint32_t HUNK_EXT     = 1007;
constexpr uint32_t HUNK_SYMBOL  = 1008;
constexpr uint32_t HUNK_DEBUG   = 1009;
constexpr uint32_t HUNK_END     = 1010;
constexpr uint32_t HUNK_HEADER  = 1011;
constexpr uint32_t HUNK_OVERLAY = 1013;
constexpr uint32_t HUNK_BREAK   = 1014;
constexpr uint32_t HUNK_DREL32  = 1015;
constexpr uint32_t HUNK_DREL16  = 1016;
constexpr uint32_t HUNK_DREL8   = 1017;
constexpr uint32_t HUNK_LIB     = 1018;
constexpr uint32_t HUNK_INDEX   = 1019;

std::string hunk_type_string(uint32_t hunk_type)
{
    switch (hunk_type) {
#define HTS(x) case x: return #x
        HTS(HUNK_UNIT);
        HTS(HUNK_NAME);
        HTS(HUNK_CODE);
        HTS(HUNK_DATA);
        HTS(HUNK_BSS);
        HTS(HUNK_RELOC32);
        HTS(HUNK_RELOC16);
        HTS(HUNK_RELOC8);
        HTS(HUNK_EXT);
        HTS(HUNK_SYMBOL);
        HTS(HUNK_DEBUG);
        HTS(HUNK_END);
        HTS(HUNK_HEADER);
        HTS(HUNK_OVERLAY);
        HTS(HUNK_BREAK);
        HTS(HUNK_DREL32);
        HTS(HUNK_DREL16);
        HTS(HUNK_DREL8);
        HTS(HUNK_LIB);
        HTS(HUNK_INDEX);
#undef HTS
    }
    return "Unknown hunk type " + std::to_string(hunk_type);
}

constexpr uint8_t EXT_SYMB	    = 0;    // symbol table
constexpr uint8_t EXT_DEF		= 1;    // relocatable definition
constexpr uint8_t EXT_ABS		= 2;    // Absolute definition
constexpr uint8_t EXT_RES		= 3;    // no longer supported
constexpr uint8_t EXT_REF32	    = 129;  // 32 bit absolute reference to symbol
constexpr uint8_t EXT_COMMON	= 130;  // 32 bit absolute reference to COMMON block
constexpr uint8_t EXT_REF16	    = 131;  // 16 bit PC-relative reference to symbol
constexpr uint8_t EXT_REF8	    = 132;  //  8 bit PC-relative reference to symbol
constexpr uint8_t EXT_DEXT32	= 133;  // 32 bit data relative reference
constexpr uint8_t EXT_DEXT16	= 134;  // 16 bit data relative reference
constexpr uint8_t EXT_DEXT8	    = 135;  //  8 bit data relative reference
constexpr uint8_t EXT_RELREF32	= 136;  // 32 bit PC-relative reference to symbol
constexpr uint8_t EXT_RELCOMMON	= 137;  // 32 bit PC-relative reference to COMMON block
constexpr uint8_t EXT_ABSREF16	= 138;  // 16 bit absolute reference to symbol
constexpr uint8_t EXT_ABSREF8	= 139;  // 8 bit absolute reference to symbol

std::string ext_type_string(uint8_t ext_type)
{
    switch (ext_type) {
#define ETS(x) case x: return #x
        ETS(EXT_SYMB);
        ETS(EXT_DEF);
        ETS(EXT_ABS);
        ETS(EXT_RES);
        ETS(EXT_REF32);
        ETS(EXT_COMMON);
        ETS(EXT_REF16);
        ETS(EXT_REF8);
        ETS(EXT_DEXT32);
        ETS(EXT_DEXT16);
        ETS(EXT_DEXT8);
        ETS(EXT_RELREF32);
        ETS(EXT_RELCOMMON);
        ETS(EXT_ABSREF16);
        ETS(EXT_ABSREF8);
#undef ETS
    }
    return "Unknown external reference type " + std::to_string(ext_type);
}


constexpr bool is_initial_hunk(uint32_t hunk_type)
{
    return hunk_type == HUNK_CODE || hunk_type == HUNK_DATA || hunk_type == HUNK_BSS;
}

class hunk_file {
public:
    explicit hunk_file(const std::vector<uint8_t>& data, analyzer* a)
        : data_ { data }
        , a_ { a }
    {
        if (data.size() < 8)
            throw std::runtime_error { "File is too small to be Amiga HUNK file" };
        const uint32_t header_type = read_u32();
        if (header_type == HUNK_UNIT)
            read_hunk_unit();
        else if (header_type == HUNK_HEADER)
            read_hunk_exe();
        else
            throw std::runtime_error { "Invalid hunk type " + std::to_string(header_type) };
    }

private:
    const std::vector<uint8_t>& data_;
    analyzer* a_;
    uint32_t pos_ = 0;

    struct reloc_info {
        uint32_t hunk_ref;
        std::vector<uint32_t> relocs;
    };

    struct symbol_info {
        std::string name;
        uint32_t addr;
    };

    void check_pos(size_t size)
    {
        if (pos_ + size > data_.size())
            throw std::runtime_error { "Unexpected end of hunk file" };
    }

    uint32_t read_u32()
    {
        check_pos(4);
        auto l = get_u32(&data_[pos_]);
        pos_ += 4;
        return l;
    }

    std::string read_string_size(uint32_t num_longs)
    {
        const auto size = num_longs << 2;
        if (!size)
            return "";
        check_pos(size);
        const auto start = pos_;
        auto end = pos_ + size - 1;
        while (end > pos_ && !data_[end])
            --end;
        pos_ += size;
        return std::string { &data_[start], &data_[end + 1] };
    }

    std::string read_string()
    {
        return read_string_size(read_u32());
    }

    std::vector<symbol_info> read_hunk_symbol()
    {
        std::vector<symbol_info> syms;
        while (const auto name_longs = read_u32()) {
            auto name = read_string_size(name_longs);
            auto ofs = read_u32();
            syms.push_back({ std::move(name), ofs });
        }
        return syms;
    }

    void read_hunk_debug()
    {
        const auto size = read_u32() << 2;
        if (!size)
            return;
        check_pos(size);

        if (size < 8)
            throw std::runtime_error { "Invalid HUNK_DEBUG size " + std::to_string(size) };

        std::cout << "HUNK_DEBUG ";
        hexdump(std::cout, &data_[pos_], 8);

        pos_ += size;
    }

    std::vector<reloc_info> read_hunk_reloc32()
    {
        std::vector<reloc_info> relocs;
        for (;;) {
            const auto num_offsets = read_u32();
            if (!num_offsets)
                break;
            reloc_info r;
            r.hunk_ref = read_u32();
            r.relocs.resize(num_offsets);
            for (uint32_t i = 0; i < num_offsets; ++i)
                r.relocs[i] = read_u32();
            relocs.emplace_back(std::move(r));
        }
        return relocs;
    }

    void read_hunk_exe();
    void read_hunk_unit();
};

void hunk_file::read_hunk_exe()
{
    // Only valid for HUNK_HEADER
    if (read_u32() != 0)
        throw std::runtime_error { "Hunk file contains resident libraries" };

    const uint32_t table_size = read_u32();
    const uint32_t first_hunk = read_u32();
    const uint32_t last_hunk = read_u32();

    if (table_size != (last_hunk - first_hunk) + 1)
        throw std::runtime_error { "Invalid hunk file. Table size does not match first/last hunk" };

    uint32_t chip_ptr = 0x400;
    uint32_t fast_ptr = 0x200000;

    struct hunk_info {
        uint32_t type;
        uint32_t addr;
        std::vector<uint8_t> data;
        uint32_t loaded_size;
    };
    std::vector<symbol_info> symbols;

    std::vector<hunk_info> hunks(table_size);
    for (uint32_t i = 0; i < table_size; ++i) {
        const auto v = read_u32();
        const auto flags = v >> 30;
        const auto size = (v & 0x3fffffff) << 2;
        if (flags == 3) {
            throw std::runtime_error { "Unsupported hunk size: $" + hexstring(v) };
        }
        const auto aligned_size = (size + 4095) & -4096;
        if (flags == 1) {
            hunks[i].addr = chip_ptr;
            chip_ptr += aligned_size;
            if (chip_ptr > 0x200000)
                throw std::runtime_error { "Out of chip mem" };
        } else {
            hunks[i].addr = fast_ptr;
            fast_ptr += aligned_size;
            if (fast_ptr > 0xa00000)
                throw std::runtime_error { "Out of fast mem" };
        }
        hunks[i].data.resize(size);
        std::cout << "Hunk " << i << " $" << hexfmt(size) << " " << (flags == 1 ? "CHIP" : flags == 2 ? "FAST" : "    ") << " @ " << hexfmt(hunks[i].addr) << "\n";
    }

    uint32_t table_index = 0;
    while (table_index < table_size) {
        const uint32_t hunk_num = first_hunk + table_index;

        const uint32_t hunk_type = read_u32();

        if (hunk_type == HUNK_DEBUG) {
            read_hunk_debug();
            continue;
        }

        if (!is_initial_hunk(hunk_type))
            throw std::runtime_error { "Expected CODE, DATA or BSS hunk for hunk " + std::to_string(hunk_num) + " got " + hunk_type_string(hunk_type) };
        const uint32_t hunk_longs = read_u32();
        const uint32_t hunk_bytes = hunk_longs << 2;
        std::cout << "\tHUNK_" << (hunk_type == HUNK_CODE ? "CODE" : hunk_type == HUNK_DATA ? "DATA" : "BSS ") << " size = $" << hexfmt(hunk_bytes) << "\n";

        hunks[hunk_num].type = hunk_type;
        hunks[hunk_num].loaded_size = hunk_bytes;

        if (hunk_bytes > hunks[hunk_num].data.size()) {
            throw std::runtime_error { "Too much data for hunk " + std::to_string(hunk_num) };
        }

        if (hunk_type != HUNK_BSS) {
            check_pos(hunk_bytes);
            memcpy(&hunks[hunk_num].data[0], &data_[pos_], hunk_bytes);
            pos_ += hunk_bytes;
        }

        while (pos_ <= data_.size() - 4 && !is_initial_hunk(get_u32(&data_[pos_]))) {
            const auto ht = read_u32();
            std::cout << "\t\t" << hunk_type_string(ht) << "\n";
            switch (ht) {
            case HUNK_SYMBOL: {
                auto syms = read_hunk_symbol();
                for (auto& s : syms) {
                    symbols.push_back({ std::move(s.name), hunks[hunk_num].addr + s.addr });
                    std::cout << "\t\t\t$" << hexfmt(symbols.back().addr) << "\t" << symbols.back().name << "\n"; 
                }
                break;
            }
            case HUNK_RELOC32:
                for (const auto& r : read_hunk_reloc32()) {
                    if (r.hunk_ref > last_hunk)
                        throw std::runtime_error { "Invalid RELOC32 refers to unknown hunk " + std::to_string(r.hunk_ref) };
                    auto& hd = hunks[hunk_num].data;
                    for (const auto ofs: r.relocs) {
                        if (ofs > hd.size() - 4)
                            throw std::runtime_error { "Invalid relocation" };
                        auto c = &hd[ofs];
                        put_u32(c, get_u32(c) + hunks[r.hunk_ref].addr);
                    }
                }
                break;
            case HUNK_END:
                break;
            case HUNK_OVERLAY: {
                // HACK: Just skip everything after HUNK_OVERLAY for now...
                // See http://aminet.net/package/docs/misc/Overlay for more info
                const auto overlay_table_size = read_u32() + 1;
                std::cout << "\t\t\tTable size " << overlay_table_size << "\n";
                check_pos(overlay_table_size << 2);
                hexdump(std::cout, &data_[pos_], overlay_table_size << 2);
                table_index = table_size - 1;
                pos_ = static_cast<uint32_t>(data_.size()); // Skip to end
                break;
            }
            default:
                throw std::runtime_error { "Unsupported HUNK type " + hunk_type_string(ht) + " in executable" };
            }
        }

        ++table_index;
    }

    if (table_index != table_size)
        throw std::runtime_error { "Only " + std::to_string(table_index) + " out of " + std::to_string(table_size) + " hunks read" };

    if (pos_ != data_.size())
        throw std::runtime_error { "File not done pos=$" + hexstring(pos_) + " size=" + hexstring(data_.size(), 8) };

    if (!a_) {
        for (uint32_t i = 0; i < table_size; ++i) {
            if (hunks[i].type != HUNK_CODE)
                continue;
            disasm_stmts(hunks[i].data, 0, hunks[i].loaded_size, hunks[i].addr);
        }
    } else {
        for (const auto& s : symbols)
            a_->add_label(s.addr, s.name, unknown_type);
        bool has_code = false;
        for (const auto& h : hunks) {
            a_->write_data(h.addr, h.data.data(), static_cast<uint32_t>(h.data.size()));
            if (!has_code && h.type == HUNK_CODE) {
                a_->add_label(h.addr, "$$entry", code_type); // Add label if not already present
                a_->add_root(h.addr, simregs {});
                has_code = true;
            }
        }
        assert(has_code);

        a_->run();
    }
}

void hunk_file::read_hunk_unit()
{
    if (a_)
        throw std::runtime_error { "Analyze not supported for HUNK_UNIT" };

    const auto unit_name = read_string();
    std::cout << "HUNK_UNIT '" << unit_name << "'\n";

    std::vector<std::vector<uint8_t>> code_hunks;

    while (pos_ < data_.size()) {
        auto hunk_type = read_u32();
        const auto flags = hunk_type >> 29;
        hunk_type &= 0x1fffffff;
        switch (hunk_type) {
        case HUNK_NAME: {
            const auto name = read_string();
            std::cout << "\tHUNK_NAME " << name << "\n";
            break;
        }
        case HUNK_CODE:
        case HUNK_DATA: {
            const auto size = read_u32() << 2;
            check_pos(size);
            std::cout << "\t" << hunk_type_string(hunk_type) << " size=$" << hexfmt(size) << " flags=" << (flags>>1) << "\n";
            if (hunk_type == HUNK_CODE)
                code_hunks.push_back(std::vector<uint8_t>(&data_[pos_], &data_[pos_ + size]));
            pos_ += size;
            break;
        }
        case HUNK_BSS: {
            const auto size = read_u32() << 2;
            std::cout << "\t" << hunk_type_string(hunk_type) << " size=$" << hexfmt(size) << " flags=" << (flags >> 1) << "\n";
            break;
        }
        case HUNK_RELOC32:
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            read_hunk_reloc32();
            break;
        case HUNK_EXT: {
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            while (const auto ext = read_u32()) {
                const auto sym_type = static_cast<uint8_t>(ext >> 24);
                const auto name     = read_string_size(ext & 0xffffff);
                std::cout << "\t\t" << ext_type_string(sym_type) << "\t" << name << "\n";
                switch (sym_type) {
                case EXT_DEF: {
                    const auto offset = read_u32();
                    std::cout << "\t\t\t$" << hexfmt(offset) << "\n";
                    break;
                }
                case EXT_REF8:
                case EXT_REF16:
                case EXT_REF32: {
                    const auto nref = read_u32();
                    for (uint32_t i = 0; i < nref; ++i) {
                        const auto offset = read_u32();
                        std::cout << "\t\t\t$" << hexfmt(offset) << "\n";
                    }
                    break;
                }
                default:
                    throw std::runtime_error { "Unsupported external reference of type " + ext_type_string(sym_type) };
                }
            }
            break;
        }
        case HUNK_SYMBOL: {
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            const auto syms = read_hunk_symbol();
            for (const auto& s : syms) {
                std::cout << "\t\t$" << hexfmt(s.addr) << "\t" << s.name << "\n";
            }
            break;
        }
        case HUNK_END:
            std::cout << "\t" << hunk_type_string(hunk_type) << "\n";
            break;
        default:
            throw std::runtime_error { "Unsupported HUNK type " + hunk_type_string(hunk_type) + " in unit" };
        }
    }

    if (pos_ != data_.size())
        throw std::runtime_error { "File not done pos=$" + hexstring(pos_) + " size=" + hexstring(data_.size(), 8) };

    for (const auto& c : code_hunks) {
        std::cout << "\n";
        disasm_stmts(c, 0, static_cast<uint32_t>(c.size()));
    }
}

void read_hunk(const std::vector<uint8_t>& data, analyzer* a)
{
    hunk_file hf { data, a };
}

struct rom_tag {
    uint32_t matchtag;  // RT_MATCHTAG     (pointer RTC_MATCHWORD)
    uint32_t endskip;   // RT_ENDSKIP      (pointer to end of code)
    uint8_t  flags;     // RT_FLAGS        (no flags)
    uint8_t  version;   // RT_VERSION      (version number)
    uint8_t  type;      // RT_TYPE         (NT_LIBRARY)
    uint8_t  priority;  // RT_PRI          (priority = 126)
    uint32_t name_addr; // RT_NAME         (pointer to name)
    uint32_t id_addr;   // RT_IDSTRING     (pointer to ID string)
    uint32_t init_addr; // RT_INIT         (execution address)*

    std::string name;
};

void handle_rom(const std::vector<uint8_t>& data, analyzer* a)
{
    const auto start = get_u32(&data[4]);
    //std::cout << "ROM detected\n";
    //std::cout << "Entry point: $" << hexfmt(start) << "\n";
    const uint32_t rom_base = start & 0xffff0000;
    if (rom_base != 0xf80000 && rom_base != 0xfc0000)
        throw std::runtime_error { "Unsupported ROM base: $" + hexstring(rom_base) };
    //std::cout << "Base address: $" << hexfmt(rom_base) << "\n";
    if (start - rom_base >= data.size())
        throw std::runtime_error { "Invalid ROM entry point: $" + hexstring(start) };

    // Scan for ROM tags
    std::vector<rom_tag> rom_tags;
    for (uint32_t offset = 0; offset < data.size() - 24;) {
        if (get_u16(&data[offset]) != 0x4afc || get_u32(&data[offset + 2]) != offset + rom_base) {
            offset += 2;
            continue;
        }

        std::cout << "Found tag at $" << hexfmt(rom_base + offset) << "\n";
        rom_tag tag
        {
            .matchtag = get_u32(&data[offset + 2]),
            .endskip = get_u32(&data[offset + 6]),
            .flags = data[offset + 10],
            .version = data[offset + 11],
            .type = data[offset + 12],
            .priority = data[offset + 13],
            .name_addr = get_u32(&data[offset + 14]),
            .id_addr = get_u32(&data[offset + 18]),
            .init_addr = get_u32(&data[offset + 22]),
        };

        uint32_t end_offset = tag.endskip - rom_base;
        if (end_offset <= offset || end_offset > data.size()) {
            std::cout << "Invalid end offset\n";
            end_offset = offset + 26;
        }

        auto get_string = [&](uint32_t addr) {
            if (addr <= rom_base || addr - rom_base >= data.size())
                throw std::runtime_error { "Invalid string in ROM tag: addr=$" + hexstring(addr) };
            addr -= rom_base;
            std::string s;
            for (; addr < data.size() && data[addr]; ++addr) {
                if (data[addr] >= ' ' && data[addr] < 128)
                    s.push_back(data[addr]);
                else
                    s += "\\x" + hexstring(data[addr]);
            }
            return s;
        };

        tag.name = get_string(tag.name_addr);

        std::cout << "  End=$" << hexfmt(tag.endskip) << "\n";
        std::cout << "  Flags=$" << hexfmt(tag.flags) << " version=$" << hexfmt(tag.version) << " type=$" << hexfmt(tag.type) << " priority=$" << hexfmt(tag.priority) << "\n";
        std::cout << "  Name='" << tag.name << "'\n";
        std::cout << "  ID='" << get_string(tag.id_addr) << "'\n";
        std::cout << "  Init=$" << hexfmt(tag.init_addr) << "\n";

        rom_tags.push_back(tag);
        offset = end_offset;
    }

    if (!a) {
        if (rom_tags.empty()) {
            disasm_stmts(data, start - rom_base, static_cast<uint32_t>(data.size()), rom_base);
        } else {
            for (const auto& tag : rom_tags) {
                disasm_stmts(data, tag.init_addr - rom_base, tag.endskip - rom_base, rom_base);
            }
        }
        return;
    }

    a->write_data(rom_base, data.data(), static_cast<uint32_t>(data.size()));

    bool has_entry_name = false;
    for (const auto& tag : rom_tags) {
        std::string name;
        for (char c : tag.name) {
            if (isalpha(c) || (!name.empty() && isdigit(c)))
                name.push_back(c);
            else
                name.push_back('_');
        }

        auto slen = [&](uint32_t addr) {
            addr -= rom_base;
            uint32_t l = 0;
            while (addr + 1 < data.size() && data[addr++])
                ++l;
            return l + 1; // Include nul terminator
        };

        a->add_label(tag.matchtag, name + "_matchword", word_type);
        a->add_label(tag.matchtag + 2, name + "_matchtag", unknown_ptr); // Pointer to struct Resident
        a->add_label(tag.matchtag + 6, name + "_endskip", unknown_ptr);
        a->add_label(tag.matchtag + 10, name + "_flags", byte_type);
        a->add_label(tag.matchtag + 11, name + "_version", byte_type);
        a->add_label(tag.matchtag + 12, name + "_type", byte_type);
        a->add_label(tag.matchtag + 13, name + "_priority", byte_type);
        a->add_label(tag.matchtag + 14, name + "_name_addr", char_ptr);
        a->add_label(tag.matchtag + 18, name + "_id_addr", char_ptr);
        a->add_label(tag.matchtag + 22, name + "_init_addr", code_ptr);

        a->add_label(tag.init_addr, name + "_init", code_type);
        a->add_label(tag.name_addr, name + "_name", make_array_type(char_type, slen(tag.name_addr)));
        a->add_label(tag.id_addr, name + "_id", make_array_type(char_type, slen(tag.id_addr)));
        a->add_root(tag.init_addr, simregs {});
        if (tag.init_addr == start)
            has_entry_name = true;
    }

    if (!has_entry_name) {
        a->add_label(start, "$$entry", code_type);
        a->add_root(start, simregs {});
    }

    a->add_label(rom_base, "RomStart", word_type);
    a->run();
}

void usage()
{
    std::cerr << "Usage: m68kdisasm [-a] [-i info] file [options...]\n";
    std::cerr << "   file    source file\n";
    std::cerr << "   -a      analyze\n";
    std::cerr << "   -i      infofile (implies -a)\n";
    std::cerr << "\n";
    std::cerr << "Options for non-hunk files:";
    std::cerr << "   Normal (non-analysis mode) options: [offset] [end] - Normal dissasembly of starting from offset..end\n";
    std::cerr << "   Analayze options: PC starting program counter of memory dump\n";
}

int main(int argc, char* argv[])
{
    try {
        std::unique_ptr<analyzer> a;
        while (argc >= 2) {
            if (!strcmp(argv[1], "-a")) {
                if (!a)
                    a = std::make_unique<analyzer>();
                ++argv;
                --argc;
            } else if (!strcmp(argv[1], "-i")) {
                ++argv;
                --argc;
                if (argc < 2) {
                    usage();
                    return 1;
                }
                if (!a)
                    a = std::make_unique<analyzer>();
                a->read_info_file(argv[1]);
                ++argv;
                --argc;
            } else {
                break;
            }
        }

        if (argc < 2) {
            usage();
            return 1;
        }
        
        const auto data = read_file(argv[1]);
        if (data.size() > 4 && (get_u32(&data[0]) == HUNK_HEADER || get_u32(&data[0]) == HUNK_UNIT)) {
            if (argc > 2)
                throw std::runtime_error { "Too many arguments" };
            read_hunk(data, a.get());
        } else if (data.size() >= 8 * 1024 && data[0] == 0x11 && (data[1] == 0x11 || data[1] == 0x14) && get_u16(&data[2]) == 0x4ef9) {
            // ROM
            if (argc > 2)
                throw std::runtime_error { "Too many arguments" };
            handle_rom(data, a.get());
        } else {
            if (a) {
                if (argc <= 2)
                    throw std::runtime_error { "Missing start PC" };
                const auto offset = hex_or_die(argv[2]);
                a->write_data(0, data.data(), static_cast<uint32_t>(data.size()));
                a->add_root(offset, simregs {});
                a->run();
            } else {
                if (argc > 4)
                    throw std::runtime_error { "Too many arguments" };
                const auto offset = argc > 2 ? hex_or_die(argv[2]) : 0;
                const auto end = argc > 3 ? hex_or_die(argv[3]) : static_cast<uint32_t>(data.size());
                disasm_stmts(data, offset, end);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
