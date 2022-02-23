#ifndef HARDDISK_H_INCLUDED
#define HARDDISK_H_INCLUDED

#include <memory>
#include <string>
#include <vector>

class memory_handler;
class autoconf_device;

class harddisk {
public:
    explicit harddisk(memory_handler& mem, bool& cpu_active, const std::vector<std::string>& hdfilenames);
    ~harddisk();

    autoconf_device& autoconf_dev();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
