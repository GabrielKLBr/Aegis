#pragma once
#include "Services.h"
#include "scanner/Store.h"
namespace av::services {
Json LookupHash(const Json&,const fs::path& cache,const scan::Transport& = scan::Https,std::string key = {});
Json HashLookupFile(const fs::path& request);
std::string BoundedFileHash(const fs::path&);
}
