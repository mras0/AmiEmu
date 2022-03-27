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

constexpr uint32_t IDNAME_RIGIDDISK     = 0x5244534B; // 'RDSK'
constexpr uint32_t IDNAME_PARTITION     = 0x50415254; // 'PART'
constexpr uint32_t IDNAME_FILESYSHEADER = 0x46534844; // 'FSHD'
constexpr uint32_t IDNAME_LOADSEG       = 0x4C534547; // 'LSEG'

constexpr int8_t IOERR_NOCMD = -3;
//constexpr int8_t IOERR_BADLENGTH = -4;
constexpr int8_t IOERR_BADADDRESS = -5;

struct scsi_cmd {
    uint32_t scsi_Data; /* word aligned data for SCSI Data Phase */
    uint32_t scsi_Length; /* even length of Data area */
    uint32_t scsi_Actual; /* actual Data used */
    uint32_t scsi_Command; /* SCSI Command (same options as scsi_Data) */
    uint16_t scsi_CmdLength; /* length of Command */
    uint16_t scsi_CmdActual; /* actual Command used */
    uint8_t scsi_Flags; /* includes intended data direction */
    uint8_t scsi_Status; /* SCSI status of command */
    uint32_t scsi_SenseData; /* sense data: filled if SCSIF_[OLD]AUTOSENSE */
    uint16_t scsi_SenseLength; /* size of scsi_SenseData, also bytes to */
    uint16_t scsi_SenseActual; /* amount actually fetched (0 means no sense) */
};

bool check_structure(const uint8_t* sector, const uint32_t id, uint32_t size_bytes = 256)
{
    if (get_u32(&sector[0]) != id) // ID
        return false;

    if (get_u32(&sector[4]) != size_bytes / 4) // Size of structure in longs
        return false;

    uint32_t csum = 0;
    for (uint32_t offset = 0; offset < size_bytes; offset += 4)
        csum += get_u32(sector + offset);

    return csum == 0;
}

std::string dos_type_string(uint32_t dostype)
{
    std::string s;
    for (int i = 0; i < 4; ++i) {
        uint8_t ch = static_cast<uint8_t>(dostype >> (24 - 8 * i));
        if (ch >= ' ') {
            s += ch;
            continue;
        }
        s += "\\x";
        s += hexstring(ch);
    }
    return s;
}

constexpr uint32_t HUNK_HEADER = 1011;
constexpr uint32_t HUNK_CODE = 1001;
constexpr uint32_t HUNK_DATA = 1002;
constexpr uint32_t HUNK_BSS = 1003;
constexpr uint32_t HUNK_RELOC32 = 1004;
constexpr uint32_t HUNK_END = 1010;

constexpr uint32_t max_hunks = 3; // keep in check with expansion rom

struct hunk_reloc {
    uint32_t dst_hunk;
    std::vector<uint32_t> offsets;
};

struct hunk {
    uint32_t flags;
    uint32_t type;
    std::vector<uint8_t> data;
    std::vector<hunk_reloc> relocs;
};

