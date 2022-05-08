#include "disk_drive.h"
#include <stdexcept>
#include <cassert>
#include <ostream>
#include <cstring>
#include <fstream>
#include "ioutil.h"
#include "memory.h"
#include "debug.h"
#include "state_file.h"

namespace {

constexpr uint16_t NUMSECS        = 11;                  // sectors per track
constexpr uint16_t TD_SECTOR      = 512;                 // bytes per sector
constexpr uint16_t TRACK_SIZE     = NUMSECS*TD_SECTOR;   // bytes per track
constexpr uint16_t MFM_SYNC       = 0x4489;              // MFM sync value
constexpr uint16_t NUM_CYLINDERS  = 80;                  // There are 80 cylinders on a Amiga floppy disk
constexpr uint32_t DISK_SIZE      = NUM_CYLINDERS * 2 * TRACK_SIZE; // Each cylinder has 2 MFM tracks, 1 on each side

// EClock is color clock frequency/5 = 708.240KHz
constexpr uint32_t ECLOCK_FREQ    = 708240;
constexpr uint32_t DISK_INDEX_CNT = ECLOCK_FREQ / 5;  // 300RPM (=5Hz)
constexpr uint32_t MOTOR_ON_CNT   = ECLOCK_FREQ / 20; // 50ms

static_assert(DISK_SIZE == 901120); // 880K

constexpr uint32_t fill = 0xaaaaaaaa; // MFM encoded 0
constexpr uint32_t mfm_mask = 0x55555555;

void put_split_long(uint8_t* dest, uint32_t l)
{
    put_u32(dest, (l >> 1) & mfm_mask); // odd
    put_u32(dest + 4, l & mfm_mask); // even
}

void put_split_long_fill(uint8_t* dest, uint32_t l)
{
    put_u32(dest, ((l >> 1) & mfm_mask) | fill); // odd
    put_u32(dest + 4, (l & mfm_mask) | fill); // even
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

uint32_t decode_long(const uint8_t* src)
{
    return (get_u32(src) & mfm_mask) << 1 | (get_u32(src + 4) & mfm_mask);
}

}


class disk_drive::impl {
public:
    explicit impl(const std::string& name)
        : name_ { name }
    {
    }

    ~impl()
    {
        flush();
    }

    const std::string& name() const
    {
        return name_;
    }

    void insert_disk(std::vector<uint8_t>&& data)
    {
        flush();

        if (DEBUG_DISK)
            *debug_stream << name_ << " Inserting disk of size $" << hexfmt(data.size(), 8) << "\n";
        data_ = std::move(data);

        if (data_.empty())
            return;

        if (data_.size() != DISK_SIZE) {
            throw std::runtime_error { name_ + " Invalid disk size $" + hexstring(data.size()) };
        }
    }

    bool step()
    {
        if (s_.motor_cnt) {
            --s_.motor_cnt;
            if (DEBUG_DISK && !s_.motor_cnt) {
                *debug_stream << name_ << " Motor is now " << (s_.motor ? "running at full speed" : "off") << "\n";
            }
        }
        // TODO: If motor isn't spinning at full speed the count should probably be different...
        if (s_.motor && s_.index_cnt) {
            if (--s_.index_cnt == 0) {
                // Disk completed a revolution
                s_.index_cnt = DISK_INDEX_CNT;
                return true;
            }
        }

        return false;
    }

    uint8_t cia_state() const
    {
        uint8_t flags = DSKF_ALL;
        if (data_.empty()) {
            flags &= ~DSKF_CHANGE;
        } else {
            //flags &= ~DSKF_PROT;
        }

        // Even if no disk is inserted track0 sensor works (a1000 bootrom)
        if (s_.cyl == 0)
            flags &= ~DSKF_TRACK0;

        // When the motor is off the RDY bit is used for drive identification.
        // The 32-bit ID of a normal drive is all 1's, which means /RDY should
        // always be reset (active low). If proper drive ID is to be supported
        // we need to shift out one bit from a 32-bit register.
        // See http://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node01AB.html
        if (!s_.motor || !s_.motor_cnt)
            flags &= ~DSKF_RDY;
        if (DEBUG_DISK) {
            *debug_stream << name_ << " State";
            if (!(flags & DSKF_RDY))
                *debug_stream << " /RDY";
            if (!(flags & DSKF_TRACK0))
                *debug_stream << " /TRACK0";
            if (!(flags & DSKF_PROT))
                *debug_stream << " /PROT";
            if (!(flags & DSKF_CHANGE))
                *debug_stream << " /CHANGE";
            *debug_stream << " motor_cnt=" << s_.motor_cnt << "\n";
        }
        return flags;
    }
    
