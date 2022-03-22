#ifndef DEBUG_BOARD_H_INCLUDED
#define DEBUG_BOARD_H_INCLUDED

#include <memory>
#include <string>
#include <vector>

class memory_handler;
class autoconf_device;

class debug_board {
public:
    explicit debug_board(memory_handler& mem);
    ~debug_board();

    autoconf_device& autoconf_dev();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
