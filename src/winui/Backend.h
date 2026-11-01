#pragma once
#include "services/Services.h"
#include <mutex>
#include <thread>
#include <memory>
namespace av::ui {
// UI-only preferences/session summaries. Scanning, quarantine and guest code stay in services.
class Backend : public std::enable_shared_from_this<Backend> {
public:
    explicit Backend(std::filesystem::path state = {});
    ~Backend();
    services::Json Request(const std::string&, const services::Json&);
    void Close() noexcept;
    static void Export(const std::filesystem::path&,const services::Json&);
    services::Json ManageServices(HWND,bool remove);
private:
    std::mutex mutex_, preferencesMutex_, sessionStartMutex_;
    std::atomic<bool> closed_{false}, cancel_{false};
    std::thread sessionThread_;
    services::Json session_, preferences_;
    std::string scanId_;
    std::filesystem::path state_;
    services::Json Call(services::Operation, services::Json = services::Json::object());
    services::Json StartSession(const std::string&, const services::Json&);
};
}
