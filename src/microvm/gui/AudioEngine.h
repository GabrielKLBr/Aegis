#pragma once
#include "guardian/Audio.h"
#include <memory>
namespace av::microvm::native_gui {
// Native audio objects never leave this process. IDs are scoped to a session
// and guest process; threads in that process can feed the same audio stream.
class AudioEngine {
public:
    explicit AudioEngine(uint64_t);~AudioEngine();
    uint32_t Execute(const guardian::nativegdi::Command&,guardian::nativegdi::Reply&,uint32_t,uint32_t);
    void Exit(uint32_t,uint32_t);void Clear();
    uint32_t Live() const;uint64_t Bytes() const;
    void Final(guardian::nativegdi::Reply&) const;
private:struct State;std::unique_ptr<State> state_;
};
}
