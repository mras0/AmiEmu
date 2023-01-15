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
#include "disk_file.h"

namespace {

// EClock is color clock frequency/5 = 708.240KHz
constexpr uint32_t ECLOCK_FREQ    = 708240;
constexpr uint32_t DISK_INDEX_CNT = ECLOCK_FREQ / 5;  // 300RPM (=5Hz)
constexpr uint32_t MOTOR_ON_CNT   = ECLOCK_FREQ / 20; // 50ms

}


class disk_drive::impl {
public:
    explicit impl(const std::string& name)
        : name_ { name }
    {
    }

    ~impl()
    {
    }

    const std::string& name() const
    {
        return name_;
    }

    void insert_disk(std::unique_ptr<disk_file>&& disk)
    {
        data_ = std::move(disk);
        if (DEBUG_DISK)
            *debug_stream << name_ << " Inserting disk " << (data_ ? data_->name() : "<empty file>") << "\n";
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
        if (!data_) {
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
        if (!s_.seek_dir && data_ && s_.cyl < data_->num_cylinders() - 1)
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

        if (!data_) {
            *debug_stream << name_ << " Trying to read from non-present disk?!\n";
            return;
        }

        // Lower side=first
        const uint8_t tracknum = !s_.side + s_.cyl * 2;

        if (DEBUG_DISK)
            *debug_stream << name_ << " Reading track $" << hexfmt(tracknum) << " cyl = " << (int)s_.cyl << " side = " << (s_.side ? "lower" : "upper") << "\n";

        if (disk_activity_handler_)
            disk_activity_handler_(tracknum, false);

        data_->read_mfm_track(tracknum, dest);
    }

    void write_mfm_track(const uint8_t* src)
    {
        if (!s_.motor)
            throw std::runtime_error { name_ + " Reading while motor is not on" };

        if (!data_)
            throw std::runtime_error { name_ + " Writing with no disk in drive" };

        const uint8_t tracknum = !s_.side + s_.cyl * 2;

        if (disk_activity_handler_)
            disk_activity_handler_(tracknum, true);

        if (DEBUG_DISK)
            *debug_stream << name_ << " writing track $" << hexfmt(tracknum) << "\n";

        data_->write_mfm_track(tracknum, src);
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
        return data_->ofs_bootable();
    }

private:
    std::string name_;
    std::unique_ptr<disk_file> data_;
    struct state {
        bool motor         = false;
        bool side          = false;          // false = upper head, true = lower head
        bool seek_dir      = false;          // false = towards center, true = towards outside (0 is the outermost cylinder)
        uint8_t cyl        = 0;
        uint32_t motor_cnt = 0;              // Countdown until motor is on/off
        uint32_t index_cnt = DISK_INDEX_CNT; // Countdown to next DSKINDEX event (once per revolution)
    } s_;
    disk_activity_handler disk_activity_handler_;
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

void disk_drive::insert_disk(std::unique_ptr<disk_file>&& disk)
{
    impl_->insert_disk(std::move(disk));
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
