#include "gui.h"
#include <cassert>
#include <system_error>
#include <array>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace {

void throw_system_error(const std::string& what, const unsigned error_code = GetLastError())
{
    assert(error_code != ERROR_SUCCESS);
    throw std::system_error(error_code, std::system_category(), what);
}

constexpr std::array<uint8_t, 256> vk_to_scan = []() constexpr {
    std::array<uint8_t, 256> map {};
    for (int i = 0; i < 256; ++i)
        map[i] = 0xFF; // No key

    // http://amigadev.elowar.com/read/ADCD_2.1/Libraries_Manual_guide/node0479.html

    map[VK_OEM_3] = 0x00; // `~
    map['1'] = 0x01;
    map['2'] = 0x02;
    map['3'] = 0x03;
    map['4'] = 0x04;
    map['5'] = 0x05;
    map['6'] = 0x06;
    map['7'] = 0x07;
    map['8'] = 0x08;
    map['9'] = 0x09;
    map['0'] = 0x0A;
    map[VK_OEM_MINUS] = 0x0B; // -_
    map[VK_OEM_PLUS] = 0x0C;  // =+
    // \| 0x0D
    // 0x0E
    // 0x0F
    map['Q'] = 0x10;
    map['W'] = 0x11;
    map['E'] = 0x12;
    map['R'] = 0x13;
    map['T'] = 0x14;
    map['Y'] = 0x15;
    map['U'] = 0x16;
    map['I'] = 0x17;
    map['O'] = 0x18;
    map['P'] = 0x19;
    // [{ 0x1A
    // ]} 0x1B
    // 0x1C
    // keypad 1 0x1D
    // keypad 2 0x1E
    // keypad 3 0x1F
    map['A'] = 0x20;
    map['S'] = 0x21;
    map['D'] = 0x22;
    map['F'] = 0x23;
    map['G'] = 0x24;
    map['H'] = 0x25;
    map['J'] = 0x26;
    map['K'] = 0x27;
    map['L'] = 0x28;
    // ;: 0x29
    // '" 0x2A
    // 0x2B
    // 0x2C
    // keypad 4 0x2D
    // keypad 5 0x2E
    // keypad 6 0x2F
    // 0x30
    map['Z'] = 0x31;
    map['X'] = 0x32;
    map['C'] = 0x33;
    map['V'] = 0x34;
    map['B'] = 0x35;
    map['N'] = 0x36;
    map['M'] = 0x37;
    // ,< 0x38
    // .> 0x39
    // /? 0x3A
    // 0x3B
    // numpad . 0x3C
    // numpad 7 0x3D
    // numpad 8 0x3E
    // numpad 9 0x3F
    map[' '] = 0x40;
    map[VK_BACK] = 0x41;
    map[VK_TAB] = 0x42;
    // enter 0x43
    map[VK_RETURN] = 0x44;
    map[VK_ESCAPE] = 0x45;
    map[VK_DELETE] = 0x46;
    // 0x47
    // 0x48
    // 0x49
    // numpad - 0x4A
    // 0x4B
    map[VK_UP] = 0x4C;
    map[VK_DOWN] = 0x4D;
    map[VK_RIGHT] = 0x4E;
    map[VK_LEFT] = 0x4F;
    map[VK_F1] = 0x50;
    map[VK_F2] = 0x51;
    map[VK_F3] = 0x52;
    map[VK_F4] = 0x53;
    map[VK_F5] = 0x54;
    map[VK_F6] = 0x55;
    map[VK_F7] = 0x56;
    map[VK_F8] = 0x57;
    map[VK_F9] = 0x58;
    map[VK_F10] = 0x59;
    // numpad ( 0x5A
    // numpad ( 0x5B
    // numpad / 0x5C
    // numpad * 0x5D
    // numpad + 0x5E
    // help 0x5F
    map[VK_LSHIFT] = 0x60;
    map[VK_RSHIFT] = 0x61;
    // capslock 0x62
    map[VK_LCONTROL] = 0x63; // No right control!
    map[VK_LMENU] = 0x64;
    map[VK_RMENU] = 0x65;
    // left amiga 0x66
    // right amiga 0x67
    

    return map;
}();

}

class gui::impl {
public:
    explicit impl(int width, int height)
        : width_ { width }
        , height_ { height }
    {
        HINSTANCE const hInstance = GetModuleHandle(nullptr);
        WNDCLASS wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &s_wndproc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = class_name_;
        if (!RegisterClass(&wc)) {
            throw_system_error("RegisterClass");
        }
        const DWORD style = (WS_VISIBLE | WS_OVERLAPPEDWINDOW) & ~WS_THICKFRAME;
        RECT r = { 0, 0, width_ , height_ };
        AdjustWindowRect(&r, style, FALSE);

        const char* title = "Amiemu";
        if (!CreateWindow(class_name_, std::wstring(title, title + strlen(title)).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInstance, this)) {
            throw_system_error("CreateWindow");
        }
        assert(hwnd_);
    }

