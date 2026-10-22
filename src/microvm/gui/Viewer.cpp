#include "Viewer.h"
#include <chrono>
#include <deque>
#include <thread>
#include <vector>
#if defined(__linux__) && defined(GUARDIAN_SDL2)
#include <SDL.h>
#elif defined(_WIN32)
#include <Windows.h>
#include <windowsx.h>
#endif
namespace av::microvm::gui {
using namespace guardian::graphics;
struct Viewer::State {
    bool closed = false, focused = false;
    int32_t x = 0, y = 0;
    uint32_t cursor = 0;
    std::vector<uint32_t> pixels = std::vector<uint32_t>(PixelCount, 0xFF172536);
#if defined(__linux__) && defined(GUARDIAN_SDL2)
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    uint32_t id = 0;
    ~State() {
        if (texture)
            SDL_DestroyTexture(texture);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
#elif defined(_WIN32)
    HWND window = nullptr;
    std::deque<Event> events;
    void Add(Event e) {
        if (events.size() < Depth)
            events.push_back(e);
    }
    static LRESULT CALLBACK Procedure(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto *s = reinterpret_cast<State *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            s = static_cast<State *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        }
        if (!s)
            return DefWindowProcW(h, msg, wp, lp);
        if (msg == WM_CLOSE) {
            s->closed = true;
            return 0;
        }
        if (msg == WM_KILLFOCUS) {
            s->focused = false;
            s->events.clear();
            s->Add({Input::Blur});
            return 0;
        }
        if (msg == WM_SETFOCUS) {
            s->focused = true;
            return 0;
        }
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            RECT rect{};
            GetClientRect(h, &rect);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = Width;
            info.bmiHeader.biHeight = -LONG(Height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, Width, Height, s->pixels.data(), &info,
                          DIB_RGB_COLORS, SRCCOPY);
            EndPaint(h, &ps);
            return 0;
        }
        if (!s->focused || GetForegroundWindow() != h)
            return DefWindowProcW(h, msg, wp, lp);
        if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
            RECT r{};
            GetClientRect(h, &r);
            const auto px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            if (px >= 0 && py >= 0 && px < r.right && py < r.bottom && r.right && r.bottom) {
                s->x = px * int32_t(Width) / r.right;
                s->y = py * int32_t(Height) / r.bottom;
                s->Add({msg == WM_MOUSEMOVE ? Input::Move : Input::Button, 0, s->x, s->y, 0,
                        msg == WM_LBUTTONDOWN ? 1u : 0u});
            }
            return 0;
        }
        if (msg == WM_CHAR && (wp == 8 || wp == 9 || wp == 13 || wp == 27 || (wp >= 32 && wp <= 126))) {
            s->Add({Input::Key, 0, 0, 0, uint32_t(wp)});
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
    ~State() {
        if (window)
            DestroyWindow(window);
    }
#endif
};
Viewer::Viewer() : state_(std::make_unique<State>()) {
#if defined(__linux__) && defined(GUARDIAN_SDL2)
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(SDL_GetError());
    auto &s = *state_;
    SDL_EventState(SDL_DROPFILE, SDL_DISABLE);
    SDL_EventState(SDL_DROPTEXT, SDL_DISABLE);
    s.window = SDL_CreateWindow("Guardian - aplicativos convidados", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, 640, 480, 0);
    if (!s.window)
        throw std::runtime_error(SDL_GetError());
    s.id = SDL_GetWindowID(s.window);
    s.renderer = SDL_CreateRenderer(s.window, -1, SDL_RENDERER_SOFTWARE);
    if (!s.renderer)
        throw std::runtime_error(SDL_GetError());
    s.texture =
        SDL_CreateTexture(s.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Width, Height);
    if (!s.texture)
        throw std::runtime_error(SDL_GetError());
    SDL_StartTextInput();
#elif defined(_WIN32)
    auto &s = *state_;
    WNDCLASSW c{};
    c.lpfnWndProc = State::Procedure;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"GuardianGuestGraphics11";
    c.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Graphics window registration failed");
    RECT r{0, 0, 640, 480};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    s.window =
        CreateWindowW(c.lpszClassName, L"Guardian - aplicativos convidados", style, CW_USEDEFAULT,
                      CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, c.hInstance, &s);
    if (!s.window)
        throw std::runtime_error("Graphics window creation failed");
    ShowWindow(s.window, SW_SHOW);
#else
    throw std::runtime_error("Native viewer unavailable: build with SDL2 development headers");
#endif
}
void Viewer::SmokeTest() {
    FrameInfo frame{};
    std::vector<uint32_t> pixels(PixelCount, 0xFF286CA5);
    Display(frame, pixels.data());
#if defined(__linux__) && defined(GUARDIAN_SDL2)
    // Push to this process's SDL queue only; never synthesize OS-wide input.
    SDL_Event e{};
    e.type = SDL_WINDOWEVENT;
    e.window.windowID = state_->id + 100;
    e.window.event = SDL_WINDOWEVENT_CLOSE;
    if (SDL_PushEvent(&e) < 0)
        throw std::runtime_error(SDL_GetError());
    const auto ignored = Poll();
    if (ignored.type == Input::Close)
        throw std::runtime_error("Foreign window event escaped filter");
    e.window.windowID = state_->id;
    if (SDL_PushEvent(&e) < 0)
        throw std::runtime_error(SDL_GetError());
    if (Poll().type != Input::Close)
        throw std::runtime_error("Own window close was lost");
#elif defined(_WIN32)
    PostMessageW(state_->window, WM_CLOSE, 0, 0);
    if (Poll().type != Input::Close)
        throw std::runtime_error("Own window close was lost");
#endif
}
Viewer::~Viewer() = default;
Event Viewer::Poll() {
    auto &s = *state_;
    if (s.closed)
        return {Input::Close};
#if defined(__linux__) && defined(GUARDIAN_SDL2)
    SDL_Event e{};
    uint32_t budget = 128;
    while (budget-- && SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            s.closed = true;
            return {Input::Close};
        }
        if (e.type == SDL_WINDOWEVENT && e.window.windowID == s.id) {
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
                s.closed = true;
                return {Input::Close};
            }
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                s.focused = false;
                return {Input::Blur};
            }
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                s.focused = true;
        }
        if (!s.focused || SDL_GetKeyboardFocus() != s.window)
            continue;
        if ((e.type == SDL_MOUSEMOTION && e.motion.windowID == s.id) ||
            ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.windowID == s.id &&
             e.button.button == SDL_BUTTON_LEFT)) {
            int w = 0, h = 0;
            SDL_GetWindowSize(s.window, &w, &h);
            const auto px = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                       py = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
            if (w <= 0 || h <= 0 || px < 0 || py < 0 || px >= w || py >= h)
                continue;
            s.x = px * int32_t(Width) / w;
            s.y = py * int32_t(Height) / h;
            return {e.type == SDL_MOUSEMOTION ? Input::Move : Input::Button,
                    0,
                    s.x,
                    s.y,
                    0,
                    e.type == SDL_MOUSEBUTTONDOWN ? 1u : 0u};
        }
        if (e.type == SDL_TEXTINPUT && e.text.windowID == s.id && e.text.text[0] >= 32 &&
            e.text.text[0] <= 126 && !e.text.text[1])
            return {Input::Key, 0, 0, 0, uint32_t(e.text.text[0])};
        if (e.type == SDL_KEYDOWN && e.key.windowID == s.id) {
            const auto key = e.key.keysym.sym;
            if (key == 8 || key == 9 || key == 13 || key == 27)
                return {Input::Key, 0, 0, 0, uint32_t(key)};
        }
    }
