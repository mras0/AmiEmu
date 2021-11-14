#include "gui.h"
#include "wavedev.h"
#include "ioutil.h"
#include <SDL.h>
#include <iostream>
#include <stdexcept>
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace {


[[noreturn]] void throw_sdl_error(const std::string& what)
{
    throw std::runtime_error { what + " failed: " + SDL_GetError() };
}

class sdl_init {
public:
    explicit sdl_init(uint32_t flags)
        : flags_ { flags }
    {
        if (SDL_InitSubSystem(flags_))
            throw_sdl_error("SDL_InitSubSystem " + hexstring(flags_));
    }

    sdl_init(const sdl_init&) = delete;
    sdl_init& operator=(const sdl_init&) = delete;

    ~sdl_init()
    {
        SDL_QuitSubSystem(flags_);
    }

private:
    const uint32_t flags_;
};

#define MAKE_SDL_PTR(type, destroyer) \
struct type##_destroyer { \
    void operator()(type* p) const { \
        if (p) destroyer(p); \
    } \
}; \
using type##_ptr = std::unique_ptr<type, type##_destroyer>

MAKE_SDL_PTR(SDL_Window, SDL_DestroyWindow);

uint8_t convert_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_RETURN: return 0x44;
    case SDLK_ESCAPE: return 0x45;
    case SDLK_BACKSPACE: return 0x41;
    case SDLK_TAB: return 0x42;
    case SDLK_SPACE: return 0x40;
    case SDLK_QUOTE: return 0x2A;
    case SDLK_COMMA: return 0x38;
    case SDLK_MINUS: return 0x0B;
    case SDLK_PERIOD: return 0x39;
    case SDLK_SLASH: return 0x3A;
    case SDLK_0: return 0x0A;
    case SDLK_1: return 0x01;
    case SDLK_2: return 0x02;
    case SDLK_3: return 0x03;
    case SDLK_4: return 0x04;
    case SDLK_5: return 0x05;
    case SDLK_6: return 0x06;
    case SDLK_7: return 0x07;
    case SDLK_8: return 0x08;
    case SDLK_9: return 0x09;
    case SDLK_SEMICOLON: return 0x29;
    case SDLK_EQUALS: return 0x0C;
    case SDLK_LEFTBRACKET: return 0x1A;
    case SDLK_BACKSLASH: return 0x0D;
    case SDLK_RIGHTBRACKET: return 0x1B;
    case SDLK_BACKQUOTE: return 0x00;
    case SDLK_a: return 0x20;
    case SDLK_b: return 0x35;
    case SDLK_c: return 0x33;
    case SDLK_d: return 0x22;
    case SDLK_e: return 0x12;
    case SDLK_f: return 0x23;
    case SDLK_g: return 0x24;
    case SDLK_h: return 0x25;
    case SDLK_i: return 0x17;
    case SDLK_j: return 0x26;
    case SDLK_k: return 0x27;
    case SDLK_l: return 0x28;
    case SDLK_m: return 0x37;
    case SDLK_n: return 0x36;
    case SDLK_o: return 0x18;
    case SDLK_p: return 0x19;
    case SDLK_q: return 0x10;
    case SDLK_r: return 0x13;
    case SDLK_s: return 0x21;
    case SDLK_t: return 0x14;
    case SDLK_u: return 0x16;
    case SDLK_v: return 0x34;
    case SDLK_w: return 0x11;
    case SDLK_x: return 0x32;
    case SDLK_y: return 0x15;
    case SDLK_z: return 0x31;
    //case SDLK_CAPSLOCK: return 0x62;
    case SDLK_F1: return 0x50;
    case SDLK_F2: return 0x51;
    case SDLK_F3: return 0x52;
    case SDLK_F4: return 0x53;
    case SDLK_F5: return 0x54;
    case SDLK_F6: return 0x55;
    case SDLK_F7: return 0x56;
    case SDLK_F8: return 0x57;
    case SDLK_F9: return 0x58;
    case SDLK_F10: return 0x59;
    case SDLK_INSERT: return 0x66; // Left amiga
    case SDLK_HOME: return 0x67; // Right amiga
    //case SDLK_PAGEUP: return 0xFF;
    case SDLK_DELETE: return 0x46;
    //case SDLK_END: return 0xFF;
    case SDLK_PAGEDOWN: return 0x5F; // Help
    case SDLK_RIGHT: return 0x4E;
    case SDLK_LEFT: return 0x4F;
    case SDLK_DOWN: return 0x4D;
    case SDLK_UP: return 0x4C;
    case SDLK_LCTRL: return 0x63;
    case SDLK_LSHIFT: return 0x60;
    case SDLK_LALT: return 0x64;
    //case SDLK_LGUI: return 0xFF;
    //case SDLK_RCTRL: return 0xFF;
    case SDLK_RSHIFT: return 0x61;
    case SDLK_RALT: return 0x65;
    //case SDLK_RGUI: return 0xFF;
    }
    std::cerr << "Unhandled key: $" << hexfmt(key) << " (" << static_cast<unsigned>(key)<<  ")\n";
    return 0xFF;
}

} // unnamed namespace

