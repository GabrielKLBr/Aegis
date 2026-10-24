#include "../src/microvm/compatibility/Binding.h"
#include <iostream>
using namespace av::microvm;
namespace {
void Check(bool ok,const char* why){state::Require(ok,why);}
template<class F> void Refused(F f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}Check(refused,"Invalid compatibility binding accepted");}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--profiles"){
        const char* names[]{"win10","win7","win11"};std::cout<<'{';unsigned i=0;
        for(const auto& t:guardian::nt::Targets){if(i)std::cout<<',';std::cout<<'"'<<names[i++]<<"\":["<<t.id<<','<<t.major<<','<<t.minor<<','<<t.build<<']';}
        std::cout<<"}\n";return 0;
    }
    const std::vector<uint8_t> abc{'a','b','c'};
    const auto digest=compatibility::Sha256(abc);
    constexpr uint8_t expected[]{0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Check(std::memcmp(digest.data(),expected,32)==0,"SHA-256 known vector failed");
    for(const auto& target:guardian::nt::Targets){
        guardian::nt::Info p;p.profile=target.id;p.major=target.major;p.minor=target.minor;p.build=target.build;
        std::memcpy(p.manifestSha,digest.data(),32);std::memcpy(p.kernelSha,digest.data(),32);std::memcpy(p.runtimeSha,digest.data(),32);
        Check(guardian::nt::Valid(p,0),"Known target rejected");compatibility::Verify(p,abc,{});
        for(unsigned mode=0;mode<3;++mode){const auto v=guardian::nt::Reported(target,static_cast<guardian::nt::Manifest>(mode));
            Check(mode!=2||v.build==target.build,"Manifest-aware version differs from target");
            Check(target.id!=2||v.build==7601,"Win7 target policy failed");
            Check(target.id==2||mode!=0||v.build==9200,"Unaware policy failed");}
        auto bad=p;bad.build=26300;Refused([&]{compatibility::Verify(bad,abc,{});});
        bad=p;bad.revision=123;Refused([&]{compatibility::Verify(bad,abc,{});});
        bad=p;bad.contract=2;Refused([&]{compatibility::Verify(bad,abc,{});});
        bad=p;bad.profile=99;Refused([&]{compatibility::Verify(bad,abc,{});});
        bad=p;bad.applicationSha[0]=1;Refused([&]{compatibility::Verify(bad,abc,{});});
        Refused([&]{compatibility::Verify(p,{},{});});
        Check(compatibility::Json(p).find("\"windowsBinaryCompatibility\":false")!=std::string::npos,"Binding implies general Windows compatibility");
    }
    std::cout<<"NT target policies, SHA-256 known vector, profile/build/revision/contract/hash refusal PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
