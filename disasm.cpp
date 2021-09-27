#include "disasm.h"

#include <string>
#include <ostream>
#include <cassert>

#include "instruction.h"
#include "ioutil.h"

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

void disasm(std::ostream& os, uint32_t pc, const uint16_t* iwords, size_t num_iwords)
{
    assert(iwords && num_iwords);
    const auto& inst = instructions[iwords[0]];
    assert(inst.ilen <= num_iwords);

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
    if (inst.type == inst_type::ILLEGAL && iwords[0] != 0x4afc)
        os << "DC.W\t$" << hexfmt(iwords[0]);
    else
        os << inst.name;

    unsigned eaw = 1;
    for (unsigned i = 0; i < inst.nea; ++i) {
        os << (i == 0 ? "\t" : ", ");
        const auto ea = inst.ea[i];
        switch (ea >> 3) {
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
            if (extw & (7 << 8)) {
                os << "Full extension word/scale not supported\n";
                assert(0);
            } else {
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
        }
        case ea_m_Other:
            switch (ea & 7) {
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
        case ea_m_inst_data:
            if (ea == ea_sr) {
                os << "SR";
                break;
            } else if (ea == ea_ccr) {
                os << "CCR";
                break;
            } else if (ea == ea_reglist) {
                assert(eaw < inst.ilen);
                assert(inst.nea == 2);
                const auto list = iwords[eaw++];
                // Note: reversed for predecrement
                os << reg_list_string(list, i == 0 && (inst.ea[1] >> 3) == ea_m_A_ind_pre);
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
        default:
            os << "TODO: Handle EA=0x" << hexfmt(ea) << "\n";
            assert(0);
        }
    }

    assert(eaw == inst.ilen);
}


