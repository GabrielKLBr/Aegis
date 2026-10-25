#include "../guest/ApiSet.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using guardian::apiset::Namespace;
void Check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
std::vector<uint8_t> Read(const char* name){std::ifstream f(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name))),std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){try{Check(argc==4,"v2, v6 and native schema inputs required");char host[128]{};
    for(int i=1;i<4;++i){auto pe=Read(argv[i]);Namespace schema;Check(schema.OpenPe(pe.data(),uint32_t(pe.size()),i==1?2u:6u),"Schema PE rejected");
        Check(schema.Resolve("API-MS-WIN-CORE-MEMORY-L1-1-0.DLL","c:\\app\\console.exe",host)&&std::string(host)=="kernelbase.dll","Core memory contract");
        Check(!schema.Resolve("api-ms-win-core-missing-l9-9-9.dll",nullptr,host),"Unknown contract accepted");
        Check(!schema.OpenPe(pe.data(),uint32_t(pe.size()),i==1?6u:2u),"Wrong profile schema accepted");
        for(uint32_t length:{0u,7u,63u,127u,263u})Check(!schema.OpenPe(pe.data(),length),"Truncated PE accepted");
        Check(schema.OpenPe(pe.data(),uint32_t(pe.size())),"Schema reload");
        if(i<3){Check(schema.Resolve("api-ms-win-core-aegis-importer-l1-1-0","c:\\app\\plugin.dll",host)&&std::string(host)=="kernel32.dll","Importer override");
            Check(schema.Resolve("api-ms-win-core-aegis-importer-l1-1-0","other.dll",host)&&std::string(host)=="kernelbase.dll","Default host");}
        if(i==3)Check(schema.Count()>500,"Native schema corpus not exercised");
    }
    const auto raw=Read(argv[2]);Namespace schema;Check(schema.OpenPe(raw.data(),uint32_t(raw.size())),"Fixture schema");
    Check(!schema.Resolve("api-ms-win-core-memory-l1-1-999.dll",nullptr,host),"Unsupported revision accepted");
    for(uint32_t offset:{4u,12u,16u,20u}){auto corrupt=raw;for(uint32_t b=0;b<4;++b)corrupt[512+offset+b]=0xff;Check(!schema.OpenPe(corrupt.data(),uint32_t(corrupt.size())),"Corrupt namespace metadata accepted");}
    std::cout<<"API set v2/v6, native corpus, revision bounds, importer aliases and malformed inputs PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
