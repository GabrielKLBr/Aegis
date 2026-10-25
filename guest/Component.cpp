#include "Component.h"
namespace guardian::component {
namespace {
void Copy(void* d,const void* s,uint32_t n) noexcept {for(uint32_t i=0;i<n;++i)static_cast<uint8_t*>(d)[i]=static_cast<const uint8_t*>(s)[i];}
bool Equal(const void* a,const void* b,uint32_t n) noexcept {for(uint32_t i=0;i<n;++i)if(static_cast<const uint8_t*>(a)[i]!=static_cast<const uint8_t*>(b)[i])return false;return true;}
bool Name(const char* s) noexcept {for(uint32_t i=0;i<64;++i){if(!s[i])return true;if(s[i]<32||s[i]>126)return false;}return false;}
}
Core::Endpoint* Core::Find(uint64_t id) noexcept {for(auto& e:endpoints_)if(e.id&&e.id==id)return &e;return nullptr;}
void Core::Drop(Endpoint& e) noexcept {for(auto& c:calls_)if(c.id&&c.endpoint==e.id&&c.state!=3){c.state=3;c.status=1726;c.length=0;}e={};}
void Core::Exit(uint32_t pid,uint32_t tid) noexcept {for(auto& e:endpoints_)if(e.id&&e.pid==pid&&(!tid||e.tid==tid))Drop(e);for(auto& c:calls_)if(c.id&&c.pid==pid&&(!tid||c.tid==tid))c={};}
bool Core::Clean() const noexcept {for(const auto& e:endpoints_)if(e.id)return false;for(const auto& c:calls_)if(c.id)return false;return true;}
uint32_t Core::Execute(uint32_t pid,uint32_t tid,Request& r) noexcept {
    if(!processes_||r.magic!=Magic||r.version!=Version||r.bytes!=sizeof(r)||r.length>Payload||r.kind>1||!Name(r.name)||r.pid||r.tid)return 87;
    const auto* caller=processes_->Get(pid);if(!caller||caller->state!=processes::State::Live||processes_->Owner(tid)!=pid)return 5;
    const auto allowed=[&](const Endpoint& e) noexcept {const auto* p=processes_->Get(e.pid);return p&&p->state==processes::State::Live&&p->token.user==caller->token.user&&p->token.integrity==caller->token.integrity;};
    if(r.op==Op::Stats){r.method=r.status=0;for(const auto& e:endpoints_)if(e.id)++r.method;for(const auto& c:calls_)if(c.id)++r.status;return 0;}
    if(r.op==Op::Register){for(const auto& e:endpoints_)if(e.id&&e.kind==r.kind&&Equal(e.iid,r.iid,16)&&Equal(e.name,r.name,64))return 183;
        if(next_==UINT64_MAX)return 1816;for(auto& e:endpoints_)if(!e.id){e.id=next_++;e.pid=pid;e.tid=tid;e.kind=r.kind;Copy(e.iid,r.iid,16);Copy(e.name,r.name,64);r.endpoint=e.id;return 0;}return 1816;}
    if(r.op==Op::Lookup){for(const auto& e:endpoints_)if(e.id&&e.kind==r.kind&&Equal(e.iid,r.iid,16)&&Equal(e.name,r.name,64)){if(!allowed(e))return 5;r.endpoint=e.id;r.pid=e.pid;r.tid=e.tid;return 0;}return 1168;}
    if(r.op==Op::Cancel||r.op==Op::Result){for(auto& c:calls_)if(c.id&&c.id==r.call){if(c.pid!=pid||c.tid!=tid)return 5;if(r.op==Op::Cancel){c={};return 0;}if(c.state!=3)return 997;r.status=c.status;r.length=c.length;Copy(r.data,c.data,c.length);c={};return 0;}return 6;}
    auto* e=Find(r.endpoint);if(!e)return 1722;
    if(r.op==Op::Call){if(!allowed(*e))return 5;if(next_==UINT64_MAX)return 1816;for(auto& c:calls_)if(!c.id){c.id=next_++;c.endpoint=e->id;c.pid=pid;c.tid=tid;c.method=r.method;c.length=r.length;c.state=1;Copy(c.data,r.data,r.length);r.call=c.id;return 0;}return 1816;}
    if(e->pid!=pid||e->tid!=tid)return 5;
    if(r.op==Op::Unregister){Drop(*e);return 0;}
    if(r.op==Op::Receive){for(auto& c:calls_)if(c.id&&c.endpoint==e->id&&c.state==1){c.state=2;r.call=c.id;r.method=c.method;r.length=c.length;r.pid=c.pid;r.tid=c.tid;Copy(r.data,c.data,c.length);return 0;}return 997;}
    if(r.op==Op::Reply){for(auto& c:calls_)if(c.id&&c.id==r.call&&c.endpoint==e->id&&c.state==2){c.status=r.status;c.length=r.length;Copy(c.data,r.data,r.length);c.state=3;return 0;}return 6;}
    return 87;
}
}
