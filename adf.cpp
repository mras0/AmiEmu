#include "adf.h"
#include "ioutil.h"
#include "memory.h"
#include <stdexcept>
#include <cassert>
#include <sstream>
#include <iostream>
#include <cstring>
#include <queue>
#include <climits>

#define EXPECT_BINOP(a, op, b)                                                                                                                 \
    do {                                                                                                                                       \
        const auto aval = (a);                                                                                                                 \
        const auto bval = (b);                                                                                                                 \
        if (!(aval op bval)) {                                                                                                                 \
            std::ostringstream oss;                                                                                                            \
            oss << "ADF: Expectation failed for: $" << hexfmt(aval) << " (" << #a << ") " << #op << " $" << hexfmt(bval) << " (" << #b << ")"; \
            throw std::runtime_error { oss.str() };                                                                                            \
        }                                                                                                                                      \
    } while (0);

#define EXPECT_EQ(a, b) EXPECT_BINOP(a, ==, b)
#define EXPECT_NE(a, b) EXPECT_BINOP(a, !=, b)
#define EXPECT_LT(a, b) EXPECT_BINOP(a, <, b)
#define EXPECT_GT(a, b) EXPECT_BINOP(a, >, b)

namespace {

constexpr uint32_t bytes_per_sector    = 512;
constexpr uint32_t sectors_per_track   = 11;
constexpr uint32_t tracks_per_cylinder = 2;
constexpr uint32_t cylinders_per_disk  = 80;
constexpr uint32_t sectors_per_disk    = sectors_per_track * tracks_per_cylinder * cylinders_per_disk;
constexpr uint32_t disk_size           = bytes_per_sector * sectors_per_disk;

constexpr uint32_t T_HEADER     = 2;
constexpr uint32_t T_DATA       = 8;
constexpr uint32_t T_LIST       = 16;
//constexpr uint32_t T_DIRCACHE   = 33;

enum second_type : uint32_t {
    ST_UNKNOWN   = 0,
    ST_ROOT      = 1,
    ST_USERDIR   = 2,
    ST_FILE      = (uint32_t)-3,
};

constexpr uint32_t ht_size = 0x48; // 72 long words for floppies

constexpr uint32_t name_max = 30;

enum block_offsets : uint32_t {
    boff_type           = 0x00, //block primary type = T_HEADER
    boff_header_key     = 0x04, //
    boff_high_seq	    = 0x08, //
    boff_ht_size        = 0x0c, //Hash table size in long (= BSIZE/4 - 56). For floppy disk value 0x48
    boff_first_data     = 0x10, //
    boff_chksum         = 0x14, //Checksum
    boff_table          = 0x18,
    boff_table_end      = 0x18+ht_size*4,

    boff_root_bm_page   = boff_table_end+0x04,   // for root block only

    boff_protect        = bytes_per_sector-0xc0, // for files and directories
    boff_file_size      = bytes_per_sector-0xbc, // for files only

    boff_time           = bytes_per_sector-0x5c,
    boff_name_len       = bytes_per_sector-0x50,
    boff_name           = bytes_per_sector-0x4f,

    boff_next_hash      = bytes_per_sector-0x10,
    boff_parent         = bytes_per_sector-0x0c,
    boff_extension      = bytes_per_sector-0x08,
    boff_2nd_type       = bytes_per_sector-0x04, // Block secondary type
};

//constexpr uint32_t boot_block_sector = 0;
constexpr uint32_t root_block_sector = 880;

constexpr uint32_t ofs_header_size = 24;

constexpr uint8_t standard_bootblock[] = {
    0x43, 0xfa, 0x00, 0x3e,             // LEA.L       $003e(PC), A1 ; expansion.library
    0x70, 0x25,                         // MOVEQ.L     #$25, D0
    0x4e, 0xae, 0xfd, 0xd8,             // JSR         _LVOOpenLibrary(A6)
    0x4a, 0x80,                         // TST.L       D0
    0x67, 0x0c,                         // BEQ.B       $00000026
    0x22, 0x40,                         // MOVEA.L     D0, A1
    0x08, 0xe9, 0x00, 0x06, 0x00, 0x22, // BSET.B      #6, $0022(A1)
    0x4e, 0xae, 0xfe, 0x62,             // JSR         _LVOCloseLibrary(A6)
    0x43, 0xfa, 0x00, 0x18,             // LEA.L       $0018(PC), A1 ; dos.library
    0x4e, 0xae, 0xff, 0xa0,             // JSR         _LVOFindResident(A6)
    0x4a, 0x80,                         // TST.L       D0
    0x67, 0x0a,                         // BEQ.B       $0000003c
    0x20, 0x40,                         // MOVEA.L     D0, A0
    0x20, 0x68, 0x00, 0x16,             // MOVEA.L     $0016(A0), A0
    0x70, 0x00,                         // MOVEQ.L     #$00, D0
    0x4e, 0x75,                         // RTS
    0x70, 0xff,                         // MOVEQ.L     #$ff, D0
    0x4e, 0x75,                         // RTS
    'd', 'o', 's', '.', 'l', 'i', 'b', 'r', 'a', 'r', 'y', 0,
    'e', 'x', 'p', 'a', 'n', 's', 'i', 'o', 'n', '.', 'l', 'i', 'b', 'r', 'a', 'r', 'y', 0,
};

bool is_ffs(const uint8_t* sector0)
{
    return sector0[3] & 1;
}

uint32_t bootblock_checksum(const uint8_t* bb)
{
    uint32_t csum = get_u32(bb + 0);
    for (uint32_t offset = 8; offset < bytes_per_sector * 2; offset += 4) {
        const auto l = get_u32(bb + offset);
        if (ULONG_MAX - csum < l) {
            ++csum;
        }
        csum += l;
    }
    return ~csum;
}

bool bootblock_valid(const uint8_t* sector0)
{
    return sector0[0] == 'D' && sector0[1] == 'O' && sector0[2] == 'S' && get_u32(sector0 + 8) == root_block_sector && bootblock_checksum(sector0) == get_u32(sector0 + 4);
}

#if 0
uint32_t convert_time(uint32_t days_since_1978, uint32_t mins, uint32_t ticks)
{
    EXPECT_LT(mins, 60 * 24);
    EXPECT_LT(ticks, 60 * 50);
    constexpr time_t seconds_per_day = 24 * 60 * 60;
    constexpr time_t amiga_epoch = seconds_per_day * (365 * 8 + 2);
    return amiga_epoch + seconds_per_day * days_since_1978 + mins * 60 + ticks / 50;
}

uint32_t convert_time(const uint8_t* d)
{
    return convert_time(get_u32(d + 0), get_u32(d + 4), get_u32(d + 8));
}
#endif

uint32_t block_checksum(const uint8_t* d, uint32_t skip_offset)
{
    uint32_t csum = 0;
    for (uint32_t i = 0; i < bytes_per_sector; i += 4) {
        if (i != skip_offset) {
            csum += get_u32(d + i);
        }
    }
    return -(int32_t)csum;
}

uint8_t nonintl_toupper(uint8_t c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

uint32_t str_hash(const uint8_t* name)
{
    uint32_t l, h;
    l = h = static_cast<uint32_t>(strlen(reinterpret_cast<const char*>(name)));
    for (uint32_t i = 0; i < l; ++i) {
        h = ((h * 13) + nonintl_toupper(name[i])) & 0x7ff;
    }
    return h % ht_size;
}

bool str_equal(const uint8_t* a, const uint8_t* b)
{
    while (*a && *b) {
        if (nonintl_toupper(*a++) != nonintl_toupper(*b++))
            return false;
    }
    return !*a && !*b;
}

void verify_block_header(const uint8_t* data, uint32_t expected_type, second_type expected_2nd_type = ST_UNKNOWN)
{
    EXPECT_EQ(get_u32(data + boff_type), expected_type);
    EXPECT_EQ(get_u32(data + boff_chksum), block_checksum(data, boff_chksum));
    const uint32_t type = get_u32(data + boff_2nd_type);
    if (expected_2nd_type != ST_UNKNOWN)
        EXPECT_EQ(expected_2nd_type, type);
}

uint8_t* disk_sector(uint8_t* disk, uint32_t index)
{
    EXPECT_LT(index, sectors_per_disk);
    return &disk[index * bytes_per_sector];
}

const uint8_t* disk_sector(const uint8_t* disk, uint32_t index)
{
    EXPECT_LT(index, sectors_per_disk);
    return &disk[index * bytes_per_sector];
}

uint8_t* disk_sector(std::vector<uint8_t>& disk, uint32_t sector)
{
    return disk_sector(disk.data(), sector);
}

const uint8_t* disk_sector(const std::vector<uint8_t>& disk, uint32_t sector)
{
    return disk_sector(disk.data(), sector);
}

uint32_t find_name_from_sector(const std::vector<uint8_t>& disk, uint32_t sector, const char* name)
{
    assert(!strchr(name, '/'));
    assert(!strchr(name, ':'));
    bool first = true;
    const uint8_t* const uname = reinterpret_cast<const uint8_t*>(name);
    const auto hash = str_hash(uname);

    while (sector) {
        const uint8_t* data = disk_sector(disk, sector);
        verify_block_header(data, T_HEADER);
        const uint32_t type = get_u32(data + boff_2nd_type);
        if (first) {
            if (type != ST_ROOT && type != ST_USERDIR)
                throw std::runtime_error { "ADF: Invalid block type $" + hexstring(type) };
            sector = get_u32(data + boff_table + hash * 4);
            first = false;
        } else {
            if (type != ST_FILE && type != ST_USERDIR)
                throw std::runtime_error { "ADF: Invalid block type $" + hexstring(type) };
            if (str_equal(uname, &data[boff_name]))
                return sector;
            sector = get_u32(data + boff_next_hash);
        }
    }

    return 0;
}

uint32_t find_name(const std::vector<uint8_t>& disk, const char* name)
{
    assert(!strchr(name, ':'));
    uint32_t sector = root_block_sector;
    while (*name && sector) {
        char name_buf[name_max + 1];
        const char* part_end = strchr(name, '/');
        const auto part_len = part_end ? part_end - name : strlen(name);
        assert(part_len <= name_max);
        memcpy(name_buf, name, part_len);
        name_buf[part_len] = 0;
        name += part_len + (name[part_len] == '/');
        sector = find_name_from_sector(disk, sector, name_buf);
        if (!*name)
            return sector;
    }
    return 0;
}

#if 0
const char* protect_string(uint32_t p)
{
    enum protect_flags : uint32_t {
        protectf_d = 1 << 0, // delete forbidden (D)
        protectf_e = 1 << 1, // not executable (E)
        protectf_w = 1 << 2, // not writable (W)
        protectf_r = 1 << 3, // not readable (R)
        protectf_a = 1 << 4, // is archived (A)
        protectf_p = 1 << 5, // pure (reetrant safe), can be made resident (P)
        protectf_s = 1 << 6, // file is a script (Arexx or Shell) (S)
        protectf_h = 1 << 7, // Hold bit. if H+P (and R+E) are set the file can be made resident on first load (OS 2.x and 3.0)
    };
    const char names[] = "hsparwed";
    static_assert(sizeof(names) == 9, "");
    static char buffer[9];
    for (int i = 0; i < 8; ++i) {
        buffer[i] = !!(p & (1 << (7 - i))) ^ (i >= 4) ? names[i] : '-';
    }
    assert((p & 0xffffff00) == 0);
    return buffer;
}
#endif

uint32_t bm_page_mask(uint32_t index)
{
    assert(index >= 2 && index < sectors_per_disk);
    return 1 << ((index - 2) % 32);
}

void set_sector_used_in_bitmap(uint8_t* bitmap_block, const uint32_t sector)
{
    const uint32_t m = bm_page_mask(sector);
    const auto l = bitmap_block + 4 + 4 * ((sector - 2) / 32);
    EXPECT_NE(get_u32(l) & m, 0);
    put_u32(l, get_u32(l) & ~m);
}

void put_block_checksum(uint8_t* block_data, uint32_t checksum_pos)
{
    put_u32(&block_data[checksum_pos], block_checksum(block_data, checksum_pos));
}

std::string block_name(const uint8_t* sector)
{
    EXPECT_LT(sector[boff_name_len], name_max + 1);
    return std::string(reinterpret_cast<const char*>(&sector[boff_name]), sector[boff_name_len]);
}

uint32_t get_parent(const std::vector<uint8_t>& disk, const char*& name)
{
    EXPECT_LT(strlen(name), 100);
    const char* p = strrchr(name, '/');
    if (!p)
        return root_block_sector;
    char temp[256];
    strncpy(temp, name, p - name);
    temp[p - name] = 0;
    const uint32_t parent = find_name(disk, temp);
    if (!parent)
        throw std::runtime_error{"Could not find parent directories for '" + std::string(name) + "'"};
    name = p + 1;
    return parent;
}

const uint8_t* bm_page(const std::vector<uint8_t>& disk)
{
    return disk_sector(disk, get_u32(disk_sector(disk, root_block_sector) + boff_root_bm_page));
}

const uint8_t* bm_page_long(const std::vector<uint8_t>& disk, uint32_t index)
{
    EXPECT_EQ(index >= 2 && index < sectors_per_disk, true);
    return bm_page(disk) + 4 + 4 * ((index - 2) / 32);
}

bool sector_in_use(const std::vector<uint8_t>& disk, uint32_t index)
{
    return !(get_u32(bm_page_long(disk, index)) & bm_page_mask(index));
}

uint32_t next_free_block(const std::vector<uint8_t>& disk)
{
    uint32_t s = root_block_sector + 1;
    for (uint32_t try_count = sectors_per_disk - 4; try_count--; ++s) {
        if (s >= sectors_per_disk)
            s = 2;
        if (!sector_in_use(disk, s)) {
            return s;
        }
    }
    throw std::runtime_error { "Disk is full" };
}

uint32_t alloc_block(std::vector<uint8_t>& disk)
{
    const uint32_t b = next_free_block(disk);
    assert(b);
    const uint32_t m = bm_page_mask(b);
    const auto l = bm_page_long(disk, b);
    const auto v = get_u32(l);
    assert(v & m);
    put_u32(const_cast<uint8_t*>(l), v & ~m);
    // Update bitmap checksum
    put_block_checksum(const_cast<uint8_t*>(bm_page(disk)), 0);
    return b;
}

void set_name(uint8_t* d, const char* name)
{
    const auto nl = strlen(name);
    assert(nl <= name_max);
    d[boff_name_len] = static_cast<uint8_t>(nl);
    memcpy(d + boff_name, name, nl);
}

void set_table_entry(std::vector<uint8_t>& disk, uint32_t parent, uint32_t table_index, uint32_t table_value)
{
    uint8_t* block_data = disk_sector(disk, parent);
    EXPECT_LT(table_index, ht_size);
    EXPECT_LT(table_value, sectors_per_disk);
    EXPECT_EQ(get_u32(block_data + boff_type), T_HEADER);
    EXPECT_EQ(get_u32(block_data + boff_2nd_type) == ST_ROOT || get_u32(block_data + boff_2nd_type) == ST_USERDIR, true);
    auto addr = block_data + boff_table + 4 * table_index;
    uint32_t chain;
    while ((chain = get_u32(addr)) != 0) {
        block_data = disk_sector(disk, chain);
        EXPECT_EQ(get_u32(block_data + boff_type), T_HEADER);
        EXPECT_EQ(get_u32(block_data + boff_2nd_type) == ST_USERDIR || get_u32(block_data + boff_2nd_type) == ST_FILE, true);
        addr = block_data + boff_next_hash;
    }
    put_u32(addr, table_value);
    put_block_checksum(block_data, boff_chksum); // Recalc checksum
}

} // unnamed namespace

class adf::impl {
public:
    explicit impl(const std::vector<uint8_t>& data)
        : data_ { data }
    {
        if (data.size() != disk_size)
            throw std::runtime_error { "ADF: Unexpected disk size $" + hexstring(data.size()) };

        if (data_[0] != 'D' || data_[1] != 'O' || data_[2] != 'S')
            throw std::runtime_error { "ADF: Not an AmigaDOS disk" };
        if ((data_[3] & 0xfe) != 0)
            throw std::runtime_error { "Unsupported disk flags $" + hexstring(data_[3]) };
        if (!bootblock_valid(data_.data())) {
            std::cerr << "ADF: Warning bootblock invalid\n";
        } else {
            EXPECT_EQ(get_u32(&data_[8]), root_block_sector);
        }

        const uint8_t* root = disk_sector(data_, root_block_sector);
        verify_block_header(root, T_HEADER, ST_ROOT);
        EXPECT_EQ(get_u32(root + boff_header_key), 0);
        EXPECT_EQ(get_u32(root + boff_high_seq), 0);
        EXPECT_EQ(get_u32(root + boff_ht_size), ht_size);
        EXPECT_EQ(get_u32(root + boff_first_data), 0);
        EXPECT_EQ(get_u32(root + boff_next_hash), 0);
        EXPECT_EQ(get_u32(root + boff_parent), 0);
        EXPECT_EQ(get_u32(root + boff_2nd_type), ST_ROOT);
        EXPECT_EQ(get_u32(root + boff_table_end + 0x00), 0xffffffff); // bm_flag, -1 = valid
        const uint32_t bm_page_0 = get_u32(root + boff_root_bm_page);
        EXPECT_NE(bm_page_0, 0); // bm_page[0]
        // check that bm_page[1] through bm_page[24] and bm_ext are all zero
        for (int i = 0; i < 25; ++i) {
            EXPECT_EQ(get_u32(root + boff_table_end + 0x08 + i * 4), 0);
        }

        // Verify bitmap
        const uint8_t* bm_data = disk_sector(data_, bm_page_0);
        EXPECT_EQ(get_u32(bm_data), block_checksum(bm_data, 0));
    }

    static adf new_disk(const std::string& label)
    {
        EXPECT_LT(label.length(), name_max + 1);

        std::vector<uint8_t> blank(disk_size);
        const uint8_t flags = 0; // OFS
        put_u32(&blank[0], 'D' << 24 | 'O' << 16 | 'S' << 8 | static_cast<uint8_t>(flags));
        put_u32(&blank[8], root_block_sector);
        memcpy(&blank[12], standard_bootblock, sizeof(standard_bootblock));

        put_u32(&blank[4], bootblock_checksum(&blank[0]));

        const uint32_t bitmap_sector = root_block_sector + 1;
        uint8_t* root_block = &blank[root_block_sector * bytes_per_sector];
        put_u32(&root_block[boff_type], T_HEADER);
        put_u32(&root_block[boff_ht_size], ht_size);
        put_u32(&root_block[boff_table_end + 0x00], UINT32_MAX); // bm_flag = -1 = valid
        put_u32(&root_block[boff_table_end + 0x04], bitmap_sector); // bm_page[0]
        root_block[boff_name_len] = static_cast<uint8_t>(label.size());
        strncpy(reinterpret_cast<char*>(&root_block[boff_name]), label.data(), name_max);
        put_u32(&root_block[boff_2nd_type], ST_ROOT);
        put_block_checksum(root_block, boff_chksum);

        uint8_t* bitmap_block = &blank[bitmap_sector * bytes_per_sector];
        const uint32_t bitmap_longs = ((sectors_per_disk - 2) + 31) / 32;
        memset(&bitmap_block[4], -1, 4 * bitmap_longs);

        set_sector_used_in_bitmap(bitmap_block, root_block_sector);
        set_sector_used_in_bitmap(bitmap_block, bitmap_sector);

        put_block_checksum(bitmap_block, 0);

        EXPECT_EQ(bootblock_valid(&blank[0]), true);
        return adf { blank };
    }

    const std::vector<uint8_t>& get() const
    {
        return data_;
    }

    std::vector<std::string> filelist() const
    {
        struct queue_entry {
            uint32_t sector;
            std::string base;
        };

        std::queue<queue_entry> q;
        q.push({ root_block_sector, "" });

        std::vector<std::string> result;

        while (!q.empty()) {
            const auto e = q.front();
            q.pop();
            assert(e.sector);

            const uint8_t* data = disk_sector(data_, e.sector);
            verify_block_header(data, T_HEADER);

            const uint32_t second_type = get_u32(data + boff_2nd_type);

            if (second_type == ST_FILE) {
                result.push_back(e.base + block_name(data));
                continue;
            }

            if (second_type != ST_ROOT && second_type != ST_USERDIR)
                throw std::runtime_error { "ADF: Unxpected block type $" + hexstring(second_type) };

            auto base = e.base;
            if (second_type == ST_USERDIR)
                base += block_name(data) + "/";

            if (auto next = get_u32(data + boff_next_hash); next)
                q.push({ next, e.base });

            for (uint32_t i = 0; i < ht_size; ++i) {
                const auto s = get_u32(data + boff_table + i * 4);
                if (s)
                    q.push({ s, base });
            }
        }

        return result;
    }

    std::string volume_label() const
    {
        return block_name(disk_sector(data_, root_block_sector));
    }

