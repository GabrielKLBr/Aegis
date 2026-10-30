#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#endif
#include "InternetBackend.h"
#include "Network.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <sstream>
#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/random.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#endif
namespace av::microvm::internet_session {
using namespace guardian::internet;
namespace {
constexpr uint32_t Invalid=10022,Denied=10013,WouldBlock=10035,NotSocket=10038,NoBuffers=10055;
#if defined(_WIN32)
using Socket=SOCKET;constexpr Socket Bad=INVALID_SOCKET;
void Close(Socket s){if(s!=Bad)closesocket(s);}
uint32_t Error(){return uint32_t(WSAGetLastError());}
#else
using Socket=int;constexpr Socket Bad=-1;
void Close(Socket s){if(s!=Bad)::close(s);}
uint32_t Error(){return errno==EAGAIN||errno==EINPROGRESS?WouldBlock:10061;}
#endif
bool Name(const char* s){uint32_t label=0;for(uint32_t i=0;i<64;++i){const auto c=s[i];if(!c){if(!i||!label||s[i-1]=='-')return false;for(uint32_t j=i+1;j<64;++j)if(s[j])return false;return true;}if(c=='.'){if(!label||s[i-1]=='-')return false;label=0;}else{if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')||(!label&&c=='-'))return false;++label;}}return false;}
bool ZeroTail(const uint8_t* p,uint32_t from,uint32_t to){for(uint32_t i=from;i<to;++i)if(p[i])return false;return true;}
}
void Validate(const Policy& p){
    state::Require(p.magic==Magic&&p.version==1&&p.bytes==sizeof(p)&&uint32_t(p.mode)<=3&&p.endpoints<=MaxEndpoints&&p.requestLimit>=16&&p.requestLimit<=65536&&p.byteLimit>=4096&&p.byteLimit<=16u<<20,"Internet policy bounds rejected");
    state::Require(p.mode!=Mode::Offline||!p.endpoints,"Offline policy must not declare destinations");
    for(uint32_t i=0;i<MaxEndpoints;++i){const auto& e=p.entries[i];
        if(i>=p.endpoints){const Endpoint empty{};state::Require(!std::memcmp(&e,&empty,sizeof(e)),"Unused internet endpoint rejected");continue;}
        state::Require(Name(e.name)&&e.family==2&&e.port>0&&e.port<=65535&&(e.kind==1||e.kind==2)&&e.service<=2&&ZeroTail(e.address,4,16),"Internet endpoint rejected");
        if(p.mode==Mode::Lab)state::Require(e.address[0]==127&&!e.address[1]&&!e.address[2]&&e.address[3]==1,"Lab requires explicit IPv4 loopback endpoints");
        if(p.mode==Mode::Authorized){guardian::net::Address address{};std::memcpy(address.bytes,e.address,4);state::Require(net::Public(4,address),"Authorized destination must be a fixed public address");}
        if(p.mode==Mode::Simulated)state::Require(e.address[0]==198&&e.address[1]==18&&e.address[2]==0&&e.address[3]>0,"Simulated endpoint must be in the owned range");
        for(uint32_t j=0;j<i;++j)state::Require(std::strcmp(e.name,p.entries[j].name)||e.port!=p.entries[j].port||e.kind!=p.entries[j].kind,"Duplicate internet endpoint");
    }
}
Policy ReadPolicy(const std::filesystem::path& path){const auto data=state::ReadFile(path,sizeof(Policy));state::Require(data.size()==sizeof(Policy),"Internet policy size rejected");Policy p;std::memcpy(&p,data.data(),sizeof(p));Validate(p);return p;}
struct Backend::Impl {
    struct Channel {uint32_t id=0,pid=0,kind=0;Socket socket=Bad;const Endpoint* endpoint=nullptr;bool connected=false,eof=false;std::vector<uint8_t> pending,input;std::deque<std::vector<uint8_t>> datagrams;};
    Policy policy;std::array<Channel,MaxSockets> channels{};uint64_t sequence=0;uint32_t next=0,requests=0,traffic=0,nativeOpened=0;
    struct Event{uint32_t op,pid,id,status,bytes;};std::vector<Event> events;
    explicit Impl(const Policy& p):policy(p){Validate(policy);
#if defined(_WIN32)
        WSADATA data{};state::Require(!WSAStartup(MAKEWORD(2,2),&data),"Internet transport initialization failed");
#endif
    }
    ~Impl(){for(auto& c:channels)Close(c.socket);
#if defined(_WIN32)
        WSACleanup();
#endif
    }
    uint32_t Ready(Channel& c,uint32_t& flags){
            flags=0;if(policy.mode==Mode::Simulated){flags=2|((!c.pending.empty()||!c.datagrams.empty()||c.eof)?1u:0u);return 0;}
#if defined(_WIN32)
            WSAPOLLFD poll{c.socket,short(POLLRDNORM|POLLWRNORM),0};const auto count=WSAPoll(&poll,1,0);
#else
            pollfd poll{c.socket,short(POLLIN|POLLOUT),0};const auto count=::poll(&poll,1,0);
#endif
            if(count<0)return Error();if(!count)return 0;if(poll.revents&(POLLERR|POLLNVAL))return 10061;
            if(poll.revents&(POLLRDNORM|POLLHUP))flags|=1;if(poll.revents&POLLWRNORM){flags|=2;c.connected=true;}return 0;
    }
    uint32_t Execute(Request& r){
        if(r.op==Op::Exit){for(auto& c:channels)if(c.pid==r.pid){Close(c.socket);c={};}return 0;}
        Channel* c=nullptr;for(auto& candidate:channels)if(candidate.id&&candidate.id==r.id&&candidate.pid==r.pid)c=&candidate;
        if(r.op==Op::Close){if(!c)return NotSocket;Close(c->socket);*c={};return 0;}
        if(++requests>policy.requestLimit||r.length>policy.byteLimit-traffic)return NoBuffers;
        if(r.op==Op::Random){
#if defined(_WIN32)
            if(BCryptGenRandom(nullptr,r.data,r.length,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 10014;
#else
            if(getrandom(r.data,r.length,0)!=r.length)return 10014;
#endif
            traffic+=r.length;return 0;
        }
        if(r.op==Op::Resolve){if(policy.mode==Mode::Offline)return Denied;if(!Name(r.name))return Invalid;for(uint32_t i=0;i<policy.endpoints;++i)if(!std::strcmp(r.name,policy.entries[i].name)){std::memcpy(r.address,policy.entries[i].address,16);return 0;}return 11001;}
        if(r.op==Op::Open){if(r.kind!=1&&r.kind!=2)return 10043;for(auto& channel:channels)if(!channel.id){if(next==UINT32_MAX)return NoBuffers;channel.id=++next;channel.pid=r.pid;channel.kind=r.kind;r.id=channel.id;r.port=49152u+uint32_t(&channel-channels.data());return 0;}return NoBuffers;}
        if(!c)return NotSocket;
        if(r.op==Op::Connect){if(c->endpoint)return c->connected?10056:WouldBlock;if(policy.mode==Mode::Offline)return Denied;
            for(uint32_t i=0;i<policy.endpoints;++i){const auto& e=policy.entries[i];if(e.port==r.port&&e.kind==c->kind&&!std::memcmp(e.address,r.address,16)){c->endpoint=&e;break;}}
            if(!c->endpoint)return Denied;
            if(policy.mode==Mode::Simulated){c->connected=true;return 0;}
            if(policy.mode==Mode::Authorized){guardian::net::Address address{};std::memcpy(address.bytes,c->endpoint->address,4);if(net::LocalEndpoint(address))return Denied;}
            c->socket=::socket(AF_INET,c->kind==1?SOCK_STREAM:SOCK_DGRAM,0);if(c->socket==Bad)return Error();++nativeOpened;
#if defined(_WIN32)
            u_long nonblocking=1;if(ioctlsocket(c->socket,FIONBIO,&nonblocking))return Error();
#else
            if(fcntl(c->socket,F_SETFL,O_NONBLOCK)<0)return Error();
#endif
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(r.port));std::memcpy(&address.sin_addr,r.address,4);
            if(!::connect(c->socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))){c->connected=true;return 0;}const auto error=Error();return error==10036||error==10037?WouldBlock:error;
        }
        if(!c->endpoint)return 10057;
        if(policy.mode==Mode::Authorized){guardian::net::Address address{};std::memcpy(address.bytes,c->endpoint->address,4);if(net::LocalEndpoint(address))return Denied;}
        if(r.op==Op::Poll)return Ready(*c,r.flags);
        if(!c->connected){uint32_t flags=0;const auto error=Ready(*c,flags);if(error)return error;}
        if(!c->connected)return WouldBlock;
        if(r.op==Op::Send){
            if(policy.mode==Mode::Simulated){if(c->endpoint->service==2){r.length=0;return WouldBlock;}if(c->input.size()+r.length>65536)return NoBuffers;c->input.insert(c->input.end(),r.data,r.data+r.length);
                if(c->endpoint->service==0){if(c->kind==2){if(c->datagrams.size()>=8)return NoBuffers;c->datagrams.emplace_back(r.data,r.data+r.length);}else{if(r.length>65536-c->pending.size())return NoBuffers;c->pending.insert(c->pending.end(),r.data,r.data+r.length);}c->input.clear();}
                else {constexpr char endHeaders[]="\r\n\r\n";if(c->input.size()>=4&&std::search(c->input.begin(),c->input.end(),std::begin(endHeaders),std::end(endHeaders)-1)!=c->input.end()){constexpr char response[]="HTTP/1.1 200 OK\r\nContent-Length: 18\r\nConnection: close\r\n\r\nMiniNT HTTP works\n";c->pending.assign(response,response+sizeof(response)-1);c->input.clear();c->eof=true;}}
            }else{const int sent=::send(c->socket,reinterpret_cast<const char*>(r.data),int(r.length),0);if(sent<0)return Error();r.length=uint32_t(sent);}traffic+=r.length;return 0;
        }
        if(r.op==Op::Receive){if(policy.mode==Mode::Simulated){if(c->kind==2){if(c->datagrams.empty()){r.length=0;return WouldBlock;}auto packet=std::move(c->datagrams.front());c->datagrams.pop_front();const bool truncated=packet.size()>r.length;r.length=uint32_t(std::min<size_t>(r.length,packet.size()));std::memcpy(r.data,packet.data(),r.length);traffic+=r.length;return truncated?10040u:0u;}if(c->pending.empty()){r.length=0;return c->eof?0:WouldBlock;}r.length=uint32_t(std::min<size_t>(r.length,c->pending.size()));std::memcpy(r.data,c->pending.data(),r.length);c->pending.erase(c->pending.begin(),c->pending.begin()+r.length);}
            else{const int received=::recv(c->socket,reinterpret_cast<char*>(r.data),int(r.length),0);if(received<0){const auto error=Error();if(error==10040)traffic+=r.length;return error;}r.length=uint32_t(received);}traffic+=r.length;return 0;}
        return Invalid;
    }
};
Backend::Backend(const Policy& p):impl_(std::make_unique<Impl>(p)){}
Backend::~Backend()=default;
bool Backend::Exchange(Request& r){auto& i=*impl_;if(r.magic!=Magic||r.version!=1||r.bytes!=sizeof(r)||i.sequence==UINT64_MAX||r.sequence!=i.sequence+1||!r.pid||r.status||r.flags||r.length>Payload||uint32_t(r.op)<1||uint32_t(r.op)>9)return false;++i.sequence;r.status=i.Execute(r);if(i.events.size()<i.policy.requestLimit)i.events.push_back({uint32_t(r.op),r.pid,r.id,r.status,r.length});return true;}
std::string Backend::Json() const {const auto& i=*impl_;std::ostringstream out;uint32_t active=0;for(const auto& c:i.channels)if(c.id)++active;out<<"{\"schema\":1,\"mode\":"<<uint32_t(i.policy.mode)<<",\"bytes\":"<<i.traffic<<",\"nativeSocketsOpened\":"<<i.nativeOpened<<",\"activeSockets\":"<<active<<",\"hostResolverUsed\":false,\"hostTlsUsed\":false,\"events\":[";bool first=true;for(const auto& e:i.events){if(!first)out<<',';first=false;out<<"{\"operation\":"<<e.op<<",\"pid\":"<<e.pid<<",\"id\":"<<e.id<<",\"status\":"<<e.status<<",\"bytes\":"<<e.bytes<<'}';}out<<"]}\n";return out.str();}
void Backend::SaveOffline(state::Writer& w) const {
    const auto& s=*impl_;state::Require(s.policy.mode==Mode::Offline&&!s.nativeOpened,"Live internet transport checkpoints are unsupported");for(const auto& c:s.channels)state::Require(!c.id,"Open internet channel checkpoint unsupported");
    w.Pod(s.policy);w.Pod(s.sequence);w.Pod(s.next);w.Pod(s.requests);w.Pod(s.traffic);w.Pod(uint32_t(s.events.size()));for(const auto& e:s.events)w.Pod(e);
}
void Backend::LoadOffline(state::Reader& r){
    auto& s=*impl_;state::Writer check;SaveOffline(check);const auto policy=r.Pod<Policy>();state::Require(!std::memcmp(&policy,&s.policy,sizeof(policy)),"Checkpoint internet policy mismatch");s.sequence=r.Pod<uint64_t>();s.next=r.Pod<uint32_t>();s.requests=r.Pod<uint32_t>();s.traffic=r.Pod<uint32_t>();const auto count=r.Pod<uint32_t>();state::Require(s.requests<=policy.requestLimit&&s.traffic<=policy.byteLimit&&count<=65536&&s.sequence<=65536&&s.next<=65536,"Checkpoint internet counters rejected");s.events.clear();for(uint32_t i=0;i<count;++i)s.events.push_back(r.Pod<Impl::Event>());
}

}
