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
    using task_info_callback = std::function<void (uint32_t)>;

    explicit debug_board(memory_handler& mem);
    ~debug_board();

    autoconf_device& autoconf_dev();

    void set_callbacks(const task_info_callback& init, const task_info_callback& add_task, const task_info_callback& rem_task);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
