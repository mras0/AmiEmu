#ifndef GUI_H
#define GUI_H

#include <vector>
#include <string>
#include <array>
#include <functional>
#include <stdint.h>

class gui {
public:
    explicit gui(unsigned width, unsigned height, const std::array<std::string, 4>& disk_filenames);
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
        uint8_t drive;
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

    using on_pause_callback = std::function<void (bool)>;

    std::vector<event> update();
    void update_image(const uint32_t* data);
    void led_state(uint8_t s);
    void serial_data(const std::vector<uint8_t>& data);
    void set_active(bool act);
    bool debug_prompt(std::string& line);
    void set_debug_memory(const std::vector<uint8_t>& mem, const std::vector<uint16_t>& custom);
    void set_debug_windows_visible(bool visible);
    void set_on_pause_callback(const on_pause_callback& on_pause);

private:
    class impl;
    impl* impl_;
};

#endif
