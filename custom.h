#ifndef CUSTOM_H
#define CUSTOM_H

#include <memory>
#include "memory.h"

constexpr unsigned graphics_width  = 768; // 24*16*2
constexpr unsigned graphics_height = 568; // 284*2 (actually 285, but only for every other field)

class custom_handler {
public:
    explicit custom_handler(memory_handler& mem_handler);
    ~custom_handler();

    void step();

    uint8_t current_ipl() const; // 0..7
    const uint32_t* new_frame();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
