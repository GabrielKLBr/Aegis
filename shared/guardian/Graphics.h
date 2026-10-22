#pragma once
#include "Abi.h"
namespace guardian::graphics {
inline constexpr uint64_t Magic = 0x31495547415647ull, PacketGpa = 0x135000, InfoGpa = 0x136000,
                          ReportGpa = 0x137000, PixelsGpa = 0x140000;
inline constexpr uint32_t Version = 1, Width = 320, Height = 240, PixelCount = Width * Height, MaxWindows = 4,
                          Depth = 16, RequiredChecks = 63;
inline constexpr uint16_t Port = 0xF3;
enum class Status : uint32_t { Ok, Empty, Invalid, Denied, Full, Unsupported, Quota };
enum class Op : uint32_t { Create = 1, Destroy, Fill, Text, Blit, Focus, Cursor, Dpi, Poll, Present, Bridge };
enum class Input : uint32_t { None, Move, Button, Key, Blur, Close };
enum class Bridge : uint32_t { Clipboard, FileDrop, Printer, Usb, SharedDrive, HostWindow };
struct Event {
    Input type{};
    uint32_t target = 0;
    int32_t x = 0, y = 0;
    uint32_t key = 0, buttons = 0;
    uint64_t serial = 0;
};
struct Command {
    Op op{};
    uint32_t handle = 0;
    int32_t x = 0, y = 0;
    uint32_t w = 0, h = 0, value = 0, count = 0;
    uint8_t data[128]{};
};
struct WindowInfo {
    uint32_t handle = 0, parent = 0;
    int32_t x = 0, y = 0;
    uint32_t w = 0, h = 0, dpi = 96, flags = 0;
    char title[32]{};
};
struct FrameInfo {
    uint32_t width = Width, height = Height, stride = Width * 4, bytes = PixelCount * 4;
    uint32_t count = 0, focus = 0, cursor = 0, reserved = 0;
    WindowInfo windows[MaxWindows]{};
};
struct Info {
    uint64_t magic, boot;
    uint32_t version, bytes, width, height, permissions, interactive;
};
struct Packet {
    uint64_t magic = Magic, boot = 0, sequence = 0;
    uint32_t version = Version, bytes = sizeof(Packet);
    Op op{};
    Status status{};
    uint32_t reserved[2]{};
    Event event{};
    FrameInfo frame{};
};
struct Report {
    uint64_t magic, boot;
    uint32_t version, bytes, checks, applications;
    uint64_t calls, frames, inputs, denied, pixelHash;
};
// No host handles, native callbacks, addresses or strings with dynamic size.
class Desktop {
  public:
    Status Execute(Command &, Event &) noexcept;
    Status Inject(const Event &) noexcept;
    void Render(uint32_t *) const noexcept;
    FrameInfo Metadata() const noexcept;

  private:
    struct Window {
        WindowInfo info{};
        uint32_t used = 0;
        Command drawings[32]{};
    };
    Window windows_[MaxWindows]{};
    uint32_t count_ = 0, focus_ = 0, cursor_ = 0, next_ = 1, head_ = 0, queued_ = 0;
    uint64_t serial_ = 0;
    Event events_[Depth]{};
    Window *Find(uint32_t) noexcept;
    uint32_t Modal() const noexcept;
};
uint64_t PixelHash(const uint32_t *) noexcept;
bool ValidFrame(const FrameInfo &) noexcept;
bool ValidInput(const Event &) noexcept;
static_assert(sizeof(Command) == 160 && sizeof(Event) == 32 && sizeof(WindowInfo) == 64);
static_assert(sizeof(FrameInfo) == 288 && sizeof(Packet) == 368 && sizeof(Info) == 40 &&
              sizeof(Report) == 72);
} // namespace guardian::graphics
