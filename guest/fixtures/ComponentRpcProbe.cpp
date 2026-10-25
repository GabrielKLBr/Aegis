#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <rpc.h>
#include <rpcdcep.h>
#include <rpcndr.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void Verify(bool b,const char* why){if(!b){std::printf("RPC FAIL %s last=%lu\n",why,GetLastError());std::exit(50);}}
static RPC_SYNTAX_IDENTIFIER identity{{0x6a140811,0x1480,0x4148,{0x81,0x48,0,0,0,0,0,1}},{1,0}};
static RPC_SYNTAX_IDENTIFIER syntax{{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,8,0,0x2b,0x10,0x48,0x60}},{2,0}};
static void __RPC_STUB Calculate(RPC_MESSAGE* message){if(message->BufferLength!=8){message->BufferLength=0;return;}LONG values[2]{};std::memcpy(values,message->Buffer,8);message->BufferLength=4;Verify(I_RpcGetBuffer(message)==RPC_S_OK,"server response allocation");const LONG result=values[0]+values[1];std::memcpy(message->Buffer,&result,4);}
static void __RPC_STUB Stop(RPC_MESSAGE* message){message->BufferLength=0;Verify(I_RpcGetBuffer(message)==RPC_S_OK,"server stop allocation");RpcMgmtStopServerListening(nullptr);}
static RPC_DISPATCH_FUNCTION dispatch[]{Calculate,Stop};
static RPC_DISPATCH_TABLE table{2,dispatch,0};
static RPC_SERVER_INTERFACE server{sizeof(RPC_SERVER_INTERFACE),identity,syntax,&table,0,nullptr,nullptr,nullptr,0};
static RPC_CLIENT_INTERFACE client{sizeof(RPC_CLIENT_INTERFACE),identity,syntax,nullptr,0,nullptr,0,nullptr,0};
static int Server(){Verify(RpcServerUseProtseqEpW(reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),8,reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"Guardian.Component.148")),nullptr)==RPC_S_OK,"endpoint");Verify(RpcServerRegisterIf(&server,nullptr,nullptr)==RPC_S_OK,"register");
#ifndef COMPONENT_REFERENCE
Verify(RpcServerListen(1,8,TRUE)==RPC_S_CANNOT_SUPPORT,"asynchronous listener rejected");
#endif
Verify(RpcServerListen(1,8,FALSE)==RPC_S_OK,"listen");Verify(RpcServerUnregisterIf(&server,nullptr,TRUE)==RPC_S_OK,"unregister");std::printf("RPC server shutdown PASS\n");return 0;}
[[maybe_unused]] static RPC_STATUS Call(RPC_BINDING_HANDLE binding,UINT procedure,LONG a,LONG b,LONG* out){RPC_MESSAGE message{};message.Handle=binding;message.DataRepresentation=NDR_LOCAL_DATA_REPRESENTATION;message.BufferLength=8;message.ProcNum=procedure;message.RpcInterfaceInformation=&client;message.TransferSyntax=&syntax;const auto allocation=I_RpcGetBuffer(&message);if(allocation)return allocation;const LONG values[]{a,b};std::memcpy(message.Buffer,values,8);const auto result=I_RpcSendReceive(&message);if(!result&&out){if(message.BufferLength!=4){I_RpcFreeBuffer(&message);return RPC_X_BAD_STUB_DATA;}std::memcpy(out,message.Buffer,4);}I_RpcFreeBuffer(&message);return result;}
int main(int argc,char** argv){if(argc>1&&std::strcmp(argv[1],"--server")==0)return Server();UUID id{};RPC_WSTR encoded=nullptr;RPC_STATUS status=0;Verify(UuidFromStringW(reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"6a140811-1480-4148-8148-000000000001")),&id)==0,"UUID parse");Verify(UuidToStringW(&id,&encoded)==0&&UuidEqual(&id,&identity.SyntaxGUID,&status)&&!status,"UUID format");RpcStringFreeW(&encoded);
#ifndef COMPONENT_REFERENCE
Verify(RpcStringBindingComposeW(nullptr,reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncacn_ip_tcp")),nullptr,reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"135")),nullptr,&encoded)!=RPC_S_OK,"remote protocol rejected");
#endif
Verify(RpcStringBindingComposeW(nullptr,reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"ncalrpc")),nullptr,reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(L"Guardian.Component.148")),nullptr,&encoded)==0,"binding string");RPC_BINDING_HANDLE binding=nullptr;Verify(RpcBindingFromStringBindingW(encoded,&binding)==0,"binding handle");RpcStringFreeW(&encoded);
#ifndef COMPONENT_REFERENCE
STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};wchar_t command[]=L"\"C:\\App\\guardian-component-rpc.exe\" --server";Verify(CreateProcessW(L"C:\\App\\guardian-component-rpc.exe",command,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child),"server launch");CloseHandle(child.hThread);LONG answer=0;const auto start=GetTickCount64();RPC_STATUS result=RPC_S_SERVER_UNAVAILABLE;do{result=Call(binding,0,17,25,&answer);if(result==RPC_S_SERVER_UNAVAILABLE)Sleep(1);}while(result==RPC_S_SERVER_UNAVAILABLE&&GetTickCount64()-start<3000);Verify(result==0&&answer==42,"cross process calculation");Verify(Call(binding,99,1,2,nullptr)==RPC_S_PROCNUM_OUT_OF_RANGE,"invalid procedure");Verify(Call(binding,1,0,0,nullptr)==0,"shutdown call");Verify(WaitForSingleObject(child.hProcess,3000)==WAIT_OBJECT_0,"server joined");DWORD exit=99;Verify(GetExitCodeProcess(child.hProcess,&exit)&&!exit,"server exit");CloseHandle(child.hProcess);Verify(Call(binding,0,1,2,&answer)==RPC_S_SERVER_UNAVAILABLE,"stale endpoint rejected");
#else
(void)argc;(void)argv;
#endif
Verify(RpcBindingFree(&binding)==0&&!binding,"binding cleanup");std::printf("Component local RPC PASS\n");return 0;}
