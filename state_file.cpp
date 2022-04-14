#include "state_file.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

constexpr uint32_t marker_scope_start = 0;
constexpr uint32_t marker_scope_end   = 1;
constexpr uint32_t marker_u8          = 100;
constexpr uint32_t marker_u16         = 101;
constexpr uint32_t marker_u32         = 102;
constexpr uint32_t marker_bool        = 103;
constexpr uint32_t marker_blob        = 200;
constexpr uint32_t marker_string      = 300;
constexpr uint32_t marker_vec_u8      = 400;
constexpr uint32_t marker_vec_string  = 500;
}

class state_file::impl {
public:
    explicit impl(dir d, const std::string& filename)
        : dir_ { d }
        , filename_ { filename }
        , f_ { filename, std::ios::binary | (d == dir::load ? std::ios::in : std::ios::out) }
    {
        if (!f_ || !f_.is_open())
            throw std::runtime_error { "Error opening " + filename };
    }

    ~impl()
    {
        assert(dir_ != dir::load || !f_.rdbuf()->in_avail() || std::uncaught_exceptions());
    }

    bool loading() const
    {
        return dir_ == dir::load;
    }

    uint32_t open_scope(const char* id, uint32_t version)
    {
        if (dir_ == dir::save) {
            //std::cout << "Saving " << id << " version " << version << "\n";
            put_u32(marker_scope_start);
            put_string(id);
            put_u32(version);
            auto p = ppos();
            put_u32(0); // size for later
            return p;
        } else {
            expect_marker(marker_scope_start);
            const std::string actual_id = get_string();
            if (id != actual_id)
                throw std::runtime_error { filename_ + ": Expected scope \"" + id + "\" got \"" + actual_id + "\"" };
            const auto ver = get_u32();
            if (ver != version)
                throw std::runtime_error { filename_ + ": Expected version " + std::to_string(version) + " got " + std::to_string(ver) + " for scope " + id };
            get_u32(); // Ignore length for now
            //std::cout << "Restoring " << id << " version " << version << "\n";
            return 0;
        }
    }

    void close_scope(uint32_t p)
    {
        if (dir_ == dir::save) {
            assert(p <= ppos());
            put_u32(marker_scope_end);
            const auto saved = ppos();
            f_.seekp(p);
            put_u32(static_cast<uint32_t>(saved - p));
            f_.seekp(saved);
        } else {
            if (!std::uncaught_exceptions())
                expect_marker(marker_scope_end);
        }
    }

    void handle(std::string& s)
    {
        if (dir_ == dir::save) {
            put_u32(marker_string);
            put_string(s);
        } else {
            expect_marker(marker_string);
            s = get_string();
        }
    }

    void handle(std::vector<std::string>& vec)
    {
        if (dir_ == dir::save)
            put_vector(marker_vec_string, vec);
        else
            get_vector(marker_vec_string, vec);
    }

    void handle(std::vector<uint8_t>& vec)
    {
        if (dir_ == dir::save)
            put_vector(marker_vec_u8, vec);
        else
            get_vector(marker_vec_u8, vec);
    }

    void handle(bool& b)
    {
        if (dir_ == dir::save) {
            put_u32(marker_bool);
            put_u8(b);
        } else {
            expect_marker(marker_bool);
            b = !!get_u8();
        }
    }

    void handle(uint8_t& num)
    {
        if (dir_ == dir::save) {
            put_u32(marker_u8);
            put_u8(num);
        } else {
            expect_marker(marker_u8);
            num = get_u8();
        }
    }

    void handle(uint16_t& num)
    {
        if (dir_ == dir::save) {
            put_u32(marker_u16);
            put_u16(num);
        } else {
            expect_marker(marker_u16);
            num = get_u16();
        }
    }

    void handle(uint32_t& num)
    {
        if (dir_ == dir::save) {
            put_u32(marker_u32);
            put_u32(num);
        } else {
            expect_marker(marker_u32);
            num = get_u32();
        }
    }

    void handle_blob(void* blob, uint32_t size)
    {
        if (dir_ == dir::save) {
            put_u32(marker_blob);
            put_u32(size);
            f_.write(static_cast<const char*>(blob), size);
        } else {
            expect_marker(marker_blob);
            const auto actual_size = get_u32();
            if (size != actual_size)
                throw std::runtime_error { filename_ + ": Expected blob size " + std::to_string(size) + " got " + std::to_string(actual_size) };
            f_.read(static_cast<char*>(blob), size);
        }
    }

private:
    dir dir_;
    const std::string filename_;
    std::fstream f_;

    uint32_t ppos()
    {
        return static_cast<uint32_t>(f_.tellp());
    }

    void expect_marker(uint32_t marker)
    {
        const auto m = get_u32();
        if (m != marker)
            throw std::runtime_error { filename_ + ": Failed to load. Expected marker " + std::to_string(marker) + " got " + std::to_string(m) };
    }

    void put_u8(uint8_t p)
    {
        f_.put(p);
    }

    uint8_t get_u8()
    {
        return static_cast<uint8_t>(f_.get());
    }

    void put_u16(uint16_t p)
    {
        f_.write(reinterpret_cast<const char*>(&p), sizeof(p));
    }

    uint16_t get_u16()
    {
        uint16_t n;
        f_.read(reinterpret_cast<char*>(&n), sizeof(n));
        return n;
    }

    void put_u32(uint32_t p)
    {
        f_.write(reinterpret_cast<const char*>(&p), sizeof(p));
    }

    uint32_t get_u32()
    {
        uint32_t n;
        f_.read(reinterpret_cast<char*>(&n), sizeof(n));
        return n;
    }

    void put_string(const std::string_view& s)
    {
        put_u32(static_cast<uint32_t>(s.length()));
        f_.write(s.data(), s.size());
    }

    std::string get_string()
    {
        std::string s(get_u32(), '\0');
        f_.read(&s[0], s.size());
        return s;
    }

    template<typename T>
    void put_vector(uint32_t marker, const std::vector<T>& v)
    {
        put_u32(marker);
        put_u32(static_cast<uint32_t>(v.size()));
        f_.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
    }

    template <typename T>
    void get_vector(uint32_t marker, std::vector<T>& v)
    {
        expect_marker(marker);
        v.resize(get_u32());
        f_.read(reinterpret_cast<char*>(v.data()), v.size() * sizeof(T));
    }
};

state_file::state_file(dir d, const std::string& filename)
    : impl_ { new impl { d, filename } }
{
}

state_file::~state_file() = default;

bool state_file::loading() const
{
    return impl_->loading();
}

    uint32_t state_file::open_scope(const char* id, uint32_t version)
{
    return impl_->open_scope(id, version);
}

void state_file::close_scope(uint32_t pos)
{
    impl_->close_scope(pos);
}

void state_file::handle(std::string& s)
{
    impl_->handle(s);
}

void state_file::handle(std::vector<std::string>& vec)
{
    impl_->handle(vec);
}

void state_file::handle(std::vector<uint8_t>& vec)
{
    impl_->handle(vec);
}

void state_file::handle_blob(void* blob, uint32_t size)
{
    impl_->handle_blob(blob, size);
}

void state_file::handle(bool& b)
{
    impl_->handle(b);
}

void state_file::handle(uint8_t& num)
{
    impl_->handle(num);
}

void state_file::handle(uint16_t& num)
{
    impl_->handle(num);
}

void state_file::handle(uint32_t& num)
{
    impl_->handle(num);
}