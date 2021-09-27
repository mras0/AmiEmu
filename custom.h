#ifndef CUSTOM_H
#define CUSTOM_H

#include <memory>
#include "memory.h"

class custom_handler {
public:
    explicit custom_handler(memory_handler& mem_handler);
    ~custom_handler();

    void step();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
