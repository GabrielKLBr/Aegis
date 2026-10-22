#pragma once
#include "../state/State.h"
#include "guardian/Graphics.h"
#include "guardian/Session.h"
#include <filesystem>
namespace av::microvm::gui {
struct TimedInput {
    uint64_t atNs = 0;
    guardian::graphics::Event event{};
};
void ValidateScript(const std::vector<TimedInput>&);
std::vector<TimedInput> ReadScript(const std::filesystem::path&);
void SaveScript(state::Writer&, const std::vector<TimedInput>&);
std::vector<TimedInput> LoadScript(state::Reader&);
} // namespace av::microvm::gui
