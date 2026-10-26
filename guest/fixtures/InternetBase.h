#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <Windows.h>
#include <intrin.h>
#include <new>
#include <initializer_list>
#include "bearssl.h"
#include "guardian/Internet.h"
namespace inet=guardian::internet;
extern "C" DWORD GuardianInternetCall(inet::Request*) noexcept;
namespace netapp {
void Copy(void*,const void*,SIZE_T);
void Wipe(void*,SIZE_T);
bool Equal(const void*,const void*,SIZE_T);
UINT Length(const wchar_t*,UINT cap=32768);
UINT Length(const char*,UINT cap=32768);
bool Text(const wchar_t*,const wchar_t*);
bool Narrow(const wchar_t*,char*,UINT);
void* Allocate(SIZE_T);
void Release(void*);
struct Guard {Guard();~Guard();};
template<class T>T* Make(){auto* p=Allocate(sizeof(T));return p?new(p)T():nullptr;}
template<class T>void Destroy(T* p){if(p){p->~T();Release(p);}}
uint64_t Handle(UINT,void*);
void* Object(uint64_t,UINT);
bool Drop(uint64_t,UINT);
UINT Handles(UINT,uint64_t*,UINT);
DWORD Exchange(inet::Request&);
bool Random(void*,UINT);
DWORD Resolve(const char*,BYTE[16]);
DWORD Open(UINT,UINT&,UINT* localPort=nullptr);
DWORD Connect(UINT,const BYTE[16],UINT);
DWORD Poll(UINT,DWORD&);
DWORD Send(UINT,const void*,UINT,UINT&);
DWORD Receive(UINT,void*,UINT,UINT&);
void Close(UINT);
void Cleanup();
}
