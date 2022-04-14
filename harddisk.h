#ifndef HARDDISK_H_INCLUDED
#define HARDDISK_H_INCLUDED

#include <memory>
#include <string>
#include <vector>
#include <functional>

class memory_handler;
class autoconf_device;

class harddisk {
public:
    using bool_func = std::function<bool ()>;

    explicit harddisk(memory_handler& mem, bool& cpu_active, const bool_func& should_disable_autoboot, const std::vector<std::string>& hdfilenames, const std::vector<std::string>& shared_folders);
    ~harddisk();

    autoconf_device& autoconf_dev();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
