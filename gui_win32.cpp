#include "gui.h"
#include <cassert>
#include <system_error>
#include <array>
#include <iostream>
#include <thread>
#include "ioutil.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commdlg.h>

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
    // help 0x5F
    map[VK_LSHIFT] = 0x60;
    map[VK_RSHIFT] = 0x61;
    // capslock 0x62
    map[VK_LCONTROL] = 0x63; // No right control!
    map[VK_LMENU] = 0x64;
    map[VK_RMENU] = 0x65;
    map[VK_HOME] = 0x66;  // left amiga 0x66
    map[VK_INSERT] = 0x67; // right amiga 0x67
 
    return map;
}();

gui::disk_inserted_event browse_for_file(HWND hwnd, const char* zz_filter)
{
    OPENFILENAMEA ofn;
    gui::disk_inserted_event evt;

    ZeroMemory(&ofn, sizeof(ofn));
    evt.filename[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = zz_filter;
    ofn.lpstrFile = evt.filename;
    ofn.nMaxFile = sizeof(evt.filename);
    ofn.Flags = OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn))
        evt.filename[0] = 0;
    return evt;
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


class serial_data_window {
public:
    static serial_data_window* create()
    {
        std::unique_ptr<serial_data_window> wnd { new serial_data_window {} };
        return wnd.release();
    }

    HWND hwnd()
    {
        return hwnd_;
    }

    void append_text(const std::string& strdat)
    {
        if (!visible_) {
            ShowWindow(hwnd_, SW_NORMAL);
            visible_ = true;
        }

        int index = GetWindowTextLength(edit_window_);
        //SetFocus(edit_window_);
        SendMessageA(edit_window_, EM_SETSEL, (WPARAM)index, (LPARAM)index);
        SendMessageA(edit_window_, EM_REPLACESEL, 0, (LPARAM)strdat.c_str());
        SendMessage(edit_window_, EM_LINESCROLL, 0, 0xFFFF); // Scroll to bottom (hackish)
    }

private:
    static constexpr const wchar_t* const class_name_ = L"SerialDataWindow";
    HWND hwnd_ = nullptr;
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

        HINSTANCE const hInstance = GetModuleHandle(nullptr);
        WNDCLASS wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &s_wndproc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = class_name_;
        if (!RegisterClass(&wc)) {
            throw_system_error("RegisterClass");
        }
        const DWORD style = WS_OVERLAPPEDWINDOW;

        const char* title = "Serial console";
        if (!CreateWindow(class_name_, std::wstring(title, title + strlen(title)).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, 800, 480, nullptr, nullptr, hInstance, this)) {
            throw_system_error("CreateWindow");
        }
        assert(hwnd_);
    }

    static LRESULT CALLBACK s_wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        serial_data_window* self = nullptr;
        if (uMsg == WM_NCCREATE) {
            self = reinterpret_cast<serial_data_window*>(reinterpret_cast<const CREATESTRUCT*>(lParam)->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<serial_data_window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        const auto ret = self ? self->wndproc(hwnd, uMsg, wParam, lParam) : DefWindowProc(hwnd, uMsg, wParam, lParam);
        if (uMsg == WM_NCDESTROY && self) {
            self->hwnd_ = nullptr;
            delete self;
        }
        return ret;
    }

    LRESULT wndproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_CREATE:
            return on_create() ? 0 : -1;
        case WM_DESTROY:
            break;
        case WM_SIZE:
            on_size(LOWORD(lParam), HIWORD(lParam));
            break;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    bool on_create()
    {
        edit_window_ = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, 0, 100, 100, hwnd_, nullptr, nullptr, nullptr);
        if (!edit_window_)
            return false;
        SendMessage(edit_window_, WM_SETFONT, (WPARAM)edit_font_.get(), FALSE);
        return true;
    }

    void on_size(UINT client_width, UINT client_height)
    {
        SetWindowPos(edit_window_, nullptr, 0, 0, client_width, client_height, SWP_NOZORDER);
    }
};


constexpr int border_height = 8;
constexpr int led_width = 64;
constexpr int led_height = 8;
constexpr int extra_height = border_height*2 + led_height;

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
        RECT r = { 0, 0, width_, height_ + extra_height };
        AdjustWindowRect(&r, style, FALSE);

        if (!CreateWindow(class_name_, title_, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInstance, this)) {
            throw_system_error("CreateWindow");
        }
        assert(hwnd_);

        ser_data_ = serial_data_window::create();

        // Move serial data window to the right of the main window
        GetWindowRect(hwnd_, &r);
        SetWindowPos(ser_data_->hwnd(), nullptr, r.right + 10, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetFocus(hwnd_);
    }

    ~impl()
    {
        if (hwnd_)
            DestroyWindow(hwnd_);
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
        redraw();
    }

    void led_state(uint8_t s)
    {
        assert(s < 2);
        if (s != led_state_) {
            led_state_ = s;
            redraw();
        }
    }

    void serial_data(const std::vector<uint8_t>& data)
    {
        ser_data_->append_text(std::string { data.begin(), data.end() });
        SetFocus(hwnd_);
    }

    void set_active(bool act)
    {
        if (act) {
            SetFocus(hwnd_);
            SetForegroundWindow(hwnd_);
            active_ = true;
        } else {
            if (mouse_captured_)
                release_mouse();
            SetFocus(GetConsoleWindow());
            SetForegroundWindow(GetConsoleWindow());
            active_ = false;
        }
    }

