#ifndef DEBUG_BOARD_H_INCLUDED
#define DEBUG_BOARD_H_INCLUDED

#include <memory>
#include <string>
#include <vector>
#include <functional>

class memory_handler;
class autoconf_device;

class debug_board {
public:
    using ptr1_cb = std::function<void (uint32_t)>;
    using ptr2_cb = std::function<void (uint32_t, uint32_t)>;

    explicit debug_board(memory_handler& mem);
    ~debug_board();

    autoconf_device& autoconf_dev();

    void set_callbacks(const ptr1_cb& init, const ptr2_cb& add_task, const ptr1_cb& rem_task, const ptr2_cb& load_seg);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
