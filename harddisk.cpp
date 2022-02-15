#include "harddisk.h"
#include "memory.h"
#include "ioutil.h"
#include "autoconf.h"
#include "state_file.h"
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cstring>

namespace {

#include "exprom.h"

static constexpr uint32_t IDNAME_RIGIDDISK = 0x5244534B; // 'RDSK'
static constexpr uint32_t IDNAME_PARTITION = 0x50415254; // 'PART'

bool check_structure(const uint8_t* sector, const uint32_t id)
{
    if (get_u32(&sector[0]) != id) // ID
        return false;

    if (get_u32(&sector[4]) != 256 / 4) // Size of structure in longs
        return false;

    uint32_t csum = 0;
    for (uint32_t offset = 0; offset < 256; offset += 4)
        csum += get_u32(sector + offset);
    if (csum)
        return false;

    return true;
}

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

        if (!total_size_ || total_size_ % sector_size_bytes || total_size_ < 1024*1024)
            throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size_) };

        const uint8_t* sector = disk_read(0, sector_size_bytes);
        if (check_structure(sector, IDNAME_RIGIDDISK)) {

            if (get_u32(&sector[16]) != 512) // Block size
                throw std::runtime_error { hdfilename + " has unsupported/invalid blocksize" };

            const uint32_t num_cylinders = get_u32(&sector[64]);
            const uint32_t sectors_per_track = get_u32(&sector[68]);
            const uint32_t num_heads = get_u32(&sector[72]);

            std::cout << "C/H/S: " << num_cylinders << "/" << num_heads << "/" << sectors_per_track << "\n";

            constexpr uint32_t end_of_list = 0xffffffff;
            uint32_t part_list = get_u32(&sector[28]);
            //uint32_t fshdr_list = get_u32(&sector[32]); (Must be read here)
            // TODO: Support loadable file systems

            while (part_list != end_of_list) {
                if (partitions_.size() >= 10)
                    throw std::runtime_error { hdfilename + " has too many partitions" };

                sector = disk_read(part_list * sector_size_bytes, sector_size_bytes);
                if (!check_structure(sector, IDNAME_PARTITION))
                    throw std::runtime_error { hdfilename + " has an invalid partition list" };

                partition_info pi {};
                const uint8_t name_len = sector[36];
                if (name_len >= sizeof(pi.name) - 1)
                    throw std::runtime_error { hdfilename + " has invalid partition name length" };
                std::memcpy(pi.name, &sector[37], name_len);
                pi.name[name_len] = 0;
                
                pi.flags = get_u32(&sector[32]);
                pi.block_size_bytes = 4 * get_u32(&sector[132]);
                pi.num_heads = get_u32(&sector[140]);
                pi.sectors_per_track = get_u32(&sector[148]);
                pi.reserved_blocks = get_u32(&sector[152]);
                pi.interleave = get_u32(&sector[160]);
                pi.lower_cylinder = get_u32(&sector[164]);
                pi.upper_cylinder = get_u32(&sector[168]);
                pi.num_buffers = get_u32(&sector[172]);
                pi.mem_buffer_type = get_u32(&sector[176]);
                pi.dos_type = get_u32(&sector[192]);
                pi.boot_flags = get_u32(&sector[20]); // bit0: bootable, bit1: no automount

                partitions_.push_back(pi);

                part_list = get_u32(&sector[16]); // Next partition
            }

            return;
        }

        // HDF: Mount as a single partition

        const uint32_t num_heads = 16;
        const uint32_t sectors_per_track = 63;

        const auto cyl_size = num_heads * sectors_per_track * sector_size_bytes;
        if (!total_size_ || total_size_ % cyl_size || total_size_ > 504 * 1024 * 1024 || total_size_ < 8 * 1024 * 1024) // Limit to 504MB for now (probably need more heads)
            throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size_) };
        const uint32_t num_cylinders = static_cast<uint32_t>(total_size_ / cyl_size);
        partitions_.push_back(partition_info {
            "DH0",
            0,
            sector_size_bytes,
            num_heads,
            sectors_per_track,
            2,
            0,
            0,
            num_cylinders - 1,
            1, // one buffer
            0,
            0x444f5300, // 'DOS\0'
            1, // Bootable
        });
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

    struct partition_info {
        char name[32];
        uint32_t flags; // Flags for OpenDevice
        uint32_t block_size_bytes;
        uint32_t num_heads;
        uint32_t sectors_per_track;
        uint32_t reserved_blocks;
        uint32_t interleave;
        uint32_t lower_cylinder;
        uint32_t upper_cylinder; // Note: Inclusive
        uint32_t num_buffers;
        uint32_t mem_buffer_type;
        uint32_t dos_type;
        uint32_t boot_flags;
    };

    memory_handler& mem_;
    bool& cpu_active_;
    std::fstream hdfile_;
    uint64_t total_size_;
    uint32_t ptr_hold_ = 0;
    std::vector<uint8_t> buffer_;
    std::vector<partition_info> partitions_; 

    void reset() override
    {
        ptr_hold_ = 0;
    }

    void handle_state(state_file& sf) override
    {
        const state_file::scope scope { sf, "Harddisk", 1 };
        sf.handle(ptr_hold_);
    }

    const uint8_t* disk_read(uint64_t offset, uint32_t len)
    {
        assert(len && len % sector_size_bytes == 0 && offset % sector_size_bytes == 0);
        buffer_.resize(len);
        hdfile_.seekg(offset);
        hdfile_.read(reinterpret_cast<char*>(&buffer_[0]), len);
        if (!hdfile_)
            throw std::runtime_error { "Error reading from harddrive" };
        return &buffer_[0];
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
        const uint32_t special_offset = static_cast<uint32_t>(config.rom_vector_offset + sizeof(exprom));

        if (offset == special_offset) {
            return static_cast<uint16_t>(partitions_.size());
        } else if (offset == special_offset + 2) {

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
            else if (val == 0xfedf)
                handle_init();
            else
                throw std::runtime_error { "Invalid HD command: $" + hexstring(val) };

            cpu_active_ = true;
            ptr_hold_ = 0;
        } else {
            std::cerr << "harddisk: Invalid write to offset $" << hexfmt(offset) << " val $" << hexfmt(val) << "\n";
        }
    }

    void handle_init()
    {
        // keep in check with exprom.asm
        constexpr uint16_t devn_dosName    = 0x00;  // APTR  Pointer to DOS file handler name
        constexpr uint16_t devn_unit       = 0x08;  // ULONG Unit number
        constexpr uint16_t devn_flags      = 0x0C;  // ULONG OpenDevice flags
        //constexpr uint16_t devn_tableSize  = 0x10;  // ULONG Environment size
        constexpr uint16_t devn_sizeBlock  = 0x14;  // ULONG # longwords in a block
        constexpr uint16_t devn_secOrg     = 0x18;  // ULONG sector origin -- unused
        constexpr uint16_t devn_numHeads   = 0x1C;  // ULONG number of surfaces
        constexpr uint16_t devn_secsPerBlk = 0x20;  // ULONG secs per logical block
        constexpr uint16_t devn_blkTrack   = 0x24;  // ULONG secs per track
        constexpr uint16_t devn_resBlks    = 0x28;  // ULONG reserved blocks -- MUST be at least 1!
        //constexpr uint16_t devn_prefac     = 0x2C;  // ULONG unused
        constexpr uint16_t devn_interleave = 0x30;  // ULONG interleave
        constexpr uint16_t devn_lowCyl     = 0x34;  // ULONG lower cylinder
        constexpr uint16_t devn_upperCyl   = 0x38;  // ULONG upper cylinder
        constexpr uint16_t devn_numBuffers = 0x3C;  // ULONG number of buffers
        constexpr uint16_t devn_memBufType = 0x40;  // ULONG Type of memory for AmigaDOS buffers
        constexpr uint16_t devn_dName      = 0x44;	// char[4] DOS file handler name
        constexpr uint16_t devn_bootflags  = 0x48;  // boot flags (not part of DOS packet)

        // unit, dosName, execName and tableSize is filled by expansion ROM

        const uint32_t unit = mem_.read_u32(ptr_hold_ + devn_unit);

        std::cout << "HD: Initializing partition " << unit << "\n";

        if (unit >= partitions_.size())
            throw std::runtime_error { "Invalid HD partition used for initialization" };

        const auto& part = partitions_[unit];

        const uint32_t name_ptr = mem_.read_u32(ptr_hold_ + devn_dosName);
        for (uint32_t i = 0; i < sizeof(part.name); ++i)
            mem_.write_u8(name_ptr + i, part.name[i]);

        mem_.write_u32(ptr_hold_ + devn_flags, part.flags);
        mem_.write_u32(ptr_hold_ + devn_sizeBlock, part.block_size_bytes / 4);
        mem_.write_u32(ptr_hold_ + devn_secOrg, 0);
        mem_.write_u32(ptr_hold_ + devn_numHeads, part.num_heads);
        mem_.write_u32(ptr_hold_ + devn_secsPerBlk, 1);
        mem_.write_u32(ptr_hold_ + devn_blkTrack, part.sectors_per_track);
        mem_.write_u32(ptr_hold_ + devn_interleave, part.interleave);
        mem_.write_u32(ptr_hold_ + devn_resBlks, part.reserved_blocks);
        mem_.write_u32(ptr_hold_ + devn_lowCyl, part.lower_cylinder);
        mem_.write_u32(ptr_hold_ + devn_upperCyl, part.upper_cylinder);
        mem_.write_u32(ptr_hold_ + devn_numBuffers, part.num_buffers);
        mem_.write_u32(ptr_hold_ + devn_memBufType, part.mem_buffer_type);
        mem_.write_u32(ptr_hold_ + devn_dName, part.dos_type);
        mem_.write_u32(ptr_hold_ + devn_bootflags, part.boot_flags);
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
                if (cmd == CMD_READ) {
                    const uint8_t* diskdata = disk_read(ofs, len);
                    for (uint32_t i = 0; i < len; i += 4)
                        mem_.write_u32(data + i, get_u32(&diskdata[i]));
                } else {
                    buffer_.resize(len);
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
