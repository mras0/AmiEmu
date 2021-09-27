#include <iostream>
#include <stdint.h>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <cassert>

#include "ioutil.h"
#include "instruction.h"

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

class memory_handler {
public:
    explicit memory_handler(const std::string& rom_file_name)
        : rom_data_ { read_file(rom_file_name) }
    {
        if (rom_data_.size() != 256 * 1024) {
            throw std::runtime_error { "Unexpected size of ROM" };
        }
    }

    memory_handler(const memory_handler&) = delete;
    memory_handler& operator=(const memory_handler&) = delete;

    uint8_t read_u8(uint32_t addr)
    {
        assert(addr >= rom_base && addr < rom_base + rom_data_.size());
        return rom_data_[addr - rom_base];
    }

    uint16_t read_u16(uint32_t addr)
    {
        assert(addr >= rom_base && addr < rom_base + rom_data_.size() - 1);
        assert(!(addr & 1)); // TODO: Handle as CPU exception
        return get_u16(&rom_data_[addr - rom_base]);
    }

    uint32_t read_u32(uint32_t addr)
    {
        assert(addr >= rom_base && addr < rom_base + rom_data_.size() - 3);
        assert(!(addr & 1)); // TODO: Handle as CPU exception
        return get_u32(&rom_data_[addr - rom_base]);
    }

private:
    static constexpr const uint32_t rom_base = 0x00fc0000;
    std::vector<uint8_t> rom_data_;
};

enum ea_m {
    ea_m_Dn            = 0b000, // Dn
    ea_m_An            = 0b001, // An
    ea_m_A_ind         = 0b010, // (An)
    ea_m_A_ind_post    = 0b011, // (An)+
    ea_m_A_ind_pre     = 0b100, // -(An)
    ea_m_A_ind_disp16  = 0b101, // (d16, An)
    ea_m_A_ind_index   = 0b110, // (d8, An, Xn)
    ea_m_Other         = 0b111, // (Other)

    ea_m_inst_data     = 0b1000, // Hack value from mktab
};

enum ea_other {
    ea_other_abs_w     = 0b000, // (addr.W)
    ea_other_abs_l     = 0b001, // (addr.L)
    ea_other_pc_disp16 = 0b010, // (d16, PC)
    ea_other_pc_index  = 0b011, // (d8, PC, Xn)
    ea_other_imm       = 0b100,
};

unsigned ea_extra_words(uint8_t ea, opsize size)
{
    switch (ea >> 3) {
    case ea_m_Dn:
    case ea_m_An:
    case ea_m_A_ind:
    case ea_m_A_ind_post:
    case ea_m_A_ind_pre:
        return 0;
    case ea_m_A_ind_disp16:
        return 1;
    case ea_m_A_ind_index:
        break;
    case ea_m_Other:
        switch (ea & 7) {
        case ea_other_abs_w:
            return 1;
        case ea_other_abs_l:
            return 2;
        case ea_other_pc_disp16:
            return 1;
        case ea_other_pc_index:
            break;
        case ea_other_imm:
            assert(size != opsize::none);
            return size == opsize::l ? 2 : 1;
        }
    case ea_m_inst_data:
        return 0;
    }
    std::cout << "Unsupported EA: 0x" << hexfmt(ea) << "\n";
    assert(0);
    return 0;
}

