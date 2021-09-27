#ifndef GUI_H
#define GUI_H

#include <memory>
#include <vector>
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
    struct event {
        event_type type;
        union {
            keyboard_event keyboard;
            mouse_button_event mouse_button;
            mouse_move_event mouse_move;
        };
    };

    std::vector<event> update();
    void update_image(const uint32_t* data);
    void led_state(uint8_t s);
    void serial_data(const std::vector<uint8_t>& data);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
