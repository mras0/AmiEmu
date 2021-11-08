#include "harddisk.h"
#include "memory.h"
#include "ioutil.h"
#include "autoconf.h"
#include "state_file.h"
#include <stdexcept>
#include <fstream>
#include <iostream>

namespace {

#include "exprom.h"

}

class harddisk::impl final : public memory_area_handler, public autoconf_device {
public:
    explicit impl(memory_handler& mem, bool& cpu_active, const std::string& hdfilename)
        : autoconf_device { mem, *this, config }
        , mem_ { mem }
        , cpu_active_ { cpu_active }
        , hdfile_ { hdfilename, std::ios::binary | std::ios::in | std::ios::out }
    {
        if (!hdfile_.is_open())
            throw std::runtime_error { "Error opening " + hdfilename };
        hdfile_.seekg(0, std::fstream::end);
        total_size_ = hdfile_.tellg();
        const auto cyl_size = num_heads_ * sectors_per_track_ * sector_size_bytes;
        if (!total_size_ || total_size_ % cyl_size || total_size_ > 504 * 1024 * 1024 || total_size_ < 8 * 1024 * 1024) // Limit to 504MB for now (probably need more heads)
            throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size_) };
        num_cylinders_ = static_cast<uint32_t>(total_size_ / cyl_size);
    }

