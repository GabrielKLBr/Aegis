#pragma once
#include <string>
#include <string_view>
namespace av::microvm::native_gui {
struct Manifest {std::string phase,backend,helperSha256,protocol;};
Manifest ReadManifest(std::string_view);
}
