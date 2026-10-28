#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <unknwn.h>
extern "C" const int GuardianWicFacadeAnchor=1410;
const void* volatile GuardianWicFacadeRelocation=&GuardianWicFacadeAnchor;
extern "C" __declspec(dllimport) HRESULT WINAPI GuardianGraphicsGetClassObject(REFCLSID,REFIID,void**);
extern "C" __declspec(dllimport) HRESULT WINAPI GuardianGraphicsCanUnloadNow();
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID cls,REFIID iid,void** out){return GuardianGraphicsGetClassObject(cls,iid,out);}
extern "C" HRESULT WINAPI DllCanUnloadNow(){return GuardianGraphicsCanUnloadNow();}
