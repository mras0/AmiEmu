#include "memory.h"
#include "ioutil.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

uint8_t default_handler::read_u8(uint32_t addr, uint32_t)
{
    // Don't warn a bunch when scanning for ROM tags / I/O space
    if (addr < 0xe00000) std::cerr << "[MEM] Unhandled byte read from $" << hexfmt(addr) << "\n";
    //return 0xff;
    return 0;
}
uint16_t default_handler::read_u16(uint32_t addr, uint32_t)
{
    // Don't warn a bunch when scanning for ROM tags / I/O space
    if (addr < 0xe00000) std::cerr << "[MEM] Unhandled word read from $" << hexfmt(addr) << "\n";
    //return 0xffff;
    return 0;
}
void default_handler::write_u8(uint32_t addr, uint32_t, uint8_t val)
{
    std::cerr << "[MEM] Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
}
void default_handler::write_u16(uint32_t addr, uint32_t, uint16_t val)
{
    std::cerr << "[MEM] Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
}

ram_handler::ram_handler(uint32_t size)
{
    ram_.resize(size);
}

uint8_t ram_handler::read_u8(uint32_t, uint32_t offset)
{
    assert(offset < ram_.size());
    return ram_[offset];
}

uint16_t ram_handler::read_u16(uint32_t, uint32_t offset)
{
    assert(offset < ram_.size() - 1);
    return get_u16(&ram_[offset]);
}

//#define WATCH
#ifdef WATCH
const uint32_t watch_start = 0x00c047bc;
const uint32_t watch_end = watch_start + 4;
#endif

void ram_handler::write_u8([[maybe_unused]] uint32_t addr, uint32_t offset, uint8_t val)
{
    assert(offset < ram_.size());

#ifdef WATCH
    if (addr >= watch_start && addr < watch_end) {
        std::cout << "Write to $" << hexfmt(addr) << " = $" << hexfmt(val) << "\n";
    }
#endif
    ram_[offset] = val;
}

void ram_handler::write_u16([[maybe_unused]] uint32_t addr, uint32_t offset, uint16_t val)
{
    assert(offset < ram_.size() - 1);

#ifdef WATCH
    if (addr >= watch_start && addr < watch_end) {
        std::cout << "Write to $" << hexfmt(addr) << " = $" << hexfmt(val) << "\n";
        if (val)
            throw std::runtime_error { "FIXME" };
    }
#endif

    put_u16(&ram_[offset], val);
}

memory_handler::memory_handler(uint32_t ram_size)
    : ram_ { ram_size }
    , ram_area_ { 0, ram_size, &ram_ }
{
}

rom_area_handler::rom_area_handler(memory_handler& mem_handler, std::vector<uint8_t>&& data)
    : mem_handler_ { mem_handler }
    , rom_data_ { std::move(data) }
{
    const auto size = static_cast<uint32_t>(rom_data_.size());
    if (size != 256 * 1024 && size != 512 * 1024 && size != 1024 * 1024) {
        throw std::runtime_error { "Unexpected size of ROM: $" + hexstring(size) };
    }

    if (size == 1024 * 1024) {
        mem_handler_.register_handler(*this, 0xe00000, size); // Extension ROM needs to be at 0xe00000?
        mem_handler_.register_handler(*this, 0xf00000, size);
        return;
    }
    mem_handler_.register_handler(*this, 0xf80000, size);
    if (rom_data_.size() != 512 * 1024)
        mem_handler_.register_handler(*this, 0xfc0000, size);
}

void rom_area_handler::set_overlay(bool ovl)
{
    const auto size = static_cast<uint32_t>(rom_data_.size());
    std::cerr << "[ROM handler] Turning overlay " << (ovl ? "on" : "off") << "\n";
    if (ovl)
        mem_handler_.register_handler(*this, 0, size);
    else
        mem_handler_.unregister_handler(*this, 0, size);
}

uint8_t rom_area_handler::read_u8(uint32_t, uint32_t offset)
{
    assert(offset < rom_data_.size());
    return rom_data_[offset];
}

