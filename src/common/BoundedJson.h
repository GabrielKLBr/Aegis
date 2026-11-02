#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace aegis::security {
// Byte limits alone do not bound DOM allocation or recursive destruction.
// Apply these limits while parsing, before an untrusted tree is constructed.
inline constexpr size_t JsonDepthLimit = 32, JsonEventLimit = 65536;
inline nlohmann::json ParseJson(std::string_view text, size_t byteLimit = 2u << 20) {
    if (text.empty() || text.size() > byteLimit)
        throw std::runtime_error("JSON byte limit exceeded");
    using Json = nlohmann::json;
    size_t events = 0;
    std::vector<std::unordered_set<std::string>> keys;
    return Json::parse(text.begin(), text.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth < 0 || size_t(depth) > JsonDepthLimit || ++events > JsonEventLimit)
            throw std::runtime_error("JSON complexity limit exceeded");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key) {
            if (keys.empty() || !keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate JSON field rejected");
        } else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
} // namespace aegis::security
