#include "Profile.h"
#include <sstream>
#include <iomanip>
namespace av::microvm::environment {
void Validate(const guardian::environment::Info& p){state::Require(guardian::environment::Valid(p)&&!p.catalogHash,"Synthetic profile version, NT, seed or ownership rejected");}
guardian::environment::Info Make(uint64_t seed,const std::string& nt,bool preserve){
    guardian::environment::Info p;p.seed=seed;p.preserve=preserve?1u:0u;
    if(nt=="win7"){p.ntMajor=6;p.ntMinor=1;p.ntBuild=7601;}
    else if(nt=="win10")p.ntBuild=19045;
    else state::Require(nt=="win11","Synthetic profile supports win7, win10 or win11 x64");
    Validate(p);return p;
}
hardware::Profile Hardware(const guardian::environment::Info& p){
    Validate(p);hardware::Profile h;h.nt=p.ntBuild==7601?"win7":p.ntBuild==19045?"win10":"win11";
    h.models={"Guardian synthetic x64 CPU","Guardian software graphics","Guardian virtual block disk","Guardian synthetic firmware","Guardian synthetic board"};
    std::ostringstream id;id<<std::hex<<std::setfill('0')<<std::setw(16)<<guardian::environment::Hash(&p,sizeof(p))<<std::setw(16)<<p.seed;h.identity=id.str();hardware::Validate(h);return h;
}
std::string Json(const guardian::environment::Info& p){
    Validate(p);std::ostringstream o;o<<"{\"schema\":1,\"contentVersion\":1,\"synthetic\":true,\"hostImports\":false,\"seed\":"<<p.seed
      <<",\"ntBuild\":"<<p.ntBuild<<",\"account\":\""<<guardian::environment::Account(p).bytes<<"\",\"userId\":100,\"preserve\":"<<(p.preserve?"true":"false")
      <<",\"programs\":[{\"name\":\"Guardian Docs\",\"version\":1,\"image\":\"guardian-docs.exe\",\"backing\":\"immutable guest catalog\"}],\"documents\":[\"C:\\\\welcome.txt\",\"C:\\\\budget.csv\",\"C:\\\\notes.txt\"],\"shortcutFormat\":\"Guardian shortcut v1\",\"fileTimeResolutionSeconds\":1}";return o.str();
}
}
