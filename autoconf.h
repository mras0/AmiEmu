#ifndef AUTOCONF_H_INCLUDED
#define AUTOCONF_H_INCLUDED

#include "memory.h"

class autoconf_device {
public:
    static constexpr uint8_t ERT_ZORROII        = 0xc0;
    static constexpr uint8_t ERTF_MEMLIST       = 1 << 5;
    static constexpr uint8_t ERTF_DIAGVALID     = 1 << 4;
    static constexpr uint8_t ERTF_CHAINEDCONFIG = 1 << 3;

    struct board_config {
        uint8_t type;
        uint32_t size;
        uint8_t product_number;
        uint16_t hw_manufacturer;
        uint32_t serial_no;
        uint16_t rom_vector_offset;
    };

    uint32_t base_address() const
    {
        return mode_ == mode::active ? base_ << 16 : 0;
    }

protected:
    explicit autoconf_device(memory_handler& mem_handler, memory_area_handler& area_handler, const board_config& config);

private:
    memory_handler& mem_handler_;
    memory_area_handler& area_handler_;
    const board_config config_;
    uint8_t conf_data_[12];
    enum class mode : uint8_t { autoconf, shutup, active } mode_ = mode::autoconf;
    uint8_t base_;

    std::string desc() const;

    friend class autoconf_handler;

    static uint8_t board_size(uint32_t size);
    uint8_t read_config_byte(uint8_t offset) const;
    void write_config_byte(uint8_t offset, uint8_t val);
    void shutup();
    void activate(uint8_t base);
    void config_mode();
    void handle_autoconf_state(state_file& sf);
    virtual void handle_state(state_file& sf) = 0;
};

class fastmem_handler : public autoconf_device, public ram_handler {
public:
    explicit fastmem_handler(memory_handler& mem_handler, uint32_t size)
        : autoconf_device { mem_handler, *this, make_config(size) }
        , ram_handler { size }
    {
    }

private:
    static constexpr board_config make_config(uint32_t size)
    {
        return board_config {
            .type = ERTF_MEMLIST,
            .size = size,
            .product_number = 0x12,
            .hw_manufacturer = 0x1234,
            .serial_no = 0x12345678,
            .rom_vector_offset = 0,
        };
    }

    void handle_state(state_file& sf)
    {
        ram_handler::handle_state(sf);
    }
};

class autoconf_handler : public memory_area_handler {
public:
    explicit autoconf_handler(memory_handler& mem_handler);

    void add_device(autoconf_device& dev);
    void handle_state(state_file& sf);

private:
    static constexpr uint32_t base = 0xe80000;
    std::vector<autoconf_device*> devices_;
    std::vector<autoconf_device*> configured_devices_;
    uint8_t low_addr_hold_ = 0;
    bool has_low_addr_ = 0;

    void device_configured();

    uint8_t read_u8(uint32_t, uint32_t offset) override;
    uint16_t read_u16(uint32_t addr, uint32_t offset) override;
    void write_u8(uint32_t, uint32_t offset, uint8_t val) override;
    void write_u16(uint32_t, uint32_t offset, uint16_t val) override;
    void reset() override;
};

#endif
