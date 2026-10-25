#pragma once
#include "DesktopBackend.h"
#include <memory>
namespace av::microvm::desktop_session {
// A single host viewer presents the guest desktop. Guest windows stay in MiniNT.
class Viewer {
public:
    explicit Viewer(bool visible=true);
    ~Viewer();
    Viewer(const Viewer&)=delete;Viewer& operator=(const Viewer&)=delete;
    guardian::desktop::Input Poll();
    void Display(const guardian::desktop::Frame&,const uint32_t*);
private:
    struct State;std::unique_ptr<State> state_;
};
}
