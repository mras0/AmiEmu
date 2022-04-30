#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <vector>
#include <functional>
#include <cassert>

constexpr uint32_t slow_base = 0xc00000;
constexpr uint32_t fast_base = 0x200000;
constexpr uint32_t max_chip_size = fast_base;
constexpr uint32_t max_fast_size = 0x800000;
constexpr uint32_t max_slow_size = 0xd80000 - slow_base;

class state_file;
class memory_handler;

constexpr uint16_t get_u16(const uint8_t* d)
{
    return d[0] << 8 | d[1];
}

constexpr uint32_t get_u32(const uint8_t* d)
{
    return static_cast<uint32_t>(d[0]) << 24 | d[1] << 16 | d[2] << 8 | d[3];
}

constexpr void put_u16(uint8_t* d, uint16_t val)
{
    d[0] = static_cast<uint8_t>(val >> 8);
    d[1] = static_cast<uint8_t>(val);
}

constexpr void put_u32(uint8_t* d, uint32_t val)
{
    d[0] = static_cast<uint8_t>(val >> 24);
    d[1] = static_cast<uint8_t>(val >> 16);
    d[2] = static_cast<uint8_t>(val >> 8);
    d[3] = static_cast<uint8_t>(val);
}

class memory_area_handler {
public:
    virtual uint8_t read_u8(uint32_t addr, uint32_t offset) = 0;
    virtual uint16_t read_u16(uint32_t addr, uint32_t offset) = 0;
    virtual void write_u8(uint32_t addr, uint32_t offset, uint8_t val) = 0;
    virtual void write_u16(uint32_t addr, uint32_t offset, uint16_t val) = 0;

    virtual void reset() = 0;
};

class default_handler : public memory_area_handler {
public:
    explicit default_handler(memory_handler& mem_handler)
        : mem_handler_ { mem_handler }
    {
    }

    uint8_t read_u8(uint32_t addr, uint32_t) override;
    uint16_t read_u16(uint32_t addr, uint32_t) override;
    void write_u8(uint32_t addr, uint32_t, uint8_t val) override;
    void write_u16(uint32_t addr, uint32_t, uint16_t val) override;
    void reset() override { }

private:
    memory_handler& mem_handler_;
};

class ram_handler : public memory_area_handler {
public:
    explicit ram_handler(uint32_t size);
    virtual ~ram_handler() = default;
    std::vector<uint8_t>& ram()
    {
        return ram_;
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override;
    uint16_t read_u16(uint32_t, uint32_t offset) override;
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override;
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override;
    void reset() override { }
    void handle_state(state_file& sf);

private:
    std::vector<uint8_t> ram_;
};

class rom_area_handler : public memory_area_handler {
public:
    explicit rom_area_handler(memory_handler& mem_handler, std::vector<uint8_t>&& data);

    const std::vector<uint8_t>& rom() const
    {
        return rom_data_;
    }

    void set_overlay(bool ovl);

    uint8_t read_u8(uint32_t, uint32_t offset) override;
    uint16_t read_u16(uint32_t, uint32_t offset) override;
    void write_u8(uint32_t addr, uint32_t offset, uint8_t val) override;
    void write_u16(uint32_t addr, uint32_t offset, uint16_t val) override;
    void reset() override {
        write_protect_ = false;
    }

private:
    memory_handler& mem_handler_;
    std::vector<uint8_t> rom_data_;
    std::vector<uint8_t> wom_;
    bool ovl_ = false;
    bool write_protect_ = false;
};

class memory_handler {
public:
    explicit memory_handler(uint32_t ram_size);

    memory_handler(const memory_handler&) = delete;
    memory_handler& operator=(const memory_handler&) = delete;

    using memory_interceptor = std::function<void (uint32_t addr, uint32_t data, uint8_t size, bool write)>;

    std::vector<uint8_t>& ram()
    {
        return ram_.ram();
    }

    void set_memory_interceptor(const memory_interceptor& interceptor)
    {
        assert(!memory_interceptor_);
        memory_interceptor_ = interceptor;
    }

    void set_illegal_access_handler(const memory_interceptor& handler)
    {
        assert(!illegal_access_handler_);
        illegal_access_handler_ = handler;
    }

    void register_handler(memory_area_handler& h, uint32_t base, uint32_t len);
    void unregister_handler(memory_area_handler& h, uint32_t base, uint32_t len);

    uint8_t read_u8(uint32_t addr);
    uint16_t read_u16(uint32_t addr);
    uint32_t read_u32(uint32_t addr);

    uint16_t hack_peek_u16(uint32_t addr); // Avoid memory interceptor (!), read must not have side effect and must be properly aligned

    void write_u8(uint32_t addr, uint8_t val);
    void write_u16(uint32_t addr, uint16_t val);
    void write_u32(uint32_t addr, uint32_t val);

    void reset();
    void handle_state(state_file& sf);

    void signal_illegal_access(uint32_t addr, uint32_t data, uint8_t size, bool write)
    {
        if (illegal_access_handler_)
            illegal_access_handler_(addr, data, size, write);
    }

private:
    struct area {
        uint32_t base;
        uint32_t len;
        memory_area_handler* handler;
    };
    std::vector<area> areas_;
    default_handler def_handler_ { *this };
    ram_handler ram_;
    area def_area_ { 0, 1U << 24, &def_handler_ };
    area ram_area_;
    memory_interceptor memory_interceptor_;
    memory_interceptor illegal_access_handler_;

    area& find_area(uint32_t& addr);
    void track(uint32_t addr, uint32_t data, uint8_t size, bool write)
    {
        if (memory_interceptor_)
            memory_interceptor_(addr, data, size, write);
    }
};

#endif
