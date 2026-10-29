#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.foundation.h>
#include "guardian/FrameworkWinrt.h"
namespace wf=ABI::Windows::Foundation;
extern "C" int _fltused=0;
extern "C" LONG NTAPI RtlGetVersion(OSVERSIONINFOW*);
extern "C" const int FrameworkWinrtClientAnchor=1411;
const void* volatile FrameworkWinrtClientRelocation=&FrameworkWinrtClientAnchor;
namespace {
void Print(const char* text){DWORD n=0,done=0;while(text[n])++n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,n,&done,nullptr);}
void Check(bool ok,const char* step){if(!ok){Print("WINRT_FAIL: ");Print(step);Print("\n");ExitProcess(1);}}
void Hr(HRESULT hr,const char* step){Check(SUCCEEDED(hr),step);}
void ClassName(IInspectable* value,const char* label){HSTRING name=nullptr;const auto hr=value->GetRuntimeClassName(&name);if(SUCCEEDED(hr)){UINT32 n=0;const auto* raw=WindowsGetStringRawBuffer(name,&n);char text[160]{};Check(n<159,"runtime class name quota");for(UINT32 i=0;i<n;++i){Check(raw[i]<128,"runtime class name text");text[i]=char(raw[i]);}Print(label);Print(text);Print("\n");WindowsDeleteString(name);}}
void Inspect(IInspectable* object,wf::IPropertyValue** value){Hr(object->QueryInterface(__uuidof(wf::IPropertyValue),reinterpret_cast<void**>(value)),"QI property");object->Release();}
DWORD WINAPI ThreadProbe(void*){
    if(FAILED(RoInitialize(RO_INIT_SINGLETHREADED)))return 1;
    HSTRING name=nullptr;IActivationFactory* f=nullptr;auto hr=WindowsCreateString(L"Windows.Foundation.PropertyValue",32,&name);
    if(SUCCEEDED(hr))hr=RoGetActivationFactory(name,__uuidof(IActivationFactory),reinterpret_cast<void**>(&f));
    if(f)f->Release();WindowsDeleteString(name);RoUninitialize();return SUCCEEDED(hr)?0:2;
}
}
extern "C" void FrameworkWinrtClient(){
    OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);Check(RtlGetVersion(&version)==0,"NT profile");
    if(version.dwMajorVersion==6&&version.dwMinorVersion<2){Check(RoInitialize(RO_INIT_MULTITHREADED)==E_NOTIMPL,"WinRT NT minimum");Print("WINRT_NT61_UNSUPPORTED_PASS\n");ExitProcess(0);}
    Hr(RoInitialize(RO_INIT_MULTITHREADED),"RoInitialize");Check(RoInitialize(RO_INIT_MULTITHREADED)==S_FALSE,"balanced initialize");
    Check(RoInitialize(RO_INIT_SINGLETHREADED)==RPC_E_CHANGED_MODE,"apartment conflict");RoUninitialize();
    HSTRING empty=nullptr;Hr(WindowsCreateString(nullptr,0,&empty),"empty string");Check(!empty&&WindowsIsStringEmpty(empty),"empty handle");
    HSTRING text=nullptr,duplicate=nullptr;const wchar_t embedded[]={L'A',0,L'B',0};
    Hr(WindowsCreateString(embedded,3,&text),"embedded string");Hr(WindowsDuplicateString(text,&duplicate),"duplicate");Hr(WindowsDeleteString(text),"delete original");
    BOOL hasNull=FALSE;Hr(WindowsStringHasEmbeddedNull(duplicate,&hasNull),"embedded check");UINT32 length=0;const auto* raw=WindowsGetStringRawBuffer(duplicate,&length);
    Check(hasNull&&length==3&&raw[0]==L'A'&&raw[1]==0&&raw[2]==L'B'&&raw[3]==0,"UTF16 lifetime");WindowsDeleteString(duplicate);
    wchar_t buffer[]=L"ref";HSTRING_HEADER header{};HSTRING reference=nullptr,copy=nullptr;
    Hr(WindowsCreateStringReference(buffer,3,&header,&reference),"string reference");Hr(WindowsDuplicateString(reference,&copy),"reference copy");
    Check(WindowsGetStringRawBuffer(reference,nullptr)==buffer&&WindowsGetStringRawBuffer(copy,nullptr)!=buffer&&WindowsGetStringRawBuffer(copy,nullptr)[0]==L'r',"reference ownership");WindowsDeleteString(reference);Check(WindowsGetStringLen(reference)==3,"reference delete is a no-op");WindowsDeleteString(copy);
    HSTRING name=nullptr;HSTRING_HEADER nameHeader{};Hr(WindowsCreateStringReference(L"Windows.Foundation.PropertyValue",32,&nameHeader,&name),"class name reference");wf::IPropertyValueStatics* factory=nullptr;
    Hr(RoGetActivationFactory(name,__uuidof(wf::IPropertyValueStatics),reinterpret_cast<void**>(&factory)),"activation factory");
    IInspectable* object=nullptr;wf::IPropertyValue* property=nullptr;Hr(factory->CreateInt32(-12,&object),"CreateInt32");Inspect(object,&property);
    ClassName(property,"PROPERTY_I4_CLASS: ");
    guardian::frameworks::ReferenceInt32* referenceValue=nullptr;Hr(property->QueryInterface(__uuidof(guardian::frameworks::ReferenceInt32),reinterpret_cast<void**>(&referenceValue)),"IReference Int32");INT32 referenced=0;Hr(referenceValue->get_Value(&referenced),"reference value");Check(referenced==-12,"referenced Int32");referenceValue->Release();
    wf::PropertyType type{};Hr(property->get_Type(&type),"property type");Check(type==wf::PropertyType_Int32,"type is Int32");INT32 number=0;Hr(property->GetInt32(&number),"GetInt32");Check(number==-12,"Int32 value");
    DOUBLE real=0;Hr(property->GetDouble(&real),"numeric coercion");Check(real==-12.0,"double value");UINT32 unsignedValue=0;Check(FAILED(property->GetUInt32(&unsignedValue)),"overflow rejected");
    HSTRING wrong=nullptr;Check(FAILED(property->GetString(&wrong))&&!wrong,"type mismatch");property->Release();
    Hr(factory->CreateBoolean(TRUE,&object),"CreateBoolean");Inspect(object,&property);ClassName(property,"PROPERTY_BOOLEAN_CLASS: ");boolean flag=0;Hr(property->GetBoolean(&flag),"GetBoolean");Check(flag,"boolean value");guardian::frameworks::ReferenceBoolean* boolReference=nullptr;Hr(property->QueryInterface(__uuidof(guardian::frameworks::ReferenceBoolean),reinterpret_cast<void**>(&boolReference)),"IReference Boolean");flag=0;Hr(boolReference->get_Value(&flag),"boolean reference value");Check(flag,"referenced Boolean");boolReference->Release();property->Release();
    Hr(WindowsCreateString(L"ação",4,&text),"Unicode string");Hr(factory->CreateString(text,&object),"CreateString");WindowsDeleteString(text);Inspect(object,&property);
    ClassName(property,"PROPERTY_STRING_CLASS: ");Hr(property->GetString(&text),"GetString");raw=WindowsGetStringRawBuffer(text,&length);Check(length==4&&raw[1]==L'ç'&&raw[2]==L'ã',"Unicode boxing");WindowsDeleteString(text);guardian::frameworks::ReferenceString* stringReference=nullptr;Hr(property->QueryInterface(__uuidof(guardian::frameworks::ReferenceString),reinterpret_cast<void**>(&stringReference)),"IReference String");Hr(stringReference->get_Value(&text),"string reference value");Check(WindowsGetStringLen(text)==4,"referenced String");WindowsDeleteString(text);stringReference->Release();property->Release();
    Hr(factory->CreateEmpty(&object),"CreateEmpty");if(object){Inspect(object,&property);Hr(property->get_Type(&type),"empty type");Check(type==wf::PropertyType_Empty,"empty value");property->Release();Print("PROPERTY_EMPTY_OBJECT\n");}else Print("PROPERTY_EMPTY_NULL\n");
    ULONG iidCount=0;IID* ids=nullptr;Hr(factory->GetIids(&iidCount,&ids),"GetIids");Check(iidCount&&ids,"interface metadata");CoTaskMemFree(ids);
