#include <cassert>
#include <iostream>

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

#include <queue>
#include <set>
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
            return os << "$" << hexfmt(v.raw_);
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

class analyzer {
public:
    explicit analyzer()
        : written_(max_ram)
        , data_(max_ram)        
        , regs_ {}
    {
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

    void add_root(uint32_t addr)
    {
        // Don't visit non-written areas
        if (!written_[addr])
            return;

        if (visited_.find(addr) == visited_.end()) {
            roots_.push(addr);
            visited_.insert(addr); // Mark visitied now to avoid re-insertion
        }
        // Add automatic label
        if (labels_.find(addr) == labels_.end())
            labels_[addr] = "lab_" + hexstring(addr);
    }

    void add_label(uint32_t addr, const std::string& name)
    {
        if (labels_.find(addr) == labels_.end())
            labels_[addr] = name;
    }

    void run()
    {
        while (!roots_.empty()) {
            const auto r = roots_.front();
            roots_.pop();
            trace(r);
        }

        for (const auto& area : areas_) {
            uint32_t pos = area.beg;

            while (pos < area.end) {

                if (visited_.find(pos) == visited_.end()) {
                    const auto next_visited = visited_.upper_bound(pos);
                    const auto next_visited_pos = std::min(area.end, next_visited == visited_.end() ? ~0U : *next_visited);
                    handle_data_area(pos, next_visited_pos);
                    pos = next_visited_pos;
                    continue;
                }

                maybe_print_label(pos);
                uint16_t iwords[max_instruction_words];
                read_instruction(iwords, pos);
                const auto& inst = instructions[iwords[0]];

                std::cout << "\t" << inst.name;
                for (int i = 0; i < inst.nea; ++i) {
                    const auto ea = inst.ea[i];
                    std::cout << (i ? ", " : "\t");
                    switch (ea >> ea_m_shift) {
                    case ea_m_Dn:
                        std::cout << "D" << (ea & ea_xn_mask);
                        break;
                    case ea_m_An:
                        std::cout << "A" << (ea & ea_xn_mask);
                        break;
                    case ea_m_A_ind:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")";
                        break;
                    case ea_m_A_ind_post:
                        std::cout << "(A" << (ea & ea_xn_mask) << ")+";
                        break;
                    case ea_m_A_ind_pre:
                        std::cout << "-(A" << (ea & ea_xn_mask) << ")";
                        break;
                    case ea_m_A_ind_disp16: {
                        auto n = static_cast<int16_t>(ea_data_[i]);
                        std::cout << "$";
                        if (n < 0) {
                            std::cout << "-";
                            n = -n;
                        }
                        std::cout << hexfmt(static_cast<uint16_t>(n));
                        std::cout << "(A" << (ea & 7) << ")";
                        break;
                    }
                    case ea_m_A_ind_index: {
                        const auto extw = ea_data_[i];
                        // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                        auto disp = static_cast<int8_t>(extw & 255);
                        std::cout << "$";
                        if (disp < 0) {
                            std::cout << "-";
                            disp = -disp;
                        }
                        std::cout << hexfmt(static_cast<uint8_t>(disp)) << "(A" << (ea & 7) << ",";
                        std::cout << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                        std::cout << ")";
                        break;
                    }
                    case ea_m_Other:
                        switch (ea & ea_xn_mask) {
                        case ea_other_abs_w:
                        case ea_other_abs_l:
                        case ea_other_pc_disp16:
                            print_addr(ea_val_[i].raw());
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
                            print_addr(ea_val_[i].raw());
                        } else {
                            std::cout << "#$" << hexfmt(inst.data);
                        }
                        break;
                    }
                }
                std::cout << "\n";
                pos += inst.ilen * 2;
            }
        }

    }

private:
    static constexpr uint32_t max_ram = 10 << 20;
    std::vector<bool> written_;
    std::vector<uint8_t> data_;
    std::queue<uint32_t> roots_;
    std::set<uint32_t> visited_;
    std::map<uint32_t, std::string> labels_;
    simval regs_[16];

    struct area {
        uint32_t beg;
        uint32_t end;
    };
    std::vector<area> areas_;

    uint32_t ea_data_[2];
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

    void maybe_print_label(uint32_t pos) const
    {
        if (auto it = labels_.find(pos); it != labels_.end())
            std::cout << std::setw(32) << std::left << it->second << "\t; $" << hexfmt(it->first) << "\n";
    }

    void handle_data_area(uint32_t pos, uint32_t end)
    {
        while (pos < end) {
            maybe_print_label(pos);
            const auto next_label = labels_.upper_bound(pos);
            const auto next_pos = std::min(end, next_label == labels_.end() ? ~0U : next_label->first);

            uint32_t runlen = 0;
            uint16_t runword = 0;
            auto output_run = [&]() {
                if (!runlen)
                    return;
                if (runlen > 1)
                    std::cout << "\tds.w\t$" << hexfmt(runword) << ", " << runlen << "\n";
                else
                    std::cout << "\tdc.w\t$" << hexfmt(runword) << "\n";
                runlen = 0;
            };
            for (; pos < next_pos; pos += 2) {
                const auto w = get_u16(&data_[pos]);
                if (runlen && w == runword) {
                    ++runlen;
                } else {
                    output_run();
                    runlen = 1;
                    runword = w;
                }
            }
            output_run();
        }
    }

    bool print_addr_maybe(uint32_t addr)
    {
        if (auto it = labels_.find(addr); it != labels_.end()) {
            std::cout << it->second;
            return true;
        }

        if (addr == 4) {
            std::cout << "AbsExecBase";
            return true;
        }

        #if 0
        //constexpr uint32_t cia_base_addr = 0xA00000;
        //constexpr uint32_t cia_mem_size = 0xC00000 - cia_base_addr;
        if (addr >= 0xA00000 && addr < 0xC00000) {
            throw std::runtime_error { "TODO: CIA" };
        }

        //constexpr uint32_t custom_base_addr = 0xDE0000;
        //constexpr uint32_t custom_mem_size = 0xE00000 - 0xDE0000;
        if (addr >= 0xDE0000 && addr < 0xE00000) {
            throw std::runtime_error { "TODO: CUSTOM REG" };
        }
        #endif

        return false;
    }

    void print_addr(uint32_t addr)
    {
        if (!print_addr_maybe(addr))
            std::cout << "$" << hexfmt(addr);
    }

    uint16_t read_iword(uint32_t addr)
    {
        if ((addr & 1) || addr < 0x400 || addr > max_ram - 2)
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
        ea_data_[0] = 0;
        ea_val_[1] = simval {};
        ea_data_[1] = 0;

        for (int i = 0; i < inst.nea; ++i) {
            const auto ea = inst.ea[i];
            switch (ea >> ea_m_shift) {
            case ea_m_Dn:
                ea_val_[i] = regs_[(ea & ea_xn_mask)];
                break;
            case ea_m_An:
                ea_val_[i] = regs_[8 + (ea & ea_xn_mask)];
                break;
            case ea_m_A_ind:
                // TODO: ea_val_
                break;
            case ea_m_A_ind_post:
                // TODO: ea_val_
                // TODO: increment
                break;
            case ea_m_A_ind_pre:
                // TODO: decrement
                // TODO: ea_val_
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
                    ea_val_[i] = simval { ea_data_[i] };
                    break;
                case ea_other_abs_l:
                    ea_data_[i] = iwords[eaw] << 16 | iwords[eaw + 1];
                    ea_val_[i] = simval { ea_data_[i] };
                    eaw += 2;
                    break;
                case ea_other_pc_disp16: {
                    assert(eaw < inst.ilen);
                    int16_t n = iwords[eaw++];
                    ea_data_[i] = static_cast<uint32_t>(n);
                    ea_val_[i] = simval { addr + (eaw - 1) * 2 + n };
                    break;
                }
                case ea_other_pc_index: {
                    assert(eaw < inst.ilen);
                    const auto extw = iwords[eaw++];
                    ea_data_[i] = static_cast<uint32_t>(extw);
                    // TODO: ea_val_
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
                    ea_val_[i] = simval { addr + 2 + disp };
                } else if (ea == ea_disp) {
                    ea_data_[i] = static_cast<int8_t>(inst.data);
                    ea_val_[i] = simval { addr + 2 + static_cast<int8_t>(inst.data) };
                } else {
                }
                break;
            }
        }
        assert(eaw == inst.ilen);

    }

    void trace(uint32_t addr)
    {
        for (;;) {
            const auto start = addr;
            visited_.insert(start);

            uint16_t iwords[max_instruction_words];
            read_instruction(iwords, addr);
            const auto& inst = instructions[iwords[0]];
            addr += 2 * inst.ilen;
#if 0
            for (uint32_t i = 0; i < 16; ++i) {
                if (i == 8)
                    std::cout << "\n";
                else if (i)
                    std::cout << " ";
                std::cout << (i & 8 ? "A" : "D") << (i & 7) << "=" << std::setw(9) << std::left << regs_[i];
            }
            std::cout << "\n";
            disasm(std::cout, start, iwords, inst.ilen);
            std::cout << "\n";
#endif

            switch (inst.type) {
            case inst_type::Bcc:
            case inst_type::BSR:
            case inst_type::BRA: {
                assert(inst.nea == 1 && inst.ea[0] == ea_disp);
                add_root(ea_val_[0].raw());
                if (inst.type == inst_type::BRA)
                    return;
                break;
            }
            case inst_type::DBcc:
                assert(inst.nea == 2 && inst.ea[1] == ea_disp && inst.ilen == 2);
                add_root(ea_val_[1].raw());
                break;

            case inst_type::JSR:
            case inst_type::JMP:
                assert(inst.nea == 1);

                if (ea_val_[0].known())
                    add_root(ea_val_[0].raw());

                if (inst.type == inst_type::JMP)
                    return;
                break;

            case inst_type::ILLEGAL:
                //throw std::runtime_error { "ILLEGAL" };
                return;

            case inst_type::RTS:
            case inst_type::RTE:
            case inst_type::RTR:
                return;
            }       
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
    explicit hunk_file(const std::vector<uint8_t>& data, bool analyze)
        : data_ { data }
        , analyze_ { analyze }
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
    bool analyze_;
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

    if (!analyze_) {
        for (uint32_t i = 0; i < table_size; ++i) {
            if (hunks[i].type != HUNK_CODE)
                continue;
            disasm_stmts(hunks[i].data, 0, hunks[i].loaded_size, hunks[i].addr);
        }
    } else {
        analyzer a;

        for (const auto& s : symbols)
            a.add_label(s.addr, s.name);
        bool has_code = false;
        for (const auto& h : hunks) {
            a.write_data(h.addr, h.data.data(), static_cast<uint32_t>(h.data.size()));
            if (!has_code && h.type == HUNK_CODE) {
                a.add_label(h.addr, "$$entry"); // Add label if not already present
                a.add_root(h.addr);
                has_code = true;
            }
        }
        assert(has_code);

        a.run();
    }
}

void hunk_file::read_hunk_unit()
{
    if (analyze_)
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

void read_hunk(const std::vector<uint8_t>& data, bool analyze)
{
    hunk_file hf { data, analyze };
}

int main(int argc, char* argv[])
{
    try {
        if (argc < 2)
            throw std::runtime_error { "Usage: m68kdisasm [-a] file [offset] [end]" };

        bool analyze = false;
        if (!strcmp(argv[1], "-a")) {
            analyze = true;
            ++argv;
            --argc;
        }
        
        const auto data = read_file(argv[1]);
        if (data.size() > 4 && (get_u32(&data[0]) == HUNK_HEADER || get_u32(&data[0]) == HUNK_UNIT)) {
            read_hunk(data, analyze);
        } else {
            if (analyze)
                throw std::runtime_error { "Analyze not supported for raw files yet" };

            const auto offset = argc > 2 ? hex_or_die(argv[2]) : 0;
            const auto end = argc > 3 ? hex_or_die(argv[3]) : static_cast<uint32_t>(data.size());
            disasm_stmts(data, offset, end);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
