#pragma once
#include "guardian/NativeGui.h"
#include <array>
#include <functional>
namespace av::microvm::native_gui {
class Core {
public:
    using Execute=std::function<uint32_t(const guardian::nativegui::Command&,uint64_t,uint64_t&)>;
    Core(uint64_t boot,uint64_t session,Execute execute):boot_(boot),session_(session),execute_(std::move(execute)){}
    bool Exchange(guardian::nativegui::Packet&);
private:
    struct Object {uint64_t id=0,parent=0;uint32_t pid=0,tid=0;guardian::nativegui::Kind kind{};guardian::nativegui::Rect bounds{};};
    Object* Find(uint64_t,guardian::nativegui::Kind,uint32_t,uint32_t);
    uint64_t boot_,session_,sequence_=0,counter_=0;bool complete_=false;Execute execute_;std::array<Object,guardian::nativegui::MaxObjects> objects_{};
};
}
