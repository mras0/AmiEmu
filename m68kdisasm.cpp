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

constexpr bool is_initial_hunk(uint32_t hunk_type)
{
    return hunk_type == HUNK_CODE || hunk_type == HUNK_DATA || hunk_type == HUNK_BSS;
}

class hunk_file {
public:
    explicit hunk_file(const std::vector<uint8_t>& data)
        : data_ { data }
    {
        if (data.size() < 8)
            throw std::runtime_error { "File is too small to be Amiga HUNK file" };
        const uint32_t header_type = read_u32();
        if (header_type == HUNK_UNIT)
            throw std::runtime_error { "HUNK_UNIT not supported yet" };
        else if (header_type != HUNK_HEADER)
            throw std::runtime_error { "Invalid hunk type " + std::to_string(header_type) };

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
        std::vector<std::pair<std::string, uint32_t>> symbols;

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
                case HUNK_SYMBOL:
                    for (;;) {
                        const auto sym = read_string();
                        if (sym.empty())
                            break;
                        const auto ofs = read_u32();
                        std::cout << "\t\t\t$" << hexfmt(hunks[hunk_num].addr + ofs) << " " << sym << "\n";
                        symbols.push_back({sym, hunks[hunk_num].addr + ofs});
                    }
                    break;
                case HUNK_RELOC32:
                    for (;;) {
                        const auto num_offsets = read_u32();
                        if (!num_offsets)
                            break;
                        const auto hunk_ref = read_u32();
                        std::cout << "\t\t\t Hunk " << hunk_ref << " num_offsets=" << num_offsets << "\n";
                        if (hunk_ref > last_hunk)
                            throw std::runtime_error { "Invalid RELOC32 refers to unknown hunk " + std::to_string(hunk_ref) };
                        auto& hd = hunks[hunk_num].data;
                        for (uint32_t i = 0; i < num_offsets; ++i) {
                            const auto ofs = read_u32();
                            if (ofs > hd.size() - 4)
                                throw std::runtime_error { "Invalid relocation" };
                            auto c = &hd[ofs];
                            put_u32(c, get_u32(c) + hunks[hunk_ref].addr);
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
                    throw std::runtime_error { "TODO: Support HUNK type " + hunk_type_string(ht) };
                }
            }

            ++table_index;
        }

        if (table_index != table_size)
            throw std::runtime_error { "Only " + std::to_string(table_index) + " out of " + std::to_string(table_size) + " hunks read" };

        if (pos_ != data.size())
            throw std::runtime_error { "File not done pos=$" + hexstring(pos_) + " size=" + hexstring(data.size(), 8) };

        for (uint32_t i = 0; i < table_size; ++i) {
            if (hunks[i].type != HUNK_CODE)
                continue;
            disasm_stmts(hunks[i].data, 0, hunks[i].loaded_size, hunks[i].addr);
        }
    }

private:
    const std::vector<uint8_t>& data_;
    uint32_t pos_ = 0;

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

    std::string read_string()
    {
        const auto size = read_u32() << 2;
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
};

void read_hunk(const std::vector<uint8_t>& data)
{
    hunk_file hf { data };
}

int main(int argc, char* argv[])
{
    try {
        if (argc < 2)
            throw std::runtime_error { "Usage: m68kdisasm file [offset] [end]" };
        const auto data = read_file(argv[1]);
        if (data.size() > 4 && (get_u32(&data[0]) == HUNK_HEADER || get_u32(&data[0]) == HUNK_UNIT)) {
            read_hunk(data);
        } else {
            const auto offset = argc > 2 ? hex_or_die(argv[2]) : 0;
            const auto end = argc > 3 ? hex_or_die(argv[3]) : static_cast<uint32_t>(data.size());
            disasm_stmts(data, offset, end);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
