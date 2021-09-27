#include "debug.h"
#include <iostream>

uint32_t debug_flags = 0;
std::ostream* debug_stream = &std::cout;