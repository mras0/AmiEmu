#include "disasm.h"

#include <string>
#include <ostream>
#include <cassert>

#include "instruction.h"
#include "ioutil.h"

std::string ea_string(uint8_t ea)
{
    switch (ea >> ea_m_shift) {
    case ea_m_Dn:
        return "D" + std::to_string(ea & ea_xn_mask);
    case ea_m_An:
        return "A" + std::to_string(ea & ea_xn_mask);
    case ea_m_A_ind:
        return "(A" + std::to_string(ea & ea_xn_mask) + ")";
    case ea_m_A_ind_post:
        return "(A" + std::to_string(ea & ea_xn_mask) + ")+";
    case ea_m_A_ind_pre:
        return "-(A" + std::to_string(ea & ea_xn_mask) + ")";
    case ea_m_A_ind_disp16:
        return "(d16, A" + std::to_string(ea & ea_xn_mask) + ")";
    case ea_m_A_ind_index:
        return "(d8, A" + std::to_string(ea & ea_xn_mask) + ", Xn)";
    case ea_m_Other:
        switch (ea & ea_xn_mask) {
        case ea_other_abs_w:
            return "(ABS.W)";
        case ea_other_abs_l:
            return "(ABS.L)";
        case ea_other_pc_disp16:
            return "(d16, PC)";
        case ea_other_pc_index:
            return "(d8, PC, Xn)";
        case ea_other_imm:
            return "#IMM";
        }
        break;
    default:
        switch (ea) {
        case ea_sr:
            return "SR";
        case ea_ccr:
            return "CCR";
        case ea_usp:
            return "USP";
        case ea_reglist:
            return "REGLIST";
        }

        if (ea <= ea_disp) {
            return "<EA (emebdded) $" + hexstring(ea) + ">";
        }
    }
    assert(!"Not handled");
    return "<EA $" + hexstring(ea) + ">";
}

std::string reg_list_string(uint16_t list, bool reverse)
{
    // postincrement a7..d0 (bit 15..0) reversed for predecrement mode
    // d0..d7a0..a7
    bool first = true;
    int last_reg = -1;
    std::string res;

    auto reg_str = [](int reg) -> std::string {
        assert(reg >= 0 && reg < 16);
        return (reg < 8 ? "D" : "A") + std::to_string(reg & 7);
    };
    for (int reg = 0; reg < 16; ++reg) {
        const int bit = reverse ? 15-reg : reg;
        if (list & (1 << bit)) {
            if (last_reg < 0) {
                last_reg = reg;
            }
        } else if (last_reg != -1) {
            if (first) {
                first = false;
            } else {
                res += '/';
            }

            res += reg_str(last_reg);
            if (last_reg != reg - 1) {
                res += '-';
                res += reg_str(reg - 1);
            }
            last_reg = -1;
        }
    }
    assert(last_reg == -1); // TODO: Handle this...
    return res;
}

