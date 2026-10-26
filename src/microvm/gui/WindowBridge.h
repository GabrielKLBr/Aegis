#pragma once
#include "guardian/Presentation.h"
#include <functional>
#include <array>
#include <filesystem>
#include <vector>
namespace av::microvm::window_session {
struct Image {uint64_t window=0;uint32_t width=0,height=0;std::vector<uint32_t> pixels;};
struct Frame {uint64_t session=0,number=0;guardian::desktop::Frame metadata{};std::array<uint64_t,guardian::desktop::MaxWindows> stacking{};std::vector<Image> images;};
struct Callbacks {std::function<guardian::presentation::Event()> poll;std::function<void(const Frame&)> display;};
uint64_t NewSession();
bool ValidMetadata(const guardian::desktop::Frame&) noexcept;
class Bridge {
public:
    Bridge(uint64_t boot,uint64_t session,Callbacks callbacks={});
    guardian::presentation::Info Enumeration() const;
    bool Exchange(guardian::presentation::Packet&,const uint32_t*,uint64_t now);
    bool Completed() const {return completed_;}
    void Export(const std::filesystem::path&) const;
private:
    uint64_t boot_,session_,sequence_=0,frames_=0,inputs_=0,rejected_=0,guestRejected_=0,now_=0,rateSecond_=0;
    uint32_t commands_=0,rateFrames_=0,rateBytes_=0,stagingBytes_=0;bool staging_=false,completed_=false;
    struct InputRecord {uint64_t ns,window;uint32_t kind;};std::vector<InputRecord> delivered_;
    std::vector<guardian::desktop::Window> identities_;Callbacks callbacks_;Frame pending_{};guardian::desktop::Frame committed_{};
};
}
