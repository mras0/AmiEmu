#ifndef COLOR_UTIL_H
#define COLOR_UTIL_H

#include <stdint.h>

constexpr uint32_t rgb4_to_8(const uint16_t rgb4)
{
    const uint32_t r = (rgb4 >> 8) & 0xf;
    const uint32_t g = (rgb4 >> 4) & 0xf;
    const uint32_t b = rgb4 & 0xf;
    return r << 20 | r << 16 | g << 12 | g << 8 | b << 4 | b;
}

constexpr uint16_t rgb8_to_4(uint32_t rgb8)
{
    const uint16_t r = (rgb8 >> 20) & 0xf;
    const uint16_t g = (rgb8 >> 12) & 0xf;
    const uint16_t b = (rgb8 >> 4) & 0xf;
    return (r << 8) | (g << 4) | b;
}

#endif
