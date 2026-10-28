#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <cstdio>
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
template<class T>void Free(T*& p){if(p)p->Release();p=nullptr;}
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
DWORD WINAPI WrongThread(void* p){auto* factory=static_cast<ID2D1Factory*>(p);ID2D1Factory* out=nullptr;const auto hr=factory->QueryInterface(__uuidof(ID2D1Factory),reinterpret_cast<void**>(&out));return hr==RPC_E_WRONG_THREAD&&!out?0:1;}
}
int main(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    ID2D1Factory* factory=nullptr;Check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory)),"D2D factory");
    ID2D1Factory* other=nullptr;Check(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,&other))&&!other,"Multithread factory explicitly unavailable");
    if(factory){
        auto thread=CreateThread(nullptr,0,WrongThread,factory,0,nullptr);Check(thread!=nullptr,"Wrong thread test created");if(thread){Check(WaitForSingleObject(thread,INFINITE)==WAIT_OBJECT_0,"Wrong thread test waited");DWORD result=9;GetExitCodeThread(thread,&result);Check(result==0,"COM affinity error");CloseHandle(thread);}
        ID2D1DCRenderTarget* target=nullptr;auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE(3));
        Check(FAILED(factory->CreateDCRenderTarget(&props,&target))&&!target,"Invalid target enum");
        props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE);Check(FAILED(factory->CreateDCRenderTarget(&props,&target))&&!target,"Hardware target refused");
        props=D2D1::RenderTargetProperties();props.dpiX=144;Check(FAILED(factory->CreateDCRenderTarget(&props,&target))&&!target,"Unsupported DPI refused");
        props=D2D1::RenderTargetProperties();props.minLevel=D2D1_FEATURE_LEVEL_10;Check(FAILED(factory->CreateDCRenderTarget(&props,&target))&&!target,"Unsupported feature level refused");
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Aegis.SoftwareGraphics.Failures";cls.lpfnWndProc=Proc;Check(RegisterClassW(&cls)!=0,"Register negative window");
        const auto window=CreateWindowW(cls.lpszClassName,L"14.10F controlled failures",WS_OVERLAPPEDWINDOW,120,120,160,140,nullptr,nullptr,cls.hInstance,nullptr);Check(window!=nullptr,"Owned negative window");
        if(window){
            ID2D1HwndRenderTarget* hwndTarget=nullptr;RECT rect{};GetClientRect(window,&rect);auto hwnd=D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(UINT32(rect.right),UINT32(rect.bottom)));
            props=D2D1::RenderTargetProperties();Check(SUCCEEDED(factory->CreateHwndRenderTarget(&props,&hwnd,&hwndTarget)),"Owned D2D target");
            if(hwndTarget){
                props.type=D2D1_RENDER_TARGET_TYPE(3);Check(!hwndTarget->IsSupported(&props),"IsSupported agrees with creation");
                ID2D1SolidColorBrush* brushes[64]{};unsigned count=0;for(;count<64;++count){const auto hr=hwndTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue),&brushes[count]);if(FAILED(hr)){Check(hr==E_OUTOFMEMORY&&count==63,"64 native resources enforced");break;}}
                Check(count==63,"Resource quota reached");for(auto*& brush:brushes)Free(brush);
                hwndTarget->BeginDraw();hwndTarget->DrawGeometry(nullptr,nullptr);Check(FAILED(hwndTarget->EndDraw()),"Unsupported void drawing fails EndDraw");
                DestroyWindow(window);Free(hwndTarget);
            }else DestroyWindow(window);
        }
        Check(UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE,"Negative class released");
    }
    Free(factory);
    IWICImagingFactory* wic=nullptr;Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"Guest WIC activation");
    if(wic){
        IWICBitmapScaler* scaler=nullptr;Check(wic->CreateBitmapScaler(&scaler)==E_NOTIMPL&&!scaler,"WIC scaler explicitly pending");
        IWICStream* stream=nullptr;Check(SUCCEEDED(wic->CreateStream(&stream)),"WIC owned stream");if(stream){
            BYTE malformed[8]{};Check(SUCCEEDED(stream->InitializeFromMemory(malformed,sizeof(malformed))),"Malformed bytes copied");IWICBitmapDecoder* decoder=nullptr;
            Check(FAILED(wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder))&&!decoder,"Malformed image refused");Free(stream);
        }Free(wic);
    }
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL chosen{};const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_9_1;
    Check(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,&chosen,&context))&&!device&&!context,"Hardware device refused");
    Check(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,&level,1,D3D11_SDK_VERSION,&device,&chosen,&context))&&!device&&!context,"Debug layer refused");
    const D3D_FEATURE_LEVEL higher=D3D_FEATURE_LEVEL_11_0;
    Check(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&higher,1,D3D11_SDK_VERSION,&device,&chosen,&context))&&!device&&!context,"Higher feature level refused");
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,&chosen,&context)),"WARP remains usable");
    if(device){
        ID3D11RenderTargetView* view=nullptr;Check(device->CreateRenderTargetView(reinterpret_cast<ID3D11Resource*>(0x12345678),nullptr,&view)==E_INVALIDARG&&!view,"Foreign resource address is never dereferenced");
        ID3D11VertexShader* shader=nullptr;BYTE code[4]{};Check(device->CreateVertexShader(code,sizeof(code),nullptr,&shader)==E_NOTIMPL&&!shader,"Shader method explicitly pending");
        Check(SUCCEEDED(device->GetDeviceRemovedReason()),"HRESULT refusal does not poison healthy device");
        if(context){context->Draw(3,0);Check(FAILED(device->GetDeviceRemovedReason()),"Unsupported void pipeline exposes deferred failure");}
    }
    Free(context);Free(device);
    IDXGIFactory2* factory2=nullptr;Check(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory2)))&&!factory2,"Modern factory interface explicitly pending");
    Check(Direct3DCreate9(D3D_SDK_VERSION)==nullptr,"D3D9 backend unavailable");
    IDirect3D9Ex* nine=nullptr;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&nine)==D3DERR_NOTAVAILABLE&&!nine,"D3D9Ex real failure code");
    CoFreeUnusedLibraries();CoUninitialize();std::printf("SOFTWARE_FAILURES checks=%u failures=%u\n",checks,failures);return failures?93:0;
}
