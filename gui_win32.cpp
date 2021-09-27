#include "gui.h"
#include <cassert>
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace {

void throw_system_error(const std::string& what, const unsigned error_code = GetLastError())
{
    assert(error_code != ERROR_SUCCESS);
    throw std::system_error(error_code, std::system_category(), what);
}

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

    bool update()
    {
        bool quit = false;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return !quit;
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
        case WM_KEYUP:
            if (wParam == VK_ESCAPE)
                PostQuitMessage(0);
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

bool gui::update()
{
    return impl_->update();
}

void gui::update_image(const uint32_t* data)
{
    impl_->update_image(data);
}