class gui::impl {
public:
    explicit impl(unsigned width, unsigned height)
        : width_ { static_cast<int>(width) }
        , height_ { static_cast<int>(height) }
    {
        window_.reset(SDL_CreateWindow("Amiemu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width_, height_, SDL_WINDOW_SHOWN));
        if (!window_)
            throw_sdl_error("SDL_CreateWindow");
    }

    std::vector<gui::event> update()
    {
        SDL_Event e;
        std::vector<gui::event> events;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                return { event { event_type::quit, {} } };
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_F12) {
                    const uint8_t* keystate = SDL_GetKeyboardState(nullptr);
                    if (keystate[SDL_SCANCODE_LSHIFT] || keystate[SDL_SCANCODE_RSHIFT]) {
                        events.push_back(event { event_type::debug_mode, {} });
                        set_active(false);
                    }
                    break;
                }
                [[fallthrough]];
            case SDL_KEYUP:
                if (auto scan = convert_key(e.key.keysym.sym); scan != 0xFF) {
                    event keyevt;
                    keyevt.type = event_type::keyboard;
                    keyevt.keyboard.pressed = e.type == SDL_KEYDOWN;
                    keyevt.keyboard.scancode = scan;
                    events.push_back(keyevt);
                    // TODO: Don't do it like this (and don't hard code keys)
                    const uint8_t* keystate = SDL_GetKeyboardState(nullptr);
                    if (keystate[SDL_SCANCODE_LCTRL] && keystate[SDL_SCANCODE_HOME] && keystate[SDL_SCANCODE_INSERT])
                        events.push_back({ event_type::reset, {} });
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
                    if (!mouse_captured_) {
                        capture_mouse(true);
                    } else {
                        event mouseevt;
                        mouseevt.type = event_type::mouse_button;
                        mouseevt.mouse_button.pressed = e.type == SDL_MOUSEBUTTONDOWN;
                        mouseevt.mouse_button.left = e.button.button == SDL_BUTTON_LEFT;
                        events.push_back(mouseevt);
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                if (mouse_captured_) {
                    event moveevt;
                    moveevt.type = event_type::mouse_move;
                    moveevt.mouse_move.dx = e.motion.xrel;
                    moveevt.mouse_move.dy = e.motion.yrel;
                    events.push_back(moveevt);
                    // Make sure mouse doesn't end up on the window border
                    SDL_WarpMouseInWindow(window_.get(), width_ / 2, height_ / 2);
                }
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    capture_mouse(false);
                }
            }
        }
        return events;
    }

    void update_image(const uint32_t* img)
    {
        SDL_Surface* surface = SDL_GetWindowSurface(window_.get());
        if (!surface)
            throw_sdl_error("SDL_GetWindowSurface");
        //assert(SDL_GetSurface
        if (SDL_LockSurface(surface))
            throw_sdl_error("SDL_LockSurface");
        assert(surface->w == width_ && surface->h == height_);
        SDL_ConvertPixels(width_, height_, SDL_PIXELFORMAT_BGRA32, img, width_ * 4, surface->format->format, surface->pixels, surface->pitch);
        SDL_UnlockSurface(surface);
        SDL_UpdateWindowSurface(window_.get());
    }

    void set_active(bool active)
    {
        if (active) {
            //SDL_RaiseWindow(window_.get());
        } else {
            capture_mouse(false);
            // No inverse of SDL_RaiseWindow?
        }
        if (on_pause_)
            on_pause_(!active);
    }

    void set_on_pause_callback(const on_pause_callback& on_pause)
    {
        assert(!on_pause_);
        on_pause_ = on_pause;
    }

private:
    const int width_;
    const int height_;
    sdl_init sdl_init_ { SDL_INIT_VIDEO };
    SDL_Window_ptr window_;
    bool mouse_captured_ = false;
    on_pause_callback on_pause_;

    void capture_mouse(bool enabled)
    {
        if (enabled == mouse_captured_)
            return;
        SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
        mouse_captured_ = enabled;
    }
};

gui::gui(unsigned width, unsigned height, const std::array<std::string, 4>&)
    : impl_ {new impl(width, height)}
{
}

gui::~gui()
{
    delete impl_;
}

std::vector<gui::event> gui::update()
{
    return impl_->update();
}

