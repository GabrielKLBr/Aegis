#pragma once
#include "guardian/NtCompatibility.h"
#include "../state/Files.h"
#include <array>
namespace av::microvm::compatibility {
using Digest=std::array<uint8_t,32>;
Digest Sha256(std::span<const uint8_t>);
guardian::nt::Info Read(const std::filesystem::path& binding,const std::filesystem::path& manifest);
void Verify(const guardian::nt::Info&,std::span<const uint8_t> kernel,std::span<const uint8_t> application);
std::string Json(const guardian::nt::Info&);
}
