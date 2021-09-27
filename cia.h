#ifndef CIA_H
#define CIA_H

#include <memory>
#include "memory.h"

class cia_handler {
public:
    explicit cia_handler(memory_handler& mem_handler, rom_area_handler& rom_handler);
    ~cia_handler();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif