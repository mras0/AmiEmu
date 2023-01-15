#ifndef DISK_FILE_H
#define DISK_FILE_H

#include <memory>
#include <vector>
#include <string>
#include <stdint.h>

class disk_file {
public:
    virtual ~disk_file() {}

    virtual const std::string& name() const = 0;
    virtual uint8_t num_cylinders() const = 0;
    virtual bool ofs_bootable() const = 0;

    virtual void read_mfm_track(uint8_t tracknum, uint8_t* dest) const = 0; // Need to be able to hold MFM_TRACK_SIZE words
    virtual void write_mfm_track(uint8_t tracknum, const uint8_t* src) = 0; // Buffer must contain MFM_TRACK_SIZE words
};

std::unique_ptr<disk_file> load_disk_file(const std::string& filename);

#endif
