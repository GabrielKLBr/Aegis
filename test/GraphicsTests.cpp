#include "../src/microvm/gui/Backend.h"
#include "../src/microvm/gui/Viewer.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace guardian::graphics;
using av::microvm::gui::Backend;
namespace {
uint32_t checks = 0;
void Check(bool ok) {
    ++checks;
    if (!ok)
        throw std::runtime_error("Graphics check " + std::to_string(checks));
}
Status Run(Desktop &d, Command &c) {
    Event e{};
    return d.Execute(c, e);
}
Command Create(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t parent = 0) {
    Command c{};
    c.op = Op::Create;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.handle = parent;
    c.value = parent ? 1u : 0u;
    c.count = 4;
    std::memcpy(c.data, "TEST", 4);
    return c;
}
bool Throws(auto f) {
    try {
        f();
        return false;
    } catch (const std::exception &) {
        return true;
    }
}
} // namespace
int main() {
    try {
        auto desktop = std::make_unique<Desktop>();
        auto &d = *desktop;
        auto c = Create(10, 10, 180, 120);
        Check(Run(d, c) == Status::Ok);
        const auto a = c.handle;
        c = Create(120, 100, 180, 130);
        Check(Run(d, c) == Status::Ok);
        const auto b = c.handle;
        Check(ValidFrame(d.Metadata()));
        std::vector<uint32_t> pixels(PixelCount);
        d.Render(pixels.data());
        Check(pixels[0] == 0xFF172536);
        Check(pixels[101 * Width + 121] == 0xFF286CA5);
        Check(pixels[40 * Width + 20] == 0xFFEBEEF2);
        c = {};
        c.op = Op::Fill;
        c.handle = a;
        c.x = -20;
        c.y = -20;
        c.w = 50;
        c.h = 50;
        c.value = 0xF000FF00;
        Check(Run(d, c) == Status::Ok);
        d.Render(pixels.data());
        Check(pixels[28 * Width + 10] == 0xFF00FF00);
        Check(pixels[27 * Width + 10] == 0xFF56616D);
        Check(pixels[28 * Width + 9] == 0xFF172536);
        c = {};
        c.op = Op::Blit;
        c.handle = a;
        c.x = 30;
        c.y = 30;
        c.w = 2;
        c.h = 1;
        c.count = 8;
        c.data[2] = 255;
        c.data[5] = 255;
        Check(Run(d, c) == Status::Ok);
        d.Render(pixels.data());
        Check(pixels[58 * Width + 40] == 0xFFFF0000);
        Check(pixels[58 * Width + 41] == 0xFF00FF00);
        c.op = Op::Dpi;
        c.value = 144;
        Check(Run(d, c) == Status::Ok);
        d.Render(pixels.data());
        Check(pixels[73 * Width + 55] == 0xFFFF0000);
        c.value = 120;
        Check(Run(d, c) == Status::Unsupported);
        Event e{Input::Button, 0, 25, 45, 0, 1};
        Check(d.Inject(e) == Status::Ok);
        c = {};
        c.op = Op::Poll;
        Event out{};
        Check(d.Execute(c, out) == Status::Ok && out.target == a && out.x == 10 && out.y == 11);
        Check(d.Metadata().focus == a);
        Check(d.Inject({Input::Key, 0, 0, 0, 'Q'}) == Status::Ok);
        Check(d.Execute(c, out) == Status::Ok && out.target == a && out.key == 'Q' && out.serial == 2);
        Check(d.Inject({Input::Blur}) == Status::Ok);
        Check(d.Inject({Input::Key, 0, 0, 0, 'X'}) == Status::Denied);
        Check(d.Execute(c, out) == Status::Empty);
        c = Create(70, 60, 120, 80, b);
        Check(Run(d, c) == Status::Ok);
        auto modal = c.handle;
        Check(d.Inject({Input::Button, 0, 20, 35, 0, 1}) == Status::Denied);
        c = {};
        c.op = Op::Focus;
        c.handle = a;
        Check(Run(d, c) == Status::Denied);
        c.op = Op::Destroy;
        c.handle = b;
        Check(Run(d, c) == Status::Denied);
        Check(d.Inject({Input::Key, 0, 0, 0, 13}) == Status::Ok);
        c.op = Op::Poll;
        Check(d.Execute(c, out) == Status::Ok && out.target == modal);
        c.op = Op::Destroy;
        c.handle = modal;
        Check(Run(d, c) == Status::Ok);
        Check(Run(d, c) == Status::Invalid);
        Check(ValidFrame(d.Metadata()));
        for (uint32_t i = 0; i < 6; ++i) {
            c = {};
            c.op = Op::Bridge;
            c.value = i;
            Check(Run(d, c) == Status::Denied);
        }
        c.value = 6;
        Check(Run(d, c) == Status::Invalid);
        for (uint32_t i = 0; i < Depth; ++i)
            Check(d.Inject({Input::Key, 0, 0, 0, 'A'}) == Status::Ok);
        Check(d.Inject({Input::Key, 0, 0, 0, 'A'}) == Status::Full);
        Check(d.Inject({Input::Blur}) == Status::Ok);
        Check(d.Inject({Input::Close}) == Status::Ok);
        c.op = Op::Poll;
        Check(d.Execute(c, out) == Status::Ok && out.type == Input::Close);
        Check(!ValidInput({Input::Key, 1, 0, 0, 65}));
        Check(!ValidInput({Input::Move, 0, 320, 0}));
        Check(!ValidInput({Input::Key, 0, 0, 0, 127}));
        Check(!ValidInput({Input::Button, 0, 0, 0, 0, 2}));
        c = {};
        c.op = Op::Fill;
        c.handle = a;
        c.x = INT32_MAX;
        Check(Run(d, c) == Status::Invalid);
        c.x = 0;
        c.w = UINT32_MAX;
        Check(Run(d, c) == Status::Invalid);
        c.op = Op::Text;
        c.w = 0;
        c.count = 129;
        Check(Run(d, c) == Status::Unsupported);
        c.count = 1;
        c.data[0] = 0xFF;
        Check(Run(d, c) == Status::Unsupported);
        c.op = Op::Blit;
        c.w = UINT32_MAX;
        c.h = UINT32_MAX;
        c.count = 4;
        Check(Run(d, c) == Status::Invalid);
        auto frame = d.Metadata();
        auto bad = frame;
        bad.width = UINT32_MAX;
        Check(!ValidFrame(bad));
        bad = frame;
        bad.windows[1].handle = bad.windows[0].handle;
        Check(!ValidFrame(bad));
        bad = frame;
        bad.windows[0].x = -1;
        Check(!ValidFrame(bad));
        bad = frame;
        bad.windows[0].title[31] = 'A';
        Check(!ValidFrame(bad));
        bad = frame;
        bad.windows[0].parent = 123;
        Check(!ValidFrame(bad));
        bad = frame;
        bad.focus = 256;
        Check(!ValidFrame(bad));
        av::microvm::gui::Options options;
        options.input = av::microvm::gui::ReferenceInput();
        Backend backend(11, options);
        uint64_t seq = 0;
        auto packet = [&](Op op) {
            Packet p{};
            p.boot = 11;
            p.sequence = ++seq;
            p.op = op;
            p.frame = frame;
            return p;
        };
        auto p = packet(Op::Present);
        d.Render(pixels.data());
        Check(backend.Exchange(p, pixels.data()) && p.status == Status::Ok && backend.Frames() == 1);
        auto hash = backend.Hash();
        p = packet(Op::Present);
        p.frame.count = 5;
        Check(backend.Exchange(p, pixels.data()) && p.status == Status::Invalid && backend.Hash() == hash &&
              backend.Frames() == 1);
        p = packet(Op::Present);
        pixels[0] = 0;
        Check(backend.Exchange(p, pixels.data()) && p.status == Status::Invalid);
        pixels[0] = 0xFF172536;
        p = packet(Op::Poll);
        Check(backend.Exchange(p, nullptr) && p.event.type == Input::Button && backend.Inputs() == 1);
        av::microvm::state::Writer w;
        backend.Save(w);
        Backend restored(11, options);
        av::microvm::state::Reader reader(w.data);
        restored.Load(reader);
        reader.End();
        Check(restored.Hash() == hash && restored.Frames() == 1 && restored.Inputs() == 1);
        p = packet(Op::Poll);
        Check(restored.Exchange(p, nullptr) && p.event.key == 13);
        auto changed = options;
        changed.input[1].key = 27;
        Backend other(11, changed);
        Check(Throws([&] {
            av::microvm::state::Reader rr(w.data);
            other.Load(rr);
        }));
        for (auto n : {0u, 1u, 7u, 31u, uint32_t(w.data.size() - 1)})
            Check(Throws([&] {
                auto bytes = w.data;
                bytes.resize(n);
                av::microvm::state::Reader rr(bytes);
                Backend x(11, options);
                x.Load(rr);
            }));
        seq = 4;
        p = packet(Op::Poll);
        p.boot = 12;
        Check(!backend.Exchange(p, nullptr));
        --seq;
        p = packet(Op::Poll);
        p.reserved[0] = 1;
        Check(!backend.Exchange(p, nullptr));
        --seq;
        p = packet(Op::Fill);
        Check(backend.Exchange(p, nullptr) && p.status == Status::Unsupported);
        Check(!backend.Exchange(p, nullptr));
        av::microvm::gui::Options invalidOptions;
        invalidOptions.input.resize(257);
        Check(Throws([&] { Backend invalid(11, invalidOptions); }));
        invalidOptions.input.clear();
        invalidOptions.interactive = true;
        Check(Throws([&] { Backend invalid(11, invalidOptions); }));
        invalidOptions.poll = [] { return Event{}; };
        invalidOptions.display = [](const FrameInfo &, const uint32_t *) {};
        Backend interactive(11, invalidOptions);
        Check(Throws([&] {
            av::microvm::state::Writer writer;
            interactive.Save(writer);
        }));
        av::microvm::gui::Options emptyOptions;
        Backend limited(11, emptyOptions);
        for (uint64_t i = 1; i <= 8192; ++i) {
            Packet request{};
            request.boot = 11;
            request.sequence = i;
            request.op = Op::Poll;
            Check(limited.Exchange(request, nullptr) && request.status == Status::Empty);
        }
        Packet excess{};
        excess.boot = 11;
        excess.sequence = 8193;
        excess.op = Op::Poll;
        Check(!limited.Exchange(excess, nullptr));
        auto drawing = std::make_unique<Desktop>();
        c = Create(0, 0, 100, 80);
        Check(Run(*drawing, c) == Status::Ok);
        auto drawingHandle = c.handle;
        for (uint32_t i = 0; i < 32; ++i) {
            c = {};
            c.op = Op::Fill;
            c.handle = drawingHandle;
            c.x = 1;
            c.y = 1;
            c.w = 2;
            c.h = 2;
            Check(Run(*drawing, c) == Status::Ok);
        }
        Check(Run(*drawing, c) == Status::Full);
        c.x = c.y = 0;
        c.w = 100;
        c.h = 80;
        Check(Run(*drawing, c) == Status::Ok);
        // Bounded, deterministic hostile commands run under ASan/UBSan too.
        uint32_t rng = 0xA51E9B13;
        auto random = [&] {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return rng;
        };
        for (uint32_t n = 0; n < 20000; ++n) {
            Command fuzz{};
            auto *bytes = reinterpret_cast<uint8_t *>(&fuzz);
            for (size_t i = 0; i < sizeof(fuzz); ++i)
                bytes[i] = uint8_t(random());
            fuzz.handle = a;
            if (n % 2 == 0)
                fuzz.op = Op(1 + random() % 11);
            if (n % 3 == 0) {
                fuzz = {};
                fuzz.op = Op::Fill;
                fuzz.handle = drawingHandle;
                fuzz.x = int32_t(random() % 2049) - 1024;
                fuzz.y = int32_t(random() % 2049) - 1024;
                fuzz.w = random() % 1025;
                fuzz.h = random() % 1025;
                fuzz.value = random();
            }
            Event ignored{};
            d.Execute(fuzz, ignored);
            if (n % 3 == 0) {
                drawing->Execute(fuzz, ignored);
                if (n % 96 == 0) {
                    drawing->Render(pixels.data());
                    Command clear{};
                    clear.op = Op::Fill;
                    clear.handle = drawingHandle;
                    clear.w = 100;
                    clear.h = 80;
                    drawing->Execute(clear, ignored);
                }
            }
            if (n % 500 == 0) {
                Check(ValidFrame(d.Metadata()));
                d.Render(pixels.data());
            }
        }
        auto full = std::make_unique<Desktop>();
        for (uint32_t i = 0; i < MaxWindows; ++i) {
            c = Create(int32_t(i * 10), int32_t(i * 10), 100, 80);
            Check(Run(*full, c) == Status::Ok);
        }
        c = Create(0, 0, 100, 80);
        Check(Run(*full, c) == Status::Full);
        auto lifecycle = std::make_unique<Desktop>();
        for (uint32_t i = 0; i < 256; ++i) {
            c = Create(0, 0, 100, 80);
            Check(Run(*lifecycle, c) == Status::Ok);
            const auto handle = c.handle;
            c.op = Op::Destroy;
            Check(Run(*lifecycle, c) == Status::Ok);
            c.op = Op::Focus;
            c.handle = handle;
            Check(Run(*lifecycle, c) == Status::Invalid);
        }
        c = Create(0, 0, 100, 80);
        Check(Run(*lifecycle, c) == Status::Quota);
        std::cout << checks
                  << " graphics boundary/raster/window/input/snapshot checks; 20000 hostile commands PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
