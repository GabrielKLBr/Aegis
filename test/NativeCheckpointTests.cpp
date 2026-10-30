#include "guardian/NativeCheckpoint.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace guardian;
int main(){try{unsigned checks=0;auto need=[&](bool ok){++checks;if(!ok)throw std::runtime_error("checkpoint verifier check "+std::to_string(checks));};checkpoint::User seed;seed.boot=seed.session=1;need(checkpoint::Valid(seed));
    seed.windows=1;seed.next=2;seed.calls[0]=1;auto& w=seed.window[0].window;w.id=(uint64_t(1)<<56)|(uint64_t(1)<<32)|1;w.pid=w.tid=1;w.kind=nativeuser::Class::Custom;w.bounds={10,10,200,200};w.dpi=96;need(checkpoint::Valid(seed));
    auto bad=seed;bad.windows=33;need(!checkpoint::Valid(bad));bad=seed;bad.window[0].window.parent=w.id;need(!checkpoint::Valid(bad));bad=seed;bad.focus=99;need(!checkpoint::Valid(bad));bad=seed;bad.calls[1]=2;need(!checkpoint::Valid(bad));bad=seed;bad.window[0].text[0]=1;need(!checkpoint::Valid(bad));
    nativeuser::Command admin;admin.op=static_cast<nativeuser::Op>(checkpoint::Begin);need(!nativeuser::Valid(admin));
    uint32_t random=0x14130001,survivors=0;for(unsigned i=0;i<2000;++i){bad=seed;random^=random<<13;random^=random>>17;random^=random<<5;reinterpret_cast<uint8_t*>(&bad)[random%sizeof(bad)]^=uint8_t(1u<<(random%8));if(checkpoint::Valid(bad))++survivors;}
    need(survivors>0&&survivors<2000);std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"mutations\":2000,\"acceptedMutations\":"<<survivors<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
