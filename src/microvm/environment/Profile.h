#pragma once
#include "guardian/Environment.h"
#include "../state/Files.h"
#include "../hardware/Profile.h"
namespace av::microvm::environment {
guardian::environment::Info Make(uint64_t seed,const std::string& nt,bool preserve=true);
void Validate(const guardian::environment::Info&);
hardware::Profile Hardware(const guardian::environment::Info&);
std::string Json(const guardian::environment::Info&);
}
