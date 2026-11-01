#pragma once
#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace aegis {
class Localization final {
public:
    explicit Localization(std::string locale = "");
    std::string text(std::string key) const;
    std::string format(std::string key, const std::map<std::string,std::string>& parameters = {}) const;
    std::string plural(std::string key, long long count) const;
    std::string timestamp(long long seconds) const;
    void set_locale(std::string locale);
    std::string locale() const;
    nlohmann::json languages() const;
private:
    std::string locale_;
};
}
