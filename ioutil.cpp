#include "ioutil.h"
#include <ostream>
#include <cassert>
#include <sstream>
#include <fstream>

std::ostream& operator<<(std::ostream& os, const num_formatter& nf)
{
    assert(nf.base_ == 2 || nf.base_ == 16); // TODO: Binary at least
    assert(nf.width_ > 0);

    const uint8_t mask = static_cast<uint8_t>(nf.base_-1);
    const uint8_t shift = nf.base_ == 16 ? 4 : 1;

    for (int w = nf.width_; w--;) {
        os << ("0123456789abcdef"[(nf.num_ >> (w*shift)) & mask]);
    }
    return os;
}

std::string detail::do_format(const num_formatter& nf)
{
    std::ostringstream os;
    os << nf;
    return os.str();
}

std::vector<uint8_t> read_file(const std::string& path)
{
    std::ifstream in { path, std::ifstream::binary };
    if (!in) {
        throw std::runtime_error { "Error opening " + path };
    }

    in.seekg(0, std::ifstream::end);
    const auto len = static_cast<unsigned>(in.tellg());
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

void hexdump(std::ostream& os, const uint8_t* data, size_t size)
{
    constexpr size_t width = 16;
    for (size_t i = 0; i < size;) {
        const size_t here = std::min(size - i, width);

        for (size_t j = 0; j < here; ++j)
            os << hexfmt(data[i + j]) << ' ';
        for (size_t j = here; j < width; ++j)
            os << "   ";
        for (size_t j = 0; j < here; ++j) {
            const uint8_t d = data[i + j];
            os << static_cast<char>(d >= 32 && d < 128 ? d : '.');
        }
        os << "\n";
        i += here;
    }
}

void hexdump16(std::ostream& os, uint32_t addr, const uint8_t* data, size_t size)
{
    constexpr size_t width = 16;
    assert(size % 2 == 0);
    for (size_t i = 0; i < size;) {
        const size_t here = std::min(size - i, width);

        os << hexfmt(addr) << "  ";

        for (size_t j = 0; j < here; ++j)
            os << hexfmt(data[i + j]) << (j & 1 ? " " : "");
        for (size_t j = here; j < width; ++j)
            os << (j & 1 ? "   " : "  ");
        for (size_t j = 0; j < here; ++j) {
            const uint8_t d = data[i + j];
            os << static_cast<char>(d >= 32 && d < 128 ? d : '.');
        }
        os << "\n";
        i += here;
        addr += static_cast<uint32_t>(here);
    }
}

std::string trim(const std::string& line)
{
    const size_t l = line.length();
    size_t s, e;
    for (s = 0; s < l && isspace(line[s]); ++s)
        ;
    for (e = l; e-- && isspace(line[e]);)
        ;
    return line.substr(s, e + 1 - s);
}

uint8_t digitval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return 0xff;
}

std::pair<bool, uint32_t> number_from_string(const char* s, uint8_t base)
{
    assert(base >= 2 && base <= 16);
    if (!*s)
        return { false, 0 };
    uint32_t val = 0;
    char c;
    int len = 0;
    while ((c = *s++) != '\0') {
        if (++len > 8)
            return { false, 0 };
        uint8_t d = digitval(c);
        if (d >= base)
            return { false, 0 };
        val = val * base + d;
    }
    return { true, val };
}

std::string toupper_str(const std::string& s)
{
    auto res = s;
    for (auto& c : res) {
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    }
    return res;
}
