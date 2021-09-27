#ifndef RTC_H_INCLUDED
#define RTC_H_INCLUDED

#include <memory>

class real_time_clock {
public:
    explicit real_time_clock(class memory_handler& mem);
    ~real_time_clock();

    void handle_state(class state_file& sf);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
