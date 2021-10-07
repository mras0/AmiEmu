#ifndef CUSTOM_H
#define CUSTOM_H

#include <memory>
#include <functional>
#include <iosfwd>
#include <string>
#include "memory.h"

constexpr unsigned graphics_width  = 768; // 24*16*2
constexpr unsigned graphics_height = 572; // 286*2

constexpr unsigned audio_samples_per_frame = 313 * 2; // Two stereo sample per scanline
constexpr unsigned audio_buffer_size = audio_samples_per_frame * 2;
constexpr unsigned audio_sample_rate = audio_samples_per_frame * 50;

constexpr uint32_t custom_base_addr = 0xDE0000;
constexpr uint32_t custom_mem_size  = 0xE00000 - 0xDE0000;

class cia_handler;

enum class bus_use {
    none,
    refresh,
    disk,
    audio,
    bitplane,
    sprite,
    copper,
    blitter,
    cpu_read,
    cpu_write,
};

// Color clocks per line
// 0..$E2 (227.5 actually, on NTSC they alternate between 227 and 228)
// 64us per line (52us visible), ~454 virtual lorespixels (~369 max visible)
// Each color clock produces 2 lores or 4 hires pixels

static constexpr uint16_t hpos_per_line = 454; // 227.5 color clocks = 455 (lores pixels), but MUST be multiple of CCKs (227 for PAL) for correct timing
static constexpr uint16_t vpos_per_field = 313;

class custom_handler {
public:
    explicit custom_handler(memory_handler& mem_handler, cia_handler& cia, uint32_t slow_end, uint32_t floppy_speed);
    ~custom_handler();

    using serial_data_handler = std::function<void(uint8_t numbits, uint8_t data)>;
    struct step_result {
        const uint32_t* frame;
        const int16_t* audio;
        uint16_t vpos;
        uint16_t hpos;
        bus_use bus;
        uint32_t dma_addr;
        uint16_t dma_val;
        bool free_chip_cycle;
        uint8_t eclock_cycle;
    };

    step_result step(bool cpu_wants_access, uint32_t current_pc);
    uint8_t current_ipl();

    void set_serial_data_handler(const serial_data_handler& handler);
    void set_rbutton_state(bool pressed);
    void mouse_move(int dx, int dy);
    void set_joystate(uint16_t dat, bool button_state);

    void show_debug_state(std::ostream& os);
    void show_registers(std::ostream& os);
    uint32_t copper_ptr(uint8_t idx); // 0=current
    std::vector<uint16_t> get_regs();

    void handle_state(state_file& sf);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

std::string custom_regname(uint32_t offset);

#endif
