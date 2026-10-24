#include "Binding.h"
#include <algorithm>
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif
namespace av::microvm::compatibility {
Digest Sha256(std::span<const uint8_t> bytes){
    Digest digest{};
#ifdef _WIN32
    state::Require(bytes.size()<=ULONG_MAX,"Hash input too large");
    const auto status=BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size()));
    state::Require(status>=0,"Compatibility SHA-256 failed");
#else
    unsigned size=0;
    state::Require(EVP_Digest(bytes.data(),bytes.size(),digest.data(),&size,EVP_sha256(),nullptr)==1&&size==32,"Compatibility SHA-256 failed");
#endif
    return digest;
}
guardian::nt::Info Read(const std::filesystem::path& binding,const std::filesystem::path& manifest){
    const auto bytes=state::ReadFile(binding,sizeof(guardian::nt::Info));
    state::Require(bytes.size()==sizeof(guardian::nt::Info),"NT binding size rejected");
    guardian::nt::Info p;std::memcpy(&p,bytes.data(),sizeof(p));
    state::Require(guardian::nt::Valid(p,0),"NT contract/profile rejected");
    const auto json=state::ReadFile(manifest,4ull<<20);
    const auto hash=Sha256(json);
    state::Require(std::equal(hash.begin(),hash.end(),p.manifestSha),"NT manifest SHA-256 mismatch");
    return p;
}
void Verify(const guardian::nt::Info& p,std::span<const uint8_t> kernel,std::span<const uint8_t> application){
    state::Require(guardian::nt::Valid(p,0),"NT contract/profile/revision rejected");
    const auto k=Sha256(kernel);
    state::Require(std::equal(k.begin(),k.end(),p.kernelSha),"NT kernel SHA-256 mismatch");
    if(application.empty())state::Require(!guardian::nt::Nonzero(p.applicationSha),"NT application required");
    else {const auto a=Sha256(application);state::Require(std::equal(a.begin(),a.end(),p.applicationSha),"NT application SHA-256 mismatch");}
}
std::string Json(const guardian::nt::Info& p){
    state::Require(guardian::nt::Valid(p,p.boot),"NT binding rejected");
    constexpr char hex[]="0123456789abcdef";
    const auto text=[&](const uint8_t* bytes){std::string s;for(unsigned i=0;i<32;++i){s+=hex[bytes[i]>>4];s+=hex[bytes[i]&15];}return s;};
    return "{\"schema\":1,\"contract\":1,\"targetProfile\":"+std::to_string(p.profile)+
        ",\"targetNt\":\""+std::to_string(p.major)+"."+std::to_string(p.minor)+"."+std::to_string(p.build)+
        "\",\"revision\":0,\"runtimeKind\":\"guardian-owned-subset\",\"windowsBinaryCompatibility\":false,\"manifestSha256\":\""+
        text(p.manifestSha)+"\",\"kernelSha256\":\""+text(p.kernelSha)+"\",\"applicationSha256\":\""+text(p.applicationSha)+
        "\",\"runtimeSha256\":\""+text(p.runtimeSha)+"\"}";
}
}
