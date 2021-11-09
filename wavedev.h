#ifndef WAVEDEV_H_INCLUDED
#define WAVEDEV_H_INCLUDED

#include <memory>
#include <functional>
#include <stdint.h>

class wavedev {
public:
    using callback_t = std::function<void(int16_t* /*buffer*/, size_t /*num_stereo_samples*/)>;

    explicit wavedev(unsigned sample_rate, unsigned buffer_size, callback_t callback);
    ~wavedev();

    wavedev(const wavedev&) = delete;
    wavedev& operator=(const wavedev&) = delete;

    void set_paused(bool pause);

    static bool is_null();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif