#include "dms.h"
#include "ioutil.h"
#include "memory.h"
#include <stdexcept>

//#define DMS_TRACE
#ifdef DMS_TRACE
#include <iostream>
#include <iomanip>
#endif

// Test cases:
// Fairlight.242.dms            store (0) + deep (6), has banner as "track 0"
// KEFRENS-DesertDream-B.dms    rle
// ANARCHY-3dDemo2.dms          deep + rle/no-rle
// PHENOMENA-Enigma.DMS         medium (3)
// SANITY-woc92.dms             banner track (0xffff)
// THEUNTO1.dms                 banner track (0 with size 1024)

namespace {

constexpr uint32_t dms_header_id = 'D' << 24 | 'M' << 16 | 'S' << 8 | '!';
constexpr uint16_t dms_track_header_id = 'T' << 8 | 'R';
constexpr uint32_t dms_archive_header_offset = 4;
constexpr uint32_t dms_archive_header_size = 52;
constexpr uint16_t dms_track_header_size = 20;

struct dms_archive_header {
    uint32_t extra;         // 0  : for future expansion
    uint32_t general;       // 4  : general purpose flags
    uint32_t timestamp;     // 8  : creation date of archive (seconds since 00:00:00 01-Jan-78)
    uint16_t lowtrack;      // 12 : first track (0-85)
    uint16_t hightrack;     // 14 : last track (0-85)
    uint32_t pack_size;     // 16 : total size of data after compression
    uint32_t unpack_size;   // 20 : total size of data before compression
    uint32_t serialno;      // 24 : serial number of creator
    uint16_t cpu;           // 28 : CPU type of creator
    uint16_t copro;         // 30 : CPU coprocessor of creator
    uint16_t machine;       // 32 : machine of creator
    uint16_t mextra;        // 34 : extra ID information (machine specific)
    uint16_t speed;         // 36 : CPU speed (approx)
    uint32_t timecreate;    // 38 : time to create archive
    uint16_t create_ver;    // 42 : version of creator
    uint16_t extract_ver;   // 44 : version needed to extract
    uint16_t disktype;      // 46 : disk type of archive
    uint16_t crunchmode;    // 48 : compression mode (generally) used
    uint16_t header_sum;    // 50 : header CRC
};

struct dms_track_header {
    uint16_t ID;            // 0  : 'TR'
    uint16_t track;         // 2  : track number -1=text
    uint16_t flags;         // 4  : general flags
    uint16_t pack_size;     // 6  : packed length
    uint16_t rle_size;      // 8  : rle-packed size
    uint16_t unpack_size;   // 10 : unpacked size
    uint8_t  pack_flag;     // 12 : general purpose compression flag
    uint8_t  pack_mode;     // 13 : compression mode
    uint16_t unpack_sum;    // 14 : unpacked CRC
    uint16_t data_sum;      // 16 : packed CRC
    uint16_t header_sum;    // 18 : header CRC
};

dms_archive_header read_archive_header(const uint8_t* src)
{
    dms_archive_header hdr;
    hdr.extra       = get_u32(&src[ 0]);
    hdr.general     = get_u32(&src[ 4]);
    hdr.timestamp   = get_u32(&src[ 8]);
    hdr.lowtrack    = get_u16(&src[12]);
    hdr.hightrack   = get_u16(&src[14]);
    hdr.pack_size   = get_u32(&src[16]);
    hdr.unpack_size = get_u32(&src[20]);
    hdr.serialno    = get_u32(&src[24]);
    hdr.cpu         = get_u16(&src[28]);
    hdr.copro       = get_u16(&src[30]);
    hdr.machine     = get_u16(&src[32]);
    hdr.mextra      = get_u16(&src[34]);
    hdr.speed       = get_u16(&src[36]);
    hdr.timecreate  = get_u32(&src[38]);
    hdr.create_ver  = get_u16(&src[42]);
    hdr.extract_ver = get_u16(&src[44]);
    hdr.disktype    = get_u16(&src[46]);
    hdr.crunchmode  = get_u16(&src[48]);
    hdr.header_sum  = get_u16(&src[50]);
    return hdr;
}

dms_track_header read_track_header(const uint8_t* src)
{
    dms_track_header hdr;
    hdr.ID          = get_u16(&src[ 0]);
    hdr.track       = get_u16(&src[ 2]);
    hdr.flags       = get_u16(&src[ 4]);
    hdr.pack_size   = get_u16(&src[ 6]);
    hdr.rle_size    = get_u16(&src[ 8]);
    hdr.unpack_size = get_u16(&src[10]);
    hdr.pack_flag   = src[12];
    hdr.pack_mode   = src[13];
    hdr.unpack_sum  = get_u16(&src[14]);
    hdr.data_sum    = get_u16(&src[16]);
    hdr.header_sum  = get_u16(&src[18]);
    return hdr;
}

uint16_t crc16(const uint8_t* src, uint32_t size)
{
    uint16_t crc = 0;
    constexpr uint16_t poly = 0xa001;
    while (size--) {
        crc ^= *src++;
        for (int i = 8; i--;) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
    }
    return crc;
}

uint16_t sum16(const uint8_t* src, uint32_t size)
{
    uint16_t sum = 0;
    while (size--)
        sum += *src++;
    return sum;
}

class invalid_dms_file : public std::runtime_error {
public:
    explicit invalid_dms_file(const std::string& msg) : std::runtime_error{ "Invalid DMS file: " + msg }
    {
    }
};

class bitbuf {
public:
    explicit bitbuf() = default;

    void init(const uint8_t* src, uint32_t src_size)
    {
        cur_ = src;
        end_ = src + src_size;
        bit_cnt_ = 0;
        drop(0);
    }

    void drop(uint8_t cnt)
    {
        assert(cnt <= bit_cnt_);
        bit_cnt_ -= cnt;
        bits_ &= (1 << bit_cnt_) - 1;
        while (bit_cnt_ < 16) {
            // Allow exactly two byte of "overrun"
            if (cur_ > end_ + 1) {
                throw invalid_dms_file { "Input overrun in bitbuf" };
            } else if (cur_ >= end_) {
                bits_ <<= 8;
                ++cur_;
            } else {
                bits_ = (bits_ << 8) | *cur_++;
            }
            bit_cnt_ += 8;
        }
    }

    uint16_t peek(uint8_t cnt)
    {
        assert(cnt <= bit_cnt_);
        return static_cast<uint16_t>(bits_ >> (bit_cnt_ - cnt));
    }

    uint16_t get(uint8_t cnt)
    {
        const auto res = peek(cnt);
        drop(cnt);
        return res;
    }

private:
    const uint8_t* cur_;
    const uint8_t* end_;
    uint32_t bits_;
    uint8_t bit_cnt_;
};

// Heavily based on:
//
//     xDMS  v1.3  -  Portable DMS archive unpacker  -  Public Domain
//     Written by     Andre Rodrigues de la Rocha  <adlroc@usa.net>
//
//     Lempel-Ziv-Huffman decompression functions used in Heavy 1 & 2 
//     compression modes. Based on LZH decompression functions from
//     UNIX LHA made by Masaru Oki
//

class hufftable {
public:
    static void make(uint16_t nchar, uint8_t* bitlen, uint16_t table_bits, uint16_t* table, uint16_t* left, uint16_t* right)
    {
        hufftable t { nchar, bitlen, table_bits, table, left, right };
        t.make_table(); // left subtree
        t.make_table(); // right subtree
        if (t.codeword != t.tblsiz)
            throw invalid_dms_file { "Error creating huffman table" };
    }

private:
    explicit hufftable(uint16_t nchar, uint8_t* bitlen, uint16_t table_bits, uint16_t* table, uint16_t* left, uint16_t* right)
    {
        n = avail = nchar;
        blen = bitlen;
        tbl = table;
        tblsiz = (uint16_t)(1 << table_bits);
        bit = (uint16_t)(tblsiz / 2);
        maxdepth = (uint16_t)(table_bits + 1);
        this->left = left;
        this->right = right;
    }

    uint16_t make_table()
    {
        uint16_t i = 0;
        if (len == depth) {
            while (++c < n)
                if (blen[c] == len) {
                    i = codeword;
                    codeword += bit;
                    if (codeword > tblsiz)
                        throw invalid_dms_file { "Error creating huffman table" };
                    while (i < codeword)
                        tbl[i++] = (uint16_t)c;
                    return (uint16_t)c;
                }
            c = 0xffff;
            len++;
            bit >>= 1;
        }
        depth++;
        if (depth < maxdepth) {
            make_table();
            make_table();
        } else if (depth > 32) {
            throw invalid_dms_file { "Error creating huffman table" };
        } else {
            if ((i = avail++) >= 2 * n - 1)
                throw invalid_dms_file { "Error creating huffman table" };
            left[i] = make_table();
            right[i] = make_table();
            if (codeword >= tblsiz)
                throw invalid_dms_file { "Error creating huffman table" };
            if (depth == maxdepth)
                tbl[codeword++] = i;
        }
        depth--;
        return i;
    }

    uint16_t c = 0xffff;
    uint16_t n, tblsiz, len = 1, depth = 1, maxdepth, avail;
    uint16_t codeword = 0, bit, *tbl;
    uint8_t* blen;
    uint16_t *left, *right;
};

class decruncher {
public:
    explicit decruncher() = default;

    void medium(const uint8_t* src, uint32_t src_size, uint8_t* output, uint16_t output_size)
    {
        bitbuf_.init(src, src_size);
        for (size_t i = 0; i < output_size;) {
            if (bitbuf_.get(1)) {
                // literal
                output[i++] = text_[medium_text_loc_++ & medium_bitmask] = static_cast<uint8_t>(bitbuf_.get(8));
            } else {
                // match
                uint16_t c = bitbuf_.get(8);
                uint16_t len = d_code[c] + 3;
                uint8_t u = d_len[c];
                c = (c << u | bitbuf_.get(u)) & 0xff;
                u = d_len[c];
                c = d_code[c] << 8 | ((c << u | bitbuf_.get(u)) & 0xff);
                uint16_t ofs = medium_text_loc_ - c - 1;
                if (i + len > output_size)
                    throw invalid_dms_file { "Output overrun" };
                while (len--)
                    output[i++] = text_[medium_text_loc_++ & medium_bitmask] = text_[ofs++ & medium_bitmask];
            }
        }
        medium_text_loc_ += 66;
    }

    void heavy(const uint8_t* src, uint32_t src_size, uint8_t* output, uint16_t output_size, bool init_table, bool heavy2)
    {
        bitbuf_.init(src, src_size);
        if (heavy2) {
            // 8KB dict
            np_ = 15;
            bitmask_ = 0x1fff;
        } else {
            // 4KB dict
            np_ = 14;
            bitmask_ = 0xfff;
        }

        if (init_table) {
            read_tree_c();
            read_tree_p();
        }

        for (size_t i = 0; i < output_size;) {
            const uint16_t c = decode_c();
            if (c < 256) {
                // Literal
                output[i++] = text_[heavy_text_loc_++ & bitmask_] = static_cast<uint8_t>(c);
            } else {
                // Copy match
                uint16_t len = c - OFFSET;
                uint16_t ofs = heavy_text_loc_ - decode_p() - 1;
                if (i + len > output_size)
                    throw invalid_dms_file {"Output overrun"};
                while (len--)
                    output[i++] = text_[heavy_text_loc_++ & bitmask_] = text_[ofs++ & bitmask_];
            }
        }
    }

    void reset()
    {
        memset(text_, 0, sizeof(text_));
        heavy_text_loc_ = 0;
        //quick_text_loc = 251;
        medium_text_loc_ = 0x3fbe;
        //deep_text_loc = 0x3fc4;
        //init_deep_tabs = 1;

    }

private:
    // Shared
    bitbuf bitbuf_;
    uint8_t text_[0x4000];

    // Medium decrunching
    static constexpr uint16_t medium_bitmask = 0x3fff;
    static const uint8_t d_code[256], d_len[256]; // also used for deep compression
    uint16_t medium_text_loc_ = 0;

    // Heavy decrunching
    static constexpr uint16_t NC = 510;
    static constexpr uint16_t NPT = 20;
    static constexpr uint16_t N1 = 510;
    static constexpr uint16_t OFFSET = 253;
    uint8_t np_;
    uint16_t bitmask_;
    uint8_t c_len_[NC];
    uint16_t c_table_[4096];
    uint8_t pt_len_[NPT];
    uint16_t pt_table_[256];
    uint16_t left_[2 * NC - 1], right_[2 * NC - 1 + 9];
    uint16_t heavy_text_loc_ = 0;
    uint16_t last_len_ = 0;

    void read_tree_c()
    {
        uint16_t n;

        n = bitbuf_.get(9);
        if (n > 0) {
            for (uint16_t i = 0; i < n; i++)
                c_len_[i] = static_cast<uint8_t>(bitbuf_.get(5));
            for (uint16_t i = n; i < 510; i++)
                c_len_[i] = 0;
            hufftable::make(510, c_len_, 12, c_table_, left_, right_);
        } else {
            n = bitbuf_.get(9);
            for (uint16_t i = 0; i < 510; i++)
                c_len_[i] = 0;
            for (uint16_t i = 0; i < 4096; i++)
                c_table_[i] = n;
        }
    }

