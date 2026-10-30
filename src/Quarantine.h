// Quarantine.h - isolates detected files so they can no longer execute.
#pragma once
#include "Common.h"
#include "SignatureDatabase.h"

namespace av {

class Quarantine {
public:
    // 'dir' is a private transactional vault, separate from legacy XOR files.
    explicit Quarantine(std::wstring dir);

    // Persists and verifies an authenticated encrypted copy before removing
    // the exact detected original. Any failure is reported, never truncated.
    bool Isolate(const Detection& det);

    // Lists current quarantine entries to the console.
    void List() const;

private:
    std::wstring dir_;
};

} // namespace av
