// Quarantine.h - isolates detected files so they can no longer execute.
#pragma once
#include "Common.h"
#include "SignatureDatabase.h"

namespace av {

class Quarantine {
public:
    // Compatibility argument; production operations use the protected service vault.
    explicit Quarantine(std::wstring dir);

    // Persists and verifies an authenticated encrypted copy before removing
    // the exact detected original. Any failure is reported, never truncated.
    bool Isolate(const Detection& det);

    // Lists current quarantine entries to the console.
    void List() const;


};

} // namespace av
