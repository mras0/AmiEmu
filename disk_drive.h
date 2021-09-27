#ifndef DISK_DRIVE_H
#define DISK_DRIVE_H

#include <memory>
#include <vector>
#include <stdint.h>

// Matches CIAF_...
constexpr uint8_t DSKF_RDY	      = 1 << 5;
constexpr uint8_t DSKF_TRACK0	  = 1 << 4;
constexpr uint8_t DSKF_PROT	      = 1 << 3;
constexpr uint8_t DSKF_CHANGE	  = 1 << 2;
constexpr uint8_t DSKF_ALL        = 0xf << 2;

class disk_drive {
public:
    explicit disk_drive();
    ~disk_drive();

    void insert_disk(std::vector<uint8_t>&& data);
    uint8_t cia_state() const;

    void set_motor(bool enabled);
    void set_side_dir(bool side, bool dir);
    void dir_step();
    void read_mfm_track(uint8_t* dest, uint16_t wordcount);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

constexpr uint8_t max_drives = 4;

#endif