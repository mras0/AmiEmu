#include "memory.h"
#include "ioutil.h"
#include "state_file.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

static uint32_t warncnt;
static bool memwarn()
{
    constexpr uint32_t maxwarn = 50;
    if (warncnt < 50) {
        if (++warncnt == maxwarn)
            std::cerr << "[MEM] Maximum warnings reached.\n";
        return true;
    }
    return false;
}

uint8_t default_handler::read_u8(uint32_t addr, uint32_t)
{
    // Don't warn a bunch when scanning for ROM tags / I/O space
    if (addr < 0xf00000 && memwarn())
        std::cerr << "[MEM] Unhandled byte read from $" << hexfmt(addr) << "\n";
    mem_handler_.signal_illegal_access(addr, 0, 1, false);
    //return 0xff;
    return 0;
}
uint16_t default_handler::read_u16(uint32_t addr, uint32_t)
{
    // Don't warn a bunch when scanning for ROM tags / I/O space
    if (addr < 0xf00000 && memwarn())
        std::cerr << "[MEM] Unhandled word read from $" << hexfmt(addr) << "\n";
    mem_handler_.signal_illegal_access(addr, 0, 2, false);
    //return 0xffff;
    return 0;
}
void default_handler::write_u8(uint32_t addr, uint32_t, uint8_t val)
{
    if (memwarn())
        std::cerr << "[MEM] Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
    mem_handler_.signal_illegal_access(addr, val, 1, false);
}
void default_handler::write_u16(uint32_t addr, uint32_t, uint16_t val)
{
    if (memwarn())
        std::cerr << "[MEM] Unhandled write to $" << hexfmt(addr) << " val $" << hexfmt(val) << "\n";
    mem_handler_.signal_illegal_access(addr, val, 2, false);
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

void ram_handler::handle_state(state_file& sf)
{
    const auto old_size = ram_.size();
    const state_file::scope scope { sf, "RAM", 1 };
    sf.handle(ram_);
    if (ram_.size() != old_size)
        throw std::runtime_error { "RAM restore error" };
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
    if (size != 64 * 1024 && size != 256 * 1024 && size != 512 * 1024 && size != 1024 * 1024) {
        throw std::runtime_error { "Unexpected size of ROM: $" + hexstring(size) };
    }

    if (size < 256 * 1024) {
        // A1000 bootrom
        wom_.resize(256 * 1024);
    }

    if (size == 1024 * 1024) {
        mem_handler_.register_handler(*this, 0xe00000, 0x80000); // Extension ROM needs to be at 0xe00000?
        mem_handler_.register_handler(*this, 0xf00000, size);
        return;
    }
    mem_handler_.register_handler(*this, 0xf80000, size);
    if (rom_data_.size() != 512 * 1024)
        mem_handler_.register_handler(*this, 0xfc0000, 256 * 1024);
}

void rom_area_handler::set_overlay(bool ovl)
{
    if (ovl_ == ovl)
        return;
    ovl_ = ovl;
    const auto size = static_cast<uint32_t>(rom_data_.size());
    std::cerr << "[ROM handler] Turning overlay " << (ovl ? "on" : "off") << "\n";
    if (ovl)
        mem_handler_.register_handler(*this, 0, size);
    else
        mem_handler_.unregister_handler(*this, 0, size);
}

uint8_t rom_area_handler::read_u8(uint32_t addr, uint32_t offset)
{
    if (addr >= 0xfc0000 && !wom_.empty()) {
        return wom_[offset];
    }
    assert(offset < rom_data_.size());
    return rom_data_[offset];
}

uint16_t rom_area_handler::read_u16(uint32_t addr, uint32_t offset)
{
    if (addr >= 0xfc0000 && !wom_.empty()) {
        return get_u16(&wom_[offset]);
    }
    assert(offset < rom_data_.size() - 1);
    return get_u16(&rom_data_[offset]);
}

void rom_area_handler::write_u8(uint32_t addr, uint32_t offset, uint8_t val)
{
    if (addr >= 0xfc0000 && !wom_.empty() && !write_protect_) {
        wom_[offset] = val;
        return;
    }
    if (memwarn())
        std::cerr << "[MEM] Write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
    mem_handler_.signal_illegal_access(addr, val, 1, false);
    //throw std::runtime_error { "Write to ROM" };
}

void rom_area_handler::write_u16(uint32_t addr, uint32_t offset, uint16_t val)
{
    if (addr >= 0xfc0000 && !wom_.empty() && !write_protect_) {
        put_u16(&wom_[offset], val);
        return;
    }
    if (memwarn())
        std::cerr << "[MEM] Write to rom area: " << hexfmt(addr) << " offset " << hexfmt(offset) << " val = $" << hexfmt(val) << "\n";
    mem_handler_.signal_illegal_access(addr, val, 2, false);
    //throw std::runtime_error { "Write to ROM" };
}

void memory_handler::register_handler(memory_area_handler& h, uint32_t base, uint32_t len)
{
    auto a = &find_area(base);
    assert(a == &def_area_ || a == &ram_area_);
    (void)a;
    areas_.push_back(area { base, len, &h });
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
    addr &= 0xffffff;
    auto& a = find_area(addr);
    track(addr, 0, 1, false);
    return a.handler->read_u8(addr, addr - a.base);
}

uint16_t memory_handler::read_u16(uint32_t addr)
{
    addr &= 0xffffff;
    if (addr & 1) {
        track(addr, 0, 2, false);
        throw std::runtime_error { "Word read from odd address " + hexstring(addr) };
    }
    auto& a = find_area(addr);
    track(addr, 0, 2, false);
    return a.handler->read_u16(addr, addr - a.base);
}

uint32_t memory_handler::read_u32(uint32_t addr)
{
    uint32_t val = read_u16(addr);
    return val << 16 | read_u16(addr + 2);
}

uint16_t memory_handler::hack_peek_u16(uint32_t addr)
{
    addr &= 0xfffffe;
    auto& a = find_area(addr);
    return a.handler->read_u16(addr, addr - a.base);
}

void memory_handler::write_u8(uint32_t addr, uint8_t val)
{
    addr &= 0xffffff;
    track(addr, val, 1, true);
    auto& a = find_area(addr);
    return a.handler->write_u8(addr, addr - a.base, val);
}

void memory_handler::write_u16(uint32_t addr, uint16_t val)
{
    addr &= 0xffffff;
    track(addr, val, 2, true);
    if (addr & 1)
        throw std::runtime_error { "Word write to odd address " + hexstring(addr) };

    auto& a = find_area(addr);
    return a.handler->write_u16(addr, addr - a.base, val);
}

void memory_handler::write_u32(uint32_t addr, uint32_t val)
{
    write_u16(addr, static_cast<uint16_t>(val >> 16));
    write_u16(addr  + 2, static_cast<uint16_t>(val));
}

memory_handler::area& memory_handler::find_area(uint32_t& addr)
{
    assert(addr < 0x1000000);
    for (auto& a : areas_) {
        if (addr >= a.base && addr < a.base + a.len)
            return a;
    }
    if (addr < ram_area_.len) {
        return ram_area_;
    }

    // Chip mem is mirrored up to 2MB
    if (addr < max_chip_size) {
        assert((ram_area_.len & (ram_area_.len - 1)) == 0);
        addr &= ram_area_.len - 1;
        return ram_area_;
    }
    return def_area_;
}

void memory_handler::reset()
{
    std::vector<memory_area_handler*> devices;
    for (auto& a : areas_) {
        if (std::find(devices.begin(), devices.end(), a.handler) != devices.end())
            continue;
        devices.push_back(a.handler);
    }
    for (auto d : devices)
        d->reset();
}

void memory_handler::handle_state(state_file& sf)
{
    const state_file::scope scope { sf, "Chipmem", 1 };
    ram_.handle_state(sf);
}