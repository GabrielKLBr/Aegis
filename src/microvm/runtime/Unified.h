#pragma once
#include "scanner/Scanner.h"
#include "guardian/Telemetry.h"
namespace av::microvm::runtime {
scan::Json Run(const scan::fs::path& input,const scan::fs::path& registry,const scan::fs::path& output,
    const guardian::telemetry::Policy& policy={},uint32_t milliseconds=15000,uint64_t checkpointAfter=0,const scan::fs::path& restore={});
}
