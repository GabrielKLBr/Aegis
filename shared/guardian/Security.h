#pragma once
#include "Storage.h"
namespace guardian::security {
inline constexpr uint32_t Magic=0x36534547,Version=1;
inline constexpr uint64_t StateGpa=0x5000000,StateBytes=65536;
inline constexpr uint32_t MaxHandles=48,MaxServices=3,MaxEvents=8;
enum class Op:uint32_t {Close=1,RegOpen,RegCreate,RegSet,RegQuery,RegDeleteValue,RegDeleteKey,RegEnum,RegSecurity,RegSetSecurity,RegFlush,RegWatch,RegPoll,
    TokenOpen=20,TokenDuplicate,TokenQuery,AccessCheck,Profile,
    ScmOpen=40,ServiceCreate,ServiceOpen,ServiceStart,ServiceQuery,ServiceControl,ServiceDelete,ServiceConfig,ServiceAssignment,ServiceStatus,ServiceNextControl,
    EventRegister=60,EventWrite,EventRead};
struct ServiceStatus {uint32_t type=16,state=1,accepted=0,exit=0,specific=0,checkpoint=0,hint=0,pid=0,flags=0;};
struct ServiceConfig {uint32_t type=16,start=3,error=1,reserved=0;char image[128]{},name[32]{},display[64]{};};
struct Request {uint32_t magic=Magic,version=Version,bytes=sizeof(Request);Op op=Op::Close;
    uint64_t handle=0,other=0;uint32_t access=0,flags=0,type=0,index=0,length=0,returned=0;
    processes::Token token{};processes::Security security{};ServiceStatus service{};
    char path[128]{},name[32]{},text[128]{};uint8_t data[512]{};
};
static_assert(sizeof(ServiceConfig)==240&&sizeof(ServiceStatus)==36&&sizeof(Request)<1200);
inline constexpr processes::Token LocalService() noexcept {processes::Token t;t.user=200;t.primaryGroup=20;t.integrity=1;return t;}
inline bool Administrator(const processes::Token& t) noexcept {return t.user==100&&t.primaryGroup==10&&t.integrity==2;}
}
