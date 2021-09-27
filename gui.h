#ifndef GUI_H
#define GUI_H

#include <memory>
#include <vector>
#include <string>
#include <stdint.h>

class gui {
public:
    explicit gui(unsigned width, unsigned height);
    ~gui();

    enum class event_type {
        quit,
        keyboard,
        mouse_button,
        mouse_move,
        disk_inserted,
        debug_mode,
    };
    struct keyboard_event {
        bool pressed;
        uint8_t scancode;
    };
    struct mouse_button_event {
        bool pressed;
        bool left;
    };
    struct mouse_move_event {
        int dx;
        int dy;
    };
    struct disk_inserted_event {
        char filename[260];
    };
    struct event {
        event_type type;
        union {
            keyboard_event keyboard;
            mouse_button_event mouse_button;
            mouse_move_event mouse_move;
            disk_inserted_event disk_inserted;
        };
    };

    std::vector<event> update();
    void update_image(const uint32_t* data);
    void led_state(uint8_t s);
    void serial_data(const std::vector<uint8_t>& data);
    void set_active(bool act);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