void gui::update_image(const uint32_t* img)
{
    impl_->update_image(img);
}

void gui::led_state(uint8_t)
{
}

void gui::disk_activty(uint8_t, uint8_t, bool)
{
}

void gui::serial_data(const std::vector<uint8_t>&)
{
}

void gui::set_active(bool active)
{
    impl_->set_active(active);
}

namespace {
class at_exit {
public:
    using func_type = std::function<void(void)>;
    explicit at_exit(const func_type& f)
        : f_ { f }
    {
    }
    at_exit(const at_exit&) = delete;
    at_exit& operator=(const at_exit&) = delete;
    ~at_exit()
    {
        f_();
    }

private:
    func_type f_;
};
}

bool gui::debug_prompt(std::string& line)
{
    // A bit ugly...
    static std::mutex mtx;
    static std::condition_variable cv_line_read;
    static std::condition_variable cv_msg;
    static enum msg_type { none, read_line, quit } msg = none;
    static bool result = false;
    static std::thread t {
        [&]() {
            for (;;) {
                msg_type m;
                {
                    std::unique_lock<std::mutex> lock { mtx };
                    cv_msg.wait(lock, [&]() { return msg != none; });
                    m = msg;
                }
                if (m == quit)
                    return;
                assert(m == read_line);
                std::cout << "> " << std::flush;
                result = !!std::getline(std::cin, line);
                {
                    std::unique_lock<std::mutex> lock { mtx };
                    assert(msg == m);
                    msg = none;
                }
                cv_line_read.notify_all();
            }
        }
    };
    auto send_msg = [&](msg_type m) {
        assert(m != none);
        {
            std::unique_lock<std::mutex> lock { mtx };
            assert(msg == none);
            result = false;
            msg = m;
        }
        cv_msg.notify_all();
    };
    static at_exit at_exit_ {
        [&]() {
            send_msg(quit);
            t.join();
        }
    };
    send_msg(read_line);

    for (;;) {
        SDL_Event evt;
        if (SDL_WaitEventTimeout(&evt, 100)) {
            if (evt.type == SDL_QUIT) {
                return false;
            }
        }
        std::unique_lock<std::mutex> lock { mtx };
        if (cv_line_read.wait_for(lock, std::chrono::milliseconds(1), [&]() { return msg == none; })) {
            break;
        }
    }
    
    return result;
}

void gui::set_debug_memory(const std::vector<uint8_t>&, const std::vector<uint16_t>&)
{
}

void gui::set_debug_windows_visible(bool)
{
}

void gui::set_on_pause_callback(const on_pause_callback& on_pause)
{
    impl_->set_on_pause_callback(on_pause);
}


class wavedev::impl {
public:
    explicit impl(unsigned sample_rate, unsigned buffer_size, callback_t callback)
        : callback_ { callback }
    {
        assert(buffer_size < 65536 * 2 && buffer_size % 2 == 0);

        SDL_AudioSpec want {};
        SDL_AudioSpec have;

        want.freq = sample_rate;
        want.format = AUDIO_S16;
        want.channels = 2;
        want.samples = static_cast<uint16_t>(buffer_size / 2);
        want.callback = &impl::s_callback;
        want.userdata = this;

        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev_)
            throw_sdl_error("SDL_OpenAudioDevice");

        if (want.freq != have.freq || want.format != have.format || want.channels != have.channels || want.samples != have.samples) {
            SDL_CloseAudioDevice(dev_);
            std::cerr << "Freq: " << have.freq << " Format: " << static_cast<int>(have.format) << " channels: " << static_cast<int>(have.channels) << " samples: " << have.samples << "\n";
            throw std::runtime_error { "Audio format not supported" };
        }
        
        set_paused(false);
    }

    ~impl()
    {
        SDL_CloseAudioDevice(dev_);
    }

    void set_paused(bool pause)
    {
        SDL_PauseAudioDevice(dev_, pause);
    }

private:
    sdl_init sdl_init_ { SDL_INIT_AUDIO };
    SDL_AudioDeviceID dev_;
    callback_t callback_;

    static void SDLCALL s_callback(void* userdata, Uint8* stream, int len) {
        assert(len % 4 == 0);
        reinterpret_cast<impl*>(userdata)->callback_(reinterpret_cast<int16_t*>(stream), len / 4);
    }
};

wavedev::wavedev(unsigned sample_rate, unsigned buffer_size, callback_t callback)
    : impl_ { std::make_unique<impl>(sample_rate, buffer_size, callback) }
{
}

wavedev::~wavedev() = default;

void wavedev::set_paused(bool paused)
{
    impl_->set_paused(paused);
}

bool wavedev::is_null()
{
    return false;
}
