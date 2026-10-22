#include "Script.h"
#include "../state/Files.h"
#include <sstream>
namespace av::microvm::gui {
void ValidateScript(const std::vector<TimedInput>& script) {
    state::Require(script.size() <= 256, "Guest input script exceeds 256 events");
    uint64_t previous = 0;
    for (const auto& item : script) {
        state::Require(item.atNs >= previous && item.atNs <= uint64_t(guardian::session::MaxDurationMs)*1000000 &&
            item.event.type != guardian::graphics::Input::None && guardian::graphics::ValidInput(item.event) &&
            !item.event.target && !item.event.serial, "Guest script time/order/event rejected");
        previous = item.atNs;
    }
}
std::vector<TimedInput> ReadScript(const std::filesystem::path& path) {
    const auto bytes = state::ReadFile(path, 32768);
    std::istringstream lines(std::string(bytes.begin(), bytes.end()));
    std::vector<TimedInput> out;
    std::string line;
    while (std::getline(lines, line)) {
        const auto comment = line.find('#');
        if (comment != line.npos) line.resize(comment);
        std::istringstream fields(line);
        std::string time, operation, extra;
        if (!(fields >> time)) continue;
        state::Require(time.find_first_not_of("0123456789") == time.npos && bool(fields >> operation),
            "Script requires milliseconds followed by key/click/move/blur/close");
        const auto ms = std::stoull(time);
        state::Require(ms <= guardian::session::MaxDurationMs, "Script timestamp exceeds seven days");
        TimedInput item; item.atNs = ms*1000000;
        using guardian::graphics::Input;
        if (operation == "key") { item.event.type = Input::Key; state::Require(bool(fields >> item.event.key), "Script key required"); }
        else if (operation == "click" || operation == "move") {
            item.event.type = operation == "click" ? Input::Button : Input::Move;
            state::Require(bool(fields >> item.event.x >> item.event.y), "Script coordinates required");
            if (operation == "click") state::Require(bool(fields >> item.event.buttons), "Script button mask required");
        } else if (operation == "blur") item.event.type = Input::Blur;
        else if (operation == "close") item.event.type = Input::Close;
        else throw std::runtime_error("Unsupported guest script operation");
        state::Require(!(fields >> extra), "Unexpected guest script field");
        out.push_back(item);
        state::Require(out.size() <= 256, "Guest input script exceeds 256 events");
    }
    ValidateScript(out);
    return out;
}
void SaveScript(state::Writer& w, const std::vector<TimedInput>& script) {
    ValidateScript(script); w.Pod(uint32_t(script.size()));
    for (const auto& item : script) { w.Pod(item.atNs); w.Pod(item.event); }
}
std::vector<TimedInput> LoadScript(state::Reader& r) {
    const auto count = r.Pod<uint32_t>(); state::Require(count <= 256, "Script snapshot count rejected");
    std::vector<TimedInput> out;
    for (uint32_t i=0; i<count; ++i) { TimedInput item; item.atNs=r.Pod<uint64_t>(); item.event=r.Pod<guardian::graphics::Event>(); out.push_back(item); }
    ValidateScript(out); return out;
}
} // namespace av::microvm::gui
