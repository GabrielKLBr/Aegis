#include "Backend.h"
#include "../state/Files.h"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>
namespace av::microvm::gui {
using namespace guardian::graphics;
Backend::Backend(uint64_t boot, const Options &o)
    : boot_(boot), options_(o), pixels_(PixelCount, 0xFF172536) {
    state::Require(boot && o.input.size() <= 256, "Graphics input quota exceeded");
    state::Require(o.interactive ? bool(o.poll) && bool(o.display) && o.input.empty() : !o.poll && !o.display,
                   "Graphics channel configuration rejected");
    state::Require(!o.continuous || o.input.empty(), "Session graphics require timed or manual input");
    ValidateScript(o.script);
    state::Require(o.script.empty() || (o.continuous && !o.interactive && o.input.empty()), "Timed guest script mode rejected");
    for (const auto &e : o.input)
        state::Require(ValidInput(e), "Malformed scripted graphics input");
}
Info Backend::Enumeration() const {
    return {Magic, boot_, Version, sizeof(Info), Width, Height, 0, uint32_t(options_.interactive)};
}
bool Backend::Exchange(Packet &p, const uint32_t *pixels, uint64_t nowNs) {
    if (p.magic != Magic || p.boot != boot_ || p.version != Version || p.bytes != sizeof(Packet) ||
        p.sequence != sequence_ + 1 || p.status != Status::Ok || p.reserved[0] || p.reserved[1] ||
        sequence_ >= (options_.continuous ? UINT64_MAX-1 : 8192))
        return false;
    if (options_.continuous) {
        state::Require(nowNs >= lastNs_ && nowNs < guardian::session::MaxElapsedNs, "Guest input clock rollback rejected");
        lastNs_ = nowNs;
    }
    ++sequence_;
    p.event = {};
    if (p.op == Op::Present) {
        if (!pixels || !ValidFrame(p.frame)) {
            p.status = Status::Invalid;
            return true;
        }
        for (uint32_t i = 0; i < PixelCount; ++i)
            if ((pixels[i] >> 24) != 255) {
                p.status = Status::Invalid;
                return true;
            }
        pixels_.assign(pixels, pixels + PixelCount);
        frame_ = p.frame;
        ++frames_;
        Export();
        if (options_.display)
            options_.display(frame_, pixels_.data());
        return true;
    }
    if (p.op != Op::Poll) {
        p.status = Status::Unsupported;
        return true;
    }
    Event event{};
    if (reattach_) { event.type = Input::Blur; reattach_ = false; }
    else if (options_.poll)
        event = options_.poll();
    else if (scriptPosition_ < options_.script.size() && options_.script[scriptPosition_].atNs <= nowNs) {
        const auto& item = options_.script[scriptPosition_++];
        event = item.event; delivered_.push_back(nowNs);
        maximumLatenessNs_ = std::max(maximumLatenessNs_, nowNs-item.atNs);
    }
    else if (position_ < options_.input.size())
        event = options_.input[position_++];
    if (event.type == Input::None) {
        // Bound idle polling without advancing any guest timer or deadline.
        if (options_.continuous) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        p.status = Status::Empty;
        return true;
    }
    if (!ValidInput(event)) {
        p.status = Status::Invalid;
        return true;
    }
    p.event = event;
    ++inputs_;
    return true;
}
void Backend::Save(state::Writer &w) const {
    state::Require(!options_.interactive || options_.continuous, "Interactive snapshots require session mode");
    w.Pod(sequence_);
    w.Pod(frames_);
    w.Pod(inputs_);
    w.Pod(uint32_t(position_));
    w.Pod(frame_);
    w.Pod(uint32_t(options_.input.size()) | (options_.continuous ? 0x80000000u : 0u) | (options_.interactive ? 0x40000000u : 0u));
    for (const auto &e : options_.input)
        w.Pod(e);
    for (auto p : pixels_)
        w.Pod(p);
    if (options_.continuous) {
        SaveScript(w, options_.script); w.Pod(uint32_t(scriptPosition_)); w.Pod(lastNs_); w.Pod(maximumLatenessNs_);
        for (auto ns : delivered_) w.Pod(ns);
    }
}
void Backend::Load(state::Reader &r) {
    state::Require(!options_.interactive || options_.continuous, "Interactive restore requires session mode");
    sequence_ = r.Pod<uint64_t>();
    frames_ = r.Pod<uint64_t>();
    inputs_ = r.Pod<uint64_t>();
    position_ = r.Pod<uint32_t>();
    frame_ = r.Pod<FrameInfo>();
    const auto encoded = r.Pod<uint32_t>();
    state::Require(bool(encoded & 0x80000000u) == options_.continuous && bool(encoded & 0x40000000u) == options_.interactive, "Graphics session policy mismatch");
    const auto count = encoded & 0x3fffffffu;
    state::Require(count == options_.input.size() && position_ <= count && sequence_ <= (options_.continuous ? UINT64_MAX-1 : 8192) &&
                       frames_ <= sequence_ && inputs_ <= sequence_ && ValidFrame(frame_),
                   "Graphics snapshot bounds rejected");
    for (const auto &expected : options_.input) {
        const auto e = r.Pod<Event>();
        state::Require(std::memcmp(&e, &expected, sizeof(e)) == 0, "Graphics input policy mismatch");
    }
    for (auto &p : pixels_) {
        p = r.Pod<uint32_t>();
        state::Require((p >> 24) == 255, "Graphics snapshot pixel rejected");
    }
    if (options_.continuous) {
        const auto script = LoadScript(r);
        state::Writer expected, actual; SaveScript(expected, options_.script); SaveScript(actual, script);
        state::Require(expected.data == actual.data, "Timed input policy mismatch");
        scriptPosition_ = r.Pod<uint32_t>(); lastNs_ = r.Pod<uint64_t>(); maximumLatenessNs_ = r.Pod<uint64_t>();
        state::Require(scriptPosition_ <= script.size() && lastNs_ < guardian::session::MaxElapsedNs &&
            maximumLatenessNs_ <= lastNs_, "Timed input checkpoint bounds rejected");
        uint64_t previous = 0;
        for (size_t i=0; i<scriptPosition_; ++i) {
            const auto ns = r.Pod<uint64_t>();
            state::Require(ns >= previous && ns >= script[i].atNs && ns <= lastNs_ && ns-script[i].atNs <= maximumLatenessNs_, "Timed input delivery state rejected");
            delivered_.push_back(ns); previous = ns;
        }
        state::Require(scriptPosition_ <= inputs_, "Input delivery count rejected");
        reattach_ = options_.interactive;
        if (options_.display) options_.display(frame_, pixels_.data());
    }
}
void Backend::Export() const {
    if (options_.output.empty())
        return;
    const std::string header = "P6\n320 240\n255\n";
    std::vector<uint8_t> ppm(header.begin(), header.end());
    ppm.reserve(ppm.size() + PixelCount * 3);
    for (auto p : pixels_) {
        ppm.push_back(uint8_t(p >> 16));
        ppm.push_back(uint8_t(p >> 8));
        ppm.push_back(uint8_t(p));
    }
    state::AtomicWrite(options_.output / "desktop.ppm", ppm);
    std::ostringstream out;out << std::boolalpha;
    out << "{\"schema\":11,\"width\":320,\"height\":240,\"windows\":" << frame_.count
        << ",\"focus\":" << frame_.focus << ",\"cursor\":" << frame_.cursor << ",\"frames\":" << frames_
        << ",\"inputs\":" << inputs_ << ",\"pixelHash\":" << Hash()
        << ",\"permissions\":0,\"accelerated\":false,\"windowInfo\":[";
    for (uint32_t i = 0; i < frame_.count; ++i) {
        const auto &w = frame_.windows[i];
        if (i)
            out << ',';
        out << "{\"handle\":" << w.handle << ",\"parent\":" << w.parent << ",\"x\":" << w.x
            << ",\"y\":" << w.y << ",\"width\":" << w.w << ",\"height\":" << w.h << ",\"dpi\":" << w.dpi
            << ",\"modal\":" << w.flags << ",\"title\":" << std::quoted(std::string(w.title)) << '}';
    }
    out << "],\"timeMode\":\"real\",\"continuous\":" << options_.continuous
        << ",\"activeNs\":" << lastNs_ << ",\"scriptConsumed\":" << scriptPosition_
        << ",\"maximumLatenessNs\":" << maximumLatenessNs_ << ",\"deliveries\":[";
    for (size_t i=0; i<delivered_.size(); ++i) {
        if (i) out << ',';
        out << "{\"dueNs\":" << options_.script[i].atNs << ",\"deliveredNs\":" << delivered_[i] << '}';
    }
    out << "]}\n";
    const auto json = out.str();
    state::AtomicWrite(options_.output / "graphics.json", std::vector<uint8_t>(json.begin(), json.end()));
}
} // namespace av::microvm::gui
