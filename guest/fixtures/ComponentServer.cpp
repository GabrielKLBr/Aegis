#include "ComponentContract.h"
#include <cstdio>
#ifdef COMPONENT_SERVER_EXE
int main(){if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;componentStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);auto* factory=ComponentMakeFactory();if(!componentStop||!factory)return 2;DWORD cookie=0;const auto registration=CoRegisterClassObject(ComponentLocal,factory,CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie);factory->Release();if(FAILED(registration))return 3;std::printf("COM local server pid=%lu tid=%lu registered\n",GetCurrentProcessId(),GetCurrentThreadId());DWORD index=99;const auto wait=CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS,10000,1,&componentStop,&index);const auto revoked=CoRevokeClassObject(cookie);CloseHandle(componentStop);componentStop=nullptr;CoUninitialize();std::printf("COM local server shutdown objects=%ld\n",componentObjects);return wait==S_OK&&SUCCEEDED(revoked)&&!componentObjects?0:4;}
#else
extern "C" __declspec(dllexport) HRESULT WINAPI ComponentGetClassObject(REFCLSID cls,REFIID iid,void** out){if(!out)return E_POINTER;*out=nullptr;if(!IsEqualGUID(cls,ComponentInproc))return CLASS_E_CLASSNOTAVAILABLE;auto* factory=ComponentMakeFactory();if(!factory)return E_OUTOFMEMORY;const auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;}
extern "C" __declspec(dllexport) HRESULT WINAPI ComponentCanUnloadNow(){return componentObjects||componentLocks?S_FALSE:S_OK;}
BOOL WINAPI DllMain(HINSTANCE,DWORD,void*){return TRUE;}
#endif
