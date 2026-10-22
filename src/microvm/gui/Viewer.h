#pragma once
#include "Backend.h"
#include <memory>
namespace av::microvm::gui {
// All operating-system input stays on this window's queue. No global hook or capture.
class Viewer {
  public:
    Viewer();
    void SmokeTest();
    ~Viewer();
    Viewer(const Viewer &) = delete;
    Viewer &operator=(const Viewer &) = delete;
    guardian::graphics::Event Poll();
    void Display(const guardian::graphics::FrameInfo &, const uint32_t *);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
std::vector<guardian::graphics::Event> ReferenceInput();
} // namespace av::microvm::gui
