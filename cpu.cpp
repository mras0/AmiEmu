#include "cpu.h"
#include "ioutil.h"
#include "instruction.h"
#include "memory.h"
#include "disasm.h"
#include "state_file.h"

#include <sstream>
#include <stdexcept>

// TODO: Prefetch before write for some MOVE operations

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

}

std::string ccr_string(uint16_t sr)
{
    char s[6], *d = s;
    for (unsigned i = 5; i--;) {
        if ((sr & (1 << i)))
            *d++ = "CVZNX"[i];
        else
            *d++ ='-';
    }
    *d = 0;
    return s;
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
    os << "PC=" << hexfmt(s.pc) << " SR=" << hexfmt(s.sr) << " SSP=" << hexfmt(s.ssp) << " USP=" << hexfmt(s.usp) << " CCR: " << ccr_string(s.sr) << " Prefetch: $" << hexfmt(s.prefecth_val) << " ($" << hexfmt(s.prefetch_address) << ")";

    if (s.stopped)
        os << " (stopped)";
    os << '\n';
}

constexpr uint8_t num_bits_set(uint16_t n)
{
    uint8_t cnt = 0;
    for (int i = 0; i < 16; ++i)
        if (n & (1 << i))
            ++cnt;
    return cnt;
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
        reset();
    }

    void reset()
    {
        memset(&state_, 0, sizeof(state_));
        state_.sr = srm_s | srm_ipl; // 0x2700
        state_.ssp = mem_.read_u32(0);
        state_.pc = mem_.read_u32(4);
        prefetch();
    }

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, "CPU", 1 };
        sf.handle_blob(&state_, sizeof(state_));        
    }

    const cpu_state& state() const
    {
        return state_;
    }

    void trace(std::ostream* os)
    {
        trace_ = os;
    }

    void show_state(std::ostream& os)
    {
        os << "After " << state_.instruction_count << " instructions:\n";
        print_cpu_state(os, state_);
        disasm(os, start_pc_, iwords_, inst_->ilen);
        os << '\n';
    }

    void set_cycle_handler(const cycle_handler& handler)
    {
        assert(!cycle_handler_);
        cycle_handler_ = handler;
    }

    void set_read_ipl(const read_ipl_func& func)
    {
        assert(!read_ipl_);
        read_ipl_ = func;
    }

    step_result step()
    {
        bool trace_active = !!(state_.sr & srm_trace);

        if (state_.stopped) {
            if (!trace_active && !state_.ipl) {
                poll_ipl();
                return { state_.pc, state_.pc, iwords_[0], true };
            }
            state_.stopped = false;
        }

        ++state_.instruction_count;
        start_pc_ = state_.pc;
        step_result step_res { state_.pc, 0, 0, false };

        if (state_.ipl > (state_.sr & srm_ipl) >> sri_ipl) {
            do_interrupt(state_.ipl);
            if (trace_) {
                *trace_ << "Interrupt switching to IPL " << static_cast<int>(state_.ipl) << "\n";
                print_cpu_state(*trace_, state_);
            }
            goto out;
        }

        if (trace_)
            print_cpu_state(*trace_, state_);

        start_pc_ = state_.pc;
        iword_idx_ = 0;
        if (state_.pc & 1) {
            invalid_access_address_ = state_.pc;
            invalid_access_info_ = 16 | 2; // 16=Read, 2=Program
            if (state_.sr & srm_s)
                invalid_access_info_ |= 4;
            trace_active = false;
            do_trap(interrupt_vector::address_error);
            goto out;
        }
        (void)read_iword();
        inst_ = &instructions[iwords_[0]];

        if ((inst_->extra & extra_priv_flag) && !(state_.sr & srm_s)) {
            if (iwords_[0] == reset_instruction_num)
                iwords_[0] = illegal_instruction_num; // HACK: Don't let main reset...
            state_.pc = start_pc_; // "The saved value of the program counter is the address of the first word of the instruction causing the privilege violation.
            do_trap(interrupt_vector::privililege_violation);
            trace_active = false;
            goto out;
        }

        if (inst_->type == inst_type::ILLEGAL) {
            state_.pc = start_pc_; // Same as for privililege violation
            trace_active = false;
            switch (iwords_[0] >> 12) {
            case 0xA:
                do_trap(interrupt_vector::line_1010);
                goto out;
            case 0xF:
                do_trap(interrupt_vector::line_1111);
                goto out;
            default:
                do_trap(interrupt_vector::illegal_instruction);
                goto out;
            }
        }

        assert(inst_->nea <= 2);

        ea_calced_[0] = ea_calced_[1] = false;
        try {

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
                HANDLE_INST(CHK);
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
                HANDLE_INST(MOVEP);
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
                HANDLE_INST(RESET);
                HANDLE_INST(RTE);
                HANDLE_INST(RTR);
                HANDLE_INST(RTS);
                HANDLE_INST(SBCD);
                HANDLE_INST(Scc);
                HANDLE_INST(STOP);
                HANDLE_INST(SUB);
                HANDLE_INST(SUBA);
                HANDLE_INST(SUBQ);
                HANDLE_INST(SUBX);
                HANDLE_INST(SWAP);
                HANDLE_INST(TAS);
                HANDLE_INST(TRAP);
                HANDLE_INST(TRAPV);
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
            assert(inst_->nea == 0 || (ea_calced_[0] && (inst_->nea == 1 || ea_calced_[1])));
        } catch (const address_error_exception&) {
            // HACK: Undo post-increment if that was the cause of the address error
            // and adjust the program counter to point to the correct instruction word
            assert(inst_->size != opsize::b);
            for (int i = inst_->nea; i--;) {
                if (!ea_calced_[i])
                    continue;
                switch (inst_->ea[i] >> ea_m_shift) {
                case ea_m_Dn:
                case ea_m_An:
                    break;
                case ea_m_A_ind:
                    if (ea_data_[i] == invalid_access_address_)
                        goto found;
                    break;
                case ea_m_A_ind_post:
                    if (ea_data_[i] == invalid_access_address_) {
                        state_.A(inst_->ea[i] & ea_xn_mask) -= inst_->size == opsize::l ? 4 : 2;
                        goto found;
                    }
                    break;
                case ea_m_A_ind_pre:
                    if (ea_data_[i] == invalid_access_address_)
                        goto found;
                    break;
                case ea_m_A_ind_disp16:
                    state_.pc -= 2;
                    if (ea_data_[i] == invalid_access_address_)
                        goto found;
                    break;
                case ea_m_A_ind_index:
                    state_.pc -= 2;
                    if (ea_data_[i] == invalid_access_address_)
                        goto found;
                    break;
                case ea_m_Other:
                    switch (inst_->ea[i] & ea_xn_mask) {
                    case ea_other_abs_w:
                        if (ea_data_[i] == invalid_access_address_)
                            goto found;
                        state_.pc -= 2;
                        break;
                    case ea_other_abs_l:
                        if (ea_data_[i] == invalid_access_address_)
                            goto found;
                        state_.pc -= 4;
                        break;
                    case ea_other_pc_disp16:
                        state_.pc -= 2;
                        if (ea_data_[i] == invalid_access_address_) {
                            // Function code should specify "program" instead of data
                            invalid_access_info_ &= ~1;
                            invalid_access_info_ |= 2;
                            goto found;
                        }
                        break;
                    case ea_other_pc_index:
                        state_.pc -= 2;
                        if (ea_data_[i] == invalid_access_address_) {
                            // Function code should specify "program" instead of data
                            invalid_access_info_ &= ~1;
                            invalid_access_info_ |= 2;
                            goto found;
                        }
                        break;
                    case ea_other_imm:
                        break;
                    }
                }
                break;
            }
found:
            trace_active = false;
            if (state_.sr & srm_s)
                invalid_access_info_ |= 4;
            do_trap(interrupt_vector::address_error);
        }

    out:
        if (trace_) {
            disasm(*trace_, start_pc_, iwords_, inst_->ilen);
            *trace_ << "\n";
        }

        step_res.current_pc = state_.pc;
        step_res.stopped = state_.stopped;

        if (state_.pc & 1) {
            invalid_access_address_ = state_.pc;
            invalid_access_info_ = 16 | 2; // 16=Read, 2=Program
            if (state_.sr & srm_s)
                invalid_access_info_ |= 4;
            do_trap(interrupt_vector::address_error);
        } else {
            prefetch();

            if (trace_active)
                do_trap(interrupt_vector::trace);
        }

        step_res.instruction = iwords_[0];

        assert(read_ipled_);

        return step_res;
    }