uint16_t rom_area_handler::read_u16(uint32_t, uint32_t offset)
{
    assert(offset < rom_data_.size() - 1);
    return get_u16(&rom_data_[offset]);
}

void rom_area_handler::write_u8(uint32_t addr, uint32_t offset, uint8_t val)
{
    std::cerr << "[MEM] Write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
    throw std::runtime_error { "FIXME" };
}

void rom_area_handler::write_u16(uint32_t addr, uint32_t offset, uint16_t val)
{
    std::cerr << "[MEM] Write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
    throw std::runtime_error { "FIXME" };
}

void memory_handler::register_handler(memory_area_handler& h, uint32_t base, uint32_t len)
{
    auto a = &find_area(base);
    assert(a == &def_area_ || a == &ram_area_);
    (void)a;
    (void)len;
    areas_.push_back(area {
        base, len, &h });
}

void memory_handler::unregister_handler(memory_area_handler& h, uint32_t base, uint32_t len)
{
    auto& a = find_area(base);
    assert(a.base == base && a.len == len && a.handler == &h);
    (void)h;
    (void)a;
    (void)len;
    areas_.erase(areas_.begin() + (&a - &areas_[0]));
}

uint8_t memory_handler::read_u8(uint32_t addr)
{
    auto& a = find_area(addr);
    const uint8_t data = a.handler->read_u8(addr, addr - a.base);
    mem_access_info_.push_back({
        .addr = addr,
        .data = data,
        .size = 1,
        .write = false
    });
    return data;
}

uint16_t memory_handler::read_u16(uint32_t addr)
{
    if (addr & 1) {
        mem_access_info_.push_back({
            .addr = addr,
            .data = 0,
            .size = 2,
            .write = false
        });
        throw std::runtime_error { "Word read from odd address " + hexstring(addr) };
    }
    auto& a = find_area(addr);
    const uint16_t data = a.handler->read_u16(addr, addr - a.base);
    mem_access_info_.push_back({
        .addr = addr,
        .data = data,
        .size = 2,
        .write = false
    });
    return data;
}

uint32_t memory_handler::read_u32(uint32_t addr)
{
    // TODO: Handle if write to two different areas...
    if (addr & 1) {
        mem_access_info_.push_back({
            .addr = addr,
            .data = 0,
            .size = 4,
            .write = false
        });
        throw std::runtime_error { "Long read from odd address " + hexstring(addr) };
    }
    auto& a = find_area(addr);
    const uint32_t data = a.handler->read_u32(addr, addr - a.base);
    mem_access_info_.push_back({
        .addr = addr,
        .data = data,
        .size = 4,
        .write = false
    });
    return data;
}

void memory_handler::write_u8(uint32_t addr, uint8_t val)
{
    mem_access_info_.push_back({
        .addr = addr,
        .data = val,
        .size = 1,
        .write = true
    });
    auto& a = find_area(addr);
    return a.handler->write_u8(addr, addr - a.base, val);
}

void memory_handler::write_u16(uint32_t addr, uint16_t val)
{
    mem_access_info_.push_back({
        .addr = addr,
        .data = val,
        .size = 2,
        .write = true
    });
    if (addr & 1)
        throw std::runtime_error { "Word write to odd address " + hexstring(addr) };

    auto& a = find_area(addr);
    return a.handler->write_u16(addr, addr - a.base, val);
}

void memory_handler::write_u32(uint32_t addr, uint32_t val)
{
    mem_access_info_.push_back({
        .addr = addr,
        .data = val,
        .size = 4,
        .write = true
    });

    // TODO: Handle if write to two different areas...
    if (addr & 1)
        throw std::runtime_error { "Long write to odd address " + hexstring(addr) };

    auto& a = find_area(addr);
    return a.handler->write_u32(addr, addr - a.base, val);
}

memory_handler::area& memory_handler::find_area(uint32_t& addr)
{
    addr &= 0xffffff;
    for (auto& a : areas_) {
        if (addr >= a.base && addr < a.base + a.len)
            return a;
    }
    if (addr < ram_area_.len) {
        return ram_area_;
    }
    return def_area_;
}
