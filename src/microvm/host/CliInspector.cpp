#include "guardian/Cli.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
int wmain(int argc,wchar_t** argv){
    if(argc!=2){std::cerr<<"Usage: guardian-cli-inspect FILE\n";return 2;}
    try{const auto path=std::filesystem::path(argv[1]);const auto size=std::filesystem::file_size(path);if(!size||size>guardian::cli::MaxFile)throw std::runtime_error("CLI file quota");std::vector<uint8_t> bytes(size_t(size),uint8_t(0));std::ifstream file(path,std::ios::binary);if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size)))throw std::runtime_error("CLI read failed");
        guardian::cli::Image image;const auto valid=image.Open(bytes.data(),uint32_t(bytes.size()))&&image.Validate();
        std::cout<<"{\"schema\":1,\"runtime\":\"minint-cil-console-v1\",\"status\":\""<<(valid?"PASS":"UNSUPPORTED")<<"\",\"diagnostic\":\""<<guardian::cli::Name(image.error)<<"\",\"detail\":"<<image.detail<<",\"entryToken\":"<<image.entry<<",\"methods\":"<<image.methods<<",\"staticInspectionOnly\":true,\"runtimeChecksRequired\":true,\"inputExecutedOnHost\":false,\"microsoftClr\":false}\n";return valid?0:3;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