#elif defined(_WIN32)
    MSG msg{};
    uint32_t budget = 128;
    while (budget-- && PeekMessageW(&msg, s.window, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (s.closed)
        return {Input::Close};
    if (!s.events.empty()) {
        const auto e = s.events.front();
        s.events.pop_front();
        return e;
    }
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    return {};
}
void Viewer::Display(const FrameInfo &frame, const uint32_t *p) {
    if (!ValidFrame(frame))
        throw std::runtime_error("Viewer frame rejected");
    auto &s = *state_;
    s.pixels.assign(p, p + PixelCount);
    s.cursor = frame.cursor;
    // Cursor is drawn inside the controlled surface. No host cursor warp/capture.
    const auto dot = [&](int32_t x, int32_t y) {
        if (x >= 0 && y >= 0 && x < int32_t(Width) && y < int32_t(Height))
            s.pixels[uint32_t(y) * Width + uint32_t(x)] = 0xFFFFFFFF;
    };
    for (int32_t d = -4; d <= 4; ++d) {
        if (s.cursor == 0) {
            dot(s.x, s.y + d + 4);
            dot(s.x + (d + 4) / 2, s.y + d + 4);
        } else if (s.cursor == 1) {
            dot(s.x + d, s.y);
            dot(s.x, s.y + d);
        } else {
            dot(s.x, s.y + d);
            dot(s.x + d / 2, s.y - 4);
            dot(s.x + d / 2, s.y + 4);
        }
    }
#if defined(__linux__) && defined(GUARDIAN_SDL2)
    if (SDL_UpdateTexture(s.texture, nullptr, s.pixels.data(), Width * 4) != 0 ||
        SDL_RenderClear(s.renderer) != 0 || SDL_RenderCopy(s.renderer, s.texture, nullptr, nullptr) != 0)
        throw std::runtime_error(SDL_GetError());
    SDL_RenderPresent(s.renderer);
#elif defined(_WIN32)
    InvalidateRect(s.window, nullptr, FALSE);
    UpdateWindow(s.window);
#endif
}
} // namespace av::microvm::gui