    void set_motor(bool enabled)
    {
        if (DEBUG_DISK && s_.motor != enabled)
            *debug_stream << name_ << " Motor turning " << (enabled ? "on" : "off") << " motor_cnt=" << s_.motor_cnt << "\n";
        if (s_.motor != enabled) {
            // If the motor is already spinning up/down, just reuse the count. Not accurate but oh well
            if (!s_.motor_cnt)
                s_.motor_cnt = MOTOR_ON_CNT; // Just use same speed for starting/stopping
        }
        s_.motor = enabled;
    }

    void set_side_dir(bool side, bool dir)
    {
        if (DEBUG_DISK && (side != s_.side || s_.seek_dir != dir))
            *debug_stream << name_ << " Changing side/stp direction cyl = " << (int)s_.cyl << " new side = " << (side ? "lower" : "upper") << " new direction = " << (dir ? "out (towards 0)" : "in (towards 79)") << "\n";
        s_.side = side;
        s_.seek_dir = dir;
    }

    void dir_step()
    {
        if (DEBUG_DISK)
            *debug_stream << name_ << " Stepping cyl = " << (int)s_.cyl << " side = " << (s_.side ? "lower" : "upper") << " direction = " << (s_.seek_dir ? "out (towards 0)" : "in (towards 79)") << "\n";
        if (!s_.seek_dir && s_.cyl < NUM_CYLINDERS - 1)
            ++s_.cyl;
        else if (s_.seek_dir && s_.cyl)
            --s_.cyl;
        if (DEBUG_DISK)
            *debug_stream << name_ << " New cyl = " << (int)s_.cyl << " side = " << (s_.side ? "lower" : "upper") << " direction = " << (s_.seek_dir ? "out (towards 0)" : "in (towards 79)") << "\n";
    }

    void read_mfm_track(uint8_t* dest)
    {
        if (!s_.motor)
            throw std::runtime_error { name_ + " Reading while motor is not on" };

        if (data_.empty()) {
            *debug_stream << name_ << " Trying to read from non-present disk?!\n";
            return;
        }

        // Lower side=first
        const uint8_t tracknum = !s_.side + s_.cyl * 2;

        if (DEBUG_DISK)
            *debug_stream << name_ << " Reading track $" << hexfmt(tracknum) << " cyl = " << (int)s_.cyl << " side = " << (s_.side ? "lower" : "upper") << "\n";

        if (disk_activity_handler_)
            disk_activity_handler_(tracknum, false);

        for (uint8_t sec = 0; sec < NUMSECS; ++sec) {
            const uint8_t* raw_data = &data_[(tracknum * NUMSECS + sec) * TD_SECTOR];
            put_u32(&dest[0], fill); // Preamble
            put_u16(&dest[4], MFM_SYNC);
            put_u16(&dest[6], MFM_SYNC);
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
            put_split_long_fill(&dest[56], checksum(&dest[64], (MFM_SECTOR_SIZE_WORDS*2 - 64) / 4));
            dest += MFM_SECTOR_SIZE_WORDS*2;
        }
        // gap
        std::memset(dest, 0xaa, MFM_GAP_SIZE_WORDS * 2);
    }

    void write_mfm_track(const uint8_t* src)
    {
        if (!s_.motor)
            throw std::runtime_error { name_ + " Reading while motor is not on" };

        if (data_.empty())
            throw std::runtime_error { name_ + " Writing with no disk in drive" };

        #if 0
        uint32_t wpos = 0;
        while (wpos < MFM_TRACK_SIZE_WORDS && get_u16(&src[2 * wpos]) != MFM_SYNC)
            ++wpos;
        if (wpos + NUMSECS * MFM_SECTOR_SIZE_WORDS > MFM_TRACK_SIZE_WORDS)
            throw std::runtime_error { name_ + " Trying to write invalid MFM data" };
        #endif

        // For ease, shuffle the data so it starts with a sync word (if present)
        std::vector<uint8_t> data(2 * MFM_TRACK_SIZE_WORDS);
        for (uint32_t wpos = 0;; ++wpos) {
            if (wpos >= MFM_TRACK_SIZE_WORDS)
                throw std::runtime_error { name_ + " no sync word found in written MFM data" };
            if (get_u16(&src[2 * wpos]) != MFM_SYNC)
                continue;
            const auto byte_ofs = wpos * 2; 
            const auto first_part = 2 * MFM_TRACK_SIZE_WORDS - byte_ofs;
            memcpy(&data[0], &src[byte_ofs], first_part);
            memcpy(&data[first_part], &src[0], byte_ofs);
            break;
        }

        const uint8_t tracknum = !s_.side + s_.cyl * 2;

        if (disk_activity_handler_)
            disk_activity_handler_(tracknum, true);

        if (DEBUG_DISK)
            *debug_stream << name_ << " writing track $" << hexfmt(tracknum) << "\n";

        uint16_t sector_mask = 0;
        for (int seccnt = 0; seccnt < NUMSECS; ++seccnt) {
            const auto ofs = seccnt * MFM_SECTOR_SIZE_WORDS * 2;
            if (get_u16(&data[ofs]) != MFM_SYNC || get_u16(&data[ofs + 2]) != MFM_SYNC)
                throw std::runtime_error { name_ + " invalid MFM data" };

            const auto info = decode_long(&data[ofs + 4]);
            const auto sec = (info >> 8) & 0xff;
            if (info >> 24 != 0xff || ((info >> 16) & 0xff) != tracknum || sec > NUMSECS || (sec + (info & 0xff)) != NUMSECS)
                throw std::runtime_error { name_ + " invalid MFM data (info long=$" + hexstring(info) + ")" };

            if (sector_mask & (1 << sec))
                throw std::runtime_error { name_ + " sector " + std::to_string(sec) + " found twice in MFM data" };

            sector_mask |= 1 << sec;

            // TODO: Verify checksums..
            uint8_t* raw_data = &data_[(tracknum * NUMSECS + sec) * TD_SECTOR];
            for (int i = 0; i < 512; ++i) {
                const uint8_t odd  = data[ofs + 60 + i] & 0x55;
                const uint8_t even = data[ofs + 60 + 512 + i] & 0x55;
                raw_data[i] = odd << 1 | even;
            }
        }

        dirty_ = true;
    }