private:
    static constexpr const wchar_t* const class_name_ = L"Display";
    static constexpr const wchar_t* const title_ = L"Amiemu";
    int width_;
    int height_;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HBITMAP hbm_ = nullptr;
    std::vector<event> events_;
    uint8_t led_state_ = 0;
    serial_data_window* ser_data_ = nullptr;
    POINT last_mouse_pos_ { 0, 0 };
    bool mouse_captured_ = false;
    bool active_ = true;

    void redraw()
    {
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

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

    void do_enqueue_keyboard_event(bool pressed, WPARAM vk)
    {
        const auto key = vk_to_scan[static_cast<uint8_t>(vk)];
        if (key == 0xff) {
            std::cerr << "Unhandled virtual key " << vk << " ($" << hexfmt(static_cast<uint8_t>(vk)) << ")\n";
            return;
        }
        events_.push_back({ event_type::keyboard, keyboard_event { pressed, key } });
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
        assert(!mouse_captured_);
        SetCapture(hwnd_);
        RECT rcClient;
        GetClientRect(hwnd_, &rcClient);
        POINT ptUL, ptLR;
        ptUL.x = rcClient.left;
        ptUL.y = rcClient.top;
        ptLR.x = rcClient.right + 1;
        ptLR.y = rcClient.bottom + 1;
        ClientToScreen(hwnd_, &ptUL);
        ClientToScreen(hwnd_, &ptLR);
        SetRect(&rcClient, ptUL.x, ptUL.y, ptLR.x, ptLR.y);
        ClipCursor(&rcClient);
        ShowCursor(FALSE);
        SetWindowText(hwnd_, (std::wstring { title_ } + L" - Mouse captured").c_str());
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
        SetWindowText(hwnd_, title_);
        mouse_captured_ = false;
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
            if (active_ && !(lParam & 0x4000'0000) && wParam != VK_F11 && wParam != VK_F12)
                enqueue_keyboard_event(true, wParam, lParam);
            return 0;
        case WM_SYSKEYUP:
        case WM_KEYUP:
            if (wParam == VK_F4 && GetKeyState(VK_MENU) < 0)
                PostQuitMessage(0);
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
                    set_active(false);
                } else {
                    evt.type = event_type::disk_inserted;
                    evt.disk_inserted = browse_for_file(hwnd_, "Amiga Disk File (*.adf)\0*.adf\0All files (*.*)\0*.*\0");
                    events_.push_back(evt);
                    if (was_captured)
                        capture_mouse();
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
            ClientToScreen(hwnd_, &mouse_pos);
            event evt;
            evt.type = event_type::mouse_move;
            evt.mouse_move = { mouse_pos.x - last_mouse_pos_.x, mouse_pos.y - last_mouse_pos_.y };
            events_.push_back(evt);
            SetCursorPos(last_mouse_pos_.x, last_mouse_pos_.y);
            break;
        }
        case WM_ACTIVATEAPP:
            if (!wParam && mouse_captured_)
                release_mouse();
            break;
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
        if (mouse_captured_)
            release_mouse();
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
            BitBlt(ps.hdc, 0, 0, width_, height_, hdc_, 0, 0, SRCCOPY);

            RECT power_led_rect = { width_ - led_width - 16, (height_ + extra_height) - led_height - border_height, width_ - 16, (height_ + extra_height) - border_height };
            HBRUSH power_led_brush = CreateSolidBrush(led_state_ & 1 ? RGB(0, 255, 0) : RGB(0, 0, 0));
            FillRect(ps.hdc, &power_led_rect, power_led_brush);
            DeleteObject(power_led_brush);

            EndPaint(hwnd_, &ps);
        }
    }
};

gui::gui(unsigned width, unsigned height)
    : impl_ { std::make_unique<impl>(static_cast<int>(width), static_cast<int>(height)) }
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

bool debug_prompt(std::string& line)
{
    const DWORD gui_thread = GetCurrentThreadId();

    // Yes, waste a thread for each line..
    std::thread t {
        [&]() {
            std::cout << "> " << std::flush;
            std::getline(std::cin, line);
            PostThreadMessage(gui_thread, WM_APP, 0, 0);
        }
    };

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) && msg.message != WM_QUIT) {
        if (msg.message == WM_APP)
            break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (msg.message == WM_QUIT) {
        PostQuitMessage(static_cast<int>(msg.wParam));
        t.detach(); // We don't care about the result any more (dangerous)
    } else {
        t.join();
    }

    return !!std::cin && msg.message != WM_QUIT;
}