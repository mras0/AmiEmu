#include "autoconf.h"
#include "ioutil.h"
#include <cassert>
#include <stdexcept>
#include <iostream>

autoconf_device::autoconf_device(memory_handler& mem_handler, memory_area_handler& area_handler, const board_config& config)
    : mem_handler_ { mem_handler }
    , area_handler_ { area_handler }
    , config_ { config }
{
    memset(conf_data_, 0, sizeof(conf_data_));
    assert((config.type & 0xc7) == 0);
    /* $00/$02 */ conf_data_[0] = ERT_ZORROII | config.type | board_size(config.size);
    /* $04/$06 */ conf_data_[1] = config.product_number;
    /* $10-$18 */ put_u16(&conf_data_[4], config.hw_manufacturer);
    /* $18-$28 */ put_u32(&conf_data_[6], config.serial_no);
    /* $28-$30 */ put_u16(&conf_data_[10], config.rom_vector_offset);
}

uint8_t autoconf_device::read_config_byte(uint8_t offset) const
{
    assert(offset < sizeof(conf_data_));
    assert(mode_ == mode::autoconf);
    return conf_data_[offset];
}

void autoconf_device::write_config_byte(uint8_t offset, uint8_t val)
{
    assert(mode_ == mode::autoconf);
    throw std::runtime_error { "TODO: Write config byte $" + hexstring(offset) + " val $" + hexstring(val) };
}

void autoconf_device::shutup()
{
    assert(mode_ == mode::autoconf);
    std::cout << "[AUTOCONF] " << desc() << " shutting up\n";
    mode_ = mode::shutup;
}

void autoconf_device::activate(uint8_t base)
{
    assert(mode_ == mode::autoconf);
    mode_ = mode::active;
    std::cout << "[AUTOCONF] " << desc() << " activating at $" << hexfmt(base << 16, 8) << "\n";
    mem_handler_.register_handler(area_handler_, base << 16, config_.size);
}


std::string autoconf_device::desc() const
{
    return hexstring(config_.product_number) + "/" + hexstring(config_.hw_manufacturer) + "/" + hexstring(config_.serial_no);
}

const uint8_t autoconf_device::board_size(uint32_t size)
{
    switch (size) {
    case 64 << 10:
        return 0b001;
    case 128 << 10:
        return 0b010;
    case 256 << 10:
        return 0b011;
    case 512 << 10:
        return 0b100;
    case 1 << 20:
        return 0b101;
    case 2 << 20:
        return 0b110;
    case 4 << 20:
        return 0b111;
    case 8 << 20:
        return 0b000;
    default:
        throw std::runtime_error { "Unsupported autoconf board size $" + hexstring(size) };
    }
}

autoconf_handler::autoconf_handler(memory_handler& mem_handler)
{
    mem_handler.register_handler(*this, base, 0x10000);
}

void autoconf_handler::add_device(autoconf_device& dev)
{
    devices_.push_back(&dev);
}

void autoconf_handler::remove_device()
{
    assert(!devices_.empty());
    has_low_addr_ = false;
    devices_.pop_back();
}

uint8_t autoconf_handler::read_u8(uint32_t, uint32_t offset)
{
    if (!(offset & 1)) {
        if (offset < 0x30) {
            if (devices_.empty()) {
                return 0xff;
            }
            auto b = devices_.back()->read_config_byte(static_cast<uint8_t>(offset >> 2));
            if (offset & 2)
                b <<= 4;
            else
                b &= 0xf0;
            return offset < 4 ? b : static_cast<uint8_t>(~b);
        } else if (offset < 0x40) {
            return 0xff;
        } else if (offset == 0x40 || offset == 0x42) {
            // Interrupt pending register - Not inverted
            return 0;
        }
    }

    std::cerr << "[AUTOCONF] Unhandled read offset $" << hexfmt(offset) << "\n";
    return 0xff;
}

uint16_t autoconf_handler::read_u16(uint32_t addr, uint32_t offset)
{
    return read_u8(addr, offset) << 8 | read_u8(addr, offset + 1);
}

void autoconf_handler::write_u8(uint32_t, uint32_t offset, uint8_t val)
{
    if (!devices_.empty()) {
        auto& dev = *devices_.back();
        if (offset == 0x48) {
            if (!has_low_addr_)
                std::cerr << "[AUTOCONF] Warning high address written without low address val=$" << hexfmt(val) << "\n";
            dev.activate(static_cast<uint16_t>((val & 0xf0) | low_addr_hold_));
            remove_device();
            return;
        } else if (offset == 0x4a) {
            if (has_low_addr_)
                std::cerr << "[AUTOCONF] Warning already has low address ($" << hexfmt(low_addr_hold_) << ") got $" << hexfmt(val) << "\n";
            has_low_addr_ = true;
            low_addr_hold_ = val >> 4;
            return;
        } else if (offset == 0x4c) {
            // shutup
            dev.shutup();
            remove_device();
            return;
        }
    } else {
        std::cerr << "[AUTCONF] Write without device\n";
    }

    std::cerr << "[AUTOCONF] Unhandled write offset $" << hexfmt(offset) << " val=$" << hexfmt(val) << "\n";
}

void autoconf_handler::write_u16(uint32_t, uint32_t offset, uint16_t val)
{
    std::cerr << "[AUTOCONF] Unhandled write offset $" << hexfmt(offset) << " val=$" << hexfmt(val) << "\n";
}
