// Quarantine.h - isolates detected files so they can no longer execute.
#pragma once
#include "Common.h"
#include "SignatureDatabase.h"

namespace av {

class Quarantine {
public:
    // 'dir' is where neutralized copies + metadata are stored.
    explicit Quarantine(std::wstring dir);

    // Moves the detected file into quarantine, XOR-neutralizing its bytes so it
    // cannot run, then deletes the original. Returns true on success.
    bool Isolate(const Detection& det);

    // Lists current quarantine entries to the console.
    void List() const;

private:
    std::wstring dir_;
    void EnsureDir() const;
};

} // namespace av
