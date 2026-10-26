#pragma once
#include "InternetBase.h"
extern "C" void* WINAPI NetWinHttpOpen(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD);
extern "C" void* WINAPI NetWinHttpConnect(void*,LPCWSTR,USHORT,DWORD);
extern "C" void* WINAPI NetWinHttpOpenRequest(void*,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR const*,DWORD);
extern "C" BOOL WINAPI NetWinHttpSendRequest(void*,LPCWSTR,DWORD,void*,DWORD,DWORD,DWORD_PTR);
extern "C" BOOL WINAPI NetWinHttpReceiveResponse(void*,void*);
extern "C" BOOL WINAPI NetWinHttpReadData(void*,void*,DWORD,DWORD*);
extern "C" BOOL WINAPI NetWinHttpQueryDataAvailable(void*,DWORD*);
extern "C" BOOL WINAPI NetWinHttpQueryHeaders(void*,DWORD,LPCWSTR,void*,DWORD*,DWORD*);
extern "C" BOOL WINAPI NetWinHttpCloseHandle(void*);
extern "C" BOOL WINAPI NetWinHttpSetOption(void*,DWORD,void*,DWORD);
extern "C" BOOL WINAPI NetWinHttpSetCredentials(void*,DWORD,DWORD,LPCWSTR,LPCWSTR,void*);