    void read_tree_p()
    {
        uint16_t n;

        n = bitbuf_.get(5);
        if (n > 0) {
            for (uint16_t i = 0; i < n; i++) {
                pt_len_[i] = static_cast<uint8_t>(bitbuf_.get(4));
            }
            for (uint16_t i = n; i < np_; i++)
                pt_len_[i] = 0;
            hufftable::make(np_, pt_len_, 8, pt_table_, left_, right_);
        } else {
            n = bitbuf_.get(5);
            for (uint16_t i = 0; i < np_; i++)
                pt_len_[i] = 0;
            for (uint16_t i = 0; i < 256; i++)
                pt_table_[i] = n;
        }
    }

    uint16_t decode_c()
    {
        uint16_t i, j, m;

        j = c_table_[bitbuf_.peek(12)];
        if (j < N1) {
            bitbuf_.drop(c_len_[j]);
        } else {
            bitbuf_.drop(12);
            i = bitbuf_.peek(16);
            m = 0x8000;
            do {
                if (i & m)
                    j = right_[j];
                else
                    j = left_[j];
                m >>= 1;
            } while (j >= N1);
            bitbuf_.drop(c_len_[j] - 12);
        }
        return j;
    }

    uint16_t decode_p() 
    {
        uint16_t i, j, m;

        j = pt_table_[bitbuf_.peek(8)];
        if (j < np_) {
            bitbuf_.drop(pt_len_[j]);
        } else {
            bitbuf_.drop(8);
            i = bitbuf_.peek(16);
            m = 0x8000;
            do {
                if (i & m)
                    j = right_[j];
                else
                    j = left_[j];
                m >>= 1;
            } while (j >= np_);
            bitbuf_.drop(pt_len_[j] - 8);
        }

        if (j != np_ - 1) {
            if (j > 0) {
                i = j - 1;
                j = bitbuf_.peek(static_cast<uint8_t>(i)) | (1U << (j - 1));
                bitbuf_.drop(static_cast<uint8_t>(i));
            }
            last_len_ = j;
        }

        return last_len_;
    }
};

std::vector<uint8_t> rle_decode(const uint8_t* input, size_t input_size)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < input_size;) {
        uint8_t a = input[i++];
        if (a != 0x90) {
            out.push_back(a);
            continue;
        }
        if (i == input_size)
            throw invalid_dms_file { "RLE decode failed" };
        uint16_t b = input[i++];
        if (!b) {
            out.push_back(a);
            continue;
        }
        if (i == input_size)
            throw invalid_dms_file { "RLE decode failed" };
        a = input[i++];
        if (b == 0xff) {
            if (i + 1 >= input_size)
                throw invalid_dms_file { "RLE decode failed" };
            b = get_u16(&input[i]);
            i += 2;
        }
        while (b--)
            out.push_back(a);
    }

    return out;
}

const uint8_t decruncher::d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
    0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
    0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
    0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

const uint8_t decruncher::d_len[256] = {
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};

} // unnamed namespace


bool dms_detect(const std::vector<uint8_t>& data)
{
    return data.size() >= dms_archive_header_offset + dms_archive_header_size && get_u32(&data[0]) == dms_header_id;
}

