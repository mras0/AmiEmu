#ifndef CIA_H
#define CIA_H

#include <memory>
#include <iosfwd>
#include "memory.h"
#include "disk_drive.h"

class cia_handler {
public:
    explicit cia_handler(memory_handler& mem_handler, rom_area_handler& rom_handler, disk_drive* dfs[max_drives]);
    ~cia_handler();

    // Call with frequency equal to timer tick rate (.715909 Mhz NTSC; .709379 Mhz PAL) == Base CPU freq / 10
    void step();

    uint8_t active_irq_mask() const;
    void increment_tod_counter(uint8_t cia);
    void keyboard_event(bool pressed, uint8_t raw);
    void set_button_state(uint8_t idx, bool pressed);
    bool power_led_on() const;
    disk_drive& active_drive(); // For DMA
    void show_debug_state(std::ostream& os);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif