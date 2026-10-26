#pragma once
#include "WindowBridge.h"
#include <memory>
#include <string>
namespace av::microvm::window_session {
struct Action {uint64_t ms=0;std::wstring title;std::string operation;int32_t a=0,b=0,c=0,d=0;};
std::vector<Action> ReadWindowScript(const std::filesystem::path&);
class Presenter {
public:
    explicit Presenter(std::vector<Action> actions={},bool visible=true,std::filesystem::path captures={});
    ~Presenter();
    Presenter(const Presenter&)=delete;Presenter& operator=(const Presenter&)=delete;
    void Display(const Frame&);
    guardian::presentation::Event Poll();
    // Join the UI thread and destroy its own HWNDs before exporting diagnostics.
    void Finish(const std::filesystem::path& output={});
    std::string Report() const;
#ifdef WINDOW_BRIDGE_TESTING
    void FailNextResourceForTesting(uint32_t stage);
#endif
private:
    struct State;std::unique_ptr<State> state_;
};
}
