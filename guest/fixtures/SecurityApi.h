#pragma once
#include "Win32Api.h"
#include "guardian/Security.h"
namespace guardian::secwin {
using namespace guardian::win;
struct Sid {uint8_t revision,count,authority[6];uint32_t sub[8];};
struct Acl {uint8_t revision,sbz;uint16_t bytes,count,reserved;};
struct Ace {uint8_t type,flags;uint16_t bytes;uint32_t mask;};
struct Descriptor {uint8_t revision,sbz;uint16_t control;void* owner;void* group;void* sacl;void* dacl;};
struct Relative {uint8_t revision,sbz;uint16_t control;uint32_t owner,group,sacl,dacl;};
struct SidAttributes {void* sid;Dword attributes;};
struct Mapping {Dword read,write,execute,all;};
struct ServiceState {Dword type,state,accepted,exit,specific,checkpoint,hint;};
struct ServiceEntry {const uint16_t* name;void (*main)(Dword,uint16_t**);};
struct Config {Dword type,start,error;uint16_t* image;uint16_t* group;Dword tag;uint16_t* dependencies;uint16_t* account;uint16_t* display;};
using Handler=Dword (*)(Dword,Dword,void*,void*);
static_assert(sizeof(Descriptor)==40&&sizeof(Relative)==20&&sizeof(Acl)==8&&sizeof(Ace)==8&&sizeof(ServiceState)==28&&sizeof(Config)==64&&sizeof(SidAttributes)==16);
}