    std::vector<event> update()
    {
        MSG msg;
        events_.clear();
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return { event { event_type::quit } };
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return std::move(events_);
    }

    void update_image(const uint32_t* data)
    {
        union {
            BITMAPINFO bmi;
            char buffer[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 3];
        } u;
        ZeroMemory(&u, sizeof(u));
        u.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        u.bmi.bmiHeader.biWidth = width_;
        u.bmi.bmiHeader.biHeight = -height_;
        u.bmi.bmiHeader.biPlanes = 1;
        u.bmi.bmiHeader.biBitCount = 32;
        u.bmi.bmiHeader.biCompression = BI_RGB;
        SetDIBits(hdc_, hbm_, 0, height_, data, &u.bmi, DIB_RGB_COLORS);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

private:
    static constexpr const wchar_t* const class_name_ = L"Display";
    int width_;
    int height_;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HBITMAP hbm_ = nullptr;
    std::vector<event> events_;

    static LRESULT CALLBACK s_wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        impl* self = nullptr;
        if (uMsg == WM_NCCREATE) {
            self = reinterpret_cast<impl*>(reinterpret_cast<const CREATESTRUCT*>(lParam)->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        const auto ret = self ? self->wndproc(hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);
        if (uMsg == WM_NCDESTROY && self) {
            self->hwnd_ = nullptr;
        }
        return ret;
    }

    void enqueue_keyboard_event(bool pressed, WPARAM wParam, LPARAM lParam)
    {
        WPARAM vk = wParam;
        // https://stackoverflow.com/a/15977613/786653
        if (wParam == VK_SHIFT) {
            vk = MapVirtualKey((lParam >> 16) & 0xff, MAPVK_VSC_TO_VK_EX);
        } else if (wParam == VK_CONTROL) {
            vk = lParam & 0x01000000 ? VK_RCONTROL : VK_LCONTROL;
        } else if (wParam == VK_MENU) {
            vk =  lParam & 0x01000000 ? VK_RMENU : VK_LMENU;
        }
        const auto key = vk_to_scan[static_cast<uint8_t>(vk)];
        if (key == 0xff) {
            std::cerr << "Unhandled virtual key " << vk << "\n";
            return;
        }

        events_.push_back({ event_type::keyboard, keyboard_event { pressed, key } });
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_CREATE:
            return on_create() ? 0 : -1;
        case WM_DESTROY:
            on_destroy();
            break;
        case WM_PAINT:
            on_paint();
            return 0;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            if (!(lParam & 0x4000'0000)) {
                enqueue_keyboard_event(true, wParam, lParam);
            }
            return 0;
        case WM_SYSKEYUP:
        case WM_KEYUP:
            if (wParam == VK_F4 && GetKeyState(VK_MENU) < 0)
                PostQuitMessage(0);
            enqueue_keyboard_event(false, wParam, lParam);
            return 0;

        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    bool on_create()
    {
        if (HDC hdc = GetWindowDC(hwnd_)) {
            if ((hdc_ = CreateCompatibleDC(hdc)) != nullptr) {
                if ((hbm_ = CreateCompatibleBitmap(hdc, width_, height_)) != nullptr) {
                    if (SelectObject(hdc_, hbm_)) {
                        ReleaseDC(hwnd_, hdc);
                        return true;
                    }
                }
                DeleteDC(hdc_);
            }
            ReleaseDC(hwnd_, hdc);
        }
        return false;
    }

    void on_destroy()
    {
        DeleteObject(hbm_);
        DeleteDC(hdc_);
        PostQuitMessage(0);
    }

    void on_paint()
    {
        PAINTSTRUCT ps;
        if (BeginPaint(hwnd_, &ps) && !IsRectEmpty(&ps.rcPaint)) {
            RECT r;
            GetClientRect(hwnd_, &r);
            assert(r.left == 0 && r.top == 0);
            StretchBlt(ps.hdc, 0, 0, r.right, r.bottom, hdc_, 0, 0, width_, height_, SRCCOPY);
            EndPaint(hwnd_, &ps);
        }
    }
};

gui::gui(unsigned width, unsigned height)
    : impl_ {
        std::make_unique<impl>(static_cast<int>(width), static_cast<int>(height))
    }
{
}

gui::~gui() = default;

std::vector<gui::event> gui::update()
{
    return impl_->update();
}

void gui::update_image(const uint32_t* data)
{
    impl_->update_image(data);
}