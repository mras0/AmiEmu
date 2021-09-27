#include "cpu.h"
#include "ioutil.h"
#include "instruction.h"
#include "memory.h"
#include "disasm.h"

#include <sstream>
#include <stdexcept>

namespace {

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

enum class interrupt_vector : uint8_t {
    reset_ssp = 0,
    reset_pc = 1,
    bus_error = 2,
    address_error = 3,
    illegal_instruction = 4,
    zero_divide = 5,
    chk_exception = 6,
    trapv_instruction = 7,
    privililege_violation = 8,
    trace = 9,
    line_1010 = 10,
    line_1111 = 11,
    level1 = 25,
    level2 = 26,
    level3 = 27,
    level4 = 28,
    level5 = 29,
    level6 = 30,
    level7 = 31,
};

}

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
    os << "PC=" << hexfmt(s.pc) << " SR=" << hexfmt(s.sr) << " SSP=" << hexfmt(s.ssp) << " USP=" << hexfmt(s.usp) << " CCR: ";

    for (unsigned i = 5; i--;) {
        if ((s.sr & (1 << i)))
            os << "CVZNX"[i];
        else
            os << '-';
    }
    os << '\n';
}

class m68000::impl {
public:
    explicit impl(memory_handler& mem, const cpu_state& state)
        : mem_ { mem }
        , state_ { state }
    {
    }

    explicit impl(memory_handler& mem)
        : mem_ { mem }
    {
        memset(&state_, 0, sizeof(state_));
        state_.sr = srm_s | srm_ipl; // 0x2700
        state_.ssp = mem.read_u32(0);
        state_.pc = mem.read_u32(4);
    }

    const cpu_state& state() const
    {
        return state_;
    }

    uint64_t instruction_count() const
    {
        return instruction_count_;
    }

    void trace(std::ostream* os)
    {
        trace_ = os;
    }

    void show_state(std::ostream& os)
    {
        os << "After " << instruction_count_ << " instructions:\n";
        print_cpu_state(os, state_);
        disasm(os, start_pc_, iwords_, inst_->ilen);
        os << '\n';
    }