    std::vector<uint8_t> read_file(const std::string& path) const
    {
        uint32_t file_header_sector = find_name(data_, path.c_str());
        if (!file_header_sector)
            throw std::runtime_error { "ADF: File '" + path + "' not found on disk" };

        const bool ffs = is_ffs(disk_sector(data_, 0));

        const uint8_t* file_header = disk_sector(data_, file_header_sector);
        verify_block_header(file_header, T_HEADER, ST_FILE);
        EXPECT_EQ(get_u32(file_header + boff_header_key), file_header_sector);
        std::vector<uint8_t> file_data(get_u32(file_header + boff_file_size));
        uint32_t data_pos = 0;
        while (data_pos < file_data.size()) {
            EXPECT_EQ(data_pos || get_u32(file_header + boff_table_end - 4) == get_u32(file_header + boff_first_data), true);
            const uint32_t high_seq = get_u32(file_header + boff_high_seq);
            EXPECT_EQ(high_seq > 0 && high_seq <= ht_size, true);
            for (uint32_t i = 0; i < high_seq; ++i) {
                const uint32_t s = get_u32(file_header + boff_table + (ht_size - 1 - i) * 4);
                assert(s > 0);
                const uint8_t* data_block = disk_sector(data_, s);
                const uint32_t remaining = static_cast<uint32_t>(file_data.size()) - data_pos;
                if (ffs) {
                    constexpr uint32_t block_data_size = 512;
                    const uint32_t here = remaining > block_data_size ? block_data_size : remaining;
                    memcpy(&file_data[data_pos], data_block, here);
                    data_pos += here;
                } else {
                    // OFS style data block
                    constexpr uint32_t block_data_size = 512 - ofs_header_size;
                    EXPECT_EQ(get_u32(data_block + boff_type), T_DATA);
                    EXPECT_EQ(get_u32(data_block + boff_chksum), block_checksum(data_block, boff_chksum));
                    EXPECT_EQ(get_u32(data_block + boff_header_key), file_header_sector);
                    EXPECT_EQ(get_u32(data_block + boff_high_seq), 1 + data_pos / block_data_size);
                    const uint32_t here = remaining > block_data_size ? block_data_size : remaining;
                    EXPECT_EQ(get_u32(data_block + boff_ht_size), here);
                    memcpy(&file_data[data_pos], data_block + ofs_header_size, here);
                    data_pos += here;
                }
            }
            const uint32_t extension = get_u32(file_header + boff_extension);
            if (!extension)
                break;
            file_header = disk_sector(data_, extension);
            verify_block_header(file_header, T_LIST, ST_FILE);
        }
        EXPECT_EQ(data_pos, file_data.size());
        return file_data;
    }