std::vector<hunk> parse_hunk_file(const std::vector<uint8_t>& code)
{
    uint32_t pos = 0;

    auto read = [&]() {
        if (pos + 4 > code.size())
            throw std::runtime_error { "Invalid HUNK file" };
        const auto val = get_u32(&code[pos]);
        pos += 4;
        return val;
    };

    if (code.size() % 4 || code.size() < 32 || read() != HUNK_HEADER || read())
        throw std::runtime_error { "Invalid HUNK file" };

    const uint32_t table_size = read();

    if (table_size == 0 || table_size > max_hunks || read() != 0 || read() != table_size - 1)
        throw std::runtime_error { "Invalid HUNK file" };

    std::vector<hunk> hunks(table_size);

    for (uint32_t i = 0; i < table_size; ++i) {
        hunks[i].flags = read();
    }

    uint32_t idx = 0;
    while (pos < code.size()) {
        const auto hunk_type = read();
        switch (hunk_type) {
        case HUNK_BSS:
        case HUNK_CODE:
        case HUNK_DATA: {
            if (idx >= table_size || hunks[idx].type)
                throw std::runtime_error { "Invalid HUNK file" };
            const auto size = read() * 4;
            if (size > (hunks[idx].flags & 0x3FFFFFFF) * 4)
                throw std::runtime_error { "Invalid HUNK file" };
            if (hunk_type == HUNK_BSS)
                break;
            if (pos + size > code.size())
                throw std::runtime_error { "Invalid HUNK file" };
            hunks[idx].type = hunk_type;
            hunks[idx].data = std::vector<uint8_t>(&code[pos], &code[pos + size]);
            pos += size;
            break;
        }
        case HUNK_RELOC32: {
            if (hunks[idx].type != HUNK_CODE && hunks[idx].type != HUNK_DATA)
                throw std::runtime_error { "Invalid HUNK file" };
            for (;;) {
                hunk_reloc hr;
                const uint32_t cnt = read();
                if (cnt == 0)
                    break;
                hr.dst_hunk = read();
                if (hr.dst_hunk >= table_size)
                    throw std::runtime_error { "Invalid HUNK file" };
                hr.offsets.resize(cnt);
                for (uint32_t i = 0; i < cnt; ++i) {
                    hr.offsets[i] = read();
                    if ((hr.offsets[i] & 1) || hr.offsets[i] + 3 > (hunks[idx].flags & 0x3FFFFFFF) * 4)
                        throw std::runtime_error { "Invalid relocation in HUNK file" };
                }
                hunks[idx].relocs.push_back(std::move(hr));
            }
            break;
        }
        case HUNK_END:
            ++idx;
            break;
        default:
            throw std::runtime_error { "Unsupported hunk type: " + std::to_string(hunk_type) };
            
        }
    }

    if (idx != table_size)
        throw std::runtime_error { "Invalid HUNK file" };

    return hunks;
}

}