private:
    struct address_error_exception {
    };

    memory_handler& mem_;
    read_ipl_func read_ipl_;
    cpu_state state_;
    cycle_handler cycle_handler_;

    // Decoder state
    uint32_t start_pc_ = 0;
    uint16_t iwords_[max_instruction_words] = { illegal_instruction_num };
    uint8_t iword_idx_ = 0;
    const instruction* inst_ = &instructions[illegal_instruction_num];
    uint32_t ea_data_[2]; // For An/Dn/Imm/etc. contains the value, for all others the address
    bool ea_calced_[2];
    std::ostream* trace_ = nullptr;
    uint32_t invalid_access_address_ = 0;
    uint16_t invalid_access_info_ = 0;
#ifndef NDEBUG
    bool read_ipled_ = false;
#endif

    void poll_ipl()
    {
#ifndef NDEBUG
        read_ipled_ = true;
#endif
        state_.ipl = read_ipl_ ? read_ipl_() : 0;
        assert(state_.ipl < 8);
    }

    void add_cycles(uint8_t cnt)
    {
        if (cycle_handler_)
            cycle_handler_(cnt);
    }

    uint8_t mem_read8(uint32_t addr)
    {
        return mem_.read_u8(addr);
    }

    uint16_t mem_read16(uint32_t addr)
    {
        if (addr & 1) {
            invalid_access_address_ = addr;
            invalid_access_info_ = 16 | 1; // 16=Read 1=Data
            throw address_error_exception {};
        }
        return mem_.read_u16(addr);
    }

    uint32_t mem_read32(uint32_t addr)
    {
        uint32_t res = mem_read16(addr) << 16;
        res |= mem_read16(addr + 2);
        return res;
    }

    void mem_write8(uint32_t addr, uint8_t val)
    {
        mem_.write_u8(addr, val);
    }

    void mem_write16(uint32_t addr, uint16_t val)
    {
        if (addr & 1) {
            invalid_access_address_ = addr;
            invalid_access_info_ = 8 | 1; // 8=Not instruction 1=Data
            throw address_error_exception {};
        }
        mem_.write_u16(addr, val);
    }

    void mem_write32(uint32_t addr, uint32_t val)
    {
        // Note: Some instructions write in opposite order...
        mem_write16(addr, static_cast<uint16_t>(val >> 16));
        mem_write16(addr + 2, static_cast<uint16_t>(val));
    }

    uint16_t read_iword()
    {
        uint16_t val;
        if (state_.prefetch_address == state_.pc)
            val = state_.prefecth_val;
        else
            val = mem_read16(state_.pc);
        state_.prefetch_address = invalid_prefetch_address;
        state_.pc += 2;
        assert(iword_idx_ < inst_->ilen);
        iwords_[iword_idx_++] = val;
        return val;
    }

    void prefetch()
    {
        if (state_.prefetch_address == state_.pc)
            return;
        if (state_.pc & 1)
            throw std::runtime_error { "Prefetch from odd address: $" + hexstring(state_.pc) };
        state_.prefetch_address = state_.pc;
        state_.prefecth_val = mem_read16(state_.prefetch_address);
    }

    void useless_prefetch()
    {
        if (state_.pc & 1) {
            invalid_access_address_ = state_.pc;
            invalid_access_info_ = 0;
            throw address_error_exception {};
        }
        mem_read16(state_.pc);
    }

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
            return mem_read8(addr);
        case opsize::w:
            return mem_read16(addr);
        case opsize::l:
            return mem_read32(addr);
        }
        throw std::runtime_error { "Invalid opsize" };
    }

    void handle_ea(uint8_t idx)
    {
        assert(idx < inst_->nea);
        assert(!ea_calced_[idx]);
        assert((idx == 0 && !ea_calced_[1]) || (idx == 1 && ea_calced_[0])); // Must be in correct order
        ea_calced_[idx] = true;
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
            res = state_.A(ea & ea_xn_mask);
            return;
        case ea_m_A_ind_post:
            res = state_.A(ea & ea_xn_mask);
            if (inst_->type != inst_type::MOVEM) {
                state_.A(ea & ea_xn_mask) += opsize_bytes(inst_->size);
                // Stack pointer is always kept word aligned
                if ((ea & ea_xn_mask) == 7 && inst_->size == opsize::b)
                    state_.A(7)++;
            }
            return;
        case ea_m_A_ind_pre:
            if (inst_->type != inst_type::MOVEM) {
                state_.A(ea & ea_xn_mask) -= opsize_bytes(inst_->size);
                // Stack pointer is always kept word aligned
                if ((ea & ea_xn_mask) == 7 && inst_->size == opsize::b)
                    state_.A(7)--;
                if (idx == 0) {
                    add_cycles(2);
                } else {
                    switch (inst_->type) {
                    case inst_type::MOVE:
                        // MOVE.W SR, -(An) isn't discounted?
                        if (inst_->ea[0] == ea_sr)
                            add_cycles(2);
                        break;
                    case inst_type::MOVEA:
                    case inst_type::MOVEM:
                    case inst_type::ADDX:
                    case inst_type::SUBX:
                    case inst_type::ABCD:
                    case inst_type::SBCD:
                        break;
                    default:
                        add_cycles(2);
                    }
                }
            }
            res = state_.A(ea & ea_xn_mask);
            return;
        case ea_m_A_ind_disp16:
            res = state_.A(ea & ea_xn_mask) + sext(read_iword(), opsize::w);
            return;
        case ea_m_A_ind_index: {
            const auto extw = read_iword();
            // Scale in bits 9/10 and full extension word format (bit 8) is ignored on 68000
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
            add_cycles(2);
            return;
        }
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
                res = static_cast<int32_t>(static_cast<int16_t>(read_iword()));
                return;
            case ea_other_abs_l:
                res = read_iword() << 16;
                res |= read_iword();
                return;
            case ea_other_pc_disp16:
                assert(state_.pc == start_pc_ + 2 * iword_idx_);
                res = state_.pc; // the PC used is the address of the extension word
                res += static_cast<int16_t>(read_iword());
                return;
            case ea_other_pc_index: {
                assert(state_.pc == start_pc_ + 2 * iword_idx_);
                res = state_.pc; // the PC used is the address of the extension word
                const auto extw = read_iword();
                // Scale in bits 9/10 and full extension word format (bit 8) is ignored on 68000
                res += sext(extw, opsize::b);
                uint32_t r = (extw >> 12) & 7;
                if ((extw >> 15) & 1) {
                    r = state_.A(r);
                } else {
                    r = state_.d[r];
                }
                if (!((extw >> 11) & 1))
                    r = sext(r, opsize::w);
                res += r;
                add_cycles(2);
                return;
            }
            case ea_other_imm:
                switch (inst_->size) {
                case opsize::b:
                    res = read_iword() & 0xff;
                    return;
                case opsize::w:
                    res = read_iword();
                    return;
                case opsize::l:
                    res = read_iword() << 16;
                    res |= read_iword();
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
                res = read_iword();
                return;
            } else if (ea == ea_bitnum) {
                res = read_iword();
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
                assert(state_.pc == start_pc_ + 2);
                res = state_.pc;
                res += static_cast<int16_t>(read_iword());
                return;
            } else if (ea == ea_disp) {
                assert(state_.pc == start_pc_ + 2);
                res = state_.pc + static_cast<int8_t>(inst_->data);
                return;
            } else {
                res = inst_->data;
                return;
            }
            break;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) };
    }

    uint32_t calc_ea(uint8_t idx)
    {
        assert(idx < inst_->nea);
        if (!ea_calced_[idx])
            handle_ea(idx);
        return ea_data_[idx];
    }

    uint32_t read_ea(uint8_t idx)
    {
        assert(idx < inst_->nea);
        const auto ea = inst_->ea[idx];
        const auto val = calc_ea(idx);
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
            // Bit of a hack to do this here...
            if (idx == 0 && (ea == ea_sr || ea == ea_ccr)) {
                assert(inst_->nea == 2 && inst_->type == inst_type::MOVE);
                if (inst_->ea[1] >> ea_m_shift == ea_m_Dn)
                    add_cycles(2);
                else
                    useless_prefetch();
            }
            return val;
        }
        throw std::runtime_error { "Not handled in " + std::string { __func__ } + ": " + ea_string(ea) };
    }

    void write_mem(uint32_t addr, uint32_t val)
    {
        switch (inst_->size) {
        case opsize::b:
            mem_write8(addr, static_cast<uint8_t>(val));
            return;
        case opsize::w:
            mem_write16(addr, static_cast<uint16_t>(val));
            return;
        case opsize::l:
            mem_write32(addr, val);
            return;
        }
        assert(!"Invalid opsize");
    }

    void write_ea(uint8_t idx, uint32_t val)
    {
        assert(idx < inst_->nea);
        const auto ea = inst_->ea[idx];
        const auto ea_val = calc_ea(idx);
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
            write_mem(ea_val, val);
            return;
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_abs_l:
            case ea_other_pc_disp16:
            case ea_other_pc_index:
                write_mem(ea_val, val);
                return;
            case ea_other_imm:
                assert(!"Write to immediate?!");
                break;
            }
            break;
        default:
            if (ea == ea_sr) {
                assert((state_.sr & srm_s));
                assert(idx == 1);
                val &= ~(srm_m | 1 << 14); // Clear unsupported bits
                state_.sr = static_cast<uint16_t>(val & ~srm_illegal);
                add_cycles(8);
                if (inst_->type != inst_type::MOVE)
                    useless_prefetch();
                return;
            } else if (ea == ea_ccr) {
                assert(idx == 1);
                state_.update_sr(srm_ccr, val & srm_ccr);
                add_cycles(8);
                if (inst_->type != inst_type::MOVE)
                    useless_prefetch();
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

    void update_flags_size(sr_mask srmask, uint32_t res, uint32_t carry, opsize size)
    {
        const uint32_t mask = opsize_msb_mask(size);
        uint16_t ccr = 0;
        if (carry & mask) {
            ccr |= srm_c;
            ccr |= srm_x;
        }
        if (((carry << 1) ^ carry) & mask)
            ccr |= srm_v;
        if (!(res & opsize_all_mask(size)))
            ccr |= srm_z;
        if (res & mask)
            ccr |= srm_n;

        state_.update_sr(srmask, (ccr & srmask));
    }

    void update_flags(sr_mask srmask, uint32_t res, uint32_t carry)
    {
        update_flags_size(srmask, res, carry, inst_->size);
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
        const sr_mask sm = cnt ? srm_ccr : srm_ccr_no_x;
        state_.update_sr(sm, ccr & sm);
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
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

        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
        if (arit && cnt) {
            const uint32_t nb = 8 * opsize_bytes(inst_->size);
            // Create mask of all bytes shifted through MSB
            const auto mask = cnt >= nb ? ~0U : ~((osmask - 1) >> cnt) & (osmask - 1);
            bool v = false;
            if (orig_val & osmask)
                v = (orig_val & mask) != mask || cnt >= nb;
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
        mem_write16(a7, val);
    }

    void push_u32(uint32_t val)
    {
        auto& a7 = state_.A(7);
        a7 -= 4;
        mem_write32(a7, val);
    }

    uint16_t pop_u16()
    {
        auto& a7 = state_.A(7);
        const auto val = mem_read16(a7);
        a7 += 2;
        return val;
    }

    uint32_t pop_u32()
    {
        auto& a7 = state_.A(7);
        const auto val = mem_read32(a7);
        a7 += 4;
        return val;
    }

    void do_interrupt_impl(interrupt_vector vec, uint8_t ipl)
    {
        assert(vec > interrupt_vector::reset_pc); // RESET doesn't save anything

        // Common:
        // push_u32(pc)        8(0/2)
        // push_u16(sr)        4(0/1)
        // read_u32(vec)       8(2/0)
        // useless_prefetch    4(1/0)
        // prefetch            4(1/0)
        // --------------------------
        //                    28(4/3)
        constexpr uint8_t common_cycles = 28;

        switch (vec) {
        case interrupt_vector::reset_ssp:
        case interrupt_vector::reset_pc:
            // /RESET              | 40(6/0)  |38   (n-)*5   nn       nF nf nV nv np np
            assert(!"Not implemented");
            break;
        case interrupt_vector::bus_error:
        case interrupt_vector::address_error:
            // Address error       | 50(4/7)  |48   nn ns ns nS ns ns ns nS nV nv np np
            // Bus error           | 50(4/7)  |48   nn ns ns nS ns ns ns nS nV nv np np
            add_cycles(6); // 4 more words are stacked (=
            break;
        case interrupt_vector::zero_divide:
            // Divide by Zero      | 38(4/3)+ |36         nn nn    ns ns nS nV nv np np
            add_cycles(38 - common_cycles);
            break;
        case interrupt_vector::chk_exception:
            // CHK Instruction     | 40(4/3)+ |38 np (n-)    nn    ns ns nS nV nv np np
            add_cycles(40 - common_cycles - 6); // 6 cycles already done
            break;
        case interrupt_vector::trapv_instruction:
            // TRAPV               | 34(5/3)  |          np ns nS ns np np np np
            add_cycles(34 - common_cycles - 4); // Extra prefect in handler
            break;
        case interrupt_vector::trace:
            // Trace | 34(4 / 3) | 32 nn ns nS ns nV nv np np
            if (inst_->type != inst_type::STOP)
                add_cycles(34 - common_cycles);
            else
                add_cycles(30 - common_cycles);
            break;
        case interrupt_vector::level1:
        case interrupt_vector::level2:
        case interrupt_vector::level3:
        case interrupt_vector::level4:
        case interrupt_vector::level5:
        case interrupt_vector::level6:
        case interrupt_vector::level7:
            // Interrupt | 44(5 / 3) | 42 n nn ns ni n - n nS ns nV nv np np
            add_cycles(44 - common_cycles - 4);
            break;
        case interrupt_vector::illegal_instruction:
        case interrupt_vector::privililege_violation:
        case interrupt_vector::line_1010:
        case interrupt_vector::line_1111:
        default: // Other traps
            // Privilege Violation | 34(4/3)  |32            nn    ns ns nS nV nv np np
            // Illegal Instruction | 34(4/3)  |32            nn    ns ns nS nV nv np np
            add_cycles(34 - common_cycles);
            break;
        }

        const uint16_t saved_sr = state_.sr;
        state_.update_sr(static_cast<sr_mask>(srm_trace | srm_s | srm_ipl), srm_s | ipl << sri_ipl); // Clear trace, set superviser mode
        // Now always on supervisor stack

        if (state_.A(7) & 1) {
            throw std::runtime_error { "Supervisor stack at odd address: $" + hexstring(state_.A(7)) };
        }

        // From MC68000UM 6.2.5
        // "The current program
        // counter value and the saved copy of the status register are stacked using the SSP. The
        // stacked program counter value usually points to the next unexecuted instruction.
        // However, for bus error and address error, the value stacked for the program counter is
        // unpredictable and may be incremented from the address of the instruction that caused the
        // error."
        push_u32(state_.pc);
        push_u16(saved_sr);

        state_.pc = mem_read32(static_cast<uint32_t>(vec) * 4);
        if (vec == interrupt_vector::address_error || vec == interrupt_vector::bus_error) {
            if (state_.pc & 1) {
                throw std::runtime_error { "Double fault. Address error vector at odd address: $" + hexstring(state_.pc) };
            }
            push_u16(iwords_[0]);
            push_u32(invalid_access_address_);
            push_u16(invalid_access_info_ | (iwords_[0] & ~0x1f)); // Opcode in undefined bits?
        }
        poll_ipl(); // TODO: Verify placement (IPL)
        useless_prefetch();
    }

    void do_trap(interrupt_vector vec)
    {
        assert(vec < interrupt_vector::level1 || vec > interrupt_vector::level7); // Should use do_interrupt
        if (trace_) {
            *trace_ << "Exception " << static_cast<int>(vec) << " ($" << hexfmt(static_cast<uint8_t>(vec)) << ")";
            if (vec <= interrupt_vector::line_1111) {
                constexpr const char* const names[12] = {
                    "Reset (initial SSP)",
                    "Reset (initial PC)",
                    "Bus Error",
                    "Address Error",
                    "Illegal Instruction",
                    "Zero Divide",
                    "CHK Instruction",
                    "TRAPV Instruction",
                    "Privilege Violation",
                    "Trace",
                    "Line 1010 Emulator",
                    "Line 1111 Emulator"
                };
                *trace_ << " " << names[static_cast<int>(vec)];
            }
            *trace_ << "\n";
        }
        do_interrupt_impl(vec, (state_.sr & srm_ipl) >> sri_ipl); // IPL not updated
    }

    void do_interrupt(uint8_t ipl)
    {
        assert(ipl >= 1 && ipl <= 7);
        // Amiga detail: Uses non-autovector and ignores the special bus cycle where A3-A1 is is to IPL and all other address lines are high to simple read from ROM
        const auto vec = mem_read8(0xfffffff1 | ipl << 1);        
        do_interrupt_impl(static_cast<interrupt_vector>(vec), ipl);
    }

    void handle_ABCD()
    {
        assert(inst_->nea == 2 && inst_->size == opsize::b);
        const auto r = read_ea(0);
        const auto l = read_ea(1);
        const auto res = l + r + !!(state_.sr & srm_x);
        const auto carry = ((l & r) | ((l | r) & ~res)) & 0x88;
        const auto carry10 = (((res + 0x66) ^ res) & 0x110) >> 1;
        const auto res2 = res + ((carry | carry10) - ((carry | carry10) >> 2));
        uint8_t ccr = (res2 & 0xf00 ? srm_c | srm_x : 0) | (res2 & 0xff ? 0 : state_.sr & srm_z) | (res2 & 0x80 ? srm_n : 0);
        if (!(res & 0x80) && (res2 & 0x80))
            ccr |= srm_v;
        if (inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
        write_ea(1, res2);
        state_.update_sr(srm_ccr, ccr);
    }

    void add_rmw_cycles()
    {
        if (inst_->ea[1] >> ea_m_shift == ea_m_Dn) {
            if (inst_->size != opsize::l)
                return;
            add_cycles(inst_->ea[0] == ea_immediate || inst_->ea[0] >> ea_m_shift <= ea_m_An ? 4 : 2);
        } else if (inst_->ea[0] >> ea_m_shift == ea_m_Dn) {
        } else if (inst_->ea[0] == ea_immediate || inst_->ea[0] == ea_data3) {
            // data3 is for addq/subq
        } else {
            if (inst_->size == opsize::l)
                add_cycles(4);
        }
    }

    void handle_ADD()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l + r;

        add_rmw_cycles();
        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)

        write_ea(1, res);
        // All flags updated
        update_flags(srm_ccr, res, (l & r) | ((l | r) & ~res));
    }

    void handle_ADDA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        (void)calc_ea(1);
        add_cycles(inst_->size == opsize::w || inst_->ea[0] >> ea_m_shift <= ea_m_An || inst_->ea[0] == ea_immediate ? 4 : 2);
        state_.A(inst_->ea[1] & ea_xn_mask) += s; // And the operation performed on the full 32-bit value
        poll_ipl(); // TODO: Verify placement (IPL)
        // No flags
    }

    void handle_ADDQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift > ea_m_Other) && inst_->ea[0] <= ea_disp);
        if ((inst_->ea[1] >> ea_m_shift) == ea_m_An) {
            // ADDQ to address register is always long and doesn't update flags
            add_cycles(4);
            state_.A(inst_->ea[1] & ea_xn_mask) += static_cast<int8_t>(calc_ea(0));
            (void)calc_ea(1);
        } else {
            handle_ADD();
            if (inst_->ea[1] >> ea_m_shift == ea_m_Dn && inst_->size == opsize::l)
                add_cycles(2);
        }
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_ADDX()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l + r + !!(state_.sr & srm_x);
        if (inst_->ea[0] >> ea_m_shift == ea_m_Dn && inst_->size == opsize::l)
            add_cycles(4);
        write_ea(1, res);
        update_flags(static_cast<sr_mask>(srm_ccr & ~srm_z), res, (l & r) | ((l | r) & ~res));
        // Z is only cleared if the result is non-zero
        if (res & opsize_all_mask(inst_->size))
            state_.update_sr(srm_z, 0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_AND()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l & r;
        add_rmw_cycles();
        prefetch();
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
        poll_ipl(); // TODO: Verify placement (IPL)
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
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

        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
    }

    void handle_Bcc()
    {
        // Base: Bcc.B  4/1, Bcc.W  8/2
        // Taken:      10/2,       10/2
        // Not taken:   8/1,       12/2
        assert(inst_->nea == 1 && inst_->ea[0] == ea_disp && (inst_->extra & extra_cond_flag));
        const auto addr = calc_ea(0);
        poll_ipl(); // TODO: Verify placement (IPL)
        if (!state_.eval_cond(static_cast<conditional>(inst_->extra >> 4))) {
            add_cycles(4);
            return;
        }
        if (inst_->size != opsize::w)
            useless_prefetch();
        add_cycles(2);
        state_.pc = addr;
    }

    void handle_BRA()
    {
        assert(inst_->nea == 1);
        if (inst_->size != opsize::w)
            useless_prefetch();
        state_.pc = calc_ea(0);
        add_cycles(2);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_BSR()
    {
        assert(inst_->nea == 1);
        auto addr = calc_ea(0);
        push_u32(state_.pc);
        if (inst_->size != opsize::w)
            useless_prefetch();
        add_cycles(2);
        poll_ipl(); // TODO: Verify placement (IPL)
        state_.pc = addr;
    }

    std::pair<uint32_t, uint32_t> bit_op_helper()
    {
        assert(inst_->nea == 2);
        auto bitnum = read_ea(0);
        const auto num = read_ea(1);
        if (inst_->size == opsize::b) {
            bitnum &= 7;
        } else {
            assert(inst_->size == opsize::l && inst_->ea[1] >> ea_m_shift == ea_m_Dn);
            bitnum &= 31;
            if (bitnum > 15)
                add_cycles(2);
        }
        state_.update_sr(srm_z, !((num >> bitnum) & 1) ? srm_z : 0); // Set according to the previous state of the bit
        return { bitnum, num };
    }

    void handle_BCHG()
    {
        const auto [bitnum, num] = bit_op_helper();
        if (inst_->ea[1] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
        write_ea(1, num ^ (1 << bitnum));
    }

    void handle_BCLR()
    {
        const auto [bitnum, num] = bit_op_helper();
        if (inst_->ea[1] >> ea_m_shift == ea_m_Dn)
            add_cycles(4);
        poll_ipl();
        prefetch();
        write_ea(1, num & ~(1 << bitnum));
    }

    void handle_BSET()
    {
        const auto [bitnum, num] = bit_op_helper();
        if (inst_->ea[1] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
        write_ea(1, num | (1 << bitnum));
    }

    void handle_BTST()
    {
        bit_op_helper(); // discard return value on purpose
        poll_ipl(); // TODO: Verify placement (IPL)
        if (inst_->ea[1] >> ea_m_shift == ea_m_Dn) {
            if (inst_->ea[0] >> ea_m_shift == ea_m_Dn)
                add_cycles(2);
            else if (inst_->ea[0] == ea_bitnum && (ea_data_[0] & 31) < 16)
                add_cycles(2);
        }
    }

    void handle_CHK()
    {
        const auto bound = static_cast<int16_t>(read_ea(0));
        const auto val   = static_cast<int16_t>(read_ea(1));
        state_.update_sr(srm_ccr_no_x, (val == 0 ? srm_z : 0) | (val < 0 ? srm_n : 0));
        add_cycles(6);
        poll_ipl(); // TODO: Verify placement (IPL)
        if (val < 0 || val > bound)
            do_trap(interrupt_vector::chk_exception);
    }

    void handle_CLR()
    {
        assert(inst_->nea == 1);
        read_ea(0); // 68000 does a superflous read
        if (inst_->size == opsize::l && inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
        write_ea(0, 0);
        state_.update_sr(srm_ccr_no_x, srm_z);
    }

    void handle_CMP()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r;
        if (inst_->size == opsize::l && inst_->ea[1] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        poll_ipl(); // TODO: Verify placement (IPL)
        update_flags(srm_ccr_no_x, res, (~l & r) | (~(l ^ r) & res));
    }

    void handle_CMPA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const uint32_t r = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        add_cycles(2);
        (void)calc_ea(1);
        const uint32_t l = state_.A(inst_->ea[1] & ea_xn_mask);
        // And the performed on the full 32-bit value
        const auto res = l - r;
        update_flags_size(srm_ccr_no_x, res, (~l & r) | (~(l ^ r) & res), opsize::l);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_CMPM()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) == ea_m_A_ind_post && (inst_->ea[1] >> ea_m_shift) == ea_m_A_ind_post);
        handle_CMP();
    }

    void handle_DBcc()
    {
        // Base clock cycles:             8/2
        // CC true:                      12/2
        // CC false, count not expired:  10/2
        // CC false, count expired:      14/3
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) == ea_m_Dn && inst_->extra & extra_cond_flag);
        assert(inst_->size == opsize::w && inst_->ea[1] == ea_disp);

        uint16_t val = static_cast<uint16_t>(read_ea(0));
        (void)calc_ea(1);
        poll_ipl(); // TODO: Verify placement (IPL)

        if (state_.eval_cond(static_cast<conditional>(inst_->extra >> 4))) {
            add_cycles(4);
            return;
        }

        --val;
        write_ea(0, val);
        add_cycles(2);
        if (val != 0xffff) {
            state_.pc = calc_ea(1);          
        } else {
            useless_prefetch();
        }
    }

    void handle_DIVU()
    {
        assert(inst_->nea == 2 && inst_->size == opsize::w && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto d = static_cast<uint16_t>(read_ea(0));
        auto& reg = state_.d[inst_->ea[1] & ea_xn_mask];
        (void)calc_ea(1);
        if (!d) {
            //  Divide by Zero      | 38(4/3)+ |36         nn nn    ns ns nS nV nv np np
            uint8_t ccr = 0;
            if ((reg >> 16) == 0)
                ccr = srm_z;
            else if (reg & 0x80000000)
                ccr = srm_n;
            state_.update_sr(srm_ccr_no_x, ccr);
            do_trap(interrupt_vector::zero_divide);
            return;
        }
             
        uint16_t ccr = 0;
        uint8_t cycles = 6; // Overflow check/fixed cost
        if (reg >> 16 >= d) {
            ccr = srm_v | srm_n;
        } else {
            // Best case: 76/ Worst case 136/140
            // 
            // From YACHT
            // .for each iteration of the loop : shift dividend to the left by 1 bit then
            //  substract divisor to the MSW of new dividend, discard after test if result
            //  is negative keep it if positive.
            // .MSB = most significant bit : bit at the far left of the dividend
            // .pMSB = previous MSB : MSB used in the previous iteration of the loop
            //

            constexpr uint32_t msb = 0x8000'0000;
            for (int i = 0; i < 16; ++i) {
                uint8_t cost = 4;
                const bool prevmsb = reg & msb;
                reg <<= 1;
                const unsigned saved = reg;
                reg -= d << 16;
                if (prevmsb) {
                    reg |= 1;
                } else {
                    cost += 2;
                    if (reg >= saved) {
                        reg = saved;
                        cost += 2;
                    } else {
                        reg |= 1;
                    }
                }
                cycles += i == 15 ? 6 : cost; // Final iteration has fixed cost
            }

            if (reg & 0x8000)
                ccr |= srm_n;
            if (!(reg & 0xffff))
                ccr |= srm_z;
        }
        add_cycles(cycles);
        poll_ipl(); // TODO: Verify placement (IPL)
        state_.update_sr(srm_ccr_no_x, ccr);
    }

    void handle_DIVS()
    {
        assert(inst_->nea == 2 && inst_->size == opsize::w && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto d = sext(read_ea(0), opsize::w);
        (void)calc_ea(1);
        if (!d) {
            //  Divide by Zero      | 38(4/3)+ |36         nn nn    ns ns nS nV nv np np
            state_.update_sr(srm_ccr_no_x, srm_z);
            do_trap(interrupt_vector::zero_divide);
            return;
        }

        auto& reg = state_.d[inst_->ea[1] & ea_xn_mask];

        const auto dividend = static_cast<int32_t>(reg);
        const auto divisor  = static_cast<int16_t>(d);
        const auto adividend = static_cast<uint32_t>(dividend < 0 ? -dividend : dividend);
        const auto adivisor = static_cast<uint16_t>(divisor < 0 ? -divisor : divisor);

        //.Best case : 120-122 cycles depending on dividend sign.                       
        //.Worst case : 156 (158 ?) cycles.                                             
        uint8_t cycles = 12;
        uint16_t ccr = 0;
        if (dividend < 0)
            cycles += 2;
        if (adividend >> 16 >= adivisor) {
            // Absolute overflow is detected early
            // .Overflow cost 16 or 18 cycles depending on dividend sign (n nn nn (n)n np).
            cycles += 2;
            ccr = srm_v | srm_n;
        } else {
            //  .for each iteration of the loop : shift quotient to the left by 1 bit.
            // .MSB = most significant bit : bit at the far left of the quotient.
            cycles += 104 + 2 * num_bits_set(~static_cast<uint16_t>(adividend / adivisor) & 0xfffe);
            if (divisor > 0) {
                if (dividend < 0)
                    cycles += 4;
            } else {
                cycles += 2;
            }
            const auto q = dividend / divisor;
            if (q > 0x7fff || q < -0x8000) {
                ccr = srm_v | srm_n;
            } else {
                const auto r = dividend % divisor;
                if (q & 0x8000)
                    ccr |= srm_n;
                if (!q)
                    ccr |= srm_z;
                reg = (q & 0xffff) | (r & 0xffff) << 16;
            }
        }

        add_cycles(cycles);
        poll_ipl(); // TODO: Verify placement (IPL)
        state_.update_sr(srm_ccr_no_x, ccr);
    }

    void handle_EOR()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l ^ r;
        add_rmw_cycles();
        prefetch();
        prefetch(); // Prefetch happens before write (needed for Razor1911-Voyage, which uses EOR.W D5,(A2) to modify the following instruction!)
        poll_ipl(); // TODO: Verify placement (IPL)
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
    }

    void handle_EXG()
    {
        assert(inst_->size == opsize::l && inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift) < 2 && (inst_->ea[1] >> ea_m_shift) < 2);
        const auto a = read_ea(0);
        const auto b = read_ea(1);
        add_cycles(2);
        write_ea(0, b);
        write_ea(1, a);
        poll_ipl(); // TODO: Verify placement (IPL)
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
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void add_jmp_jsr_cycles()
    {
        switch (inst_->ea[0] >> ea_m_shift) {
        case ea_m_A_ind:
            useless_prefetch();
            break;
        case ea_m_A_ind_disp16:
            add_cycles(2);
            break;
        case ea_m_A_ind_index:
            add_cycles(4);
            break;
        case ea_m_Other:
            switch (inst_->ea[0] & ea_xn_mask) {
            case ea_other_abs_w:
            case ea_other_pc_disp16:
                add_cycles(2);
                break;
            case ea_other_pc_index:
                add_cycles(4);
                break;
            case ea_other_abs_l:
                break;
            default:
                assert(0);
            }
            break;
        default:
            assert(0);
        }
    }

    void handle_JMP()
    {
        assert(inst_->nea == 1);
        add_jmp_jsr_cycles();
        state_.pc = calc_ea(0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_JSR()
    {
        assert(inst_->nea == 1);
        const auto addr = calc_ea(0);
        add_jmp_jsr_cycles();
        push_u32(state_.pc);
        state_.pc = addr;
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_LEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift == ea_m_An) && inst_->size == opsize::l);
        const auto addr = calc_ea(0);
        if ((inst_->ea[0] >> ea_m_shift == ea_m_A_ind_index) || inst_->ea[0] == ea_pc_index)
            add_cycles(2);
        write_ea(1, addr);
        poll_ipl(); // TODO: Verify placement (IPL)
        // No flags affected
    }

    void handle_LINK()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift == ea_m_An) && (inst_->ea[1] == ea_immediate) && inst_->size == opsize::w);
        (void)calc_ea(0);
        auto& a = state_.A(inst_->ea[0] & ea_xn_mask);
        push_u32(a);
        auto& sp = state_.A(7);
        a = sp;
        sp += sext(calc_ea(1), inst_->size);
        poll_ipl(); // TODO: Verify placement (IPL)
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
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

        prefetch();
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, carry);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_MOVE()
    {
        assert(inst_->nea == 2);
        const uint32_t src = read_ea(0);
        write_ea(1, src);
        // Reading/writing SR/CCR/USP should not update flags
        if (inst_->ea[0] != ea_sr && inst_->ea[0] != ea_ccr && inst_->ea[0] != ea_usp && inst_->ea[1] != ea_sr && inst_->ea[1] != ea_ccr && inst_->ea[1] != ea_usp)
            update_flags(srm_ccr_no_x, src, 0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_MOVEA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        (void)calc_ea(1);
        state_.A(inst_->ea[1] & ea_xn_mask) = s; // Always write full 32-bit result
        poll_ipl(); // TODO: Verify placement (IPL)
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
            reglist = static_cast<uint16_t>(calc_ea(0));
            addr = calc_ea(1);
            reverse_mode = inst_->ea[1] >> ea_m_shift == ea_m_A_ind_pre;
        } else {
            // reglist always immediately follows instruction
            ea_data_[1] = reglist = read_iword();
            addr = calc_ea(0);
            ea_calced_[1] = true;
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
                    reg = sext(read_mem(addr), inst_->size);
                } else
                    write_mem(addr, reg);
                addr += nb;
            }

            // Update address register if post-increment mode
            if (inst_->ea[0] >> ea_m_shift == ea_m_A_ind_post) {
                state_.A(inst_->ea[0] & ea_xn_mask) = addr;
            }
        }

        if (mem_to_reg) {
            // Mem to reg mode apparently does an extra read access
            (void)mem_read16(addr);
        }
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_MOVEP()
    {
        auto shift = inst_->size == opsize::l ? 32 : 16;
        if (inst_->ea[0] >> ea_m_shift == ea_m_Dn) {
            // Reg-to-mem
            auto val = read_ea(0);
            auto addr = calc_ea(1);
            do {
                shift -= 8;
                mem_write8(addr, static_cast<uint8_t>(val >> shift));
                addr += 2;
            } while (shift);

        } else {
            // Mem-to-reg
            uint32_t val = 0;
            auto addr = calc_ea(0);
            do {
                shift -= 8;
                val |= mem_read8(addr) << shift;
                addr += 2;
            } while (shift);
            write_ea(1, val);
        }
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_MOVEQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift > ea_m_Other) && inst_->ea[0] <= ea_disp && (inst_->ea[1] >> ea_m_shift == ea_m_Dn));
        const uint32_t src = static_cast<int32_t>(static_cast<int8_t>(read_ea(0)));
        write_ea(1, src);
        update_flags(srm_ccr_no_x, src, 0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_MULS()
    {
        assert(inst_->size == opsize::w && inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto a = sext(read_ea(0) & 0xffff, opsize::w);
        const auto b = sext(read_ea(1) & 0xffff, opsize::w);
        const uint32_t res = static_cast<int32_t>(a) * b;
        prefetch();
        add_cycles(34 + 2*num_bits_set(static_cast<uint16_t>(a ^ (a << 1))));
        state_.d[inst_->ea[1] & ea_xn_mask] = res;
        uint16_t ccr = 0;
        if (!res)
            ccr |= srm_z;
        if (res & 0x80000000)
            ccr |= srm_n;
        state_.update_sr(srm_ccr_no_x, ccr);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_MULU()
    {
        assert(inst_->size == opsize::w && inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_Dn);
        const auto a = static_cast<uint16_t>(read_ea(0) & 0xffff);
        const auto b = static_cast<uint16_t>(read_ea(1) & 0xffff);
        const uint32_t res = static_cast<uint32_t>(a) * b;
        prefetch();
        add_cycles(34 + 2*num_bits_set(static_cast<uint16_t>(a)));
        state_.d[inst_->ea[1] & ea_xn_mask] = res;
        uint16_t ccr = 0;
        if (!res)
            ccr |= srm_z;
        if (res & 0x80000000)
            ccr |= srm_n;
        state_.update_sr(srm_ccr_no_x, ccr);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_NBCD()
    {
        assert(inst_->nea == 1);

        // SBCD with l = 0
        const uint32_t r = read_ea(0);
        const uint32_t l = 0;
        const auto res = l - r - !!(state_.sr & srm_x);
        const auto carry10 = ((~l & r) | (~(l ^ r) & res)) & 0x88;
        const auto res2 = res - (carry10 - (carry10 >> 2));
        uint8_t ccr = (res2 & 0xf00 ? srm_c | srm_x : 0) | (res2 & 0xff ? 0 : state_.sr & srm_z) | (res2 & 0x80 ? srm_n : 0);
        if ((res & 0x80) && !(res2 & 0x80))
            ccr |= srm_v;
        if (inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        write_ea(0, res2);
        state_.update_sr(srm_ccr, ccr);
        poll_ipl(); // TODO: Verify placement (IPL)
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
        if (inst_->size == opsize::l && inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        write_ea(0, n);
        state_.update_sr(srm_ccr, ccr);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_NEGX()
    {
        // Like SUBX with LHS = 0
        assert(inst_->nea == 1);
        const uint32_t r = read_ea(0);
        const uint32_t l = 0;
        const uint32_t res = l - r - !!(state_.sr & srm_x);
        if (inst_->size == opsize::l && inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        write_ea(0, res);
        update_flags(static_cast<sr_mask>(srm_ccr & ~srm_z), res, (~l & r) | (~(l ^ r) & res));
        // Z is only cleared if the result is non-zero
        if (res & opsize_all_mask(inst_->size))
            state_.update_sr(srm_z, 0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_NOT()
    {
        assert(inst_->nea == 1);
        auto n = ~read_ea(0);
        if (inst_->size == opsize::l && inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        update_flags(srm_ccr_no_x, n, 0);
        write_ea(0, n);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_NOP()
    {
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_OR()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l | r;
        add_rmw_cycles();
        prefetch();
        update_flags(srm_ccr_no_x, res, 0);
        write_ea(1, res);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_PEA()
    {
        assert(inst_->nea == 1 && inst_->size == opsize::l);
        const auto addr = calc_ea(0);
        if ((inst_->ea[0] >> ea_m_shift == ea_m_A_ind_index) || inst_->ea[0] == ea_pc_index)
            add_cycles(2);
        push_u32(addr);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_RESET()
    {
        // Handle externally
        state_.pc = start_pc_;
        poll_ipl(); // TODO: Verify placement (IPL)
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
        }

        bool carry = false;
        if (cnt) {
            const auto nbits = opsize_bytes(inst_->size) * 8;
            cnt &= (nbits - 1);
            if (cnt) {
                val = (val << cnt) | (val >> (nbits - cnt));
            }
            carry = !!(val & 1);
        }

        prefetch();
        write_ea(inst_->nea - 1, val);
        const uint16_t old_x = state_.sr & srm_x; // X is not affected
        update_flags_rot(val, cnt, carry);
        state_.update_sr(srm_x, old_x);
        poll_ipl(); // TODO: Verify placement (IPL)
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
        }

        bool carry = false;
        if (cnt) {
            const auto nbits = opsize_bytes(inst_->size) * 8;
            cnt &= (nbits-1);
            if (cnt)
                val = (val >> cnt) | (val << (nbits - cnt));
            carry = val & opsize_msb_mask(inst_->size);
        }

        prefetch();
        write_ea(inst_->nea - 1, val);
        const uint16_t old_x = state_.sr & srm_x; // X is not affected
        update_flags_rot(val, cnt, carry);
        state_.update_sr(srm_x, old_x);
        poll_ipl(); // TODO: Verify placement (IPL)
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
        }

        // TODO: Could optimize
        const auto msb = opsize_msb_mask(inst_->size);
        uint32_t x = !!(state_.sr & srm_x);
        while (cnt--) {
            const auto new_x = !!(val & msb);
            val = (val << 1) | x;
            x = new_x;
        }
        prefetch();
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, !!x);
        poll_ipl(); // TODO: Verify placement (IPL)
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
            add_cycles(static_cast<uint8_t>(2 * cnt + (inst_->size == opsize::l ? 4 : 2)));
        }

        // TODO: Could optimize
        const auto shift = opsize_bytes(inst_->size) * 8 - 1;
        uint32_t x = !!(state_.sr & srm_x);
        while (cnt--) {
            const auto new_x = val & 1;
            val = (val >> 1) | (x << shift);
            x = new_x;
        }
        prefetch();
        write_ea(inst_->nea - 1, val);
        update_flags_rot(val, cnt, !!x);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_RTE()
    {
        assert(inst_->nea == 0);
        assert(state_.sr & srm_s);
        const uint16_t sr = pop_u16() & ~srm_illegal;
        state_.pc = pop_u32();
        state_.sr = sr; // Only after popping PC (otherwise we switch stacks too early)
        useless_prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_RTR()
    {
        const auto ccr = pop_u16();
        state_.pc = pop_u32();
        state_.sr = (state_.sr & ~srm_ccr) | (ccr & srm_ccr);
        useless_prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_RTS()
    {
        assert(inst_->nea == 0);
        state_.pc = pop_u32();
        useless_prefetch();
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_SBCD()
    {
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const auto res = l - r - !!(state_.sr & srm_x);
        const auto carry10 = ((~l & r) | (~(l ^ r) & res)) & 0x88;
        const auto res2 = res - (carry10 - (carry10 >> 2));
        uint8_t ccr = (res2 & 0xf00 ? srm_c | srm_x : 0) | (res2 & 0xff ? 0 : state_.sr & srm_z) | (res2 & 0x80 ? srm_n : 0);
        if ((res & 0x80) && !(res2 & 0x80))
            ccr |= srm_v;
        if (inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        prefetch();
        write_ea(1, res2);
        state_.update_sr(srm_ccr, ccr);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_Scc()
    {
        assert(inst_->nea == 1 && inst_->size == opsize::b && (inst_->extra & extra_cond_flag));
        const bool cond = state_.eval_cond(static_cast<conditional>(inst_->extra >> 4));
        if (cond && inst_->ea[0] >> ea_m_shift == ea_m_Dn)
            add_cycles(2);
        else
            (void)read_ea(0);
        prefetch();
        write_ea(0, cond ? 0xff : 0x00);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_STOP()
    {
        assert(inst_->nea == 1);
        const auto orig_sr = state_.sr;
        state_.sr = static_cast<uint16_t>(read_ea(0) & ~srm_illegal);
        state_.stopped = !(orig_sr & srm_trace); // If tracing is enabled, drop through immediately
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_SUB()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r;
        add_rmw_cycles();
        prefetch();
        write_ea(1, res);
        // All flags updated
        update_flags(srm_ccr, res, (~l & r) | (~(l ^ r) & res));
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_SUBA()
    {
        assert(inst_->nea == 2 && (inst_->ea[1] >> ea_m_shift) == ea_m_An);
        const auto s = sext(read_ea(0), inst_->size); // The source is sign-extended (if word sized)
        (void)calc_ea(1);
        add_cycles(inst_->size == opsize::w || inst_->ea[0] >> ea_m_shift <= ea_m_An || inst_->ea[0] == ea_immediate ? 4 : 2);
        state_.A(inst_->ea[1] & ea_xn_mask) -= s; // And the operation performed on the full 32-bit value
        // No flags
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_SUBQ()
    {
        assert(inst_->nea == 2 && (inst_->ea[0] >> ea_m_shift > ea_m_Other) && inst_->ea[0] <= ea_disp);
        if ((inst_->ea[1] >> ea_m_shift) == ea_m_An) {
            // SUBQ to address register is always long and doesn't update flags
            add_cycles(4);
            state_.A(inst_->ea[1] & ea_xn_mask) -= static_cast<int8_t>(calc_ea(0));
            (void)calc_ea(1);
        } else {
            handle_SUB();
            if (inst_->ea[1] >> ea_m_shift == ea_m_Dn && inst_->size == opsize::l)
                add_cycles(2);
        }
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_SUBX()
    {
        assert(inst_->nea == 2);
        const uint32_t r = read_ea(0);
        const uint32_t l = read_ea(1);
        const uint32_t res = l - r - !!(state_.sr & srm_x);
        if (inst_->ea[0] >> ea_m_shift == ea_m_Dn && inst_->size == opsize::l)
            add_cycles(4);
        write_ea(1, res);
        update_flags(static_cast<sr_mask>(srm_ccr & ~srm_z), res, (~l & r) | (~(l ^ r) & res));
        // Z is only cleared if the result is non-zero
        if (res & opsize_all_mask(inst_->size))
            state_.update_sr(srm_z, 0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_SWAP()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift == ea_m_Dn));
        (void)calc_ea(0);
        auto& r = state_.d[inst_->ea[0] & ea_xn_mask];
        r = (r & 0xffff) << 16 | ((r >> 16) & 0xffff);
        uint8_t ccr = 0;
        if (r & 0x80000000)
            ccr |= srm_n;
        else if (!r)
            ccr |= srm_z;

        state_.update_sr(srm_ccr_no_x, ccr);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_TAS()
    {
        auto v = read_ea(0);
        uint8_t ccr = 0;
        if (v & 0x80)
            ccr |= srm_n;
        else if (!v)
            ccr |= srm_z;
        state_.update_sr(srm_ccr_no_x, ccr);
        if (inst_->ea[0] >> ea_m_shift != ea_m_Dn)
            add_cycles(2); // TAS uses a special (10 cycle) RMW cycle
        write_ea(0, v | 0x80);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_TRAP()
    {
        const uint8_t trap = static_cast<uint8_t>(calc_ea(0));
        assert(inst_->nea == 1 && trap < 16);
        do_trap(static_cast<interrupt_vector>(32 + (trap & 0xf)));
    }

    void handle_TRAPV()
    {
        if (state_.sr & srm_v) {
            do_trap(interrupt_vector::trapv_instruction);
            useless_prefetch();
        }
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_TST()
    {
        assert(inst_->nea == 1);
        update_flags(srm_ccr_no_x, read_ea(0), 0);
        poll_ipl(); // TODO: Verify placement (IPL)
    }

    void handle_UNLK()
    {
        assert(inst_->nea == 1 && (inst_->ea[0] >> ea_m_shift) == ea_m_An);
        (void)calc_ea(0);
        auto& a = state_.A(inst_->ea[0] & ea_xn_mask);
        state_.A(7) = a;
        a = pop_u32();
        poll_ipl(); // TODO: Verify placement (IPL)
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

void m68000::trace(std::ostream* os)
{
    impl_->trace(os);
}

void m68000::show_state(std::ostream& os)
{
    impl_->show_state(os);
}

void m68000::set_cycle_handler(const cycle_handler& handler)
{
    impl_->set_cycle_handler(handler);
}

void m68000::set_read_ipl(const read_ipl_func& func)
{
    impl_->set_read_ipl(func);
}

m68000::step_result m68000::step()
{
    return impl_->step();
}

void m68000::reset()
{
    impl_->reset();
}

void m68000::handle_state(state_file& sf)
{
    impl_->handle_state(sf);
}