std::vector<uint8_t> dms_unpack(const std::vector<uint8_t>& data)
{
    if (!dms_detect(data))
        throw invalid_dms_file{"Header invalid"};

    const auto hdr = read_archive_header(&data[dms_archive_header_offset]);
    if (crc16(&data[dms_archive_header_offset], dms_archive_header_size - 2) != hdr.header_sum) {
        throw invalid_dms_file{"Header CRC mismatch"};
    }

#ifdef DMS_TRACE
#define PR(f) std::cout << std::left << std::setw(32) << #f << "\t$" << hexfmt(hdr.f) << "\n"
    PR(extra);
    PR(general);
    PR(timestamp);
    PR(lowtrack);
    PR(hightrack);
    PR(pack_size);
    PR(unpack_size);
    PR(serialno);
    PR(cpu);
    PR(copro);
    PR(machine);
    PR(mextra);
    PR(speed);
    PR(timecreate);
    PR(create_ver);
    PR(extract_ver);
    PR(disktype);
    PR(crunchmode);
    PR(header_sum);
#undef PR
    std::cout << "Track Flags Psize RSize USize PF  PM  USum  DSum\n";
#endif
    decruncher decrunch;
    decrunch.reset();

    constexpr uint16_t track_size = 11 * 512 * 2;

    std::vector<uint8_t> res;
    res.resize(track_size * (hdr.hightrack + 1));

    std::vector<uint8_t> track_data;
    for (uint32_t offset = dms_archive_header_size + dms_archive_header_offset; offset < data.size();) {
        if (offset + dms_track_header_size > data.size())
            throw invalid_dms_file{"End of file while reading track header"};
        const auto tr_hdr = read_track_header(&data[offset]);
        if (tr_hdr.ID != dms_track_header_id)
            throw invalid_dms_file{"Invalid track header ID"};
        if (crc16(&data[offset], dms_track_header_size - 2) != tr_hdr.header_sum)
            throw invalid_dms_file{"Track header CRC mismatch"};

#ifdef DMS_TRACE
        std::cout << "$" << hexfmt(tr_hdr.track) << " $" << hexfmt(tr_hdr.flags) << " $" << hexfmt(tr_hdr.pack_size) << " $" << hexfmt(tr_hdr.rle_size) << " $" << hexfmt(tr_hdr.unpack_size) << " $" << hexfmt(tr_hdr.pack_flag) << " $" << hexfmt(tr_hdr.pack_mode) << " $" << hexfmt(tr_hdr.unpack_sum) << " $" << hexfmt(tr_hdr.data_sum) << "\n";
#endif
        offset += dms_track_header_size;

        if (offset + tr_hdr.pack_size > data.size())
            throw invalid_dms_file { "End of file while reading track packed data" };

        if (crc16(&data[offset], tr_hdr.pack_size) != tr_hdr.data_sum)
            throw invalid_dms_file { "CRC mismatch for packed data" };

        track_data.resize(tr_hdr.unpack_size);

        switch (tr_hdr.pack_mode) {
        case 0: // Store
            if (tr_hdr.unpack_size != tr_hdr.pack_size)
                throw invalid_dms_file { "Invalid unpack size for store" };
            memcpy(&track_data[0], &data[offset], tr_hdr.unpack_size);
            break;
        case 1: // Simple (= RLE only)
            if (tr_hdr.pack_size != tr_hdr.rle_size)
                throw invalid_dms_file { "Invalid rle/pack size for simple crunch mode" };
            track_data = rle_decode(&data[offset], tr_hdr.rle_size);
            break;
        case 3: // Medium
            track_data.resize(tr_hdr.rle_size);
            decrunch.medium(&data[offset], tr_hdr.pack_size, &track_data[0], tr_hdr.rle_size);
            track_data = rle_decode(track_data.data(), track_data.size());
            break;
        case 5: // Heavy1
        case 6: // Heavy2
            track_data.resize(tr_hdr.rle_size);
            decrunch.heavy(&data[offset], tr_hdr.pack_size, &track_data[0], tr_hdr.rle_size, !!(tr_hdr.pack_flag & 2), tr_hdr.pack_mode == 6);
            if (tr_hdr.pack_flag & 4)
                track_data = rle_decode(track_data.data(), track_data.size());
            break;
        default:
            throw invalid_dms_file { "Unsupported DMS packing method $" + hexstring(tr_hdr.pack_mode) };
        }

        if (track_data.size() != tr_hdr.unpack_size)
            throw invalid_dms_file { "Track decrunch failed" };

        if (sum16(track_data.data(), static_cast<uint32_t>(track_data.size())) != tr_hdr.unpack_sum)
            throw invalid_dms_file { "Track checksum invalid" };

        if (!(tr_hdr.pack_flag & 1))
            decrunch.reset();

        // Track 0 of size 1024 is also a banner track
        if (tr_hdr.track == 0xffff || (tr_hdr.unpack_size == 1024 && tr_hdr.track == 0)) {
#ifdef DMS_TRACE
            std::cout << "Skipping track $" << hexfmt(tr_hdr.track) << " - Banner track\n";
            hexdump(std::cout, track_data.data(), track_data.size());
#endif
        } else {
            if (tr_hdr.track > hdr.hightrack)
                throw invalid_dms_file { "Track $" + hexstring(tr_hdr.track) + " is out range $" + hexstring(hdr.hightrack) };

            if (tr_hdr.unpack_size != track_size)
                throw invalid_dms_file { "Track $" + hexstring(tr_hdr.track) + " has invalid size $" + hexstring(tr_hdr.unpack_size) };

            memcpy(&res[tr_hdr.track * track_size], track_data.data(), track_size);
        }

        offset += tr_hdr.pack_size;
    }

    return res;
}
