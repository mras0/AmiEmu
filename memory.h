#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <vector>

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
    uint32_t read_u32(uint32_t addr, uint32_t offset)
    {
        const uint32_t hi = read_u16(addr, offset) << 16;        
        return hi | read_u16(addr + 2, offset + 2);
    }

    virtual void write_u8(uint32_t addr, uint32_t offset, uint8_t val) = 0;
    virtual void write_u16(uint32_t addr, uint32_t offset, uint16_t val) = 0;
    void write_u32(uint32_t addr, uint32_t offset, uint32_t val)
    {
        write_u16(addr, offset, val >> 16);
        write_u16(addr + 2, offset + 2, val & 0xffff);
    }
};

class default_handler : public memory_area_handler {
public:
    uint8_t read_u8(uint32_t addr, uint32_t) override;
    uint16_t read_u16(uint32_t addr, uint32_t) override;
    void write_u8(uint32_t addr, uint32_t, uint8_t val) override;
    void write_u16(uint32_t addr, uint32_t, uint16_t val) override;
};

class ram_handler : public memory_area_handler {
public:
    explicit ram_handler(uint32_t size);
    std::vector<uint8_t>& ram()
    {
        return ram_;
    }

    uint8_t read_u8(uint32_t, uint32_t offset) override;
    uint16_t read_u16(uint32_t, uint32_t offset) override;
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override;
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override;

private:
    std::vector<uint8_t> ram_;
};

class rom_area_handler : public memory_area_handler {
public:
    explicit rom_area_handler(class memory_handler& mem_handler, std::vector<uint8_t>&& data);

    const std::vector<uint8_t>& rom() const
    {
        return rom_data_;
    }

    void set_overlay(bool ovl);

    uint8_t read_u8(uint32_t, uint32_t offset) override;
    uint16_t read_u16(uint32_t, uint32_t offset) override;
    void write_u8(uint32_t addr, uint32_t offset, uint8_t val) override;
    void write_u16(uint32_t addr, uint32_t offset, uint16_t val) override;

private:
    memory_handler& mem_handler_;
    std::vector<uint8_t> rom_data_;
};

class memory_handler {
public:
    explicit memory_handler(uint32_t ram_size);

    memory_handler(const memory_handler&) = delete;
    memory_handler& operator=(const memory_handler&) = delete;

    std::vector<uint8_t>& ram()
    {
        return ram_.ram();
    }

    void register_handler(memory_area_handler& h, uint32_t base, uint32_t len);
    void unregister_handler(memory_area_handler& h, uint32_t base, uint32_t len);

    uint8_t read_u8(uint32_t addr);
    uint16_t read_u16(uint32_t addr);
    uint32_t read_u32(uint32_t addr);

    void write_u8(uint32_t addr, uint8_t val);
    void write_u16(uint32_t addr, uint16_t val);
    void write_u32(uint32_t addr, uint32_t val);

private:
    struct area {
        uint32_t base;
        uint32_t len;
        memory_area_handler* handler;
    };
    std::vector<area> areas_;
    default_handler def_handler_;
    ram_handler ram_;
    area def_area_ { 0, 1U << 24, &def_handler_ };
    area ram_area_;

    area& find_area(uint32_t& addr);
};

#endif
