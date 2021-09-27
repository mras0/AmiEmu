#ifndef DISK_DRIVE_H
#define DISK_DRIVE_H

#include <memory>
#include <vector>
#include <functional>
#include <stdint.h>

// Matches CIAF_...
constexpr uint8_t DSKF_RDY	      = 1 << 5;
constexpr uint8_t DSKF_TRACK0	  = 1 << 4;
constexpr uint8_t DSKF_PROT	      = 1 << 3;
constexpr uint8_t DSKF_CHANGE	  = 1 << 2;
constexpr uint8_t DSKF_ALL        = 0xf << 2;

constexpr uint16_t MFM_SECTOR_SIZE_WORDS = 0x220;  // Number of words in a MFM sector
constexpr uint16_t MFM_TRACK_SIZE_WORDS  = 0x1900; // Number of words in a MFM track

class disk_drive {
public:
    explicit disk_drive(const std::string& name);
    ~disk_drive();

    const std::string& name() const;

    using disk_activity_handler = std::function<void (uint8_t track, bool write)>;

    void insert_disk(std::vector<uint8_t>&& data);
    uint8_t cia_state() const;

    void set_motor(bool enabled);
    void set_side_dir(bool side, bool dir);
    void dir_step();
    void read_mfm_track(uint8_t* dest); // Need to be able to hold MFM_TRACK_SIZE words
    void set_disk_activity_handler(const disk_activity_handler& handler);
    void show_debug_state(std::ostream& os);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

constexpr uint8_t max_drives = 4;

#endif