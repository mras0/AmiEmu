#include "gui.h"
#include <cassert>
#include <system_error>
#include <array>
#include <iostream>
#include <thread>
#include <atomic>
#include "ioutil.h"
#include "color_util.h"
#include "memory.h"
#include "state_file.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>

namespace {

constexpr uint8_t gfx_scale = 2;

[[noreturn]] void throw_system_error(const std::string& what, const unsigned error_code = GetLastError())
{
    assert(error_code != ERROR_SUCCESS);
    throw std::system_error(error_code, std::system_category(), what);
}

[[noreturn]] void throw_com_error(const std::string& what, HRESULT hr)
{
    assert(FAILED(hr));
    throw std::runtime_error { what + " failed: 0x" + hexstring(hr) };
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
    map[VK_OEM_5] = 0x0D;  // \| 0x0D
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
    map[VK_OEM_4] = 0x1A; // [{ 0x1A
    map[VK_OEM_6] = 0x1B; // ]} 0x1B
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
    map[VK_OEM_1] = 0x29;  // ;: 0x29
    map[VK_OEM_7] = 0x2A;  // '" 0x2A
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
    map[VK_OEM_COMMA] = 0x38;  // ,< 0x38
    map[VK_OEM_PERIOD] = 0x39; // .> 0x39
    map[VK_OEM_2] = 0x3A; // /? 0x3A
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
    map[VK_NEXT] = 0x5F; // help 0x5F
    map[VK_LSHIFT] = 0x60;
    map[VK_RSHIFT] = 0x61;
    // capslock 0x62
    map[VK_LCONTROL] = 0x63; // No right control!
    map[VK_LMENU] = 0x64;
    map[VK_RMENU] = 0x65;
    map[VK_INSERT] = 0x66;  // left amiga 0x66
    map[VK_HOME] = 0x67; // right amiga 0x67
 
    return map;
}();

bool browse_for_file(HWND hwnd, const char* zz_filter, std::string& filename, bool load = true, const char* defext = nullptr)
{
    OPENFILENAMEA ofn;

    char path[MAX_PATH];
    ZeroMemory(&ofn, sizeof(ofn));
    path[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = zz_filter;
    ofn.lpstrDefExt = defext;
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = load ? OFN_PATHMUSTEXIST : 0;
    if (load) {
        if (!GetOpenFileNameA(&ofn))
            return false;
    } else {
        if (!GetSaveFileNameA(&ofn))
            return false;
    }
    filename = path;
    return true;
}

struct gdi_deleter {
    //using pointer = HGDIOBJ;
    void operator()(void* font)
    {
        if (font)
            DeleteObject(font);
    }
};
using font_ptr = std::unique_ptr<HFONT__, gdi_deleter>;

constexpr int maxpalette = 32;
constexpr const uint32_t default_palette[maxpalette] = {
    0x5D8AA8,
    0xF0F8FF,
    0xE32636,
    0xE52B50,
    0xFFBF00,
    0xA4C639,
    0x8DB600,
    0xFBCEB1,
    0x7FFFD4,
    0x4B5320,
    0x3B444B,
    0xE9D66B,
    0xB2BEB5,
    0x87A96B,
    0xFF9966,
    0x6D351A,
    0x007FFF,
    0x89CFF0,
    0xA1CAF1,
    0xF4C2C2,
    0xFFD12A,
    0x848482,
    0x98777B,
    0xF5F5DC,
    0x3D2B1F,
    0x000000,
    0x318CE7,
    0xFAF0BE,
    0x0000FF,
    0xDE5D83,
    0x79443B,
    0xCC0000,
};

template <typename Derived>
class window_base {
public:
    HWND handle()
    {
        return hwnd_;
    }

protected:
    ~window_base()
    {
        if (hwnd_)
            DestroyWindow(hwnd_);
    }

    void do_create(const std::wstring& title, DWORD style, int x, int y, int cx, int cy, HWND parent)
    {
        assert(!hwnd_);

        if (!class_atom_)
            do_register_class();

        if (!CreateWindow(MAKEINTATOM(class_atom_), title.c_str(), style, x, y, cx, cy, parent, nullptr, GetModuleHandle(nullptr), static_cast<Derived*>(this))) {
            throw_system_error("CreateWindow");
        }
        assert(hwnd_);
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

private:
    static ATOM class_atom_;
    HWND hwnd_ = nullptr;

    static void modify_window_class(WNDCLASS&)
    {
    }

    static void do_register_class()
    {
        assert(!class_atom_);
        WNDCLASS wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &s_wndproc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = Derived::class_name_;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        Derived::modify_window_class(wc);
        if ((class_atom_ = RegisterClass(&wc)) == 0) {
            throw_system_error("RegisterClass");
        }
    }

    #define MAKE_DETECTOR(name)                                                                         \
    template<typename T, typename = void> struct has_##name##_t : std::false_type { };                  \
    template<typename T> struct has_##name##_t<T, std::void_t<decltype(&T::name)>> : std::true_type {}; \
    template<typename T = Derived> static constexpr bool has_##name = has_##name##_t<T>::value

    MAKE_DETECTOR(on_create);
    MAKE_DETECTOR(on_destroy);
    MAKE_DETECTOR(on_paint);
    MAKE_DETECTOR(on_erase_background);
    MAKE_DETECTOR(on_size);
    MAKE_DETECTOR(on_close);

    #undef MAKE_DETECTOR

    static LRESULT CALLBACK s_wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        Derived* self = nullptr;
        if (uMsg == WM_NCCREATE) {
            self = reinterpret_cast<Derived*>(reinterpret_cast<const CREATESTRUCT*>(lParam)->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Derived*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (self) {
            switch (uMsg) {
            case WM_CREATE:
                if constexpr (has_on_create<>)
                    return self->on_create(hwnd, *reinterpret_cast<const CREATESTRUCT*>(lParam)) ? 0 : -1;
                break;
            case WM_DESTROY:
                if constexpr (has_on_destroy<>)
                    self->on_destroy(hwnd);
                break;
            case WM_PAINT:
                if constexpr (has_on_paint<>) {
                    self->on_paint(hwnd);
                    return 0;
                }
                break;
            case WM_ERASEBKGND:
                if constexpr (has_on_erase_background<>) {
                    self->on_erase_background(hwnd, reinterpret_cast<HDC>(wParam));
                    return 1;
                }
                break;
            case WM_SIZE:
                if constexpr (has_on_size<>) {
                    self->on_size(hwnd, LOWORD(lParam), HIWORD(lParam));
                    return 0;
                }
                break;
            case WM_CLOSE:
                if constexpr (has_on_close<>) {
                    self->on_close(hwnd);
                    return 0;
                }
                break;
            }
        }

        const auto ret = self ? self->wndproc(hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);
        if (uMsg == WM_NCDESTROY && self) {
            self->hwnd_ = nullptr;
            delete self;
        }
        return ret;
    }
};
template <typename Derived>
ATOM window_base<Derived>::class_atom_ = 0;

class serial_data_window : public window_base<serial_data_window> {
public:
    static serial_data_window* create(int x, int y)
    {
        std::unique_ptr<serial_data_window> wnd { new serial_data_window {} };
        wnd->do_create(L"Serial console", WS_OVERLAPPEDWINDOW, x, y, 800, 480, nullptr);
        return wnd.release();
    }

    void append_text(const std::string& strdat)
    {
        if (!visible_) {
            ShowWindow(handle(), SW_SHOWNOACTIVATE);
            visible_ = true;
        }

        int index = GetWindowTextLength(edit_window_);
        //SetFocus(edit_window_);
        SendMessageA(edit_window_, EM_SETSEL, (WPARAM)index, (LPARAM)index);
        SendMessageA(edit_window_, EM_REPLACESEL, 0, (LPARAM)strdat.c_str());
        SendMessage(edit_window_, EM_LINESCROLL, 0, 0xFFFF); // Scroll to bottom (hackish)
    }

private:
    friend window_base<serial_data_window>;
    static constexpr const wchar_t* const class_name_ = L"SerialDataWindow";
    HWND edit_window_ = nullptr;
    font_ptr edit_font_;
    bool visible_ = false;

    explicit serial_data_window() {
        LOGFONTA lf {};
        lf.lfHeight = -16;
        lf.lfWidth = 0;
        lf.lfEscapement = 0;
        lf.lfOrientation = 0;
        lf.lfWeight = FW_DONTCARE;
        lf.lfItalic = FALSE;
        lf.lfUnderline = FALSE;
        lf.lfStrikeOut = FALSE;
        lf.lfCharSet = ANSI_CHARSET;
        lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = DEFAULT_QUALITY;
        lf.lfPitchAndFamily = FIXED_PITCH;
        snprintf(lf.lfFaceName, sizeof(lf.lfFaceName), "Consolas");
        edit_font_.reset(CreateFontIndirectA(&lf));
        if (!edit_font_)
            throw_system_error("CreateFontIndirect");
    }

    bool on_create(HWND hwnd, const CREATESTRUCT&)
    {
        edit_window_ = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, 0, 100, 100, hwnd, nullptr, nullptr, nullptr);
        if (!edit_window_)
            return false;
        SendMessage(edit_window_, WM_SETFONT, (WPARAM)edit_font_.get(), FALSE);
        return true;
    }

    void on_size(HWND, UINT client_width, UINT client_height)
    {
        SetWindowPos(edit_window_, nullptr, 0, 0, client_width, client_height, SWP_NOZORDER);
    }

    void on_close(HWND)
    {
    }
};

class bitmap_window : public window_base<bitmap_window> {
public:
    static bitmap_window* create(int x, int y, int width, int height, HWND parent)
    {
        assert(parent);
        std::unique_ptr<bitmap_window> wnd { new bitmap_window { width, height } };
        wnd->do_create(L"", WS_CHILD|WS_VISIBLE, x, y, width, height, parent);
        return wnd.release();
    }

    int width() const
    {
        return width_;
    }

    int height() const
    {
        return width_;
    }

    bool set_size(int width, int height)
    {
        HWND hwnd = handle();
        destroy();
        width_ = width;
        height_ = height;
        if (HDC hdc = GetWindowDC(hwnd)) {
            if ((hdc_ = CreateCompatibleDC(hdc)) != nullptr) {
                if ((hbm_ = CreateCompatibleBitmap(hdc, width_, height_)) != nullptr) {
                    if (SelectObject(hdc_, hbm_)) {
                        ReleaseDC(hwnd, hdc);
                        return true;
                    }
                }
                DeleteDC(hdc_);
            }
            ReleaseDC(hwnd, hdc);
        }
        return false;
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
        InvalidateRect(handle(), nullptr, FALSE);
    }

private:
    friend window_base;
    static constexpr const wchar_t* const class_name_ = L"bitmap_window";
    int width_;
    int height_;
    HDC hdc_ = nullptr;
    HBITMAP hbm_ = nullptr;

    explicit bitmap_window(int width, int height)
        : width_ { width }
        , height_ { height }
    {
    }

    void destroy()
    {
        if (hbm_)
            DeleteObject(hbm_);
        hbm_ = nullptr;
        if (hdc_)
            DeleteDC(hdc_);
        hdc_ = nullptr;
    }

    bool on_create(HWND hwnd, const CREATESTRUCT&)
    {
        EnableWindow(hwnd, FALSE); // Let input messages through to parent
        return set_size(width_, height_);
    }

    void on_destroy(HWND)
    {
        destroy();
    }

    void on_erase_background(HWND, HDC)
    {
    }

    void on_paint(HWND hwnd)
    {
        PAINTSTRUCT ps;
        if (BeginPaint(hwnd, &ps) && !IsRectEmpty(&ps.rcPaint)) {
            RECT r;
            GetClientRect(hwnd, &r);
            assert(r.left == 0 && r.top == 0);
            //BitBlt(ps.hdc, 0, 0, width_, height_, hdc_, 0, 0, SRCCOPY);
            StretchBlt(ps.hdc, 0, 0, r.right, r.bottom, hdc_, 0, 0, width_, height_, SRCCOPY);
            EndPaint(hwnd, &ps);
        }
    }

    void on_close(HWND)
    {
    }
};

class palette_edit_dialog : public window_base<palette_edit_dialog> {
public:
    static void run(HWND parent, uint16_t* pal, const std::vector<uint16_t>& custom)
    {
        bool done = false;
        auto wnd = new palette_edit_dialog { done, pal, custom };
        EnableWindow(parent, FALSE);
        wnd->do_create(L"Edit palette", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 800, 400, parent);

        MSG msg{};
        while (!done && GetMessage(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessage(wnd->handle(), &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (msg.message == WM_QUIT)
            PostQuitMessage(static_cast<int>(msg.wParam));
    }

private:
    friend window_base;
    static constexpr const wchar_t* const class_name_ = L"palette_edit_dialog";
    bool& done_;
    uint16_t* pal_;
    const std::vector<uint16_t>& custom_;
    HWND parent_ = nullptr;
    HWND last_focus_ = nullptr;
    HWND color_edit_[maxpalette];
    HWND color_preview_[maxpalette];

    palette_edit_dialog(bool& done, uint16_t* pal, const std::vector<uint16_t>& custom)
        : done_ { done }
        , pal_ { pal }
        , custom_ { custom }
    {
        assert(custom_.size() == 0x100);
    }

    bool on_create(HWND hwnd, const CREATESTRUCT& cs)
    {
        parent_ = cs.hwndParent;

        auto hInstance = GetModuleHandle(nullptr);
        const int w = 85;
        const int h = 20;
        const int ncols = 4;
        const int xmargin = 10;
        const int ymargin = 10;
        int ypos = ymargin;
        for (int y = 0, n = 0; y < maxpalette / ncols; ++y) {
            for (int x = 0, xpos = xmargin; x < ncols; ++x, ++n) {
                wchar_t temp[256];
                wsprintfW(temp, L"COLOR%02d", n);
                CreateWindow(L"STATIC", temp, WS_CHILD | WS_VISIBLE, xpos, ypos, w, h, hwnd, nullptr, hInstance, nullptr);
                xpos += w + xmargin;
                wsprintfW(temp, L"$%03X", pal_[n] & 0xfff);
                color_edit_[n] = CreateWindow(L"EDIT", temp, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, xpos, ypos, w - 34, h, hwnd, reinterpret_cast<HMENU>(intptr_t(100) + n), hInstance, nullptr);
                color_preview_[n] = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, xpos + w - 32, ypos, 32, h, hwnd, reinterpret_cast<HMENU>(intptr_t(1000) + n), hInstance, nullptr);
                xpos += w + xmargin;
            }
            ypos += h + ymargin;
        }
        const int button_w = 100;
        int x = xmargin;
        CreateWindow(L"BUTTON", L"&Ok", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, xmargin, ypos, button_w, h, hwnd, reinterpret_cast<HMENU>(IDOK), hInstance, nullptr);
        x += button_w + xmargin;
        CreateWindow(L"BUTTON", L"&Cancel", WS_CHILD | WS_TABSTOP | WS_VISIBLE, x, ypos, button_w, h, hwnd, reinterpret_cast<HMENU>(IDCANCEL), hInstance, nullptr);
        x += button_w + xmargin;
        CreateWindow(L"BUTTON", L"&From custom", WS_CHILD | WS_TABSTOP | WS_VISIBLE, x, ypos, button_w, h, hwnd, reinterpret_cast<HMENU>(300), hInstance, nullptr);
        x += button_w + xmargin;
        CreateWindow(L"BUTTON", L"&Default", WS_CHILD | WS_TABSTOP | WS_VISIBLE, x, ypos, button_w, h, hwnd, reinterpret_cast<HMENU>(301), hInstance, nullptr);

        SetFocus(color_edit_[0]);

        return true;
    }

    void on_close(HWND hwnd)
    {
        EnableWindow(parent_, TRUE);
        done_ = true;
        DestroyWindow(hwnd);
    }

    static constexpr int invalid_color_value = -1;
    int color_value_from_control(int i)
    {
        char temp[256];
        GetWindowTextA(color_edit_[i], temp, sizeof(temp));
        unsigned val = ~0U;
        if (temp[0] != '$' || !sscanf(temp + 1, "%x", &val) || val > 0xfff) {
            return invalid_color_value;
        }
        return val;
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
                last_focus_ = GetFocus();
            break;
        case WM_SETFOCUS:
            SetFocus(last_focus_);
            break;
        case WM_COMMAND: {
            const auto id = LOWORD(wParam);
            const auto code = HIWORD(wParam);
            if (id == IDOK && code == BN_CLICKED) {
                uint16_t newpal[maxpalette];
                for (int i = 0; i < maxpalette; ++i) {
                    int val = color_value_from_control(i);
                    if (val == invalid_color_value) {
                        char temp[256];
                        GetWindowTextA(color_edit_[i], temp, sizeof(temp));
                        wchar_t msg[512];
                        wsprintfW(msg, L"COLOR%2d value \"%S\" is invalid", i, temp);
                        MessageBox(hwnd, msg, L"Invalid color value", MB_ICONSTOP);
                        return 0;
                    }
                    newpal[i] = static_cast<uint16_t>(val);
                }
                memcpy(pal_, newpal, sizeof(newpal));
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (id == IDCANCEL && code == BN_CLICKED) {
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (id == 300 && code == BN_CLICKED) {
                for (int i = 0; i < maxpalette; ++i) {
                    char temp[256];
                    sprintf(temp, "$%03X", custom_[0x180/2 + i] & 0xfff);
                    SetWindowTextA(color_edit_[i], temp);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (id == 301 && code == BN_CLICKED) {
                for (int i = 0; i < maxpalette; ++i) {
                    char temp[256];
                    sprintf(temp, "$%03X", rgb8_to_4(default_palette[i]));
                    SetWindowTextA(color_edit_[i], temp);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (id >= 100 && id < 100 + maxpalette && code == EN_UPDATE) {
                InvalidateRect(color_preview_[id - 100], nullptr, TRUE);
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);

            if (int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam)); id >= 1000) {
                const auto col = color_value_from_control(id - 1000);
                if (col != invalid_color_value) {
                    const auto rgb = rgb4_to_8(static_cast<uint16_t>(col));
                    COLORREF col8 = (rgb & 0xff0000) >> 16 | (rgb & 0x00ff00) | (rgb & 0xff) << 16;
                    SetDCBrushColor(hdc, col8);
                    SetBkColor(hdc, col8);
                    return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
                }
            }

            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        }
        return window_base::wndproc(hwnd, uMsg, wParam, lParam);
    }
};

class memory_visualizer_window : public window_base<memory_visualizer_window> {
public:
    static memory_visualizer_window* create(int x, int y)
    {
        std::unique_ptr<memory_visualizer_window> wnd { new memory_visualizer_window {} };
        wnd->do_create(L"Memory visualizer", WS_OVERLAPPEDWINDOW | WS_MINIMIZE, x, y, 1024, 900, nullptr);
        return wnd.release();
    }

    void set_debug_memory(const std::vector<uint8_t>& mem, const std::vector<uint16_t>& custom)
    {
        assert(custom.size() == 0x100);
        mem_ = mem;
        custom_ = custom;
        update();
    }

private:
    friend window_base;
    static constexpr const wchar_t* const class_name_ = L"memory_visualizer_window";
    static constexpr int toolbar_margin_x = 10;
    static constexpr int toolbar_margin_y = 10;
    static constexpr int toolbar_width_ = 200;
    static constexpr uint8_t maxplanes = 6;

    enum field {
        F_WIDTH,
        F_HEIGHT,
        F_BPLCON0,
        F_MODULO1,
        F_MODULO2,
        F_PL1,
        F_PL2,
        F_PL3,
        F_PL4,
        F_PL5,
        F_PL6,
        F_MAX,
    };
    static constexpr struct {
        const wchar_t* const name;
        int val;
    } fields[] = {
        { L"Width", 320 },
        { L"Height", 256 },
        { L"BPLCON0", 1<<12 },
        { L"Modulo1", 0 },
        { L"Modulo2", 0 },
        { L"Plane1", 0 },
        { L"Plane2", 0 },
        { L"Plane3", 0 },
        { L"Plane4", 0 },
        { L"Plane5", 0 },
        { L"Plane6", 0 },
    };
    static constexpr auto num_fields = sizeof(fields) / sizeof(*fields);
    static_assert(num_fields == F_MAX);
    static constexpr const char* const lpzz_mvfilter = "Memviz (*.mvpreset)\0*.mvpreset\0All files (*.*)\0*.*\0";
    struct settings {
        int fields[num_fields];
        uint16_t palette[32];
        bool other_playfield;
    } presets_[10];
    uint32_t current_preset_ = 0;
    bitmap_window* bitmap_window_;
    HWND field_combo_[num_fields];
    HWND update_button_;
    HWND palette_button_;
    HWND last_focus_ = nullptr;
    HWND playfield_selection_;
    std::vector<uint8_t> mem_;
    std::vector<uint16_t> custom_;

    memory_visualizer_window()
    {
        custom_.resize(0x100);
    }

    settings& current_settings()
    {
        return presets_[current_preset_];
    }

    bool on_create(HWND hwnd, const CREATESTRUCT&)
    {
        for (auto& p : presets_) {
            for (int i = 0; i < 32; ++i)
                p.palette[i] = rgb8_to_4(default_palette[i]);
            for (size_t i = 0; i < num_fields; ++i)
                p.fields[i] = fields[i].val;
            p.other_playfield = false;
        }

        const auto hInstance = GetModuleHandle(nullptr);
        const int w = toolbar_width_ - 2 * toolbar_margin_x;
        const int elem_y = 16;
        int y = toolbar_margin_y;

        const int radio_spacing = 60;
        for (size_t i = 0; i < 10; ++i) {
            wchar_t temp[20];
            wsprintfW(temp, L"&%d", static_cast<int>(i + 1) % 10);
            auto btn = CreateWindow(L"BUTTON", temp, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | (i ? 0 : WS_GROUP), toolbar_margin_x + static_cast<int>(i) * radio_spacing, y, 30, elem_y, hwnd, reinterpret_cast<HMENU>(400 + i), hInstance, nullptr);
            if (!i)
                SendMessage(btn, BM_SETCHECK, 1, 0);
        }
        CreateWindow(L"BUTTON", L"&Save", WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP, toolbar_margin_x * 2 + 10 * radio_spacing, y, 80, elem_y, hwnd, reinterpret_cast<HMENU>(350), hInstance, nullptr);
        CreateWindow(L"BUTTON", L"&Load", WS_CHILD | WS_VISIBLE | WS_TABSTOP, toolbar_margin_x * 3 + 10 * radio_spacing + 80, y, 80, elem_y, hwnd, reinterpret_cast<HMENU>(351), hInstance, nullptr);

        y += toolbar_margin_y * 2 + elem_y;

        auto& s = current_settings();

        bitmap_window_ = bitmap_window::create(toolbar_width_, y, fields[F_WIDTH].val, fields[F_HEIGHT].val, hwnd);

        for (size_t i = 0; i < num_fields; ++i) {
            const auto& f = fields[i];
            wchar_t text[256];
            s.fields[i] = f.val;
            if (i >= F_PL1 || i == F_BPLCON0)
                wsprintfW(text, L"$%X", f.val);
            else
                wsprintfW(text, L"%d", f.val);

            CreateWindow(L"STATIC", f.name, WS_CHILD | WS_VISIBLE, toolbar_margin_x, y, w, elem_y, hwnd, nullptr, hInstance, nullptr);
            y += toolbar_margin_y + elem_y;
            field_combo_[i] = CreateWindow(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, toolbar_margin_x, y, w, elem_y, hwnd, reinterpret_cast<HMENU>(101 + i), hInstance, nullptr);
            y += toolbar_margin_y + elem_y;
        }

        y += toolbar_margin_y;
        update_button_ = CreateWindow(L"BUTTON", L"&Update", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, toolbar_margin_x, y, w, elem_y * 2, hwnd, reinterpret_cast<HMENU>(IDOK), hInstance, nullptr);

        y += toolbar_margin_y*2 + elem_y;
        update_button_ = CreateWindow(L"BUTTON", L"From &custom", WS_CHILD | WS_VISIBLE | WS_TABSTOP, toolbar_margin_x, y, w, elem_y * 2, hwnd, reinterpret_cast<HMENU>(300), hInstance, nullptr);

        y += toolbar_margin_y * 2 + elem_y;
        palette_button_ = CreateWindow(L"BUTTON", L"&Palette", WS_CHILD | WS_VISIBLE | WS_TABSTOP, toolbar_margin_x, y, w, elem_y * 2, hwnd, reinterpret_cast<HMENU>(301), hInstance, nullptr);

        y += toolbar_margin_y * 2 + elem_y;
        playfield_selection_ = CreateWindow(L"BUTTON", L"&Other playfield", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, toolbar_margin_x, y, w, elem_y * 2, hwnd, reinterpret_cast<HMENU>(302), hInstance, nullptr);

        update();

        last_focus_ = update_button_;
        SetFocus(update_button_);

        return true;
    }

    void on_close(HWND)
    {
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
                last_focus_ = GetFocus();
            break;
        case WM_SETFOCUS:
            SetFocus(last_focus_);
            break;
        case WM_COMMAND: {
            const auto id = LOWORD(wParam);
            const auto code = HIWORD(wParam);
            if (id == IDOK && code == BN_CLICKED) {
                update();
            } else if (id == 300 && code == BN_CLICKED) {
                update_from_custom();
            } else if (id == 301 && code == BN_CLICKED) {
                palette_edit_dialog::run(hwnd, current_settings().palette, custom_);
                update();
            } else if (id == 302 && code == BN_CLICKED) {
                // Other playfield
                current_settings().other_playfield = !!SendMessage(playfield_selection_, BM_GETCHECK, 0, 0);
                update();
            } else if (id == 350 && code == BN_CLICKED) {
                // Save preset
                std::string filename;
                if (browse_for_file(hwnd, lpzz_mvfilter, filename, false, "mvpreset")) {
                    state_file sf { state_file::dir::save, filename };
                    handle_state(sf);
                }
            } else if (id == 351 && code == BN_CLICKED) {
                // Load preset
                std::string filename;
                if (browse_for_file(hwnd, lpzz_mvfilter, filename, true)) {
                    try {
                        state_file sf { state_file::dir::load, filename };
                        const auto old = current_preset_;
                        handle_state(sf);
                        SendDlgItemMessage(hwnd, 400 + old, BM_SETCHECK, 0, 0);
                        SendDlgItemMessage(hwnd, 400 + current_preset_, BM_SETCHECK, 1, 0);
                        set_controls_from_settings();
                        update();
                    } catch (const std::exception& e) {
                        MessageBoxA(hwnd, e.what(), "Load failed", MB_ICONERROR);
                    }
                }
            } else if (code == BN_CLICKED && id >= 400 && id <= 409) {
                if (current_preset_ != static_cast<uint32_t>(id - 400)) {
                    current_preset_ = id - 400;
                    set_controls_from_settings();
                    update();
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        }
        return window_base::wndproc(hwnd, uMsg, wParam, lParam);
    }

    void handle_state(state_file& sf)
    {
        const state_file::scope scope { sf, "Memory Visualizer Preset", 1 };
        sf.handle_blob(presets_, sizeof(presets_));
        sf.handle(current_preset_);
    }

    int get_field(enum field f)
    {
        assert(static_cast<size_t>(f) < num_fields);
        char buf[256];
        GetWindowTextA(field_combo_[f], buf, sizeof(buf));
        int val;
        if (buf[0] == '$') {
            if (!sscanf(buf + 1, "%X", reinterpret_cast<unsigned*>(&val))) {
                throw std::runtime_error("Could not convert \"" + std::string(buf) + "\" to value (hex)");
            }
        } else {
            if (!sscanf(buf, "%d", &val)) {
                throw std::runtime_error("Could not convert \"" + std::string(buf) + "\" to value (hex)");
            }
        }
        return val;
    }

    void update()
    {
        if (mem_.empty())
            return;

        settings& s = current_settings();

        try {
            for (size_t i = 0; i < num_fields; ++i)
                s.fields[i] = get_field(static_cast<enum field>(i));
            

            const int width    = s.fields[F_WIDTH];
            const int height   = s.fields[F_HEIGHT];
            const int bplcon0  = s.fields[F_BPLCON0];
            const int16_t mod1 = static_cast<int16_t>(s.fields[F_MODULO1]);
            const int16_t mod2 = static_cast<int16_t>(s.fields[F_MODULO1]);
            uint32_t pt[maxplanes];

            for (size_t i = 0; i < maxplanes; ++i)
                pt[i] = s.fields[F_PL1 + i];

            if (width != bitmap_window_->width() || height != bitmap_window_->height()) {
                bitmap_window_->set_size(width, height);
                SetWindowPos(bitmap_window_->handle(), nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE);
            }
            const bool altpf = s.other_playfield;
            const int nbpls = std::min(6, (bplcon0 >> 12) & 7);
            std::vector<uint32_t> bitmap(static_cast<size_t>(width * height));
            const uint32_t ptmask = static_cast<uint32_t>(mem_.size() - 1);
            for (int y = 0; y < height; ++y) {
                uint16_t data[maxplanes];
                uint32_t hamcolor = rgb4_to_8(s.palette[0]);
                for (int x = 0; x < width; ++x) {
                    if ((x & 15) == 0) {
                        for (int p = 0; p < nbpls; ++p) {
                            pt[p] &= ptmask;
                            data[p] = get_u16(&mem_[pt[p]]);
                            pt[p] += 2;
                        }
                    }

                    uint8_t idx = 0;
                    for (int p = 0; p < nbpls; ++p) {
                        if (data[p] & 0x8000)
                            idx |= 1 << p;
                        data[p] <<= 1;
                    }

                    if (bplcon0 & 0x400) { // Dual playfield
                        if (altpf) {
                            bitmap[x + y * width] = rgb4_to_8(s.palette[8 + (((idx & 2) >> 1) | ((idx & 8) >> 2) | ((idx & 32) >> 3))]);
                        } else {
                            bitmap[x + y * width] = rgb4_to_8(s.palette[(idx & 1) | ((idx & 4) >> 1) | ((idx & 16) >> 2)]);
                        }
                    } else if (bplcon0 & 0x800) { // HAM
                        const int ibits = ((nbpls + 1) & ~1) - 2;
                        const int val = (idx & 0xf) << (8 - ibits);
                        switch (idx >> ibits) {
                        case 0: // Palette entry
                            hamcolor = rgb4_to_8(s.palette[idx & 0xf]);
                            break;
                        case 1: // Modify B
                            hamcolor = (hamcolor & 0xffff00) | val;
                            break;
                        case 2: // Modify R
                            hamcolor = (hamcolor & 0x00ffff) | val << 16;
                            break;
                        case 3: // Modify G
                            hamcolor = (hamcolor & 0xff00ff) | val << 8;
                            break;
                        default:
                            assert(false);
                        }
                        bitmap[x + y * width] = hamcolor;
                    } else if (nbpls == 6) { // EHB
                        const auto val = rgb4_to_8(s.palette[idx & 0x1f]);
                        bitmap[x + y * width] = idx & 0x20 ? (val & 0xfefefe) >> 1 : val;
                    } else {
                        bitmap[x + y * width] = rgb4_to_8(s.palette[idx]);
                    }
                }

                for (int p = 0; p < nbpls; ++p)
                    pt[p] += p & 1 ? mod2 : mod1;
            }

            bitmap_window_->update_image(bitmap.data());
        } catch (const std::exception& e) {
            MessageBoxA(handle(), e.what(), "Error", MB_ICONSTOP);
        }
    }

    void set_controls_from_settings()
    {
        const settings& s = current_settings();
        for (size_t i = 0; i < num_fields; ++i) {
            wchar_t text[256];
            if (i >= F_PL1 || i == F_BPLCON0)
                wsprintfW(text, L"$%X", s.fields[i]);
            else
                wsprintfW(text, L"%d", s.fields[i]);
            SetWindowTextW(field_combo_[i], text);
        }
        SendMessage(playfield_selection_, BM_SETCHECK, s.other_playfield, 0);
    }

    void update_from_custom()
    {
        settings& s = current_settings();
        // BPLxPT
        for (int i = 0; i < maxplanes; ++i) {
            const auto ofs = 0xe0 / 2 + i * 2;
             s.fields[F_PL1 + i]  = custom_[ofs] << 16 | custom_[ofs + 1];
        }

        // COLOR
        for (int i = 0; i < 32; ++i) {
            s.palette[i] = custom_[0x180 / 2 + i];
        }

        const auto bplcon0 = custom_[0x100 / 2];
        const auto ddfstrt = custom_[0x92 / 2] & 0xfc;
        const auto ddfstop = custom_[0x94 / 2] & 0xfc;

        unsigned w;
        if (bplcon0 & 0x8000) {
            w = ((ddfstop - ddfstrt) / 4 + 2) * 16;
            if ((ddfstop - ddfstrt) / 4 & 1) // Hack
                w += 16;
        } else {
            w = ((ddfstop - ddfstrt) / 8 + 1) * 16;
        }


        const auto ydiwstart = custom_[0x8e / 2] >> 8;
        const auto ydiwstop = custom_[0x90 / 2] >> 8 | (custom_[0x90 / 2] & 0x8000 ? 0 : 0x100);

        s.fields[F_BPLCON0] = bplcon0;
        s.fields[F_WIDTH] = w;
        s.fields[F_HEIGHT] = ydiwstop - ydiwstart;
        s.fields[F_MODULO1] = static_cast<int16_t>(custom_[0x108 / 2]); // BPL1MOD
        s.fields[F_MODULO2] = static_cast<int16_t>(custom_[0x10A / 2]); // BPL2MOD
        set_controls_from_settings();
        InvalidateRect(handle(), nullptr, FALSE);
        update();
    }
};

class config_dialog : public window_base<config_dialog> {
public:
    static void run(HWND parent, std::array<std::string, 4>& disk_filenames, bool& joystick_mode, bool& fullscreen)
    {
        bool done = false;
        auto wnd = new config_dialog { done, disk_filenames, joystick_mode, fullscreen };
        EnableWindow(parent, FALSE);
        wnd->do_create(L"Configuration", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 800, 400, parent);

        MSG msg{};
        while (!done && GetMessage(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessage(wnd->handle(), &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (msg.message == WM_QUIT)
            PostQuitMessage(static_cast<int>(msg.wParam));
    }

private:
    friend window_base;
    static constexpr const wchar_t* const class_name_ = L"config_dialog";
    bool& done_;
    HWND parent_ = nullptr;
    HWND last_focus_ = nullptr;
    std::array<std::string, 4>& disk_filenames_;
    bool& joystick_mode_;
    bool& fullscreen_;
    static constexpr int max_disks = 2;
    HWND filename_edit_[max_disks];
    HWND joystick_mode_ctrl_;
    HWND fullscreen_ctrl_;

    config_dialog(bool& done, std::array<std::string, 4>& disk_filenames, bool& joystick_mode, bool& fullscreen)
        : done_ { done }
        , disk_filenames_ { disk_filenames }
        , joystick_mode_ { joystick_mode }
        , fullscreen_ { fullscreen }
    {
    }

    bool on_create(HWND hwnd, const CREATESTRUCT& cs)
    {
        parent_ = cs.hwndParent;

        auto hInstance = GetModuleHandle(nullptr);
        const int h = 20;
        const int xmargin = 10;
        const int ymargin = 10;
        int y = ymargin;
        for (size_t i = 0; i < max_disks; ++i) {
            char temp[256];
            snprintf(temp, sizeof(temp), "DF%d:", static_cast<int>(i));
            const int descw = 40;
            int x = xmargin;
            CreateWindowA("STATIC", temp, WS_CHILD | WS_VISIBLE, x, y, descw, h, hwnd, nullptr, hInstance, nullptr);
            x += xmargin + descw;
            filename_edit_[i] = CreateWindowA("EDIT", disk_filenames_[i].c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, x, y, 600, h, hwnd, reinterpret_cast<HMENU>(100 + i), hInstance, nullptr);
            x += xmargin + 600;
            CreateWindowA("BUTTON", "...", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, x, y, 24, h, hwnd, reinterpret_cast<HMENU>(200 + i), hInstance, nullptr);
            x += xmargin + 24;
            CreateWindowA("BUTTON", "Eject", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, x, y, 48, h, hwnd, reinterpret_cast<HMENU>(300 + i), hInstance, nullptr);
            x += xmargin + 48;
            y += h + ymargin;
        }

        const int button_w = 100;

        joystick_mode_ctrl_ = CreateWindowA("BUTTON", "&Joystick mode", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, xmargin, y, button_w*2, h, hwnd, reinterpret_cast<HMENU>(400), hInstance, nullptr);
        if (joystick_mode_)
            SendMessage(joystick_mode_ctrl_, BM_SETCHECK, 1, 0);

        fullscreen_ctrl_ = CreateWindowA("BUTTON", "&Fullscreen", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, xmargin*2 + button_w*2, y, button_w * 2, h, hwnd, reinterpret_cast<HMENU>(400), hInstance, nullptr);
        if (fullscreen_)
            SendMessage(fullscreen_ctrl_, BM_SETCHECK, 1, 0);
        y += h + ymargin;

        int x = xmargin;
        HWND hwndOK = CreateWindow(L"BUTTON", L"&Ok", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, xmargin, y, button_w, h, hwnd, reinterpret_cast<HMENU>(IDOK), hInstance, nullptr);
        x += button_w + xmargin;
        CreateWindow(L"BUTTON", L"&Cancel", WS_CHILD | WS_TABSTOP | WS_VISIBLE, x, y, button_w, h, hwnd, reinterpret_cast<HMENU>(IDCANCEL), hInstance, nullptr);
        x += button_w + xmargin;
        SetFocus(hwndOK);
        return true;
    }

    void on_close(HWND hwnd)
    {
        EnableWindow(parent_, TRUE);
        done_ = true;
        DestroyWindow(hwnd);
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
                last_focus_ = GetFocus();
            break;
        case WM_SETFOCUS:
            SetFocus(last_focus_);
            break;
        case WM_COMMAND: {
            const auto id = LOWORD(wParam);
            const auto code = HIWORD(wParam);
            if (id == IDOK && code == BN_CLICKED) {
                for (int i = 0; i < max_disks; ++i) {
                    const int l = GetWindowTextLengthA(filename_edit_[i]);
                    if (l) {
                        disk_filenames_[i].resize(l + 1);
                        GetWindowTextA(filename_edit_[i], &disk_filenames_[i][0], l + 1);
                        assert(disk_filenames_[i][l] == '\0');
                        disk_filenames_[i].pop_back();
                    } else {
                        disk_filenames_[i].clear();
                    }
                }
                joystick_mode_ = !!SendMessage(joystick_mode_ctrl_, BM_GETCHECK, 0, 0);
                fullscreen_ = !!SendMessage(fullscreen_ctrl_, BM_GETCHECK, 0, 0);
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (id == IDCANCEL && code == BN_CLICKED) {
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (id >= 200 && id < 200 + max_disks && code == BN_CLICKED) {
                std::string f;
                if (browse_for_file(hwnd, "Floppy image/exe (*.adf;*.dms;*.exe)\0*.adf;*.dms;*.exe\0Amiga Disk File (*.adf)\0*.adf\0Disk Masher System (*.dms)\0*.dms\0Executable (*.exe)\0*.exe\0All files (*.*)\0*.*\0", f))
                    SetWindowTextA(filename_edit_[id - 200], f.c_str());
            } else if (id >= 300 && id < 300 + max_disks && code == BN_CLICKED) {
                SetWindowTextA(filename_edit_[id - 300], "");
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        }
        return window_base::wndproc(hwnd, uMsg, wParam, lParam);
    }
};



constexpr int border_height = 8;
constexpr int led_width = 64;
constexpr int led_height = 8;
constexpr int extra_height = border_height*2 + led_height;


class drop_target : public IDropTarget {
public:
    using on_drop = std::function<void (const std::string&)>;

    explicit drop_target()
    {
    }

    ~drop_target()
    {
        assert(ref_count_ == 1);
    }

    void set_callback(const on_drop& callback)
    {
        assert(!callback_);
        callback_ = callback;
    }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++ref_count_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        return --ref_count_;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override
    {
        *ppv = nullptr;
        if (iid == __uuidof(IUnknown))
            *ppv = static_cast<IUnknown*>(this);
        else if (iid == __uuidof(IDropTarget))
            *ppv = static_cast<IDropTarget*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* /*pDataObj*/, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect) override
    {
        *pdwEffect &= DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect) override
    {
        *pdwEffect &= DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave(void) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect) override
    {
        *pdwEffect &= DROPEFFECT_COPY;

        if (*pdwEffect != DROPEFFECT_NONE && callback_) {
            // https://devblogs.microsoft.com/oldnewthing/20100503-00/?p=14183
            FORMATETC fmte = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            STGMEDIUM stgm;
            if (SUCCEEDED(pDataObj->GetData(&fmte, &stgm))) {
                HDROP hdrop = reinterpret_cast<HDROP>(stgm.hGlobal);
                UINT cFiles = DragQueryFile(hdrop, 0xFFFFFFFF, NULL, 0);
                for (UINT i = 0; i < cFiles; i++) {
                    const auto cchFile = MAX_PATH;
                    char szFile[cchFile];
                    UINT cch = DragQueryFileA(hdrop, i, szFile, cchFile);
                    if (cch > 0 && cch < cchFile) {
                        callback_(szFile);
                    }
                }
                ReleaseStgMedium(&stgm);
            }
        }
        return S_OK;
    }

private:
    ULONG ref_count_ = 1;
    on_drop callback_;
};

std::atomic<bool> quitting; // Bit of a hack

}

class gui::impl : public window_base<impl> {
public:
    static impl* create(int width, int height, const std::array<std::string, 4>& disk_filenames)
    {
        quitting = false;
        SetProcessDPIAware(); // Avoid GUI scaling (must be called before any windows are created)

        return new impl { width, height, disk_filenames };
    }

    ~impl()
    {
        quitting = true; // Going away!
        thread_.detach();
        OleUninitialize();
    }

    std::vector<event> update()
    {
        if (quitting) {
            return { event { event_type::quit, {} } };
        }
        std::vector<event> res;
        perform([this, &res]() {
            std::swap(res, events_);
        });
        return res;
    }

    void update_image(const uint32_t* data)
    {
        perform([this, data]() {
            bitmap_window_->update_image(data);
            for (int i = 0; i < 4; ++i) {
                if (disk_activity_[i].countdown) {
                    repaint_extra();
                    --disk_activity_[i].countdown;
                }
            }
        });
    }

    void led_state(uint8_t s)
    {
        assert(s < 2);
        perform([this, s]() {
            if (s != led_state_) {
                led_state_ = s;
                repaint_extra();
            }
        });
    }

    void serial_data(const std::vector<uint8_t>& data)
    {
        perform([this, data]() {
            ser_data_->append_text(std::string { data.begin(), data.end() });
        });
    }

    void set_active(bool act)
    {
        perform([this, act]() {
            if (act) {
                SetFocus(handle());
                SetForegroundWindow(handle());
                active_ = true;
            } else {
                if (mouse_captured_)
                    release_mouse();
                SetFocus(GetConsoleWindow());
                SetForegroundWindow(GetConsoleWindow());
                active_ = false;
            }
        });
        if (on_pause_)
            on_pause_(!act);
    }

    void handle_message(MSG& msg)
    {
        if (!IsDialogMessage(mem_vis_window_->handle(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void set_debug_memory(const std::vector<uint8_t>& mem, const std::vector<uint16_t>& custom)
    {
        mem_vis_window_->set_debug_memory(mem, custom);
    }

    void set_debug_windows_visible(bool visible)
    {
        ShowWindow(mem_vis_window_->handle(), visible ? SW_SHOW : SW_HIDE);
    }

    void set_on_pause_callback(const on_pause_callback& on_pause)
    {
        assert(!on_pause_);
        on_pause_ = on_pause;
    }

    void disk_activty(uint8_t idx, uint8_t track, bool write)
    {
        assert(idx < 4);
        perform([=, this]() {
            disk_activity_[idx].track = track;
            disk_activity_[idx].write = write;
            disk_activity_[idx].countdown = disk_activity_countdown_max;
            repaint_extra();
        });
    }

private:
    friend window_base<impl>;
    static constexpr const wchar_t* const class_name_ = L"Display";
    static constexpr const wchar_t* const title_ = L"Amiemu";
    int width_;
    int height_;
    std::vector<event> events_;
    uint8_t led_state_ = 0;
    bitmap_window* bitmap_window_ = nullptr;
    serial_data_window* ser_data_ = nullptr;
    memory_visualizer_window* mem_vis_window_ = nullptr;
    POINT last_mouse_pos_ { 0, 0 };
    bool mouse_captured_ = false;
    bool active_ = true;
    std::array<std::string, 4> disk_filenames_;
    bool joystick_mode_ = false;
    on_pause_callback on_pause_;
    struct disk_activity {
        uint8_t track;
        bool write;
        int countdown;
    } disk_activity_[4] = {};
    static constexpr int disk_activity_countdown_max = 25; // ~0.5s
    WINDOWPLACEMENT old_placement_;
    DWORD old_style_ = 0;
    drop_target drop_target_;
    static constexpr auto default_window_style_ = (WS_VISIBLE | WS_OVERLAPPEDWINDOW) & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    std::thread thread_;

    impl(int width, int height, const std::array<std::string, 4>& disk_filenames)
        : width_ { width }
        , height_ { height }
        , disk_filenames_ { disk_filenames }
    {
        HANDLE hStartEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!hStartEvent)
            throw_system_error("CreateEvent");

        thread_ = std::thread([this, hStartEvent]() {
            {
                MSG msg;
                PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE); // Make sure thread message queue is created
            }
            try {
                init();
                SetEvent(hStartEvent);
                thread_main_loop();
            } catch (const std::exception& e) {
                MessageBoxA(nullptr, e.what(), "Fatal error", MB_ICONERROR);
            }
        });

        WaitForSingleObject(hStartEvent, INFINITE);
        CloseHandle(hStartEvent);
    }

    void thread_main_loop()
    {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void perform(const std::function<void(void)>& f)
    {
        if (quitting)
            return;

        assert(std::this_thread::get_id() != thread_.get_id());

        SendMessage(handle(), WM_APP, 0, reinterpret_cast<LPARAM>(&f));
    }
    
    static void modify_window_class(WNDCLASS& wc)
    {
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    }

    void init()
    {
        if (const auto hr = OleInitialize(nullptr); FAILED(hr))
            throw_com_error("OleInitialize", hr);

        RECT r = { 0, 0, width_ * gfx_scale, height_ * gfx_scale + extra_height };
        AdjustWindowRect(&r, default_window_style_, FALSE);
        do_create(title_, default_window_style_, 100, 100, r.right - r.left, r.bottom - r.top, nullptr);

        // Position serial data window to the right of the main window
        GetWindowRect(handle(), &r);
        ser_data_ = serial_data_window::create(r.right + 10, r.top);
        mem_vis_window_ = memory_visualizer_window::create(r.right + 10, r.top);
        SetFocus(handle());
    }

    void repaint_extra()
    {
        RECT cr;
        GetClientRect(handle(), &cr);
        RECT r = { cr.left, cr.bottom - extra_height, cr.right, cr.bottom };
        InvalidateRect(handle(), &r, FALSE);
    }

    void do_enqueue_keyboard_event(bool pressed, WPARAM vk)
    {
        if (joystick_mode_) {
            switch (vk) {
            case VK_LEFT:
            case VK_RIGHT:
            case VK_UP:
            case VK_DOWN:
            case VK_LCONTROL:
            case VK_LMENU:
                events_.push_back(event {
                    .type = event_type::joystick,
                    .joystick = {
                        .left = GetKeyState(VK_LEFT) < 0,
                        .right = GetKeyState(VK_RIGHT) < 0,
                        .up = GetKeyState(VK_UP) < 0,
                        .down = GetKeyState(VK_DOWN) < 0,
                        .button1 = GetKeyState(VK_LCONTROL) < 0,
                        .button2 = GetKeyState(VK_LMENU) < 0,
                    } });
                return;
            }
        }

        const auto key = vk_to_scan[static_cast<uint8_t>(vk)];
        if (key == 0xff) {
            std::cerr << "Unhandled virtual key " << vk << " ($" << hexfmt(static_cast<uint8_t>(vk)) << ")\n";
            return;
        }
        if (pressed && (key == 0x63 || key == 0x66 || key == 0x67)) {
            if (GetKeyState(VK_LCONTROL) < 0 && GetKeyState(VK_HOME) < 0 && GetKeyState(VK_INSERT) < 0)
                events_.push_back({ event_type::reset, {} });
        }
        events_.push_back({ event_type::keyboard, { keyboard_event { pressed, key } } });
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
        do_enqueue_keyboard_event(pressed, vk);
    }

    void enqueue_mouse_button_event(bool pressed, bool left)
    {
        if (!active_)
            return;
        if (!mouse_captured_) {
            capture_mouse();
            return;
        }
        event evt;
        evt.type = event_type::mouse_button;
        evt.mouse_button = { pressed, left };
        events_.push_back(evt);
    }

    void capture_mouse()
    {
        HWND hwnd = handle();
        assert(!mouse_captured_);
        SetCapture(hwnd);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        POINT ptUL, ptLR;
        ptUL.x = rcClient.left;
        ptUL.y = rcClient.top;
        ptLR.x = rcClient.right + 1;
        ptLR.y = rcClient.bottom + 1;
        ClientToScreen(hwnd, &ptUL);
        ClientToScreen(hwnd, &ptLR);
        SetRect(&rcClient, ptUL.x, ptUL.y, ptLR.x, ptLR.y);
        ClipCursor(&rcClient);
        ShowCursor(FALSE);
        SetWindowText(hwnd, (std::wstring { title_ } + L" - Mouse captured").c_str());
        // Center mouse in window
        last_mouse_pos_ = { ptUL.x + (ptLR.x - ptUL.x) / 2, ptUL.y + (ptLR.y - ptUL.y) / 2 };
        SetCursorPos(last_mouse_pos_.x, last_mouse_pos_.y);
        mouse_captured_ = true;
    }

    void release_mouse()
    {
        assert(mouse_captured_);
        ShowCursor(TRUE);
        ClipCursor(NULL);
        ReleaseCapture();
        SetWindowText(handle(), title_);
        mouse_captured_ = false;
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_APP: {
            const auto& f = *reinterpret_cast<std::function<void(void)>*>(lParam);
            f();
            return 0;
        }
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            if (active_ && !(lParam & 0x4000'0000) && wParam != VK_F11 && wParam != VK_F12)
                enqueue_keyboard_event(true, wParam, lParam);
            return 0;
        case WM_SYSKEYUP:
        case WM_KEYUP:
            if (wParam == VK_F4 && GetKeyState(VK_MENU) < 0)
                SendMessage(hwnd, WM_CLOSE, 0, 0);
            else if (!active_)
                return 0;
            else if (wParam == VK_F11) {
                // For now do nothing
            }
            else if (wParam == VK_F12) {
                const bool was_captured = mouse_captured_;
                if (was_captured)
                    release_mouse();
                event evt;
                if (GetKeyState(VK_SHIFT) < 0) {
                    evt.type = event_type::debug_mode;
                    events_.push_back(evt);
                } else {
                    if (on_pause_)
                        on_pause_(true);
                    std::array<std::string, 4> disk_filenames = disk_filenames_;
                    const bool fullscreen_before = !!old_style_;
                    bool fullscreen = fullscreen_before;
                    config_dialog::run(hwnd, disk_filenames, joystick_mode_, fullscreen);
                    for (int i = 0; i < 4; ++i) {
                        if (disk_filenames[i] == disk_filenames_[i])
                            continue;
                        evt.type = event_type::disk_inserted;
                        evt.disk_inserted.drive = static_cast<uint8_t>(i);
                        snprintf(evt.disk_inserted.filename, sizeof(evt.disk_inserted.filename), "%s", disk_filenames[i].c_str());
                        disk_filenames_[i] = disk_filenames[i];
                        events_.push_back(evt);
                    }
                    if (fullscreen_before != fullscreen)
                        toggle_fullscreen();
                    if (was_captured)
                        capture_mouse();
                    if (on_pause_)
                        on_pause_(false);
                    SetForegroundWindow(hwnd);
                }
            } else
                enqueue_keyboard_event(false, wParam, lParam);
            return 0;
        case WM_LBUTTONDOWN:
            enqueue_mouse_button_event(true, true);
            break;
        case WM_LBUTTONUP:
            enqueue_mouse_button_event(false, true);
            break;
        case WM_RBUTTONDOWN:
            enqueue_mouse_button_event(true, false);
            break;
        case WM_RBUTTONUP:
            enqueue_mouse_button_event(false, false);
            break;
        case WM_MOUSEMOVE: {
            if (!mouse_captured_)
                break;
            POINT mouse_pos = { LOWORD(lParam), HIWORD(lParam) };
            ClientToScreen(hwnd, &mouse_pos);
            event evt;
            evt.type = event_type::mouse_move;
            evt.mouse_move = { mouse_pos.x - last_mouse_pos_.x, mouse_pos.y - last_mouse_pos_.y };
            events_.push_back(evt);
            SetCursorPos(last_mouse_pos_.x, last_mouse_pos_.y);
            break;
        }
        case WM_ACTIVATEAPP:
            if (!wParam) {
                if (mouse_captured_)
                    release_mouse();

                // Hack: Fake release of all modifier keys when switching away (to avoid stuck keys)
                constexpr uint8_t vks[] = {
                    VK_LSHIFT,
                    VK_RSHIFT,
                    VK_LMENU,
                    VK_RMENU,
                    VK_LCONTROL,
                    VK_RCONTROL
                };
                for (auto vk : vks)
                    if (GetKeyState(vk) < 0)
                        do_enqueue_keyboard_event(false, vk);
            }
            break;
        }
        return window_base::wndproc(hwnd, uMsg, wParam, lParam);
    }

    bool on_create(HWND hwnd, const CREATESTRUCT&)
    {
        if (const auto hr = RegisterDragDrop(hwnd, &drop_target_); FAILED(hr)) {
            std::cerr << "RegisterDragDrop failed: 0x" << hexfmt(hr) << "\n";
            return false;
        }
        bitmap_window_ = bitmap_window::create(0, 0, width_, height_, hwnd);
        if (!bitmap_window_) {
            RevokeDragDrop(hwnd);
            return false;
        }
        MoveWindow(bitmap_window_->handle(), 0, 0, width_ * gfx_scale, height_ * gfx_scale, FALSE);
        drop_target_.set_callback([&](const std::string& filename) {
            event evt;
            const uint8_t drive = 0;
            evt.type = event_type::disk_inserted;
            evt.disk_inserted.drive =drive;
            snprintf(evt.disk_inserted.filename, sizeof(evt.disk_inserted.filename), "%s", filename.c_str());
            disk_filenames_[drive] = filename;
            events_.push_back(evt);

        });
        return true;
    }

    void on_destroy(HWND hwnd)
    {
        if (mouse_captured_)
            release_mouse();
        RevokeDragDrop(hwnd);
        PostQuitMessage(0);
    }

    void on_paint(HWND hwnd)
    {
        PAINTSTRUCT ps;
        if (BeginPaint(hwnd, &ps) && !IsRectEmpty(&ps.rcPaint)) {
            RECT r;
            GetClientRect(handle(), &r);
            const auto w = r.right - r.left;
            const auto h = (r.bottom - r.top) - extra_height;
            RECT bck { 0, h, w, h + extra_height };
            FillRect(ps.hdc, &bck, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetBkColor(ps.hdc, RGB(0, 0, 0));
            for (int i = 0; i < 4; ++i) {
                const auto& act = disk_activity_[i];
                if (act.countdown) {
                    int n = act.countdown * 255 / disk_activity_countdown_max;
                    SetTextColor(ps.hdc, act.write ? RGB(n, 0, 0) : RGB(n, n, n));
                    char msg[16];
                    n = _snprintf(msg, sizeof(msg), "DF%d: $%02X", i, act.track);
                    TextOutA(ps.hdc, 30 + 100 * i, h + 5, msg, n);
                }
            }

            RECT power_led_rect = { w - led_width - 16, (h + extra_height) - led_height - border_height, w - 16, (h + extra_height) - border_height };
            HBRUSH power_led_brush = CreateSolidBrush(led_state_ & 1 ? RGB(0, 255, 0) : RGB(0, 0, 0));
            FillRect(ps.hdc, &power_led_rect, power_led_brush);
            DeleteObject(power_led_brush);
            EndPaint(hwnd, &ps);
        }
    }

    // https://devblogs.microsoft.com/oldnewthing/20100412-00/?p=14353
    void toggle_fullscreen()
    {
        const auto style = GetWindowLong(handle(), GWL_STYLE);
        auto bw = width_ * gfx_scale;
        auto bh = height_ * gfx_scale;

        if (style & WS_OVERLAPPEDWINDOW) {
            MONITORINFO mi {};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetWindowPlacement(handle(), &old_placement_) && GetMonitorInfo(MonitorFromWindow(handle(), MONITOR_DEFAULTTOPRIMARY), &mi)) {
                const auto mw = mi.rcMonitor.right - mi.rcMonitor.left;
                const auto mh = mi.rcMonitor.bottom - mi.rcMonitor.top;

                const double scale = std::min(static_cast<double>(mw) / bw, static_cast<double>(mh - extra_height) / bh);

                bw = static_cast<unsigned>(bw * scale);
                bh = static_cast<unsigned>(bh * scale);

                old_style_ = style;
                SetWindowLong(handle(), GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(handle(), HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mw, mh, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                MoveWindow(bitmap_window_->handle(), (mw - bw)/2, ((mh - extra_height) - bh)/2, bw, bh, TRUE);
            }
        } else {
            SetWindowLong(handle(), GWL_STYLE, old_style_);
            SetWindowPlacement(handle(), &old_placement_);
            SetWindowPos(handle(), NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            MoveWindow(bitmap_window_->handle(), 0, 0, bw, bh, TRUE);
            old_style_ = 0;
        }
    }
};

gui::gui(unsigned width, unsigned height, const std::array<std::string, 4>& disk_filenames)
    : impl_ { impl::create(static_cast<int>(width), static_cast<int>(height), disk_filenames) }
{
}

gui::~gui() = default; // impl_ already destroyed

std::vector<gui::event> gui::update()
{
    return impl_->update();
}

void gui::update_image(const uint32_t* data)
{
    impl_->update_image(data);
}

void gui::led_state(uint8_t s)
{
    impl_->led_state(s);
}

void gui::serial_data(const std::vector<uint8_t>& data)
{
    impl_->serial_data(data);
}

void gui::set_active(bool act)
{
    impl_->set_active(act);
}

bool gui::debug_prompt(std::string& line)
{
    std::cout << "> " << std::flush;
    std::getline(std::cin, line);
    return !!std::cin && !quitting;
}

void gui::set_debug_memory(const std::vector<uint8_t>& mem, const std::vector<uint16_t>& custom)
{
    impl_->set_debug_memory(mem, custom);
}

void gui::set_debug_windows_visible(bool visible)
{
    impl_->set_debug_windows_visible(visible);
}

void gui::set_on_pause_callback(const on_pause_callback& on_pause)
{
    impl_->set_on_pause_callback(on_pause);
}

void gui::disk_activty(uint8_t idx, uint8_t track, bool write)
{
    impl_->disk_activty(idx, track, write);
}
