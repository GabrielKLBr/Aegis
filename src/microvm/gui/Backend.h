#pragma once
#include "Script.h"
#include "guardian/Graphics.h"
#include <filesystem>
#include <functional>
#include <vector>
namespace av::microvm::gui {
struct Options {
    bool interactive = false;
    bool continuous = false;
    std::vector<TimedInput> script;
    std::filesystem::path output;
    std::vector<guardian::graphics::Event> input;
    std::function<guardian::graphics::Event()> poll;
    std::function<void(const guardian::graphics::FrameInfo &, const uint32_t *)> display;
};
class Backend {
  public:
    Backend(uint64_t, const Options &);
    bool Exchange(guardian::graphics::Packet &, const uint32_t *, uint64_t nowNs = 0);
    guardian::graphics::Info Enumeration() const;
    uint64_t Frames() const { return frames_; }
    uint64_t LastNs() const { return lastNs_; }
    uint64_t Inputs() const { return inputs_; }
    uint64_t Hash() const { return guardian::graphics::PixelHash(pixels_.data()); }
    bool Interactive() const { return options_.interactive; }
    void Save(state::Writer &) const;
    void Load(state::Reader &);
    void Export() const;

  private:
    uint64_t boot_, sequence_ = 0, frames_ = 0, inputs_ = 0;
    size_t position_ = 0, scriptPosition_ = 0;
    uint64_t lastNs_ = 0, maximumLatenessNs_ = 0;
    std::vector<uint64_t> delivered_;
    bool reattach_ = false;
    Options options_;
    guardian::graphics::FrameInfo frame_{};
    std::vector<uint32_t> pixels_;
};
} // namespace av::microvm::gui