    void step(uint8_t current_ipl)
    {
        assert(current_ipl < 8);

        ++instruction_count_;

        if (current_ipl > (state_.sr & srm_ipl) >> sri_ipl) {
            do_interrupt(current_ipl);
        }

        if (trace_)
            print_cpu_state(*trace_, state_);

        start_pc_ = state_.pc;
        iwords_[0] = mem_.read_u16(state_.pc);
        state_.pc += 2;
        inst_ = &instructions[iwords_[0]];
        for (unsigned i = 1; i < inst_->ilen; ++i) {
            iwords_[i] = mem_.read_u16(state_.pc);
            state_.pc += 2;
        }

        if (trace_) {
            disasm(*trace_, start_pc_, iwords_, inst_->ilen);
            *trace_ << '\n';
        }

        if ((inst_->extra & extra_priv_flag) && !(state_.sr & srm_s)) {
            state_.pc = start_pc_; // "The saved value of the program counter is the address of the first word of the instruction causing the privilege violation.
            do_trap(interrupt_vector::privililege_violation);
            goto out;
        }

        if (inst_->type == inst_type::ILLEGAL) {
            if ((iwords_[0] & 0xfffe) == 0x4e7a) {
                // For now only handle movec
                do_trap(interrupt_vector::illegal_instruction);
                goto out;
            }
            // line 1111
            if ((iwords_[0] & 0xf000) == 0xf000) {
                do_trap(interrupt_vector::line_1111);
                goto out;
            }

            throw std::runtime_error { "ILLEGAL" };
        }

        assert(inst_->nea <= 2);

        iword_idx_ = 1;

        // Pre-increment & handle ea, MOVEM handles things on its own
        if (inst_->type != inst_type::MOVEM) {
            for (uint8_t i = 0; i < inst_->nea; ++i) {
                const auto ea = inst_->ea[i];
                if ((ea >> ea_m_shift) == ea_m_A_ind_pre) {
                    state_.A(ea & ea_xn_mask) -= opsize_bytes(inst_->size);
                }
                handle_ea(i);
            }
        }

        switch (inst_->type) {
#define HANDLE_INST(t) \
    case inst_type::t: \
        handle_##t();  \
        break;
            HANDLE_INST(ABCD);
            HANDLE_INST(ADD);
            HANDLE_INST(ADDA);
            HANDLE_INST(ADDQ);
            HANDLE_INST(ADDX);
            HANDLE_INST(AND);
            HANDLE_INST(ASL);
            HANDLE_INST(ASR);
            HANDLE_INST(Bcc);
            HANDLE_INST(BRA);
            HANDLE_INST(BSR);
            HANDLE_INST(BCHG);
            HANDLE_INST(BCLR);
            HANDLE_INST(BSET);
            HANDLE_INST(BTST);
            HANDLE_INST(CLR);
            HANDLE_INST(CMP);
            HANDLE_INST(CMPA);
            HANDLE_INST(CMPM);
            HANDLE_INST(DBcc);
            HANDLE_INST(DIVU);
            HANDLE_INST(DIVS);
            HANDLE_INST(EOR);
            HANDLE_INST(EXG);
            HANDLE_INST(EXT);
            HANDLE_INST(JMP);
            HANDLE_INST(JSR);
            HANDLE_INST(LEA);
            HANDLE_INST(LINK);
            HANDLE_INST(LSL);
            HANDLE_INST(LSR);
            HANDLE_INST(MOVE);
            HANDLE_INST(MOVEA);
            HANDLE_INST(MOVEM);
            HANDLE_INST(MOVEQ);
            HANDLE_INST(MULS);
            HANDLE_INST(MULU);
            HANDLE_INST(NBCD);
            HANDLE_INST(NEG);
            HANDLE_INST(NEGX);
            HANDLE_INST(NOT);
            HANDLE_INST(NOP);
            HANDLE_INST(OR);
            HANDLE_INST(PEA);
            HANDLE_INST(ROL);
            HANDLE_INST(ROR);
            HANDLE_INST(ROXL);
            HANDLE_INST(ROXR);
            HANDLE_INST(RTE);
            HANDLE_INST(RTS);
            HANDLE_INST(SBCD);
            HANDLE_INST(Scc);
            HANDLE_INST(STOP);
            HANDLE_INST(SUB);
            HANDLE_INST(SUBA);
            HANDLE_INST(SUBQ);
            HANDLE_INST(SUBX);
            HANDLE_INST(SWAP);
            HANDLE_INST(TST);
            HANDLE_INST(UNLK);
#undef HANDLE_INST
        default: {
            std::ostringstream oss;
            disasm(oss, start_pc_, iwords_, inst_->ilen);
            throw std::runtime_error { "Unhandled instruction: " + oss.str() };
        }
        }

        assert(iword_idx_ == inst_->ilen);

        // Post-increment, MOVEM handles this on its own
        if (inst_->type != inst_type::MOVEM) {
            for (uint8_t i = 0; i < inst_->nea; ++i) {
                const auto ea = inst_->ea[i];
                if ((ea >> ea_m_shift) == ea_m_A_ind_post) {
                    state_.A(ea & ea_xn_mask) += opsize_bytes(inst_->size);
                }
            }
        }

    out:
        if (trace_)
            *trace_ << "\n";
    }

private:
    memory_handler& mem_;
    cpu_state state_;