    void set_disk_activity_handler(const disk_activity_handler& handler)
    {
        assert(!disk_activity_handler_);
        disk_activity_handler_ = handler;
    }

    void show_debug_state(std::ostream& os)
    {
        os << "motor " << (s_.motor ? "on" : "off") << " cylinder " << (int)s_.cyl << " side " << (int)s_.side << " (" << (s_.side ? "upper" : "lower") << ")" << " dir " << (int)s_.seek_dir << " (towards " << (s_.seek_dir ? "outside(0)" : "center(79)") << ")\n";
    }

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, ("Drive " + name_).c_str(), 1 };
        sf.handle_blob(&s_, sizeof(s_));
    }

    bool ofs_bootable_disk_inserted() const
    {
        if (data_.size() < 1024 || data_[0] != 'D' || data_[1] != 'O' || data_[2] != 'S' || data_[3] != 0)
            return false;
        uint32_t csum = 0;
        for (uint32_t i = 0; i < 1024; i += 4) {
            const auto before = csum;
            csum += get_u32(&data_[i]);
            if (csum < before) // carry?
                ++csum;
        }
        return csum == 0xffffffff;
    }

private:
    std::string name_;
    std::vector<uint8_t> data_;
    bool dirty_ = false;
    struct state {
        bool motor         = false;
        bool side          = false;          // false = upper head, true = lower head
        bool seek_dir      = false;          // false = towards center, true = towards outside (0 is the outermost cylinder)
        uint8_t cyl        = 0;
        uint32_t motor_cnt = 0;              // Countdown until motor is on/off
        uint32_t index_cnt = DISK_INDEX_CNT; // Countdown to next DSKINDEX event (once per revolution)
    } s_;
    disk_activity_handler disk_activity_handler_;

    void flush()
    {
        if (data_.empty() || !dirty_)
            return;
        dirty_ = false;

        auto filename = name_;
        assert(filename.size() == 4 && filename.back() == ':');
        filename.erase(3);
        filename += "_modified.adf";
        if (debug_stream)
            *debug_stream << name_ << " has been written. Saving as " << filename << "\n";
        std::ofstream out { filename, std::ofstream::binary };
        if (out && out.is_open() && out.write(reinterpret_cast<const char*>(&data_[0]), data_.size()))
            return;
        if (debug_stream)
            *debug_stream << name_ << " error writing " << filename << "\n";
    }
};

disk_drive::disk_drive(const std::string& name)
    : impl_ { new impl{ name } }
{
}

disk_drive::~disk_drive() = default;

const std::string& disk_drive::name() const
{
    return impl_->name();
}

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

void disk_drive::read_mfm_track(uint8_t* dest)
{
    impl_->read_mfm_track(dest);
}

void disk_drive::write_mfm_track(const uint8_t* src)
{
    impl_->write_mfm_track(src);
}

void disk_drive::set_disk_activity_handler(const disk_activity_handler& handler)
{
    impl_->set_disk_activity_handler(handler);
}

void disk_drive::show_debug_state(std::ostream& os)
{
    impl_->show_debug_state(os);
}

void disk_drive::handle_state(state_file& sf)
{
    impl_->handle_state(sf);
}

bool disk_drive::step()
{
    return impl_->step();
}

bool disk_drive::ofs_bootable_disk_inserted() const
{
    return impl_->ofs_bootable_disk_inserted();
}