#ifdef FRAMEWORK_UNSUPPORTED_PROBE
    object=reinterpret_cast<IInspectable*>(1);Check(factory->CreateDouble(1.25,&object)==E_NOTIMPL&&!object,"unsupported scalar clears output");
    object=reinterpret_cast<IInspectable*>(1);INT32 array[2]{1,2};Check(factory->CreateInt32Array(2,array,&object)==E_NOTIMPL&&!object,"unsupported array clears output");
    HSTRING missing=nullptr;Hr(WindowsCreateString(L"Unknown.Class",13,&missing),"unknown class name");void* absent=reinterpret_cast<void*>(1);Check(RoGetActivationFactory(missing,__uuidof(wf::IPropertyValueStatics),&absent)==REGDB_E_CLASSNOTREG&&!absent,"unknown class diagnostic");WindowsDeleteString(missing);
#endif
    factory->Release();WindowsDeleteString(name);
    HANDLE thread=CreateThread(nullptr,0,ThreadProbe,nullptr,0,nullptr);Check(thread!=nullptr,"apartment thread");Check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"thread completion");DWORD exit=1;Check(GetExitCodeThread(thread,&exit)&&!exit,"thread apartment");CloseHandle(thread);
    RoUninitialize();Print("WINRT_SCALARS_PASS\n");ExitProcess(0);
}
