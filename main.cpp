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

int main()
{
    try {
        memory_handler mem { "../../rom.bin" };
        // TODO: ROM is actually mapped to address 0 at start, get PC/SP by reading that
        const uint32_t initial_sp = mem.read_u32(0x00fc0000);
        const uint32_t initial_pc = mem.read_u32(0x00fc0004);
        std::cout << "Initial SP=" << hexfmt(initial_sp) << " PC=" << hexfmt(initial_pc) << "\n";
        uint32_t pc = initial_pc;

        for (;;) {
            const auto start_pc = pc;
            uint16_t iwords[max_instruction_words];
            iwords[0] = mem.read_u16(pc);
            pc += 2;

            const auto& inst = instructions[iwords[0]];
            assert(inst.ilen <= std::size(iwords));

            for (unsigned i = 1; i < inst.ilen; ++i) {
                iwords[i] = mem.read_u16(pc);
                pc += 2;
            }

            disasm(std::cout, start_pc, iwords, inst.ilen);
            std::cout << "\n";

            if (inst.type == inst_type::ILLEGAL)
                break;
        }        


    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}