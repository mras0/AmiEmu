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
        keyboard
    };
    struct keyboard_event {
        bool pressed;
        uint8_t scancode;
    };
    struct event {
        event_type type;
        union {
            keyboard_event keyboard;
        };
    };

    std::vector<event> update();
    void update_image(const uint32_t* data);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
