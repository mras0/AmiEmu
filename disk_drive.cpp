#include "disk_drive.h"
#include <stdexcept>
#include <cassert>
#include "ioutil.h"
#include "memory.h"

//#define DISK_DEBUG

#ifdef DISK_DEBUG
#include <iostream>
#endif

namespace {

constexpr uint16_t NUMSECS        = 11;                  // sectors per track
constexpr uint16_t TD_SECTOR      = 512;                 // bytes per sector
constexpr uint16_t TRACK_SIZE     = NUMSECS*TD_SECTOR;   // bytes per track
constexpr uint16_t MFM_SYNC       = 0x4489;              // MFM sync value
constexpr uint16_t MFM_TRACK_SIZE = 0x1900;              // Number of words in a MFM track
constexpr uint16_t NUM_CYLINDERS  = 80;                  // There are 80 cylinders on a Amiga floppy disk
constexpr uint32_t DISK_SIZE      = NUM_CYLINDERS * 2 * TRACK_SIZE; // Each cylinder has 2 MFM tracks, 1 on each side 

static_assert(DISK_SIZE == 901120); // 880K

constexpr uint16_t sync = 0x4489;
constexpr uint32_t fill = 0xaaaaaaaa; // MFM encoded 0

void put_split_long(uint8_t* dest, uint32_t l)
{
    put_u32(dest, (l >> 1) & 0x55555555); // odd
    put_u32(dest + 4, l & 0x55555555);        // even
}

void put_split_long_fill(uint8_t* dest, uint32_t l)
{
    put_u32(dest, ((l >> 1) & 0x55555555) | fill); // odd
    put_u32(dest + 4, (l & 0x55555555) | fill); // even
}

uint32_t checksum(const uint8_t* data, uint32_t nlongs)
{
    uint32_t csum = 0;
    while (nlongs--) {
        csum ^= get_u32(data);
        data += 4;
    }
    return csum;
}

}


class disk_drive::impl {
public:
    explicit impl()
    {
    }

    void insert_disk(std::vector<uint8_t>&& data)
    {
        if (data.size() != 0 && data.size() != DISK_SIZE) {
            throw std::runtime_error { "Invalid disk size $" + hexstring(data.size()) };
        }
        data_ = std::move(data);
    }

    uint8_t cia_state() const
    {
        uint8_t flags = DSKF_ALL;
        if (data_.empty()) {
            flags &= ~(DSKF_CHANGE | DSKF_TRACK0);
        } else {
            if (cyl_ == 0)
                flags &= ~DSKF_TRACK0;
            flags &= ~DSKF_PROT;
            if (motor_)
                flags &= ~DSKF_RDY;
        }
        return flags;
    }
    
    void set_motor(bool enabled)
    {
#ifdef DISK_DEBUG
        if (motor_ != enabled)
            std::cout << "Motor turning " << (enabled ? "on" : "off") << "\n";
#endif
        motor_ = enabled;
    }

    void set_side_dir(bool side, bool dir)
    {
#ifdef DISK_DEBUG
        if (side != side_ || seek_dir_ != dir)
            std::cout << "Changing side/stp direction cyl = " << (int)cyl_ << " new side = " << (side ? "lower" : "upper") << " new direction = " << (dir ? "out (towards 0)" : "in (towards 79)") << "\n";
#endif
        side_ = side;
        seek_dir_ = dir;
    }

    void dir_step()
    {
#ifdef DISK_DEBUG
        std::cout << "Stepping cyl = " << (int)cyl_ << " side = " << (side_ ? "lower" : "upper") << " direction = " << (seek_dir_ ? "out (towards 0)" : "in (towards 79)") << "\n";
#endif
        if (!seek_dir_ && cyl_ < NUM_CYLINDERS - 1)
            ++cyl_;
        else if (seek_dir_ && cyl_)
            --cyl_;
#ifdef DISK_DEBUG
        std::cout << "New cyl = " << (int)cyl_ << " side = " << (side_ ? "lower" : "upper") << " direction = " << (seek_dir_ ? "out (towards 0)" : "in (towards 79)") << "\n";
#endif
    }

    void read_mfm_track(uint8_t* dest, uint16_t wordcount)
    {
        if (wordcount < (1088 / 2) * NUMSECS)
            throw std::runtime_error { "Unsupported MFM read from drive (count=$" + hexstring(wordcount) + ")" };
        if (!motor_)
            throw std::runtime_error { "Reading while motor is not on" };

        // Lower side=first
        const uint8_t tracknum = !side_ + cyl_ * 2;

#ifdef DISK_DEBUG
        std::cout << "Reading track $" << hexfmt(tracknum) << " cyl = " << (int)cyl_ << " side = " << (side_ ? "lower" : "upper") << "\n";
#endif

        if (disk_activity_handler_)
            disk_activity_handler_(tracknum, false);

        for (uint8_t sec = 0; sec < NUMSECS; ++sec) {
            const uint8_t* raw_data = &data_[(tracknum * NUMSECS + sec) * TD_SECTOR];
            put_u32(&dest[0], fill); // Preamble
            put_u16(&dest[4], sync);
            put_u16(&dest[6], sync);
            put_split_long(&dest[8], 0xffU<<24 | tracknum << 16 | sec << 8 | (11 - sec));
            // sector label
            for (uint32_t i = 16; i < 48; i += 4)
                put_u32(&dest[i], fill);
            // header checksum
            put_split_long_fill(&dest[48], checksum(&dest[8], (48 - 8)/4));
            // data
            for (uint16_t i = 0; i < 512; ++i) {
                dest[64 + i] = 0xaa | ((raw_data[i] >> 1) & 0x55);
                dest[64 + 512 + i] = 0xaa | (raw_data[i] & 0x55);
            }
            // data checksum
            put_split_long_fill(&dest[56], checksum(&dest[64], (1088 - 64)/4));
            dest += 1088;
        }
        // gap
        for (uint16_t i = (1088/2) * NUMSECS; i < wordcount; ++i) {
            *dest++ = 0xaa;
            *dest++ = 0xaa;
        }
    }

    void set_disk_activity_handler(const disk_activity_handler& handler)
    {
        assert(!disk_activity_handler_);
        disk_activity_handler_ = handler;
    }

private:
    std::vector<uint8_t> data_;
    bool motor_ = false;
    bool side_ = false;      // false = upper head, true = lower head
    bool seek_dir_ = false;  // false = towards center, true = towards outside (0 is the outermost cylinder)
    uint8_t cyl_ = 0;
    disk_activity_handler disk_activity_handler_;
};

disk_drive::disk_drive()
    : impl_ { new impl() }
{
}

disk_drive::~disk_drive() = default;

void disk_drive::insert_disk(std::vector<uint8_t>&& data)
{
    impl_->insert_disk(std::move(data));
}

uint8_t disk_drive::cia_state() const
{
    return impl_->cia_state();
}

void disk_drive::set_motor(bool enabled)
{
    impl_->set_motor(enabled);
}

void disk_drive::set_side_dir(bool side, bool dir)
{
    impl_->set_side_dir(side, dir);
}

void disk_drive::dir_step()
{
    impl_->dir_step();
}

void disk_drive::read_mfm_track(uint8_t* dest, uint16_t wordcount)
{
    impl_->read_mfm_track(dest, wordcount);
}

void disk_drive::set_disk_activity_handler(const disk_activity_handler& handler)
{
    impl_->set_disk_activity_handler(handler);
}