    // Decoder state
    uint32_t start_pc_ = 0;
    uint16_t iwords_[max_instruction_words] = { illegal_instruction_num };
    uint8_t iword_idx_ = 0;
    const instruction* inst_ = &instructions[illegal_instruction_num];
    uint32_t ea_data_[2]; // For An/Dn/Imm/etc. contains the value, for all others the address
    uint64_t instruction_count_ = 0;
    std::ostream* trace_ = nullptr;

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
        throw std::runtime_error { "Invalid opsize" };
    }

    uint32_t read_mem(uint32_t addr)
    {
        switch (inst_->size) {
        case opsize::b:
            return mem_.read_u8(addr);
        case opsize::w:
            return mem_.read_u16(addr);
        case opsize::l:
            return mem_.read_u32(addr);
        }
        throw std::runtime_error { "Invalid opsize" };
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
        case ea_m_A_ind_index: {
            assert(iword_idx_ < inst_->ilen);
            const auto extw = iwords_[iword_idx_++];
            assert(!(extw & (7 << 8)));
            res = state_.A(ea & ea_xn_mask) + sext(extw, opsize::b);
            uint32_t r = (extw >> 12) & 7;
            if ((extw >> 15) & 1) {
                r = state_.A(r);
            } else {
                r = state_.d[r];
            }
            if (!((extw >> 11) & 1))
                r = sext(r, opsize::w);
            res += r;
            return;
        }
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
                assert(iword_idx_ < inst_->ilen);
                res = static_cast<int32_t>(static_cast<int16_t>(iwords_[iword_idx_++]));
                return;
            case ea_other_abs_l:
                assert(iword_idx_ + 1 < inst_->ilen);
                res = iwords_[iword_idx_++] << 16;
                res |= iwords_[iword_idx_++];
                return;
            case ea_other_pc_disp16:
                assert(iword_idx_ < inst_->ilen);
                res = start_pc_ + 2 + sext(iwords_[iword_idx_++], opsize::w);
                return;
            case ea_other_pc_index: {
                assert(iword_idx_ < inst_->ilen);
                const auto extw = iwords_[iword_idx_++];
                assert(!(extw & (7 << 8)));
                res = start_pc_ + 2 + sext(extw, opsize::b);
                uint32_t r = (extw >> 12) & 7;
                if ((extw >> 15) & 1) {
                    r = state_.A(r);
                } else {
                    r = state_.d[r];
                }
                if (!((extw >> 11) & 1))
                    r = sext(r, opsize::w);
                res += r;
                return;
            }
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
        default:
            if (ea == ea_sr) {
                res = state_.sr;
                return;
            } else if (ea == ea_ccr) {
                res = state_.sr & srm_ccr;
                return;
            } else if (ea == ea_reglist) {
                assert(iword_idx_ < inst_->ilen);
                res = iwords_[iword_idx_++];
                return;
            } else if (ea == ea_bitnum) {
                assert(iword_idx_ < inst_->ilen);
                res = iwords_[iword_idx_++];
                return;
            } else if (ea == ea_usp) {
                assert(state_.sr & srm_s);
                res = state_.usp;
                return;
            }
            assert(ea <= ea_disp);
            if (inst_->extra & extra_disp_flag) {
                assert(ea == ea_disp);
                assert(iword_idx_ < inst_->ilen);
                res = start_pc_ + 2 + static_cast<int16_t>(iwords_[iword_idx_++]);
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
        case ea_m_A_ind_disp16:
        case ea_m_A_ind_index:
            return read_mem(val);
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
            case ea_other_pc_disp16:
            case ea_other_pc_index:
                return read_mem(val);
            case ea_other_imm:
                return val;
            }
            break;
        default:
            return val;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) };
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
                reg = (reg & 0xffff0000) | (val & 0xffff);
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
        default:
            if (ea == ea_sr) {
                assert(!(val & srm_illegal));
                // Note: MOVE to SR is not actually privileged until 68010
                assert((state_.sr & srm_s) || inst_->type == inst_type::MOVE);
                state_.sr = static_cast<uint16_t>(val);
                return;
            } else if (ea == ea_ccr) {
                state_.update_sr(srm_ccr, val & srm_ccr);
                return;
            } else if (ea == ea_usp) {
                assert(state_.sr & srm_s); // Checked
                state_.usp = val;
                return;
            }
            break;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) + " val = $" + hexstring(val) };
    }

    void update_flags(sr_mask srmask, uint32_t res, uint32_t carry)
    {
        assert(inst_->size != opsize::none);
        const uint32_t mask = opsize_msb_mask(inst_->size);
        uint16_t ccr = 0;
        if (carry & mask) {
            ccr |= srm_c;
            ccr |= srm_x;
        }
        if (((carry << 1) ^ carry) & mask)
            ccr |= srm_v;
        if (!(res & opsize_all_mask(inst_->size)))
            ccr |= srm_z;
        if (res & mask)
            ccr |= srm_n;

        state_.update_sr(srmask, (ccr & srmask));
    }

    void update_flags_rot(uint32_t res, uint32_t cnt, bool carry)
    {
        assert(inst_->size != opsize::none);
        const uint32_t mask = opsize_msb_mask(inst_->size);
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

    void do_left_shift(bool arit)
    {
        uint32_t val, cnt;
        if (inst_->nea == 1) {
            val = read_ea(0);
            cnt = 1;
        } else {
            cnt = read_ea(0) & 63;
            val = read_ea(1);
        }
        const auto osmask = opsize_msb_mask(inst_->size);
        const auto orig_val = val;
        bool carry;
        if (!cnt) {
            carry = false;
        } else if (cnt < 32) {
            carry = !!((val << (cnt - 1)) & osmask);
            val <<= cnt;
        } else if (cnt == 32 && inst_->size == opsize::l) {
            val = 0;
            carry = !!(orig_val & 1);
        } else {
            val = 0;
            carry = false;
        }

        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
        if (arit && cnt) {
            const uint32_t nb = 8 * opsize_bytes(inst_->size);
            // Create mask of all bytes shifted through MSB
            const auto mask = cnt >= nb ? ~0U : ~((osmask - 1) >> cnt) & (osmask - 1);
            bool v = false;
            if (orig_val & osmask)
                v = (orig_val & mask) != mask;
            else
                v = !!(orig_val & mask);
            if (v)
                state_.update_sr(srm_v, srm_v);
        }
    }

    void push_u16(uint16_t val)
    {
        auto& a7 = state_.A(7);
        a7 -= 2;
        mem_.write_u16(a7, val);
    }

    void push_u32(uint32_t val)
    {
        auto& a7 = state_.A(7);
        a7 -= 4;
        mem_.write_u32(a7, val);
    }

    uint16_t pop_u16()
    {
        auto& a7 = state_.A(7);
        const auto val = mem_.read_u16(a7);
        a7 += 2;
        return val;
    }

    uint32_t pop_u32()
    {
        auto& a7 = state_.A(7);
        const auto val = mem_.read_u32(a7);
        a7 += 4;
        return val;
    }

    void do_interrupt_impl(interrupt_vector vec, uint8_t ipl)
    {
        assert(vec > interrupt_vector::reset_pc); // RESET doesn't save anything
        const uint16_t saved_sr = state_.sr;
        state_.update_sr(static_cast<sr_mask>(srm_trace | srm_s | srm_ipl), srm_s | ipl << sri_ipl); // Clear trace, set superviser mode
        // Now always on supervisor stack

        // From MC68000UM 6.2.5
        // "The current program
        // counter value and the saved copy of the status register are stacked using the SSP. The
        // stacked program counter value usually points to the next unexecuted instruction.
        // However, for bus error and address error, the value stacked for the program counter is
        // unpredictable and may be incremented from the address of the instruction that caused the
        // error."
        push_u32(state_.pc);
        push_u16(saved_sr);

        state_.pc = mem_.read_u32(static_cast<uint32_t>(vec) * 4);
    }

    void do_trap(interrupt_vector vec)
    {
        assert(vec < interrupt_vector::level1); // Should use do_interrupt
        do_interrupt_impl(vec, 7); // XXX: IPL 7?
    }

    void do_interrupt(uint8_t ipl)
    {
        assert(ipl >= 1 && ipl <= 7);
        // Amiga detail: Uses non-autovector and ignores the special bus cycle where A3-A1 is is to IPL and all other address lines are high to simple read from ROM
        const auto vec = mem_.read_u8(0xfffffff1 | ipl << 1);        
        do_interrupt_impl(static_cast<interrupt_vector>(vec), ipl);
    }

    void handle_ABCD()
    {
        assert(inst_->nea == 2 && inst_->size == opsize::b);
        const auto r = read_ea(0);
        const auto l = read_ea(1);
        auto res = l + r + !!(state_.sr & srm_x);
        const auto carry = ((l & r) | ((l | r) & ~res)) & 0x88;
        const auto carry10 = (((res + 0x66) ^ res) & 0x110) >> 1;
        res += (carry | carry10) - ((carry | carry10) >> 2);
        write_ea(1, res);
        state_.update_sr(static_cast<sr_mask>(srm_c | srm_x), res & 0xf00 ? srm_c | srm_x : 0);
        if (res & 0xff)
            state_.update_sr(srm_z, 0);
    }

    void handle_ADD()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l + r;
        write_ea(1, res);
        // All flags updated
        update_flags(srm_ccr, res, (l & r) | ((l | r) & ~res));
    }

    void handle_ADDA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        state_.A(inst_->ea[1] & ea_xn_mask) += s; // And the operation performed on the full 32-bit value
        // No flags
    }

    void handle_ADDQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift > ea_m_Other) && inst_->ea[0] <= ea_disp);
        handle_ADD();
    }

    void handle_ADDX()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l + r + !!(state_.sr & srm_x);
        write_ea(1, res);
        update_flags(static_cast<sr_mask>(srm_ccr & ~srm_z), res, (l & r) | ((l | r) & ~res));
        // Z is only cleared if the result is non-zero
        if (res & opsize_all_mask(inst_->size))
            state_.update_sr(srm_z, 0);
    }

    void handle_AND()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l & r;
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
    }

    void handle_ASL()
    {
        do_left_shift(true);
    }

    void handle_ASR()
    {
        uint32_t val, cnt;
        if (inst_->nea == 1) {
            val = read_ea(0);
            cnt = 1;
        } else {
            cnt = read_ea(0) & 63;
            val = read_ea(1);
        }
        const auto msb = !!(val & opsize_msb_mask(inst_->size));

        bool carry;
        if (!cnt) {
            carry = false;
        } else if (cnt < 32) {
            carry = !!((val >> (cnt - 1)) & 1);
            val = sext(val, inst_->size) >> cnt;
            if (msb && cnt >= opsize_bytes(inst_->size) * 8U) // These bits also count
                carry = true;
        } else {
            val = msb ? ~0U : 0;
            carry = msb;
        }

        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
    }

    void handle_Bcc()
    {
        assert(inst_->nea == 1 && inst_->ea[0] == ea_disp && (inst_->extra & extra_cond_flag));
        if (!state_.eval_cond(static_cast<conditional>(inst_->extra >> 4)))
            return;
        state_.pc = ea_data_[0];
    }

    void handle_BRA()
    {
        assert(inst_->nea == 1);
        state_.pc = ea_data_[0];
    }

    void handle_BSR()
    {
        assert(inst_->nea == 1);
        push_u32(state_.pc);
        state_.pc = ea_data_[0];
    }

    std::pair<uint32_t, uint32_t> bit_op_helper()
    {
        assert(inst_->nea == 2);
        auto bitnum = read_ea(0);
        const auto num = read_ea(1);
        if (inst_->size == opsize::b) {
            bitnum &= 7;
        } else {
            assert(inst_->size == opsize::l);
            bitnum &= 31;
        }
        state_.update_sr(srm_z, !((num >> bitnum) & 1) ? srm_z : 0); // Set according to the previous state of the bit
        return { bitnum, num };
    }

    void handle_BCHG()
    {
        const auto [bitnum, num] = bit_op_helper();
        write_ea(1, num ^ (1 << bitnum));
    }

    void handle_BCLR()
    {
        const auto [bitnum, num] = bit_op_helper();
        write_ea(1, num & ~(1 << bitnum));
    }

    void handle_BSET()
    {
        const auto [bitnum, num] = bit_op_helper();
        write_ea(1, num | (1 << bitnum));
    }

    void handle_BTST()
    {
        bit_op_helper(); // discard return value on purpose
    }

    void handle_CLR()
    {
        assert(inst_->nea == 1);
        // TODO: read cycle?
        write_ea(0, 0);
        state_.update_sr(srm_ccr_no_x, srm_z);
    }

    void handle_CMP()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r;
        update_flags(srm_ccr_no_x, res, (~l & r) | (~(l ^ r) & res));
    }

    void handle_CMPA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const uint32_t r = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        const uint32_t l = state_.A(inst_->ea[1] & ea_xn_mask);
        // And the performed on the full 32-bit value
        const auto res = l - r;
        update_flags(srm_ccr_no_x, res, (~l & r) | (~(l ^ r) & res));
    }

    void handle_CMPM()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) == ea_m_A_ind_post && (inst_->ea[1] >> ea_m_shift) == ea_m_A_ind_post);
        handle_CMP();
    }

    void handle_DBcc()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) == ea_m_Dn && inst_->extra & extra_cond_flag);
        assert(inst_->size == opsize::w && inst_->ea[1] == ea_disp);

        if (state_.eval_cond(static_cast<conditional>(inst_->extra >> 4)))
            return;

        uint16_t val = static_cast<uint16_t>(read_ea(0));
        --val;
        write_ea(0, val);
        if (val != 0xffff) {
            state_.pc = ea_data_[1];
        }
    }

    void handle_DIVU()
    {
        assert(inst_->nea == 2 && inst_->size == opsize::w && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        auto& reg = state_.d[inst_->ea[1] & ea_xn_mask];
        const auto d = read_ea(0);
        if (!d)
            throw std::runtime_error { "TODO: Handle division by zero" };

        const auto q = reg / d;
        const auto r = reg % d;
        reg = (q & 0xffff) | (r & 0xffff) << 16;
        uint16_t ccr = 0;
        if (q & 0x8000)
            ccr |= srm_n;
        if (!q)
            ccr |= srm_z;
        if (q > 0x8000)
            ccr |= srm_v;
        state_.update_sr(srm_ccr_no_x, ccr);
    }

    void handle_DIVS()
    {
        handle_DIVU();
    }

    void handle_EOR()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l ^ r;
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
    }

    void handle_EXG()
    {
        assert(inst_->size == opsize::l && inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) < 2 && (inst_->ea[1] >> ea_m_shift) < 2);
        const auto a = read_ea(0);
        const auto b = read_ea(1);
        write_ea(0, b);
        write_ea(1, a);
    }

    void handle_EXT()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift) == ea_m_Dn);
        auto r = state_.d[inst_->ea[0] & ea_xn_mask];
        uint32_t res;
        if (inst_->size == opsize::w) {
            res = sext(r, opsize::b);
        } else {
            assert(inst_->size == opsize::l);
            res = sext(r, opsize::w);
        }
        write_ea(0, res);
        update_flags(srm_ccr_no_x, res, 0);
    }

    void handle_JMP()
    {
        assert(inst_->nea == 1);
        state_.pc = ea_data_[0];
    }

    void handle_JSR()
    {
        assert(inst_->nea == 1);
        push_u32(state_.pc);
        state_.pc = ea_data_[0];
    }

    void handle_LEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift == ea_m_An) && inst_->size == opsize::l);
        write_ea(1, ea_data_[0]);
        // No flags affected
    }

    void handle_LINK()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift == ea_m_An) && (inst_->ea[1] == ea_immediate) && inst_->size == opsize::w);
        auto& a = state_.A(inst_->ea[0] & ea_xn_mask);
        push_u32(a);
        auto& sp = state_.A(7);
        a = sp;
        sp += sext(ea_data_[1], inst_->size);
    }

    void handle_LSL()
    {
        do_left_shift(false);
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
        } else if (cnt < 32) {
            carry = !!((val >> (cnt - 1)) & 1);
            val >>= cnt;
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
        update_flags(srm_ccr_no_x, src, 0); // Before write EA in case it's to CCR/SR
        write_ea(1, src);
    }

    void handle_MOVEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        state_.A(inst_->ea[1] & ea_xn_mask) = s; // Always write full 32-bit result
        // No flags
    }

    void handle_MOVEM()
    {
        assert(inst_->nea == 2);

        uint16_t reglist;
        uint32_t addr;
        bool reverse_mode = false;
        bool mem_to_reg = false;
        const auto nb = opsize_bytes(inst_->size);

        if (inst_->ea[0] == ea_reglist) {
            handle_ea(0);
            handle_ea(1);
            reglist = static_cast<uint16_t>(ea_data_[0]);
            addr = ea_data_[1];
            reverse_mode = inst_->ea[1] >> ea_m_shift == ea_m_A_ind_pre;
        } else {
            // reglist always immediately follows instruction
            handle_ea(1);
            handle_ea(0);
            reglist = static_cast<uint16_t>(ea_data_[1]);
            addr = ea_data_[0];
            mem_to_reg = true;
        }

        if (reverse_mode) {
            assert(inst_->ea[1] >> ea_m_shift == ea_m_A_ind_pre && !mem_to_reg);
            for (unsigned bit = 0; bit < 16; ++bit) {
                if (!(reglist & (1 << bit)))
                    continue;
                const auto regnum = 15 - bit;
                addr -= nb;
                write_mem(addr, regnum < 8 ? state_.d[regnum] : state_.A(regnum & 7));
            }
            // Update address register
            state_.A(inst_->ea[1] & ea_xn_mask) = addr;
        } else {
            for (unsigned bit = 0; bit < 16; ++bit) {
                if (!(reglist & (1 << bit)))
                    continue;
                auto& reg = bit < 8 ? state_.d[bit] : state_.A(bit & 7);
                if (mem_to_reg) {
                    assert(inst_->size == opsize::l);
                    reg = read_mem(addr);
                } else
                    write_mem(addr, reg);
                addr += nb;
            }

            // Update address register if post-increment mode
            if (inst_->ea[0] >> ea_m_shift == ea_m_A_ind_post) {
                state_.A(inst_->ea[0] & ea_xn_mask) = addr;
            }
        }
    }

    void handle_MOVEQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift > ea_m_Other) && inst_->ea[0] <= ea_disp && (inst_->ea[1] >> ea_m_shift == ea_m_Dn));
        const uint32_t src = static_cast<int32_t>(static_cast<int8_t>(read_ea(0)));
        write_ea(1, src);
        update_flags(srm_ccr_no_x, src, 0);
    }

    void handle_MULS()
    {
        assert(inst_->size == opsize::w && inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto a = sext(read_ea(0) & 0xffff, opsize::w);
        const auto b = sext(read_ea(1) & 0xffff, opsize::w);
        const uint32_t res = static_cast<int32_t>(a) * b;
        state_.d[inst_->ea[1] & ea_xn_mask] = res;
        uint16_t ccr = 0;
        if (!res)
            ccr |= srm_z;
        if (res & 0x80000000)
            ccr |= srm_n;
        state_.update_sr(srm_ccr_no_x, ccr);
    }

    void handle_MULU()
    {
        assert(inst_->size == opsize::w && inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto a = static_cast<uint16_t>(read_ea(0) & 0xffff);
        const auto b = static_cast<uint16_t>(read_ea(1) & 0xffff);
        const uint32_t res = static_cast<uint32_t>(a) * b;
        state_.d[inst_->ea[1] & ea_xn_mask] = res;
        uint16_t ccr = 0;
        if (!res)
            ccr |= srm_z;
        if (res & 0x80000000)
            ccr |= srm_n;
        state_.update_sr(srm_ccr_no_x, ccr);
    }

    void handle_NBCD()
    {
        assert(inst_->nea == 1);

        // SBCD with l = 0
        const uint32_t r = read_ea(0);
        const uint32_t l = 0;
        auto res = l - r - !!(state_.sr & srm_x);
        const auto carry10 = ((~l & r) | (~(l ^ r) & res)) & 0x88;
        res -= carry10 - (carry10 >> 2);
        write_ea(0, res);
        state_.update_sr(static_cast<sr_mask>(srm_c | srm_x), res & 0xf00 ? srm_c | srm_x : 0);
        if (res & 0xff)
            state_.update_sr(srm_z, 0);
    }

    void handle_NEG()
    {
        assert(inst_->nea == 1);
        auto n = read_ea(0);
        const bool overflow = (n == opsize_msb_mask(inst_->size));
        n = -sext(n, inst_->size);

        uint8_t ccr = 0;
        if (!n) {
            assert(!overflow);
            ccr |= srm_z;
        } else {
            ccr |= srm_c | srm_x;
            if (n & opsize_msb_mask(inst_->size))
                ccr |= srm_n;
            if (overflow)
                ccr |= srm_v;
        }
        write_ea(0, n);
        state_.update_sr(srm_ccr, ccr);
    }

    void handle_NEGX()
    {
        // Like SUBX with LHS = 0
        assert(inst_->nea == 1);
        const uint32_t r = read_ea(0);
        const uint32_t l = 0;
        const uint32_t res = l - r - !!(state_.sr & srm_x);
        write_ea(0, res);
        update_flags(static_cast<sr_mask>(srm_ccr & ~srm_z), res, (~l & r) | (~(l ^ r) & res));
        // Z is only cleared if the result is non-zero
        if (res & opsize_all_mask(inst_->size))
            state_.update_sr(srm_z, 0);

    }

    void handle_NOT()
    {
        assert(inst_->nea == 1);
        auto n = ~read_ea(0);
        update_flags(srm_ccr_no_x, n, 0);
        write_ea(0, n);
    }

    void handle_NOP()
    {
        return;
    }

    void handle_OR()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l | r;
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
    }

    void handle_PEA()
    {
        assert(inst_->nea == 1 && inst_->size == opsize::l);
        push_u32(ea_data_[0]);
    }

    void handle_ROL()
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
        } else {
            const auto nbits = opsize_bytes(inst_->size) * 8;
            cnt &= (nbits - 1);
            if (cnt)
                val = (val << cnt) | (val >> (nbits - cnt));
        }

        write_ea(inst_->nea - 1, val);
        const uint16_t old_x = state_.sr & srm_x; // X is not affected
        update_flags_rot(val, cnt, !!(val & opsize_msb_mask(inst_->size)));
        state_.update_sr(srm_x, old_x);
    }

    void handle_ROR()
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
        } else {
            const auto nbits = opsize_bytes(inst_->size) * 8;
            cnt &= (nbits-1);
            if (cnt)
                val = (val >> cnt) | (val << (nbits - cnt));
        }

        write_ea(inst_->nea - 1, val);
        const uint16_t old_x = state_.sr & srm_x; // X is not affected
        update_flags_rot(val, cnt, !!(val & 1));
        state_.update_sr(srm_x, old_x);
    }

    void handle_ROXL()
    {
        uint32_t val, cnt;
        if (inst_->nea == 1) {
            val = read_ea(0);
            cnt = 1;
        } else {
            cnt = read_ea(0) & 63;
            val = read_ea(1);
        }

        // TODO: Could optimize
        const auto msb = opsize_msb_mask(inst_->size);
        uint32_t x = !!(state_.sr & srm_x);
        while (cnt--) {
            const auto new_x = !!(val & msb);
            val = (val << 1) | x;
            x = new_x;
        }
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, !!x);
    }

    void handle_ROXR()
    {
        uint32_t val, cnt;
        if (inst_->nea == 1) {
            val = read_ea(0);
            cnt = 1;
        } else {
            cnt = read_ea(0) & 63;
            val = read_ea(1);
        }

        // TODO: Could optimize
        const auto shift = opsize_bytes(inst_->size) * 8 - 1;
        uint32_t x = !!(state_.sr & srm_x);
        while (cnt--) {
            const auto new_x = val & 1;
            val = (val >> 1) | (x << shift);
            x = new_x;
        }
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, !!x);
    }

    void handle_RTE()
    {
        assert(inst_->nea == 0);
        assert(state_.sr & srm_s);
        const auto sr = pop_u16();
        state_.pc = pop_u32();
        state_.sr = sr; // Only after popping PC (otherwise we switch stacks too early)
    }

    void handle_RTS()
    {
        assert(inst_->nea == 0);
        state_.pc = pop_u32();
    }

    void handle_SBCD()
    {
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        auto res = l - r - !!(state_.sr & srm_x);
        const auto carry10 = ((~l & r) | (~(l ^ r) & res)) & 0x88;
        res -= carry10 - (carry10 >> 2);
        write_ea(1, res);
        state_.update_sr(static_cast<sr_mask>(srm_c | srm_x), res & 0xf00 ? srm_c | srm_x : 0);
        if (res & 0xff)
            state_.update_sr(srm_z, 0);
    }

    void handle_Scc()
    {
        assert(inst_->nea == 1 && inst_->size == opsize::b && (inst_->extra & extra_cond_flag));
        const bool cond = state_.eval_cond(static_cast<conditional>(inst_->extra >> 4));
        write_ea(0, cond ? 0xff : 0x00);
    }

    void handle_STOP()
    {
        assert(inst_->nea == 1);
        state_.sr = static_cast<uint16_t>(read_ea(0));
        state_.pc = start_pc_;
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

    void handle_SUBA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        state_.A(inst_->ea[1] & ea_xn_mask) -= s; // And the operation performed on the full 32-bit value
        // No flags
    }

    void handle_SUBQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift > ea_m_Other) && inst_->ea[0] <= ea_disp);
        handle_SUB();
    }

    void handle_SUBX()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r - !!(state_.sr & srm_x);
        write_ea(1, res);
        update_flags(static_cast<sr_mask>(srm_ccr & ~srm_z), res, (~l & r) | (~(l ^ r) & res));
        // Z is only cleared if the result is non-zero
        if (res & opsize_all_mask(inst_->size))
            state_.update_sr(srm_z, 0);
    }

    void handle_SWAP()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift == ea_m_Dn));
        auto& r = state_.d[inst_->ea[0] & ea_xn_mask];
        r = (r & 0xffff) << 16 | ((r >> 16) & 0xffff);
        update_flags(srm_ccr_no_x, r, 0);
    }

    void handle_TST()
    {
        assert(inst_->nea == 1);
        update_flags(srm_ccr_no_x, read_ea(0), 0);
    }

    void handle_UNLK()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift) == ea_m_An);
        auto& a = state_.A(inst_->ea[0] & ea_xn_mask);
        state_.A(7) = a;
        a = pop_u32();
    }
};

m68000::m68000(memory_handler& mem, const cpu_state& state)
    : impl_ { std::make_unique<impl>(mem, state) }
{
}

m68000::m68000(memory_handler& mem)
    : impl_ {
        std::make_unique<impl>(mem)
    }
{
}

m68000::~m68000() = default;

const cpu_state& m68000::state() const
{
    return impl_->state();
}

uint64_t m68000::instruction_count() const
{
    return impl_->instruction_count();
}

void m68000::trace(std::ostream* os)
{
    impl_->trace(os);
}

void m68000::show_state(std::ostream& os)
{
    impl_->show_state(os);
}

void m68000::step(uint8_t current_ipl)
{
    impl_->step(current_ipl);
}