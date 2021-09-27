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