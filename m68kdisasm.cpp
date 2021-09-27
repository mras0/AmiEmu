#include <cassert>
#include <iostream>

#include "disasm.h"
#include "ioutil.h"
#include "instruction.h"
#include "memory.h"

void disasm_stmts(const std::vector<uint8_t>& data, uint32_t offset, uint32_t end)
{
    auto get_word = [&]() {
        if (offset + 2 > data.size())
            throw std::runtime_error { "offset $" + hexstring(offset) + " out of range $" + hexstring(data.size(), 8) };
        const auto w = get_u16(&data[offset]);
        offset += 2;
        return w;
    };
    while (offset < end) {
        uint16_t iw[max_instruction_words];
        const auto pc = offset;
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

int main(int argc, char* argv[])
{
    try {
        if (argc < 4)
            throw std::runtime_error { "Usage: m68kdisasm file offset end" };
        const auto data = read_file(argv[1]);
        const auto offset = hex_or_die(argv[2]);
        const auto end = hex_or_die(argv[3]);
        disasm_stmts(data, offset, end);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