private:
    static constexpr board_config config {
        .type = ERTF_DIAGVALID,
        .size = 64 << 10,
        .product_number = 0x88,
        .hw_manufacturer = 1337,
        .serial_no = 1,
        .rom_vector_offset = EXPROM_BASE,
    };
    static constexpr uint32_t sector_size_bytes = 512;
    memory_handler& mem_;
    bool& cpu_active_;
    std::fstream hdfile_;
    uint32_t num_heads_ = 16;
    uint32_t sectors_per_track_ = 63;
    uint32_t num_cylinders_;
    uint64_t total_size_;
    uint32_t ptr_hold_ = 0;
    std::vector<uint8_t> buffer_;

    void reset() override
    {
        ptr_hold_ = 0;
    }

    void handle_state(state_file& sf) override
    {
        const state_file::scope scope { sf, "Harddisk", 1 };
        sf.handle(ptr_hold_);
    }


    uint8_t read_u8(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset < config.rom_vector_offset + sizeof(exprom)) {
            return exprom[offset - config.rom_vector_offset];
        }

        std::cerr << "harddisk: Read U8 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    uint16_t read_u16(uint32_t, uint32_t offset) override
    {
        if (offset >= config.rom_vector_offset && offset + 1 < config.rom_vector_offset + sizeof(exprom)) {
            offset -= config.rom_vector_offset;
            return static_cast<uint16_t>(exprom[offset] << 8 | exprom[offset + 1]);
        }
        std::cerr << "harddisk: Read U16 offset $" << hexfmt(offset) << "\n";
        return 0;
    }

    void write_u8(uint32_t, uint32_t offset, uint8_t val) override
    {
        std::cerr << "harddisk: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
    }

    void write_u16(uint32_t, uint32_t offset, uint16_t val) override
    {
        const uint32_t special_offset = static_cast<uint32_t>(config.rom_vector_offset + sizeof(exprom));
        if (offset == special_offset) {
            ptr_hold_ = val << 16 | (ptr_hold_ & 0xffff);
            return;
        } else if (offset == special_offset + 2) {
            ptr_hold_ = (ptr_hold_ & 0xffff0000) | val;
        } else if (offset == special_offset + 4) {
            if ((val != 0xfede && val != 0xfedf) || !ptr_hold_ || (ptr_hold_ & 1) || !cpu_active_) {
                std::cerr << "harddisk: Invalid conditions! written val = $" << hexfmt(val) << " ptr_hold = $" << hexfmt(ptr_hold_) << " cpu_active_ = " << cpu_active_ << "\n";
                return;
            }
            cpu_active_ = false;

            if (val == 0xfede)
                handle_disk_cmd();
            else
                handle_init();

            cpu_active_ = true;
            ptr_hold_ = 0;
        } else {
            std::cerr << "harddisk: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
        }
    }

    void handle_init()
    {
        // keep in check with exprom.asm
        constexpr uint16_t devn_sizeBlock = 0x14; // # longwords in a block
        constexpr uint16_t devn_numHeads  = 0x1C; // number of surfaces
        constexpr uint16_t devn_blkTrack  = 0x24; // secs per track
        constexpr uint16_t devn_upperCyl  = 0x38; // upper cylinder

        mem_.write_u32(ptr_hold_ + devn_sizeBlock, sector_size_bytes / 4);
        mem_.write_u32(ptr_hold_ + devn_numHeads, num_heads_);
        mem_.write_u32(ptr_hold_ + devn_blkTrack, sectors_per_track_);
        mem_.write_u32(ptr_hold_ + devn_upperCyl, num_cylinders_- 1);
    }

    void handle_disk_cmd()
    {
        constexpr int8_t IOERR_NOCMD      = -3;
        //constexpr int8_t IOERR_BADLENGTH  = -4;
        constexpr int8_t IOERR_BADADDRESS = -5;

        // Standard commands
        //constexpr uint16_t CMD_INVALID     = 0;
        constexpr uint16_t CMD_RESET       = 1;
        constexpr uint16_t CMD_READ        = 2;
        constexpr uint16_t CMD_WRITE       = 3;
        constexpr uint16_t CMD_UPDATE      = 4;
        constexpr uint16_t CMD_CLEAR       = 5;
        constexpr uint16_t CMD_STOP        = 6;
        constexpr uint16_t CMD_START       = 7;
        constexpr uint16_t CMD_FLUSH       = 8;
        constexpr uint16_t CMD_NONSTD      = 9;
        constexpr uint16_t TD_MOTOR        = CMD_NONSTD + 0;  // 09
        constexpr uint16_t TD_SEEK         = CMD_NONSTD + 1;  // 0A 
        constexpr uint16_t TD_FORMAT       = CMD_NONSTD + 2;  // 0B 
        constexpr uint16_t TD_REMOVE       = CMD_NONSTD + 3;  // 0C 
        constexpr uint16_t TD_CHANGENUM    = CMD_NONSTD + 4;  // 0D 
        constexpr uint16_t TD_CHANGESTATE  = CMD_NONSTD + 5;  // 0E 
        constexpr uint16_t TD_PROTSTATUS   = CMD_NONSTD + 6;  // 0F 
        //constexpr uint16_t TD_RAWREAD      = CMD_NONSTD + 7;  // 10
        //constexpr uint16_t TD_RAWWRITE     = CMD_NONSTD + 8;  // 11
        //constexpr uint16_t TD_GETDRIVETYPE = CMD_NONSTD + 9;  // 12
        //constexpr uint16_t TD_GETNUMTRACKS = CMD_NONSTD + 10; // 13
        constexpr uint16_t TD_ADDCHANGEINT = CMD_NONSTD + 11; // 14
        constexpr uint16_t TD_REMCHANGEINT = CMD_NONSTD + 12; // 15

        constexpr uint32_t IO_COMMAND = 0x1C;
        constexpr uint32_t IO_ERROR   = 0x1F;
        constexpr uint32_t IO_ACTUAL  = 0x20;
        constexpr uint32_t IO_LENGTH  = 0x24;
        constexpr uint32_t IO_DATA    = 0x28;
        constexpr uint32_t IO_OFFSET  = 0x2C;
        const auto cmd  = mem_.read_u16(ptr_hold_ + IO_COMMAND);
        const auto len  = mem_.read_u32(ptr_hold_ + IO_LENGTH);
        const auto data = mem_.read_u32(ptr_hold_ + IO_DATA);
        const auto ofs  = mem_.read_u32(ptr_hold_ + IO_OFFSET);

        //std::cerr << "harddisk: Command=$" << hexfmt(cmd) << " Length=$" << hexfmt(len) << " Data=$" << hexfmt(data) << " Offset=$" << hexfmt(ofs) << "\n";
        switch (cmd) {
        case CMD_READ:
        case CMD_WRITE:
        case TD_FORMAT:
            if (ofs > total_size_ || len > total_size_ || ofs > total_size_ - len || ofs % sector_size_bytes) {
                std::cerr << "Test board: Invalid offset $" << hexfmt(ofs) << " length=$" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
            } else if (len % sector_size_bytes) {
                std::cerr << "Test board: Invalid length $" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
            } else {
                buffer_.resize(len);
                if (cmd == CMD_READ) {
                    hdfile_.seekg(ofs);
                    hdfile_.read(reinterpret_cast<char*>(&buffer_[0]), len);
                    if (!hdfile_)
                        throw std::runtime_error { "Error reading from harddrive" };
                    for (uint32_t i = 0; i < len; i += 4)
                        mem_.write_u32(data + i, get_u32(&buffer_[i]));
                } else {
                    for (uint32_t i = 0; i < len; i += 4)
                        put_u32(&buffer_[i], mem_.read_u32(data + i));
                    hdfile_.seekp(ofs);
                    hdfile_.write(reinterpret_cast<const char*>(&buffer_[0]), len);
                    if (!hdfile_)
                        throw std::runtime_error { "Error writing to harddrive" };
                }
                mem_.write_u32(ptr_hold_ + IO_ACTUAL, len);
                mem_.write_u8(ptr_hold_ + IO_ERROR, 0);
            }
            break;
        case CMD_RESET:
        case CMD_UPDATE:
        case CMD_CLEAR:
        case CMD_STOP:
        case CMD_START:
        case CMD_FLUSH:
        case TD_MOTOR:
        case TD_SEEK:
        case TD_REMOVE:
        case TD_CHANGENUM:
        case TD_CHANGESTATE:
        case TD_PROTSTATUS:
        case TD_ADDCHANGEINT:
        case TD_REMCHANGEINT:
            mem_.write_u32(ptr_hold_ + IO_ACTUAL, 0);
            mem_.write_u8(ptr_hold_ + IO_ERROR, 0);
            break;
        default:
            std::cerr << "Test board: Unsupported command $" << hexfmt(cmd) << "\n";
            mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_NOCMD));
        }
    }
};

harddisk::harddisk(memory_handler& mem, bool& cpu_active, const std::string& hdfilename)
    : impl_{ new impl(mem, cpu_active, hdfilename) }
{
}

harddisk::~harddisk() = default;

autoconf_device& harddisk::autoconf_dev()
{
    return *impl_;
}
