#ifndef CUSTOM_H
#define CUSTOM_H

#include <memory>
#include <functional>
#include <iosfwd>
#include <string>
#include "memory.h"

constexpr unsigned graphics_width  = 768; // 24*16*2
constexpr unsigned graphics_height = 570; // 285*2 (285 is only for every other field)

class cia_handler;

class custom_handler {
public:
    explicit custom_handler(memory_handler& mem_handler, cia_handler& cia);
    ~custom_handler();

    using serial_data_handler = std::function<void(uint8_t numbits, uint8_t data)>;

    void step();

    uint8_t current_ipl() const; // 0..7
    const uint32_t* new_frame();

    void set_serial_data_handler(const serial_data_handler& handler);
    void set_rbutton_state(bool pressed);
    void mouse_move(int dx, int dy);

    void show_debug_state(std::ostream& os);
    uint32_t copper_ptr(uint8_t idx); // 0=current

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

std::string custom_regname(uint32_t offset);

#endif
