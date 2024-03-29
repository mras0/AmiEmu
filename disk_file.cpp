#include "disk_file.h"
#include "disk_drive.h"
#include "adf.h"
#include "dms.h"
#include "memory.h"
#include "ioutil.h"
#include "debug.h"

#include <stdexcept>
#include <fstream>
#include <cassert>

namespace {

constexpr uint16_t NUMSECS = 11; // sectors per track
constexpr uint16_t TD_SECTOR = 512; // bytes per sector
constexpr uint16_t TRACK_SIZE = NUMSECS * TD_SECTOR; // bytes per track
constexpr uint16_t MFM_SYNC = 0x4489; // MFM sync value
constexpr uint16_t NUM_CYLINDERS = 80; // There are 80 cylinders on a Amiga floppy disk
constexpr uint32_t DISK_SIZE = NUM_CYLINDERS * 2 * TRACK_SIZE; // Each cylinder has 2 MFM tracks, 1 on each side

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


std::vector<uint8_t> make_exe_disk(const std::string& filename, const std::vector<uint8_t>& data)
{
    std::string fname = filename;
    if (auto pos = fname.find_last_of("/\\"); pos != std::string::npos)
        fname = fname.substr(pos + 1);
    auto disk = adf::new_disk(fname);
    auto ss = "\":" + fname + "\"";
    disk.make_dir("S");
    disk.write_file("s/startup-sequence", std::vector<uint8_t>(ss.begin(), ss.end()));
    disk.write_file(fname, data);
    return disk.get();
}

void format_std_track(uint8_t* dest, uint8_t tracknum, const uint8_t* raw_data)
{
    for (uint8_t sec = 0; sec < NUMSECS; ++sec, raw_data += TD_SECTOR) {
        put_u32(&dest[0], fill); // Preamble
        put_u16(&dest[4], MFM_SYNC);
        put_u16(&dest[6], MFM_SYNC);
        put_split_long(&dest[8], 0xffU << 24 | tracknum << 16 | sec << 8 | (11 - sec));
        // sector label
        for (uint32_t i = 16; i < 48; i += 4)
            put_u32(&dest[i], fill);
        // header checksum
        put_split_long_fill(&dest[48], checksum(&dest[8], (48 - 8) / 4));
        // data
        for (uint16_t i = 0; i < 512; ++i) {
            dest[64 + i] = 0xaa | ((raw_data[i] >> 1) & 0x55);
            dest[64 + 512 + i] = 0xaa | (raw_data[i] & 0x55);
        }
        // data checksum
        put_split_long_fill(&dest[56], checksum(&dest[64], (MFM_SECTOR_SIZE_WORDS * 2 - 64) / 4));
        dest += MFM_SECTOR_SIZE_WORDS * 2;
    }
    // gap
    std::memset(dest, 0xaa, MFM_GAP_SIZE_WORDS * 2);
}

class adf_disk_file : public disk_file {
public:
    adf_disk_file(const std::string& name, std::vector<uint8_t>&& data)
        : name_ { name }
        , data_ { std::move(data) }
        , dirty_ { false }
    {
        if (data_.size() != DISK_SIZE)
            throw std::runtime_error { name_ + " has unsupported size $" + hexstring(data_.size()) };
    }

    ~adf_disk_file()
    {
        if (!dirty_)
            return;
        auto filename = name_ + "_modified.adf"; // TODO: Fix me
        if (debug_stream)
            *debug_stream << name_ << " has been written. Saving as " << filename << "\n";
        std::ofstream out { filename, std::ofstream::binary };
        if (out && out.is_open() && out.write(reinterpret_cast<const char*>(&data_[0]), data_.size()))
            return;
        if (debug_stream)
            *debug_stream << name_ << " error writing " << filename << "\n";
    }

    const std::string& name() const override
    {
        return name_;
    }

    uint8_t num_cylinders() const override
    {
        return NUM_CYLINDERS;
    }

    bool ofs_bootable() const override
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

    void read_mfm_track(uint8_t tracknum, uint8_t* dest) const override
    {
        format_std_track(dest, tracknum, &data_[tracknum * NUMSECS * TD_SECTOR]);
    }

    void write_mfm_track(uint8_t tracknum, const uint8_t* src) override
    {
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
                const uint8_t odd = data[ofs + 60 + i] & 0x55;
                const uint8_t even = data[ofs + 60 + 512 + i] & 0x55;
                raw_data[i] = odd << 1 | even;
            }
        }

        dirty_ = true;
    }

private:
    std::string name_;
    std::vector<uint8_t> data_;
    bool dirty_;
};

