#pragma once
#include "guardian/SoftwareGraphics.h"
#include <Windows.h>
#include <functional>
#include <memory>
namespace av::microvm::native_gui {
class SoftwareGraphicsEngine {
public:
    struct Binding {HWND hwnd=nullptr;HDC dc=nullptr;int width=0,height=0;uint64_t window=0;};
    using Bind=std::function<Binding(uint64_t,bool,uint32_t,uint32_t)>;
    using Present=std::function<void(uint64_t)>;
    explicit SoftwareGraphicsEngine(uint64_t);
    ~SoftwareGraphicsEngine();
    uint32_t Execute(const guardian::nativegdi::Command&,guardian::nativegdi::Reply&,uint32_t,uint32_t,const Bind&,const Present&);
    void Exit(uint32_t,uint32_t);void DropWindow(uint64_t);void Clear();
    uint64_t Bytes() const;uint32_t Live() const;
    void Final(guardian::nativegdi::Reply&) const;
private:
    struct State;std::unique_ptr<State> state_;
};
}