    void make_dir(const std::string& path)
    {
        EXPECT_EQ(!strchr(path.c_str(), ':'), true);
        EXPECT_EQ(!find_name(data_, path.c_str()), true);
        const char* const orig_name = path.c_str();
        const char* dirname = path.c_str();
        const uint32_t parent = get_parent(data_, dirname);

        const uint32_t dir_block = alloc_block(data_);

        uint8_t* dir_data = disk_sector(data_, dir_block);
        put_u32(dir_data + boff_type, T_HEADER);
        put_u32(dir_data + boff_header_key, dir_block);
        put_u32(dir_data + boff_parent, parent);
        put_u32(dir_data + boff_2nd_type, ST_USERDIR);
        set_name(dir_data, dirname);
        put_block_checksum(dir_data, boff_chksum);

        set_table_entry(data_, parent, str_hash(reinterpret_cast<const uint8_t*>(dirname)), dir_block);

        EXPECT_EQ(!!find_name(data_, orig_name), true);
    }

    void write_file(const std::string& path, const std::vector<uint8_t>& data)
    {
        EXPECT_EQ(!strchr(path.c_str(), ':'), true);
        EXPECT_EQ(!find_name(data_, path.c_str()), true);
        const char* const orig_name = path.c_str();

        const bool ffs = is_ffs(disk_sector(data_, 0));
        const uint32_t data_block_bytes = ffs ? 512 : 512 - ofs_header_size;

        const uint32_t header_block = alloc_block(data_);
        const uint32_t n_data_blocks = static_cast<uint32_t>((data.size() + data_block_bytes - 1) / data_block_bytes);
        std::vector<uint32_t> extension_blocks;
        std::vector<uint32_t> data_blocks;
        if (n_data_blocks > ht_size) {
            for (uint32_t i = 0; i < (n_data_blocks - ht_size + ht_size - 1) / ht_size; ++i) {
                extension_blocks.push_back(alloc_block(data_));
            }
        }
        for (uint32_t i = 0; i < n_data_blocks; ++i) {
            data_blocks.push_back(alloc_block(data_));
        }

        const char* filename = path.c_str();
        const uint32_t parent = get_parent(data_, filename);

        uint8_t* file_header = disk_sector(data_, header_block);
        put_u32(file_header + boff_type, T_HEADER);
        put_u32(file_header + boff_header_key, header_block);
        put_u32(file_header + boff_first_data, n_data_blocks ? data_blocks.front() : 0);
        put_u32(file_header + boff_file_size, static_cast<uint32_t>(data.size()));
        put_u32(file_header + boff_parent, parent);
        put_u32(file_header + boff_2nd_type, ST_FILE);
        set_name(file_header, filename);
        const uint32_t n_data_blocks_in_file_header = n_data_blocks > ht_size ? ht_size : n_data_blocks;
        put_u32(file_header + boff_high_seq, n_data_blocks_in_file_header);
        for (uint32_t i = 0; i < n_data_blocks_in_file_header; ++i) {
            put_u32(file_header + boff_table + (ht_size - 1 - i) * 4, data_blocks[i]);
        }
        if (!extension_blocks.empty()) {
            put_u32(file_header + boff_extension, extension_blocks[0]);
            for (uint32_t ext = 0; ext < (uint32_t)extension_blocks.size(); ++ext) {
                uint8_t* ext_header = disk_sector(data_, extension_blocks[ext]);
                put_u32(ext_header + boff_type, T_LIST);
                put_u32(ext_header + boff_header_key, extension_blocks[ext]);
                put_u32(ext_header + boff_parent, header_block);
                put_u32(ext_header + boff_2nd_type, ST_FILE);
                uint32_t this_size = ht_size;
                if (ext == extension_blocks.size() - 1) {
                    this_size = n_data_blocks % ht_size;
                } else {
                    put_u32(ext_header + boff_extension, extension_blocks[ext + 1]);
                }
                put_u32(ext_header + boff_high_seq, this_size);
                for (uint32_t i = 0; i < this_size; ++i) {
                    put_u32(ext_header + boff_table + (ht_size - 1 - i) * 4, data_blocks[i + (ext + 1) * ht_size]);
                }
                put_block_checksum(ext_header, boff_chksum);
            }
        }
        put_block_checksum(file_header, boff_chksum);

        for (uint32_t i = 0; i < n_data_blocks; ++i) {
            const uint8_t* const src = &data[i * data_block_bytes];
            uint8_t* const data_block = disk_sector(data_, data_blocks[i]);
            const uint32_t this_block = std::min(data_block_bytes, static_cast<uint32_t>(data.size() - i * data_block_bytes));
            if (ffs) {
                memcpy(data_block, src, this_block);
                if (this_block < data_block_bytes)
                    memset(data_block + this_block, 0, data_block_bytes - this_block);
            } else {
                put_u32(data_block + boff_type, T_DATA);
                put_u32(data_block + boff_header_key, header_block);
                put_u32(data_block + boff_high_seq, 1 + i);
                put_u32(data_block + boff_ht_size, this_block);
                put_u32(data_block + boff_first_data, i + 1 < n_data_blocks ? data_blocks[i + 1] : 0);
                memcpy(data_block + ofs_header_size, src, this_block);
                if (this_block < data_block_bytes)
                    memset(data_block + ofs_header_size + this_block, 0, data_block_bytes - this_block);
                put_block_checksum(data_block, boff_chksum);
            }
        }

        set_table_entry(data_, parent, str_hash(reinterpret_cast<const uint8_t*>(filename)), header_block);

        EXPECT_EQ(!!find_name(data_, orig_name), true);
    }

private:
    std::vector<uint8_t> data_;
};

adf::adf(const std::vector<uint8_t>& data)
    : impl_ { std::make_unique<impl>(data) }
{
}

adf::~adf() = default;

const std::vector<uint8_t>& adf::get() const
{
    return impl_->get();
}

std::vector<std::string> adf::filelist() const
{
    return impl_->filelist();
}

std::string adf::volume_label() const
{
    return impl_->volume_label();
}

std::vector<uint8_t> adf::read_file(const std::string& path) const
{
    return impl_->read_file(path);
}

adf adf::new_disk(const std::string& label)
{
    return impl::new_disk(label);
}

void adf::make_dir(const std::string& path)
{
    impl_->make_dir(path);
}

void adf::write_file(const std::string& path, const std::vector<uint8_t>& data)
{
    impl_->write_file(path, data);
}
