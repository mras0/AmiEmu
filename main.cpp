#include <iostream>
#include <stdint.h>
#include <vector>
#include <string>
#include <fstream>
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
        std::cerr << "Unhandled read from $" << hexfmt(addr) << "\n";
        return 0xff;
    }
    uint16_t read_u16(uint32_t addr, uint32_t) override
    {
        std::cerr << "Unhandled read from $" << hexfmt(addr) << "\n";
        return 0xffff;
    }
    void write_u8(uint32_t addr, uint32_t, uint8_t val) override
    {
        std::cerr << "Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
    }
    void write_u16(uint32_t addr, uint32_t, uint16_t val) override
    {
        std::cerr << "Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
    }
};

class memory_handler {
public:
    explicit memory_handler()
    {
    }

    memory_handler(const memory_handler&) = delete;
    memory_handler& operator=(const memory_handler&) = delete;

    void register_handler(memory_area_handler& h, uint32_t base, uint32_t len)
    {
        assert(&find_area(base) == &def_area_);
        areas_.push_back(area {
            base, len, &h });
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
    area def_area_ { 0, 1U << 24, &def_handler_ };

    area& find_area(uint32_t addr)
    {
        addr &= 0xffffff;
        for (auto& a : areas_) {
            if (addr >= a.base && addr < a.base + a.len)
                return a;
        }
        return def_area_;
    }
};

class rom_area_handler : public memory_area_handler {
public:
    explicit rom_area_handler(memory_handler& mh, std::vector<uint8_t>&& data)
        : rom_data_ { std::move(data) }
    {
        const auto size = static_cast<uint32_t>(rom_data_.size());
        if (size != 256 * 1024 && size != 512*1024) {
            throw std::runtime_error { "Unexpected size of ROM" };
        }
        mh.register_handler(*this, 0, size);
        mh.register_handler(*this, 0xf80000, size);
        if (rom_data_.size() != 512 * 1024)
            mh.register_handler(*this, 0xfc0000, size);
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
        std::cout << "byte write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
        assert(0);
    }

    void write_u16(uint32_t addr, uint32_t offset, uint16_t val) override
    {
        std::cout << "word write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
        assert(0);
    }

private:
    std::vector<uint8_t> rom_data_;
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
    srm_ccr_no_x = srm_c | srm_v | srm_z | srm_z,
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
        switch (c) {
        case conditional::t:
            return true;
        case conditional::f:
            return false;
        default:
            assert(static_cast<unsigned>(c) < 16);
            std::cerr << "TODO: condition: " << conditional_strings[static_cast<uint8_t>(c)] << "\n";
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
    os << "PC=" << hexfmt(s.pc) << " SR=" << hexfmt(s.sr) << " SSP=" << hexfmt(s.ssp) << " USP=" << hexfmt(s.usp) << "\n";
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
    }

