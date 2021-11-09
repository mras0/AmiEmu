#include "gui.h"
#include <iostream>

class gui::impl {
};

gui::gui(unsigned, unsigned, const std::array<std::string, 4>&)
    : impl_ {}
{
}

gui::~gui() = default;

std::vector<gui::event> gui::update()
{
    return {};
}

void gui::update_image(const uint32_t*)
{
}

void gui::led_state(uint8_t)
{
}

void gui::disk_activty(uint8_t, uint8_t, bool)
{
}

void gui::serial_data(const std::vector<uint8_t>&)
{
}

void gui::set_active(bool)
{
}

bool gui::debug_prompt(std::string& line)
{
    std::cout << "> " << std::flush;
    return !!std::getline(std::cin, line);
}

void gui::set_debug_memory(const std::vector<uint8_t>&, const std::vector<uint16_t>&)
{
}

void gui::set_debug_windows_visible(bool)
{
}

void gui::set_on_pause_callback(const on_pause_callback&)
{
}