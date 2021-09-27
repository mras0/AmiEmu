#ifndef IOUTIL_H
#define IOUTIL_H

#include <iosfwd>
#include <stdint.h>

class num_formatter {
public:
    explicit num_formatter(uint64_t num, int base, int width) : num_{num}, base_{base}, width_{width} {}
    friend std::ostream& operator<<(std::ostream& os, const num_formatter& hf);
private:
    uint64_t num_;
    int base_;
    int width_;
};


template <typename T>
num_formatter hexfmt(T n, int w = static_cast<int>(sizeof(T) * 2))
{
    return num_formatter { static_cast<uint64_t>(n), 16, w };
}

template <typename T>
num_formatter binfmt(T n, int w = static_cast<int>(sizeof(T) * 8))
{
    return num_formatter { static_cast<uint64_t>(n), 2, w };
}

#endif
