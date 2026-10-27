#pragma once
#include "guardian/Desktop.h"
#include "guardian/Presentation.h"
#include "guardian/NativeGui.h"
#include "guardian/NativeUser32.h"
#include "guardian/NativeGdi.h"
#include <filesystem>
#include <functional>
#include <vector>
namespace av::microvm::window_session {struct Frame;}
namespace av::microvm::desktop_session {
struct TimedInput {uint64_t ns=0;guardian::desktop::Input event{};};
struct Options {bool nativeGdi=false;bool nativeControls=false;std::function<bool(guardian::nativegdi::Packet&)> gdiExchange;bool nativeUser32=false;std::function<bool(guardian::nativeuser::Packet&)> userExchange;bool nativeGui=false;guardian::nativegui::Info nativeInfo{};std::function<bool(guardian::nativegui::Packet&)> nativeExchange;std::function<bool()> nativeCompleted;bool interactive=false,integrated=false;std::function<guardian::presentation::Event()> windowPoll;std::function<void(const window_session::Frame&)> windowDisplay;uint32_t telemetryCapacity=0;std::filesystem::path output;std::vector<TimedInput> script;std::function<guardian::desktop::Input()> poll;std::function<void(const guardian::desktop::Frame&,const uint32_t*)> display;};
std::vector<TimedInput> ReadScript(const std::filesystem::path&);
class Backend {
public:
    Backend(uint64_t,const Options&);
    bool Exchange(guardian::desktop::Packet&,const uint32_t*,uint64_t);
    bool Complete(const guardian::desktop::Report&);
    bool Completed() const {return completed_;}
    void Export() const;
    guardian::desktop::Info Enumeration() const;
private:
    void Capture();
    uint64_t boot_=0,sequence_=0,frames_=0,inputs_=0,now_=0,hash_=0,scriptOrigin_=0;size_t input_=0;uint32_t captures_=0;bool completed_=false,capture_=true,scriptStarted_=false;
    Options options_;guardian::desktop::Frame frame_{};std::vector<uint32_t> pixels_;std::vector<std::string> history_;std::vector<uint64_t> delivered_;
};
}
