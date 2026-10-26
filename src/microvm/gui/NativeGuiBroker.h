#pragma once
#include "guardian/NativeGui.h"
#include "guardian/NativeUser32.h"
#include "guardian/NativeGdi.h"
#include <filesystem>
#include <memory>
namespace av::microvm::native_gui {
class Broker {
public:
    explicit Broker(const std::filesystem::path& helper,uint64_t boot=1,uint64_t session=0,bool user32=false,bool gdi=false);
    ~Broker();Broker(const Broker&)=delete;Broker& operator=(const Broker&)=delete;
    guardian::nativegui::Info Enumeration() const;
    bool Exchange(guardian::nativegui::Packet&);
    bool Exchange(guardian::nativeuser::Packet&);
    bool Exchange(guardian::nativegdi::Packet&);
    bool Completed() const;
    void Finish(const std::filesystem::path& output={});
#ifdef NATIVE_GUI_TESTING
    void StallForTesting();void KillForTesting();uint32_t ProcessForTesting() const;
#endif
private:
    struct State;std::unique_ptr<State> state_;
};
}
