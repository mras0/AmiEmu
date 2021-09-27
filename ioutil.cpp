#include "ioutil.h"
#include <ostream>
#include <cassert>

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