int main()
{
    try {
        memory_handler mem { "../../rom.bin" };
        // TODO: ROM is actually mapped to address 0 at start, get PC/SP by reading that
        const uint32_t initial_sp = mem.read_u32(0x00fc0000);
        const uint32_t initial_pc = mem.read_u32(0x00fc0004);
        std::cout << "Initial SP=" << hexfmt(initial_sp) << " PC=" << hexfmt(initial_pc) << "\n";
        uint32_t pc = initial_pc;

        constexpr const unsigned max_inst_words = 4; // XXX
        for (;;) {
            const auto start_pc = pc;
            const auto iword = mem.read_u16(pc);
            uint16_t eawords[32];
            pc += 2;

            const auto& inst = instructions[iword];

            unsigned ea_words = 0;
            for (int i = 0; i < inst.nea; ++i) {
                ea_words += ea_extra_words(inst.ea[i], inst.size);
            }
            if (inst.extra & extra_disp_flag)
                ++ea_words;
            std::cout << hexfmt(start_pc) << "\t" << hexfmt(iword);
            for (unsigned i = 0; i < ea_words; ++i) {
                eawords[i] = mem.read_u16(pc);
                pc += 2;
                std::cout << " " << hexfmt(eawords[i]);
            }
            for (unsigned i = ea_words; i < max_inst_words; ++i) {
                std::cout << "     ";
            }
            std::cout << "\t" << inst.name;

            unsigned eaw = 0;
            for (unsigned i = 0; i < inst.nea; ++i) {
                std::cout << (i == 0 ? "\t" : ", ");
                const auto ea = inst.ea[i];
                switch (ea >> 3) {
                case ea_m_Dn:
                    std::cout << "D" << (ea & 7);
                    break;
                case ea_m_An:
                    std::cout << "A" << (ea & 7);
                    break;
                case ea_m_A_ind:
                    std::cout << "(A" << (ea & 7) << ")";
                    break;
                case ea_m_A_ind_post:
                    std::cout << "(A" << (ea & 7) << ")+";
                    break;
                case ea_m_A_ind_pre:
                    std::cout << "-(A" << (ea & 7) << ")";
                    break;
                case ea_m_A_ind_disp16: {
                    assert(eaw < ea_words);
                    int16_t n = eawords[eaw++];
                    std::cout << "$";
                    if (n < 0) {
                        std::cout << "-";
                        n = -n;
                    }
                    std::cout << hexfmt(static_cast<uint16_t>(n));
                    std::cout << "(A" << (ea & 7) << ")";
                    break;
                }
                case ea_m_Other:
                    switch (ea & 7) {
                    case ea_other_abs_w:
                        assert(eaw < ea_words);
                        std::cout << "$";
                        std::cout << hexfmt(eawords[eaw++]);
                        std::cout << ".W";
                        break;
                    case ea_other_abs_l:
                        assert(eaw + 1 < ea_words);
                        std::cout << "$";
                        std::cout << hexfmt(eawords[eaw++]);
                        std::cout << hexfmt(eawords[eaw++]);
                        std::cout << ".L";
                        break;
                    case ea_other_imm:
                        std::cout << "#$";
                        if (inst.size == opsize::l) {
                            assert(eaw + 1 < ea_words);
                            std::cout << hexfmt(eawords[eaw++]);
                            std::cout << hexfmt(eawords[eaw++]);
                        } else {
                            assert(eaw < ea_words);
                            if (inst.size == opsize::b)
                                std::cout << hexfmt(static_cast<uint8_t>(eawords[eaw++]));
                            else
                                std::cout << hexfmt(eawords[eaw++]);
                        }
                        break;
                    default:
                        std::cout << "\nTODO: Handle EA other=0x" << hexfmt(ea&3) << "\n";
                        assert(0);
                    }
                    break;
                case ea_m_inst_data:
                    if (ea == ea_sr) {
                        std::cout << "SR";
                        break;
                    }

                    if (inst.extra & extra_disp_flag) {
                        assert(ea == ea_disp);
                        assert(eaw < ea_words);
                        int16_t n = eawords[eaw++];
                        std::cout << "$" << hexfmt(start_pc + 2 + n);
                    } else if (ea == ea_disp) {
                        std::cout << "$" << hexfmt(start_pc + 2 + static_cast<int8_t>(inst.data));
                    } else {
                        std::cout << "#$" << hexfmt(inst.data);
                    }
                    break;
                default:
                    std::cout << "TODO: Handle EA=0x" << hexfmt(ea) << "\n";
                    assert(0);
                }
            }

            assert(eaw == ea_words);

            std::cout << "\n";

            if (inst.type == inst_type::ILLEGAL)
                break;
        }        


    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}