#pragma once
#include "../state/State.h"
namespace av::microvm::boot {
struct SessionSnapshot {
    state::MachineState machine;
    std::vector<uint8_t> metadata, disk;
    std::vector<uint8_t> Encode() const {
        state::Writer w;
        w.Pod(uint32_t(1));
        machine.Save(w);
        w.Bytes(metadata);
        w.Bytes(disk);
        return state::Seal(w.data);
    }
    static SessionSnapshot Decode(std::span<const uint8_t> file) {
        auto bytes = state::Open(file);
        state::Reader r(bytes);
        state::Require(r.Pod<uint32_t>() == 1, "Session snapshot version rejected");
        SessionSnapshot s;
        s.machine = state::MachineState::Load(r);
        s.metadata = r.Bytes(5 << 20);
        s.disk = r.Bytes(272ull << 20);
        r.End();
        return s;
    }
    uint64_t BootId() const {
        state::Reader r(metadata);
        return r.Pod<uint64_t>();
    }
};
} // namespace av::microvm::boot
