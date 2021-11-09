#include "wavedev.h"

class wavedev::impl {
};

wavedev::wavedev(unsigned, unsigned, callback_t) { }

wavedev::~wavedev() = default;

void wavedev::set_paused(bool)
{
}

bool wavedev::is_null()
{
    return true;
}
