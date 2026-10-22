#include "guardian/Graphics.h"
namespace guardian::graphics {
namespace {
bool Ascii(const uint8_t *text, uint32_t size) noexcept {
    for (uint32_t i = 0; i < size; ++i)
        if (text[i] < 32 || text[i] > 126)
            return false;
    return true;
}
bool Dpi(uint32_t n) noexcept { return n == 96 || n == 144 || n == 192; }
bool Bounds(const WindowInfo &w) noexcept {
    return w.x >= 0 && w.y >= 0 && w.w >= 40 && w.h >= 36 && w.w <= Width && w.h <= Height &&
           uint32_t(w.x) <= Width - w.w && uint32_t(w.y) <= Height - w.h && Dpi(w.dpi);
}
// Project-owned 5x7 bitmap font. Columns, least significant bit at the top.
constexpr uint8_t font[37][5] = {
    {62, 81, 73, 69, 62},  {0, 66, 127, 64, 0},   {66, 97, 81, 73, 70},   {33, 65, 69, 75, 49},
    {24, 20, 18, 127, 16}, {39, 69, 69, 69, 57},  {60, 74, 73, 73, 48},   {1, 113, 9, 5, 3},
    {54, 73, 73, 73, 54},  {6, 73, 73, 41, 30},   {126, 17, 17, 17, 126}, {127, 73, 73, 73, 54},
    {62, 65, 65, 65, 34},  {127, 65, 65, 34, 28}, {127, 73, 73, 73, 65},  {127, 9, 9, 9, 1},
    {62, 65, 73, 73, 122}, {127, 8, 8, 8, 127},   {0, 65, 127, 65, 0},    {32, 64, 65, 63, 1},
    {127, 8, 20, 34, 65},  {127, 64, 64, 64, 64}, {127, 2, 12, 2, 127},   {127, 4, 8, 16, 127},
    {62, 65, 65, 65, 62},  {127, 9, 9, 9, 6},     {62, 65, 81, 33, 94},   {127, 9, 25, 41, 70},
    {70, 73, 73, 73, 49},  {1, 1, 127, 1, 1},     {63, 64, 64, 64, 63},   {31, 32, 64, 32, 31},
    {63, 64, 56, 64, 63},  {99, 20, 8, 20, 99},   {7, 8, 112, 8, 7},      {97, 81, 73, 69, 67},
    {2, 1, 81, 9, 6}};
struct Raster {
    uint32_t *pixels;
    int32_t left, top, right, bottom;
    void Box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) const noexcept {
        int32_t r = x + w, b = y + h;
        if (x < left)
            x = left;
        if (y < top)
            y = top;
        if (r > right)
            r = right;
        if (b > bottom)
            b = bottom;
        for (int32_t yy = y; yy < b; ++yy)
            for (int32_t xx = x; xx < r; ++xx)
                pixels[uint32_t(yy) * Width + uint32_t(xx)] = 0xFF000000u | (color & 0xFFFFFF);
    }
    void Text(int32_t x, int32_t y, const uint8_t *s, uint32_t n, uint32_t color,
              uint32_t dpi) const noexcept {
        for (uint32_t i = 0; i < n; ++i) {
            auto c = s[i];
            if (c == ' ')
                continue;
            if (c >= 'a' && c <= 'z')
                c -= 32;
            const auto index = c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 36);
            for (uint32_t col = 0; col < 5; ++col)
                for (uint32_t row = 0; row < 7; ++row)
                    if (font[index][col] & (1u << row)) {
                        auto a = (i * 6 + col) * dpi / 96, b = row * dpi / 96;
                        Box(x + int32_t(a), y + int32_t(b), int32_t((i * 6 + col + 1) * dpi / 96 - a),
                            int32_t((row + 1) * dpi / 96 - b), color);
                    }
        }
    }
};
} // namespace
bool ValidInput(const Event &e) noexcept {
    if (e.target || e.serial || e.x < 0 || e.y < 0 || e.x >= int32_t(Width) || e.y >= int32_t(Height) ||
        e.buttons > 1)
        return false;
    switch (e.type) {
    case Input::Move:
        return !e.key && !e.buttons;
    case Input::Button:
        return !e.key;
    case Input::Key:
        return !e.buttons &&
               (e.key == 8 || e.key == 9 || e.key == 13 || e.key == 27 || (e.key >= 32 && e.key <= 126));
    case Input::Blur:
    case Input::Close:
        return !e.key && !e.buttons && !e.x && !e.y;
    default:
        return false;
    }
}
bool ValidFrame(const FrameInfo &f) noexcept {
    if (f.width != Width || f.height != Height || f.stride != Width * 4 || f.bytes != PixelCount * 4 ||
        f.count > MaxWindows || f.cursor > 2 || f.reserved)
        return false;
    bool focused = !f.focus;
    uint32_t modal = 0;
    for (uint32_t i = 0; i < f.count; ++i) {
        const auto &w = f.windows[i];
        if (!w.handle || w.handle > 256 || !Bounds(w) || w.flags > 1 || w.title[31])
            return false;
        for (uint32_t k = 0; k < 31; ++k)
            if (w.title[k] && (uint8_t(w.title[k]) < 32 || uint8_t(w.title[k]) > 126))
                return false;
        for (uint32_t j = 0; j < i; ++j)
            if (f.windows[j].handle == w.handle)
                return false;
        if (w.flags) {
            if (modal || !w.parent || w.parent == w.handle)
                return false;
            modal = w.handle;
            bool parent = false;
            for (uint32_t j = 0; j < i; ++j)
                if (f.windows[j].handle == w.parent)
                    parent = true;
            if (!parent)
                return false;
        } else if (w.parent)
            return false;
        if (w.handle == f.focus)
            focused = true;
    }
    return focused && (!modal || !f.focus || f.focus == modal);
}
Desktop::Window *Desktop::Find(uint32_t h) noexcept {
    for (uint32_t i = 0; i < count_; ++i)
        if (windows_[i].info.handle == h)
            return &windows_[i];
    return nullptr;
}
uint32_t Desktop::Modal() const noexcept {
    for (uint32_t i = 0; i < count_; ++i)
        if (windows_[i].info.flags)
            return windows_[i].info.handle;
    return 0;
}
Status Desktop::Execute(Command &c, Event &event) noexcept {
    event = {};
    auto *w = Find(c.handle);
    if (c.op == Op::Bridge)
        return c.value <= uint32_t(Bridge::HostWindow) ? Status::Denied : Status::Invalid;
    if (c.op == Op::Create) {
        if (c.value > 1 || c.count > 31 || !Ascii(c.data, c.count))
            return Status::Invalid;
        WindowInfo info{next_, c.handle, c.x, c.y, c.w, c.h, 96, c.value, {}};
        if (!Bounds(info) || (!c.value && c.handle) || (c.value && (!w || Modal())))
            return Status::Invalid;
        if (count_ == MaxWindows)
            return Status::Full;
        if (next_ > 256)
            return Status::Quota;
        for (uint32_t i = 0; i < c.count; ++i)
            info.title[i] = char(c.data[i]);
        windows_[count_++] = {};
        windows_[count_ - 1].info = info;
        focus_ = next_++;
        c.handle = focus_;
        return Status::Ok;
    }
    if (c.op == Op::Cursor) {
        if (c.value > 2)
            return Status::Unsupported;
        cursor_ = c.value;
        return Status::Ok;
    }
    if (c.op == Op::Poll) {
        if (!queued_)
            return Status::Empty;
        event = events_[head_];
        head_ = (head_ + 1) % Depth;
        --queued_;
        return Status::Ok;
    }
    if (c.op == Op::Present)
        return Status::Ok;
    if (!w)
        return Status::Invalid;
    if (c.op == Op::Destroy) {
        for (uint32_t i = 0; i < count_; ++i)
            if (windows_[i].info.parent == c.handle)
                return Status::Denied;
        const auto index = uint32_t(w - windows_);
        for (uint32_t i = index + 1; i < count_; ++i)
            windows_[i - 1] = windows_[i];
        --count_;
        windows_[count_] = {};
        queued_ = head_ = 0;
        if (focus_ == c.handle)
            focus_ = count_ ? windows_[count_ - 1].info.handle : 0;
        return Status::Ok;
    }
    if (c.op == Op::Focus) {
        if (Modal() && Modal() != c.handle)
            return Status::Denied;
        const auto copy = *w;
        auto index = uint32_t(w - windows_);
        for (uint32_t i = index + 1; i < count_; ++i)
            windows_[i - 1] = windows_[i];
        windows_[count_ - 1] = copy;
        focus_ = c.handle;
        return Status::Ok;
    }
    if (c.op == Op::Dpi) {
        if (!Dpi(c.value))
            return Status::Unsupported;
        w->info.dpi = c.value;
        return Status::Ok;
    }
    if (c.op != Op::Fill && c.op != Op::Text && c.op != Op::Blit)
        return Status::Unsupported;
    if (c.x < -1024 || c.y < -1024 || c.x > 1024 || c.y > 1024 || c.w > 1024 || c.h > 1024)
        return Status::Invalid;
    if (c.op == Op::Text && (c.count > 128 || !Ascii(c.data, c.count)))
        return Status::Unsupported;
    if (c.op == Op::Blit &&
        (!c.w || !c.h || c.w > 32 || c.h > 32 || c.w * c.h > 32 || c.count != c.w * c.h * 4))
        return Status::Invalid;
    // A full client background replaces its previous display list.
    if (c.op == Op::Fill && c.x == 0 && c.y == 0 && c.w >= w->info.w && c.h >= w->info.h)
        w->used = 0;
    if (w->used == 32)
        return Status::Full;
    w->drawings[w->used++] = c;
    return Status::Ok;
}
Status Desktop::Inject(const Event &input) noexcept {
    if (!ValidInput(input))
        return Status::Invalid;
    if (input.type == Input::Blur) {
        focus_ = 0;
        head_ = queued_ = 0;
        return Status::Ok;
    }
    if (queued_ == Depth)
        return Status::Full;
    auto e = input;
    if (e.type == Input::Move || e.type == Input::Button) {
        Window *hit = nullptr;
        for (uint32_t i = count_; i > 0; --i) {
            auto &w = windows_[i - 1];
            if (e.x >= w.info.x && e.y >= w.info.y && e.x < w.info.x + int32_t(w.info.w) &&
                e.y < w.info.y + int32_t(w.info.h)) {
                hit = &w;
                break;
            }
        }
        if (!hit || (Modal() && Modal() != hit->info.handle))
            return Status::Denied;
        e.target = hit->info.handle;
        if (e.type == Input::Button && e.buttons) {
            Command c{};
            c.op = Op::Focus;
            c.handle = e.target;
            Event ignored{};
            Execute(c, ignored);
            hit = Find(e.target);
        }
        e.x = (e.x - hit->info.x) * 96 / int32_t(hit->info.dpi);
        e.y = (e.y - hit->info.y - 18) * 96 / int32_t(hit->info.dpi);
    } else {
        if (!focus_ && e.type != Input::Close)
            return Status::Denied;
        e.target = focus_;
    }
    e.serial = ++serial_;
    events_[(head_ + queued_) % Depth] = e;
    ++queued_;
    return Status::Ok;
}
FrameInfo Desktop::Metadata() const noexcept {
    FrameInfo f{};
    f.count = count_;
    f.focus = focus_;
    f.cursor = cursor_;
    for (uint32_t i = 0; i < count_; ++i)
        f.windows[i] = windows_[i].info;
    return f;
}
void Desktop::Render(uint32_t *pixels) const noexcept {
    Raster screen{pixels, 0, 0, int32_t(Width), int32_t(Height)};
    screen.Box(0, 0, Width, Height, 0x172536);
    for (uint32_t i = 0; i < count_; ++i) {
        const auto &w = windows_[i];
        const auto &a = w.info;
        screen.Box(a.x, a.y, a.w, a.h, 0xEBEEF2);
        screen.Box(a.x, a.y, a.w, 18, a.handle == focus_ ? 0x286CA5 : 0x56616D);
        Raster title{pixels, a.x, a.y, a.x + int32_t(a.w), a.y + 18};
        uint32_t n = 0;
        while (n < 31 && a.title[n])
            ++n;
        title.Text(a.x + 4, a.y + 4, reinterpret_cast<const uint8_t *>(a.title), n, 0xFFFFFF, 96);
        Raster client{pixels, a.x, a.y + 18, a.x + int32_t(a.w), a.y + int32_t(a.h)};
        for (uint32_t j = 0; j < w.used; ++j) {
            const auto &c = w.drawings[j];
            const auto x = a.x + c.x * int32_t(a.dpi) / 96, y = a.y + 18 + c.y * int32_t(a.dpi) / 96;
            if (c.op == Op::Fill)
                client.Box(x, y, c.w * a.dpi / 96, c.h * a.dpi / 96, c.value);
            else if (c.op == Op::Text)
                client.Text(x, y, c.data, c.count, c.value, a.dpi);
            else
                for (uint32_t row = 0; row < c.h; ++row)
                    for (uint32_t col = 0; col < c.w; ++col) {
                        uint32_t color = 0;
                        for (uint32_t b = 0; b < 4; ++b)
                            color |= uint32_t(c.data[(row * c.w + col) * 4 + b]) << (b * 8);
                        client.Box(x + int32_t(col * a.dpi / 96), y + int32_t(row * a.dpi / 96),
                                   int32_t((col + 1) * a.dpi / 96 - col * a.dpi / 96),
                                   int32_t((row + 1) * a.dpi / 96 - row * a.dpi / 96), color);
                    }
        }
    }
}
uint64_t PixelHash(const uint32_t *p) noexcept {
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t i = 0; i < PixelCount; ++i)
        for (uint32_t b = 0; b < 4; ++b) {
            hash ^= (p[i] >> (b * 8)) & 255;
            hash *= 1099511628211ull;
        }
    return hash;
}
} // namespace guardian::graphics