class extended_adf_disk_file : public disk_file {
public:
    extended_adf_disk_file(const std::string& name, std::vector<uint8_t>&& data)
        : name_ { name }
        , data_ { std::move(data) }
    {
        if (!detect(data_))
            throw std::runtime_error { name_ + " is not a valid extended ADF file" };
        const auto num_tracks = get_u16(&data_[10]); 
        info_.resize(num_tracks);

        if (debug_stream)
            *debug_stream << "Loading extended ADF file " << name_ << " " << info_.size() << " tracks\n";
        uint32_t pos = 12;
        const auto end = static_cast<uint32_t>(data_.size());
        uint32_t ofs = 12 + 12 * num_tracks;
        for (auto& ti: info_) {
            if (pos + 12 > end)
                throw std::runtime_error { name_ + " is not a valid extended ADF file (unexpected EOF)" };
            const auto type = get_u16(&data_[pos + 2]);
            const auto nbytes = get_u32(&data_[pos + 4]);
            const auto nbits = get_u32(&data_[pos + 8]);

            if (type != 0 && (type&0xff) != 1)
                throw std::runtime_error { name_ + ": Invalid track type $" + hexstring(type) };
            if (nbytes > MFM_TRACK_SIZE_WORDS * 2 || nbits > nbytes * 8 || ofs + nbytes > end)
                throw std::runtime_error { name_ + ": Invalid track length" };
            ti.type = type;
            ti.num_bits = nbits;
            ti.offset = ofs;
            ti.allocated_bytes = nbytes;
            pos += 12;
            ofs += nbytes;
        }
    }

    ~extended_adf_disk_file() = default;

    static bool detect(const std::vector<uint8_t>& data)
    {
        // Limit to 84 cylinders (probably 83 is the right number)
        if (data.size() < 12 || memcmp(data.data(), magic, sizeof(magic)))
            return false;
        const auto num_tracks = get_u16(&data[10]);
        if (num_tracks % 2 || num_tracks > max_cylinders * 2)
            return false;
        return true;
    }

    const std::string& name() const override
    {
        return name_;
    }

    uint8_t num_cylinders() const override
    {
        return static_cast<uint8_t>(info_.size() / 2);
    }

    bool ofs_bootable() const override
    {
        if (debug_stream)
            *debug_stream << "TODO: extended_adf_disk_file::ofs_toobtable() for " << name_ << "\n";
        return false;
    }

    void read_mfm_track(uint8_t tracknum, uint8_t* dest) const override
    {
        assert(tracknum < info_.size());
        const auto& ti = info_[tracknum];

        if (!ti.type) {
            format_std_track(dest, tracknum, &data_[ti.offset]);
            return;
        }

        const auto nbytes = (ti.num_bits + 7) / 8;
        assert(nbytes <= MFM_TRACK_SIZE_WORDS * 2);
        memcpy(dest, &data_[ti.offset], nbytes);
        memset(dest + nbytes, 0xaa, MFM_TRACK_SIZE_WORDS * 2 - nbytes); // Should actually adjust MFM data etc. but don't bother for now
    }

    void write_mfm_track(uint8_t tracknum, const uint8_t* src) override
    {
        (void)tracknum;
        (void)src;
        throw std::runtime_error { "Writing to extended ADF file not supported yet" };
    }

private:
    struct track_info {
        uint16_t type; // 0=normal AmigaDOS track, 1 = raw MFM (upper byte = disk revolutions - 1)
        uint32_t num_bits;
        uint32_t offset;
        uint32_t allocated_bytes;
    };
    static constexpr uint8_t magic[8] = { 'U', 'A', 'E', '-', '1', 'A', 'D', 'F' };
    static constexpr uint8_t max_cylinders = 84; // Probably 83 is more correct

    std::string name_;
    std::vector<uint8_t> data_;
    std::vector<track_info> info_;
};
}

std::unique_ptr<disk_file> load_disk_file(const std::string& filename)
{
    auto data = read_file(filename);
    if (data.size() < 32)
        throw std::runtime_error { filename + " is not a disk image" };
    if (dms_detect(data))
        return std::make_unique<adf_disk_file>(filename, dms_unpack(data));
    if (extended_adf_disk_file::detect(data))
        return std::make_unique<extended_adf_disk_file>(filename, std::move(data));
    if (get_u32(&data[0]) == 1011) // HUNK_HEADER
        return std::make_unique<adf_disk_file>(filename, make_exe_disk(filename, data));
    if (data.size() != DISK_SIZE)
        throw std::runtime_error { filename + " is not a valid disk image (wrong size)" };
    return std::make_unique<adf_disk_file>(filename, std::move(data));
}