void disasm(std::ostream& os, uint32_t pc, const uint16_t* iwords, [[maybe_unused]] size_t num_iwords)
{
    assert(iwords && num_iwords);
    const auto& inst = instructions[iwords[0]];
    assert(inst.ilen > 0 && inst.ilen <= num_iwords);

    os << hexfmt(pc) << ' ';
    for (unsigned i = 0; i < max_instruction_words; ++i) {
        os << ' ';
        if (i < inst.ilen)
            os << hexfmt(iwords[i]);
        else
            os << "    ";
    }
    os << "  ";
    // Only print "ILLEGAL" for the explicitly defined illegal instruction
    if (inst.type == inst_type::ILLEGAL && iwords[0] != illegal_instruction_num)
        os << "DC.W\t$" << hexfmt(iwords[0]);
    else
        os << inst.name;

    unsigned eaw = 1;

    uint16_t reglist = 0;
    // Reglist always follows immediately after instruction
    for (unsigned i = 0; i < inst.nea; ++i) {
        if (inst.ea[i] == ea_reglist) {
            assert(eaw < inst.ilen);
            reglist = iwords[eaw++];            
        }
    }

    for (unsigned i = 0; i < inst.nea; ++i) {
        os << (i == 0 ? "\t" : ", ");
        const auto ea = inst.ea[i];
        switch (ea >> ea_m_shift) {
        case ea_m_Dn:
            os << "D" << (ea & 7);
            break;
        case ea_m_An:
            os << "A" << (ea & 7);
            break;
        case ea_m_A_ind:
            os << "(A" << (ea & 7) << ")";
            break;
        case ea_m_A_ind_post:
            os << "(A" << (ea & 7) << ")+";
            break;
        case ea_m_A_ind_pre:
            os << "-(A" << (ea & 7) << ")";
            break;
        case ea_m_A_ind_disp16: {
            assert(eaw < inst.ilen);
            int16_t n = iwords[eaw++];
            os << "$";
            if (n < 0) {
                os << "-";
                n = -n;
            }
            os << hexfmt(static_cast<uint16_t>(n));
            os << "(A" << (ea & 7) << ")";
            break;
        }
        case ea_m_A_ind_index: {
            assert(eaw < inst.ilen);
            const auto extw = iwords[eaw++];
            // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
            auto disp = static_cast<int8_t>(extw & 255);
            os << "$";
            if (disp < 0) {
                os << "-";
                disp = -disp;
            }
            os << hexfmt(static_cast<uint8_t>(disp)) << "(A" << (ea & 7) << ",";
            os << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
            os << ")";
            break;
        }
        case ea_m_Other:
            switch (ea & ea_xn_mask) {
            case ea_other_abs_w:
                assert(eaw < inst.ilen);
                os << "$";
                os << hexfmt(iwords[eaw++]);
                os << ".W";
                break;
            case ea_other_abs_l:
                assert(eaw + 1 < inst.ilen);
                os << "$";
                os << hexfmt(iwords[eaw++]);
                os << hexfmt(iwords[eaw++]);
                os << ".L";
                break;
            case ea_other_pc_disp16: {
                assert(eaw < inst.ilen);
                int16_t n = iwords[eaw++];
                os << "$";
                if (n < 0) {
                    os << "-";
                    n = -n;
                }
                os << hexfmt(static_cast<uint16_t>(n));
                os << "(PC)";
                break;
            }
            case ea_other_pc_index: {
                assert(eaw < inst.ilen);
                const auto extw = iwords[eaw++];
                // Note: 68000 ignores scale in bits 9/10 and full extension word bit (8)
                auto disp = static_cast<int8_t>(extw & 255);
                os << "$";
                if (disp < 0) {
                    os << "-";
                    disp = -disp;
                }
                os << hexfmt(static_cast<uint8_t>(disp)) << "(PC,";
                os << ((extw & (1 << 15)) ? "A" : "D") << ((extw >> 12) & 7) << "." << (((extw >> 11) & 1) ? "L" : "W");
                os << ")";
                break;
            }
            case ea_other_imm:
                os << "#$";
                if (inst.size == opsize::l) {
                    assert(eaw + 1 < inst.ilen);
                    os << hexfmt(iwords[eaw++]);
                    os << hexfmt(iwords[eaw++]);
                } else {
                    assert(eaw < inst.ilen);
                    if (inst.size == opsize::b)
                        os << hexfmt(static_cast<uint8_t>(iwords[eaw++]));
                    else
                        os << hexfmt(iwords[eaw++]);
                }
                break;
            default:
                os << "\nTODO: Handle EA other=0x" << hexfmt(ea & 3) << "\n";
                assert(0);
            }
            break;
        default:
            if (ea == ea_sr) {
                os << "SR";
                break;
            } else if (ea == ea_ccr) {
                os << "CCR";
                break;
            } else if (ea == ea_usp) {
                os << "USP";
                break;
            } else if (ea == ea_reglist) {
                assert(inst.nea == 2);
                // Note: reversed for predecrement
                os << reg_list_string(reglist, i == 0 && (inst.ea[1] >> 3) == ea_m_A_ind_pre);
                break;
            } else if (ea == ea_bitnum) {
                assert(eaw < inst.ilen);
                uint16_t b = iwords[eaw++];
                if (inst.size == opsize::b)
                    b &= 7;
                else
                    b &= 31;
                os << "#" << b;
                break;
            }

            if (inst.extra & extra_disp_flag) {
                assert(ea == ea_disp);
                assert(eaw < inst.ilen);
                int16_t n = iwords[eaw++];
                os << "$" << hexfmt(pc + 2 + n);
            } else if (ea == ea_disp) {
                os << "$" << hexfmt(pc + 2 + static_cast<int8_t>(inst.data));
            } else {
                os << "#$" << hexfmt(inst.data);
            }
            break;
        }
    }

    assert(eaw == inst.ilen);
}