    void step()
    {
        print_cpu_state(std::cout, state_);

        start_pc_ = state_.pc;
        iwords_[0] = mem_.read_u16(state_.pc);
        state_.pc += 2;
        inst_ = &instructions[iwords_[0]];
        for (unsigned i = 1; i < inst_->ilen; ++i) {
            iwords_[i] = mem_.read_u16(state_.pc);
            state_.pc += 2;
        }

        disasm(std::cout, start_pc_, iwords_, inst_->ilen);
        std::cout << "\n";

        if (inst_->type == inst_type::ILLEGAL)
            throw std::runtime_error { "ILLEGAL" };

        assert(inst_->nea <= 2);

        iword_idx_ = 1;

        // Pre-increment & handle ea
        for (uint8_t i = 0; i < inst_->nea; ++i) {
            const auto ea = inst_->ea[i];
            if ((ea >> ea_m_shift) == ea_m_A_ind_pre) {
                state_.A(ea & ea_xn_mask) -= opsize_bytes(inst_->size);
            }
            handle_ea(i);
        }

        switch (inst_->type) {
        #define HANDLE_INST(t) case inst_type::t: handle_##t(); break;
            HANDLE_INST(BRA);
            HANDLE_INST(DBcc);
            HANDLE_INST(LEA);
            HANDLE_INST(LSR);
            HANDLE_INST(MOVE);
            HANDLE_INST(MOVEQ);
            HANDLE_INST(SUB);
        #undef HANDLE_INST
        default:
            throw std::runtime_error { "Unhandled instruction" };
        }
        
        assert(iword_idx_ == inst_->ilen);

        // Post-increment
        for (uint8_t i = 0; i < inst_->nea; ++i) {
            const auto ea = inst_->ea[i];
            if ((ea >> ea_m_shift) == ea_m_A_ind_post) {
                state_.A(ea & ea_xn_mask) += opsize_bytes(inst_->size);
            }
        }
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
        case ea_m_A_ind_index:
            break;
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
                // Remember sign!
                break;
            case ea_other_abs_l:
                assert(iword_idx_ + 1 < inst_->ilen);
                res = iwords_[iword_idx_++] << 16;
                res |= iwords_[iword_idx_++];
                return;
            case ea_other_pc_disp16:
                break;
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
            assert(ea <= ea_disp); // TODO: CCR/SR/REGLIST
            if (inst_->extra & extra_disp_flag) {
                assert(ea == ea_disp);
                assert(iword_idx_ < inst_->ilen);
                res = start_pc_ + 2 + static_cast < int16_t > (iwords_[iword_idx_++]);
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
            // TODO: Read correct size at (val)
            break;
        case ea_m_A_ind_disp16:
            break;
        case ea_m_A_ind_index:
            break;
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
                // TODO: Read correct size at (val)
                break;
            case ea_other_pc_disp16:
                break;
            case ea_other_pc_index:
                break;
            case ea_other_imm:
                return val;
            }
            break;
        case ea_m_inst_data:
            return val;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea)};
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
                reg = sext(val, opsize::w);
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
            break;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) + " val = $" + hexstring(val) };
    }

    void update_flags(sr_mask srmask, uint32_t res, uint32_t carry)
    {
        assert(inst_->size != opsize::none);
        const uint32_t mask = inst_->size == opsize::b ? 0x80 : inst_->size == opsize::w ? 0x8000
                                                                                         : 0x80000000;
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
        const uint32_t mask = inst_->size == opsize::b ? 0x80 : inst_->size == opsize::w ? 0x8000
                                                                                         : 0x80000000;
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

    void handle_BRA()
    {
        assert(inst_->nea == 1);
        state_.pc = ea_data_[0];
    }

    void handle_DBcc()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) == ea_m_Dn && inst_->extra & extra_cond_flag);
        assert(inst_->size == opsize::w);

        if (state_.eval_cond(static_cast<conditional>(inst_->extra >> 4)))
            return;

        uint16_t val = static_cast<uint16_t>(read_ea(0));
        --val;
        write_ea(0, val);
        if (val != 0xffff) {
            state_.pc = ea_data_[1];
        }
    }

    void handle_LEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift == ea_m_An) && inst_->size == opsize::l);
        write_ea(1, ea_data_[0]);
        // No flags affected
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
            val >>= cnt;
            carry = !!((val >> (cnt - 1)) & 1);
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

    void handle_MOVEQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift == ea_m_inst_data) && inst_->ea[0] <= ea_disp && (inst_->ea[1] >> ea_m_shift == ea_m_Dn));
        const uint32_t src = static_cast<int32_t>(static_cast<int8_t>(read_ea(0)));
        write_ea(1, src);
        update_flags(srm_ccr_no_x, src, 0);
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
};

int main()
{
    try {
        memory_handler mem {};
        rom_area_handler rom { mem, read_file("../../rom.bin") };
        //rom_area_handler rom { mem, read_file("../../Misc/DiagROM/DiagROM") };
        m68000 cpu { mem };

        for (;;) {
            cpu.step();
            std::cout << "\n";
        }        


    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}