class harddisk::impl final : public memory_area_handler, public autoconf_device {
public:
    explicit impl(memory_handler& mem, bool& cpu_active, const std::vector<std::string>& hdfilenames)
        : autoconf_device { mem, *this, config }
        , mem_ { mem }
        , cpu_active_ { cpu_active }
    {
        if (hdfilenames.empty())
            throw std::runtime_error { "Harddisk initialized with no filenames" };
        if (hdfilenames.size() > 9)
            throw std::runtime_error { "Too many harddrive files" };

        init_disks(hdfilenames);
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

    struct hd_info {
        std::string filename;
        std::fstream f;
        uint64_t size;
        uint32_t cylinders;
        uint8_t heads;
        uint16_t sectors_per_track;
    };

    struct partition_info {
        hd_info& hd;
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
        uint32_t max_transfer;
        uint32_t mask;
        uint32_t boot_priority;
        uint32_t dos_type;
        uint32_t boot_flags;
    };

    struct fs_info {
        uint32_t dos_type;
        uint32_t version;
        uint32_t patch_flags;
        std::vector<hunk> hunks;
        uint32_t seglist_bptr;
    };

    memory_handler& mem_;
    bool& cpu_active_;
    std::vector<std::unique_ptr<hd_info>> hds_;
    uint32_t ptr_hold_ = 0;
    std::vector<uint8_t> buffer_;
    std::vector<partition_info> partitions_;
    std::vector<fs_info> filesystems_;

    void reset() override
    {
        ptr_hold_ = 0;
        // HACK to re-read RDB in case of format etc.
        std::vector<std::string> disk_filenames;
        for (const auto& hd : hds_)
            disk_filenames.push_back(hd->filename);
        hds_.clear();
        partitions_.clear();
        filesystems_.clear();
        init_disks(disk_filenames);
    }

    void init_disks(const std::vector<std::string>& hdfilenames)
    {
        for (const auto& hdfilename : hdfilenames) {
            std::fstream hdfile { hdfilename, std::ios::binary | std::ios::in | std::ios::out };

            if (!hdfile.is_open())
                throw std::runtime_error { "Error opening " + hdfilename };
            hdfile.seekg(0, std::fstream::end);
            const uint64_t total_size = hdfile.tellg();

            if (!total_size || total_size % sector_size_bytes || total_size < 100 * 1024)
                throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size) };

            hds_.push_back(std::unique_ptr<hd_info>(new hd_info{hdfilename, std::move(hdfile), total_size, uint32_t(0), uint8_t(0), uint16_t(0)}));
            auto& hd = *hds_.back();

            const uint8_t* sector = disk_read(hd, 0, sector_size_bytes);
            if (check_structure(sector, IDNAME_RIGIDDISK)) {

                if (get_u32(&sector[16]) != 512) // Block size
                    throw std::runtime_error { hdfilename + " has unsupported/invalid blocksize" };

                const uint32_t num_cylinders = get_u32(&sector[64]);
                const uint32_t sectors_per_track = get_u32(&sector[68]);
                const uint32_t num_heads = get_u32(&sector[72]);

                std::cout << "C/H/S = " << num_cylinders << "/" << num_heads << "/" << sectors_per_track << "\n";

                hd.cylinders = num_cylinders;
                hd.heads = static_cast<uint8_t>(num_heads);
                hd.sectors_per_track = static_cast<uint16_t>(sectors_per_track);

                constexpr uint32_t end_of_list = 0xffffffff;
                uint32_t part_list = get_u32(&sector[28]);
                uint32_t fshdr_list = get_u32(&sector[32]);

                // Read partition list

                for (uint32_t cnt = 0; part_list != end_of_list; ++cnt) {
                    if (cnt >= 10)
                        throw std::runtime_error { hdfilename + " has too many partitions" };

                    sector = disk_read(hd, part_list * sector_size_bytes, sector_size_bytes);
                    if (!check_structure(sector, IDNAME_PARTITION))
                        throw std::runtime_error { hdfilename + " has an invalid partition list" };

                    partition_info pi {hd};
                    const uint8_t name_len = sector[36];
                    if (name_len >= sizeof(pi.name) - 1)
                        throw std::runtime_error { hdfilename + " has invalid partition name length" };
                    std::memcpy(pi.name, &sector[37], name_len);
                    pi.name[name_len] = 0;

                    if (!partition_name_ok(pi.name))
                        throw std::runtime_error { "Multiple partitions named " + std::string { pi.name } };

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
                    pi.max_transfer = get_u32(&sector[180]);
                    pi.mask = get_u32(&sector[184]);
                    pi.boot_priority = get_u32(&sector[188]);
                    pi.dos_type = get_u32(&sector[192]);
                    pi.boot_flags = get_u32(&sector[20]); // bit0: bootable, bit1: no automount

                    part_list = get_u32(&sector[16]); // Next partition

                    if (pi.boot_flags & 2) {
                        std::cout << "[HD] Skipping partition \"" << pi.name << "\" DOS type: \"" << dos_type_string(pi.dos_type) << "\" - No auto mount\n";
                        continue;
                    }

                    partitions_.push_back(pi);

                    std::cout << "[HD] Found partition \"" << pi.name << "\" DOS type: \"" << dos_type_string(pi.dos_type) << "\" " << (pi.boot_flags & 1 ? "" : "not") << "bootable\n";
                }

                for (uint32_t cnt = 0; fshdr_list != end_of_list; ++cnt) {
                    if (cnt > 10)
                        throw std::runtime_error { hdfilename + " has an invalid file system header list (too many)" };

                    sector = disk_read(hd, fshdr_list * sector_size_bytes, sector_size_bytes);
                    if (!check_structure(sector, IDNAME_FILESYSHEADER))
                        throw std::runtime_error { hdfilename + " has an invalid file system header list" };

                    fshdr_list = get_u32(&sector[16]); // Next

                    const uint32_t dos_type = get_u32(&sector[32]);
                    const uint32_t version = get_u32(&sector[36]);
                    const uint32_t patch_flags = get_u32(&sector[40]);
                    uint32_t seg_list = get_u32(&sector[72]);

                    if (seg_list == end_of_list)
                        continue;

                    bool needed = true;
                    // Do we already have the same filesystem in the same or newer versoin?
                    for (const auto& fs : filesystems_) {
                        if (fs.dos_type == dos_type && fs.version >= version) {
                            needed = false;
                            break;
                        }
                    }
                    if (!needed)
                        continue;
                    needed = false;

                    // Check if any partitions use this filesystem
                    for (const auto& pi : partitions_) {
                        if (pi.dos_type == dos_type) {
                            needed = true;
                            break;
                        }
                    }
                    if (!needed)
                        continue;

                    if (patch_flags != 0x180) {
                        std::cerr << "[HD] Warning: Don't know how to handle patch flags $" << hexfmt(patch_flags) << "\n";
                    }

                    std::cout << "[HD] Found filesystem for : \"" << dos_type_string(dos_type) << "\" Version " << (version >> 16) << "." << (version & 0xffff) << " seg_list: " << (int)seg_list << "\n";

                    std::vector<uint8_t> fs_code;
                    while (seg_list != end_of_list) {
                        // No file system is ever going to be this large (right?)
                        if (fs_code.size() > 1024 * 1024)
                            throw std::runtime_error { hdfilename + " has an invalid segment list" };

                        sector = disk_read(hd, seg_list * sector_size_bytes, sector_size_bytes);
                        const auto size_bytes = get_u32(&sector[4]) * 4;
                        if (size_bytes < 24 || size_bytes > sector_size_bytes)
                            throw std::runtime_error { hdfilename + " has an invalid segment list" };
                        if (!check_structure(sector, IDNAME_LOADSEG, size_bytes))
                            throw std::runtime_error { hdfilename + " has an invalid segment list" };

                        fs_code.insert(fs_code.end(), &sector[20], &sector[size_bytes]);

                        seg_list = get_u32(&sector[16]); // Next
                    }

                    filesystems_.push_back(fs_info {
                        dos_type,
                        version,
                        patch_flags,
                        parse_hunk_file(fs_code),
                        0 });
                }

                continue;
            }

            // HDF: Mount as a single partition

            const uint32_t num_heads = 1;
            const uint32_t sectors_per_track = 32;

            const auto cyl_size = num_heads * sectors_per_track * sector_size_bytes;
            if (!total_size || total_size % cyl_size || total_size > 504 * 1024 * 1024 || total_size < 8 * 1024 * 1024) // Limit to 504MB for now (probably need more heads)
                throw std::runtime_error { "Invalid size for " + hdfilename + " " + std::to_string(total_size) };
            const uint32_t num_cylinders = static_cast<uint32_t>(total_size / cyl_size);

            hd.cylinders = num_cylinders;
            hd.heads = static_cast<uint8_t>(num_heads);
            hd.sectors_per_track = static_cast<uint16_t>(sectors_per_track);

            partitions_.push_back(partition_info {
                hd,
                "",
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
                0x7ffe, // Max transfer
                0xfffffffe, // Mask
                0, // Boot priority
                0x444f5300, // 'DOS\0'
                0 << 24 | 1, // Bootable
            });
            for (uint32_t num = 0;; ++num) {
                std::string name = "DH" + std::to_string(num);
                if (partition_name_ok(name)) {
                    strcpy(partitions_.back().name, name.c_str());
                    break;
                }
            }
        }
    }

    void handle_state(state_file& sf) override
    {
        const state_file::scope scope { sf, "Harddisk", 1 };
        sf.handle(ptr_hold_);
    }

    bool partition_name_ok(const std::string& name)
    {
        return std::find_if(partitions_.begin(), partitions_.end(), [&name](const partition_info& pi) { return pi.name == name; }) == partitions_.end();
    }

    const uint8_t* disk_read(hd_info& hd, uint64_t offset, uint32_t len)
    {
        assert(len && len % sector_size_bytes == 0 && offset % sector_size_bytes == 0 && offset < hd.size && len < hd.size && offset + len <= hd.size);
        buffer_.resize(len);
        hd.f.seekg(offset);
        hd.f.read(reinterpret_cast<char*>(&buffer_[0]), len);
        if (!hd.f)
            throw std::runtime_error { "Error reading from " + hd.filename};
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
            return static_cast<uint16_t>(filesystems_.size());
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
            if (!ptr_hold_ || (ptr_hold_ & 1) || !cpu_active_) {
                std::cerr << "harddisk: Invalid conditions! written val = $" << hexfmt(val) << " ptr_hold = $" << hexfmt(ptr_hold_) << " cpu_active_ = " << cpu_active_ << "\n";
                return;
            }
            cpu_active_ = false;

            if (val == 0xfede)
                handle_disk_cmd();
            else if (val == 0xfedf)
                handle_init();
            else if (val == 0xfee0)
                handle_fs_resource();
            else if (val == 0xfee1)
                handle_fs_info_req();
            else if (val == 0xfee2)
                handle_fs_initseg();
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
        constexpr uint16_t devn_dosName      = 0x00;  // APTR  Pointer to DOS file handler name
        constexpr uint16_t devn_unit         = 0x08;  // ULONG Unit number
        constexpr uint16_t devn_flags        = 0x0C;  // ULONG OpenDevice flags
        constexpr uint16_t devn_sizeBlock    = 0x14;  // ULONG # longwords in a block
        constexpr uint16_t devn_secOrg       = 0x18;  // ULONG sector origin -- unused
        constexpr uint16_t devn_numHeads     = 0x1C;  // ULONG number of surfaces
        constexpr uint16_t devn_secsPerBlk   = 0x20;  // ULONG secs per logical block
        constexpr uint16_t devn_blkTrack     = 0x24;  // ULONG secs per track
        constexpr uint16_t devn_resBlks      = 0x28;  // ULONG reserved blocks -- MUST be at least 1!
        constexpr uint16_t devn_interleave   = 0x30;  // ULONG interleave
        constexpr uint16_t devn_lowCyl       = 0x34;  // ULONG lower cylinder
        constexpr uint16_t devn_upperCyl     = 0x38;  // ULONG upper cylinder
        constexpr uint16_t devn_numBuffers   = 0x3C;  // ULONG number of buffers
        constexpr uint16_t devn_memBufType   = 0x40;  // ULONG Type of memory for AmigaDOS buffers
        constexpr uint16_t devn_transferSize = 0x44; // LONG  largest transfer size (largest signed #)
        constexpr uint16_t devn_addMask      = 0x48;  // ULONG address mask
        constexpr uint16_t devn_bootPrio     = 0x4c;  // ULONG boot priority
        constexpr uint16_t devn_dName        = 0x50;	// char[4] DOS file handler name
        constexpr uint16_t devn_bootflags    = 0x54;  // boot flags (not part of DOS packet)
        constexpr uint16_t devn_segList      = 0x58;  // filesystem segment list (not part of DOS packet)

        // unit, dosName, execName and tableSize is filled by expansion ROM

        const uint32_t unit = mem_.read_u32(ptr_hold_ + devn_unit);

        if (unit >= partitions_.size())
            throw std::runtime_error { "Invalid HD partition used for initialization" };

        const auto& part = partitions_[unit];

        std::cout << "[HD] Initializing partition " << unit << " " << dos_type_string(part.dos_type) << " \"" << part.name << "\"\n";
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
        mem_.write_u32(ptr_hold_ + devn_transferSize, part.max_transfer);
        mem_.write_u32(ptr_hold_ + devn_addMask, part.mask);
        mem_.write_u32(ptr_hold_ + devn_bootPrio, part.boot_priority);
        mem_.write_u32(ptr_hold_ + devn_dName, part.dos_type);
        mem_.write_u32(ptr_hold_ + devn_bootflags, part.boot_flags);

        uint32_t seglist_bptr = 0;
        for (const auto& fs : filesystems_) {
            if (part.dos_type == fs.dos_type) {
                std::cout << "[HD] Partition " << unit << " seglist: $" << hexfmt(fs.seglist_bptr * 4) << "\n";
                seglist_bptr = fs.seglist_bptr;
                break;
            }
        }

        mem_.write_u32(ptr_hold_ + devn_segList, seglist_bptr);
    }

    void do_scsi_cmd(hd_info& hd, scsi_cmd& sc)
    {
        std::vector<uint8_t> cmd(sc.scsi_CmdLength);
        for (uint32_t i = 0; i < sc.scsi_CmdLength; ++i)
            cmd[i] = mem_.read_u8(sc.scsi_Command + i);

        static constexpr bool scsi_debug = false;

        if constexpr (scsi_debug) {
            std::cout << "[HD] SCSI command for " << hd.filename << ": ";
            hexdump(std::cout, cmd.data(), cmd.size());
        }

        if (sc.scsi_CmdLength < 6) {
            std::cerr << "[HD] Invalid SCSI command length: " << sc.scsi_CmdLength << "\n";
            sc.scsi_Status = 2; // check condition (TODO: sense data)
            return;
        }

        auto copy_data = [&](const uint8_t* data, size_t len) {
            const uint32_t actlen = std::min(sc.scsi_Length, static_cast<uint32_t>(len));
            for (uint32_t i = 0; i < actlen; ++i)
                mem_.write_u8(sc.scsi_Data + i, data[i]);

            if constexpr (scsi_debug) {
                std::cout << "[HD] Returning: ";
                hexdump(std::cout, data, actlen);
            }

            sc.scsi_Actual = actlen;
            sc.scsi_CmdActual = sc.scsi_CmdLength;
            sc.scsi_Status = 0;
            sc.scsi_SenseActual = 0;
        };

        if (cmd[0] == 0x25 && sc.scsi_CmdLength == 10) {
            // READ CAPACITY (10)
            uint8_t data[8] = { 0, };
            const uint32_t max_block = static_cast<uint32_t>(std::min(uint64_t(0xffffffff), hd.size / sector_size_bytes - 1));
            if (cmd[8] & 1) { // pmi
                uint32_t lba = get_u32(&cmd[2]);
                lba += hd.sectors_per_track * hd.heads;
                lba /= hd.sectors_per_track * hd.heads;
                lba *= hd.sectors_per_track * hd.heads;
                put_u32(&data[0], std::min(max_block, lba));
            } else {
                put_u32(&data[0], max_block);
            }
            put_u32(&data[4], sector_size_bytes);
            copy_data(data, sizeof(data));
            return;
        }

        if (cmd[0] == 0x12 && sc.scsi_CmdLength == 6) {
            // INQUIRY
            uint8_t data[36] = { 0, };

            auto copy_string = [](uint8_t* d, const char* s, int len) {
                while (*s && len--)
                    *d++ = *s++;
                while (len--)
                    *d++ = ' ';
            };

            data[2] = 2; // Version
            data[4] = static_cast<uint8_t>(sizeof(data) - 4); // Additional length
            copy_string(&data[8], "AmiEmu", 8); // vendor
            copy_string(&data[16], "Virtual HD", 16); // product id
            copy_string(&data[32], "0.1", 4); // revision

            copy_data(data, sizeof(data));
            return;
        }

        if (cmd[0] == 0x1a && sc.scsi_CmdLength == 6 && (cmd[2] == 3 || cmd[2] == 4)) {
            // MODE SENSE (6) with PC = 0 and Page Code = 3 (Format Parameters page) or 4 (Rigid drive geometry parameters)
            uint8_t data[256];
            memset(data, 0, sizeof(data));
            data[3] = 8;
            put_u32(&data[4], static_cast<uint32_t>(std::min(uint64_t(0xffffffff), hd.size / sector_size_bytes)));
            put_u32(&data[8], sector_size_bytes);

            uint8_t* page = &data[12];

            page[0] = cmd[2]; // page code
            page[1] = 0x16; // page length

            if (cmd[2] == 3) {
                put_u16(&page[2], 1); // tracks per zone
                put_u16(&page[10], hd.sectors_per_track);
                put_u16(&page[12], sector_size_bytes); // data bytes per physical sector
                put_u16(&page[14], 1); // interleave
                page[20] = 0x80; // Drive type
            } else {
                assert(cmd[2] == 4);
                page[14] = page[2] = static_cast<uint8_t>((hd.cylinders >> 16) & 0xff);
                page[15] = page[3] = static_cast<uint8_t>((hd.cylinders >> 8) & 0xff);
                page[16] = page[4] = static_cast<uint8_t>((hd.cylinders >> 0) & 0xff);
                page[5] = hd.heads;
                put_u16(&page[20], 5400); // rotation speed
            }
            page += page[1] + 2;

            data[0] = static_cast<uint8_t>(page - data - 1);

            copy_data(data, page - data);
            return;
        }

        if (cmd[0] == 0x37 && sc.scsi_CmdLength == 10) { // READ DEFECT DATA
            uint8_t data[4] = { 0, static_cast<uint8_t>(cmd[1] & 0x1f), 0, 0 };
            copy_data(data, sizeof(data));
            return;
        }
        
        std::cerr << "[HD] Unsupported SCSI command: ";
        hexdump(std::cerr, cmd.data(), cmd.size());
        //#define SCSI_INVALID_COMMAND 0x20

        sc.scsi_Status = /*SCSI_INVALID_COMMAND*/0x20; // SCSI status
        sc.scsi_Actual = 0; //sc.scsi_Length;
        sc.scsi_CmdActual = 0; //sc.scsi_CmdLength; // Whole command used
        sc.scsi_SenseActual = 0; // Not used
    }

    void handle_disk_cmd()
    {
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
        constexpr uint16_t HD_SCSICMD      = 28;

        constexpr uint32_t IO_UNIT    = 0x18;
        constexpr uint32_t IO_COMMAND = 0x1C;
        constexpr uint32_t IO_ERROR   = 0x1F;
        constexpr uint32_t IO_ACTUAL  = 0x20;
        constexpr uint32_t IO_LENGTH  = 0x24;
        constexpr uint32_t IO_DATA    = 0x28;
        constexpr uint32_t IO_OFFSET  = 0x2C;

        constexpr uint16_t devunit_UnitNum = 0x2A;

        const auto unit = mem_.read_u32(mem_.read_u32(ptr_hold_ + IO_UNIT) + devunit_UnitNum); // grab from private field
        const auto cmd  = mem_.read_u16(ptr_hold_ + IO_COMMAND);
        const auto len  = mem_.read_u32(ptr_hold_ + IO_LENGTH);
        const auto data = mem_.read_u32(ptr_hold_ + IO_DATA);
        const auto ofs  = mem_.read_u32(ptr_hold_ + IO_OFFSET);

        if (unit >= partitions_.size()) {
            throw std::runtime_error { "Invalid partition (unit) $" + hexstring(unit) + " in IORequest" };
        }
        auto& hd = partitions_[unit].hd;

        //std::cerr << "[HD]: Command=$" << hexfmt(cmd) << " Unit=" << unit << " Length=$" << hexfmt(len) << " Data=$" << hexfmt(data) << " Offset=$" << hexfmt(ofs) << "\n";
        switch (cmd) {
        case CMD_READ:
        case CMD_WRITE:
        case TD_FORMAT:
            // TODO: Maybe check against partition offset/size?
            if (ofs > hd.size || len > hd.size || ofs > hd.size - len || ofs % sector_size_bytes) {
                std::cerr << "[HD] Invalid offset $" << hexfmt(ofs) << " length=$" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
            } else if (len % sector_size_bytes) {
                std::cerr << "[HD] Invalid length $" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
            } else {
                if (cmd == CMD_READ) {
                    const uint8_t* diskdata = disk_read(hd, ofs, len);
                    for (uint32_t i = 0; i < len; i += 4)
                        mem_.write_u32(data + i, get_u32(&diskdata[i]));
                } else {
                    buffer_.resize(len);
                    for (uint32_t i = 0; i < len; i += 4)
                        put_u32(&buffer_[i], mem_.read_u32(data + i));
                    hd.f.seekp(ofs);
                    hd.f.write(reinterpret_cast<const char*>(&buffer_[0]), len);
                    if (!hd.f)
                        throw std::runtime_error { "Error writing to " + hd.filename };
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
        case HD_SCSICMD: {
            constexpr uint16_t scsi_Data          = 0x00;
            constexpr uint16_t scsi_Length        = 0x04;
            constexpr uint16_t scsi_Actual        = 0x08;
            constexpr uint16_t scsi_Command       = 0x0c;
            constexpr uint16_t scsi_CmdLength     = 0x10;
            constexpr uint16_t scsi_CmdActual     = 0x12;
            constexpr uint16_t scsi_Flags         = 0x14;
            constexpr uint16_t scsi_Status        = 0x15;
            constexpr uint16_t scsi_SenseData     = 0x16;
            constexpr uint16_t scsi_SenseLength   = 0x1a;
            constexpr uint16_t scsi_SenseActual   = 0x1c;
            constexpr uint16_t scsi_SizeOf        = 0x1e;
            if (ofs || (len != scsi_SizeOf && len != 0x22)) { // HdToolBox seems to set length to $22?
                std::cerr << "[HD] Bad HD_SCSICMD offset $" << hexfmt(ofs) << " length=$" << hexfmt(len) << "\n";
                mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_BADADDRESS));
                break;
            }

            scsi_cmd scmd {};
            scmd.scsi_Data = mem_.read_u32(data + scsi_Data);
            scmd.scsi_Length = mem_.read_u32(data + scsi_Length);
            scmd.scsi_Command = mem_.read_u32(data + scsi_Command);
            scmd.scsi_CmdLength = mem_.read_u16(data + scsi_CmdLength);
            scmd.scsi_Flags = mem_.read_u8(data + scsi_Flags);
            scmd.scsi_SenseData = mem_.read_u32(data + scsi_SenseData);
            scmd.scsi_SenseLength = mem_.read_u16(data + scsi_SenseLength);

            do_scsi_cmd(hd, scmd);
            mem_.write_u32(data + scsi_Actual, scmd.scsi_Actual);
            mem_.write_u32(data + scsi_CmdActual, scmd.scsi_CmdActual);
            mem_.write_u8(data + scsi_Status, scmd.scsi_Status);
            mem_.write_u16(data + scsi_SenseActual, scmd.scsi_SenseActual);
            mem_.write_u8(ptr_hold_ + IO_ERROR, 0);
            mem_.write_u32(ptr_hold_ + IO_ACTUAL, /*len*/ scmd.scsi_Actual);
            break;
        }
        default:
            std::cerr << "[HD] Unsupported command $" << hexfmt(cmd) << "\n";
            mem_.write_u8(ptr_hold_ + IO_ERROR, static_cast<uint8_t>(IOERR_NOCMD));
        }
    }

    void handle_fs_resource()
    {
        if (!ptr_hold_)
            return;

        // Grab head of fsr_FileSysEntries
        uint32_t node = mem_.read_u32(ptr_hold_ + 0x12);
        for (;;) {
            const auto succ = mem_.read_u32(node);
            if (!succ)
                break;

            const uint32_t dos_type = mem_.read_u32(node + 0x0e); // fsr_DosType
            const uint32_t version = mem_.read_u32(node + 0x12); // fsr_Version

            // Check if we were going to provide this FS
            for (auto it = filesystems_.begin(); it != filesystems_.end();) {
                if (it->dos_type == dos_type && it->version <= version) {
                    it = filesystems_.erase(it);
                } else {
                    ++it;
                }
            }
            node = succ;
        }
    }

    void handle_fs_info_req()
    {
        // Keep structure in sync with exprom.asm
        static constexpr uint16_t fsinfo_num = 0x00;
        static constexpr uint16_t fsinfo_dosType = 0x02;
        static constexpr uint16_t fsinfo_version = 0x06;
        static constexpr uint16_t fsinfo_numHunks = 0x0a;
        static constexpr uint16_t fsinfo_hunk = 0x0e;

        const uint16_t fs_num = mem_.read_u16(ptr_hold_ + fsinfo_num);
        if (fs_num >= filesystems_.size())
            throw std::runtime_error { "Request for invalid filesystem number " + std::to_string(fs_num) };
        const auto& fs = filesystems_[fs_num];
        mem_.write_u32(ptr_hold_ + fsinfo_dosType, fs.dos_type);
        mem_.write_u32(ptr_hold_ + fsinfo_version, fs.version);
        mem_.write_u32(ptr_hold_ + fsinfo_numHunks, static_cast<uint16_t>(fs.hunks.size()));
        for (uint32_t i = 0; i < fs.hunks.size(); ++i)
            mem_.write_u32(ptr_hold_ + fsinfo_hunk + i * 4, fs.hunks[i].flags);
    }

    void handle_fs_initseg()
    {
        static constexpr uint16_t fsinitseg_hunk = 0;
        static constexpr uint16_t fsinitseg_num = 12;
        const uint32_t fs_num = mem_.read_u32(ptr_hold_ + fsinitseg_num);
        if (fs_num >= filesystems_.size())
            throw std::runtime_error { "Request for invalid filesystem number " + std::to_string(fs_num) };

        uint32_t segptr[max_hunks ] = { 0, };
        auto& fs = filesystems_[fs_num];
        assert(fs.seglist_bptr == 0);
        for (uint32_t i = 0; i < fs.hunks.size(); ++i) {
            segptr[i] = mem_.read_u32(ptr_hold_ + fsinitseg_hunk + i * 4);
            if (!segptr[i])
                throw std::runtime_error { "Memory allocation failed inside AmigaOS" };
        }
        for (uint32_t i = 0; i < fs.hunks.size(); ++i) {
            mem_.write_u32(segptr[i], static_cast<uint32_t>(fs.hunks[i].data.size()) - 8); // Hunk size
            mem_.write_u32(segptr[i] + 4, i == fs.hunks.size() - 1 ? 0 : (segptr[i + 1] + 4) >> 2); // Link hunks
            const uint32_t start = segptr[i] + 8;
            for (uint32_t j = 0; j < fs.hunks[i].data.size(); j += 4)
                mem_.write_u32(start + j, get_u32(&fs.hunks[i].data[j]));

            for (const auto& hr : fs.hunks[i].relocs) {
                const uint32_t dst_start = segptr[hr.dst_hunk] + 8;
                for (const auto ofs : hr.offsets) {
                    mem_.write_u32(start + ofs, mem_.read_u32(start + ofs) + dst_start);
                }
            }
        }

        std::cout << "[HD] Filesystem " << dos_type_string(fs.dos_type) << " loaded. SegList at $" << hexfmt(segptr[0] + 4) << "\n";
        fs.seglist_bptr = (segptr[0] + 4) >> 2;
    }
};

harddisk::harddisk(memory_handler& mem, bool& cpu_active, const std::vector<std::string>& hdfilenames)
    : impl_{ new impl(mem, cpu_active, hdfilenames) }
{
}

harddisk::~harddisk() = default;

autoconf_device& harddisk::autoconf_dev()
{
    return *impl_;
}
