#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
namespace {
unsigned checks=0,paints=0;bool failed=false,automatic=false;
ID2D1Factory* d2d=nullptr;IDWriteFactory* write=nullptr;IWICImagingFactory* wic=nullptr;
ID2D1DCRenderTarget* target=nullptr;ID2D1SolidColorBrush* brush=nullptr;IDWriteTextFormat* format=nullptr;ID2D1Bitmap* bitmap=nullptr;
IWICFormatConverter* images[3]{};
void Check(bool ok,const char* what){++checks;if(!ok){failed=true;std::printf("SOFTWARE FAIL %s error=%lu\n",what,GetLastError());}}
void Hr(HRESULT hr,const char* what){if(FAILED(hr))std::printf("HRESULT %s %08lx\n",what,ULONG(hr));Check(SUCCEEDED(hr),what);}
template<class T>void Release(T*& value){if(value){value->Release();value=nullptr;}}
uint64_t Hash(const BYTE* bytes,size_t size){uint64_t result=14695981039346656037ull;for(size_t i=0;i<size;++i){result^=bytes[i];result*=1099511628211ull;}return result;}
void Images(){
    Hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC factory");
    if(!wic)return;
    for(unsigned i=0;i<3;++i){
        IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;
        const auto name=i==0?L"image.png":i==1?L"image.bmp":L"image.jpg";
        Hr(wic->CreateDecoderFromFilename(name,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"WIC file decoder");
        if(!decoder)continue;UINT count=0;Hr(decoder->GetFrameCount(&count),"WIC frame count");Check(count==1,"Single WIC frame");
        Hr(decoder->GetFrame(0,&frame),"WIC frame");if(!frame){Release(decoder);continue;}
        UINT width=0,height=0;Hr(frame->GetSize(&width,&height),"WIC dimensions");Check(width==8&&height==8,"Expected fixture dimensions");
        Hr(wic->CreateFormatConverter(&images[i]),"WIC converter");if(images[i]){
            Hr(images[i]->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"WIC conversion");
            Release(frame);Release(decoder);BYTE data[8*8*4]{};Hr(images[i]->CopyPixels(nullptr,32,sizeof(data),data),"WIC copy after decoder release");
            BYTE crop[3*4*3]{};const WICRect rect{2,1,3,3};Hr(images[i]->CopyPixels(&rect,12,sizeof(crop),crop),"WIC cropped pixels");bool same=true;for(unsigned y=0;y<3;++y)if(std::memcmp(crop+y*12,data+((y+1)*8+2)*4,12))same=false;Check(same,"WIC rectangle stride semantics");
            std::printf("SOFTWARE_WIC %u %llu\n",i,static_cast<unsigned long long>(Hash(data,sizeof(data))));
        }
        Release(frame);Release(decoder);
    }
}
void Direct3D(HWND window){
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;IDXGISwapChain* swap=nullptr;ID3D11Texture2D* buffer=nullptr;ID3D11RenderTargetView* view=nullptr;ID3D11Texture2D* staging=nullptr;ID3D11Query* query=nullptr;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_9_1};D3D_FEATURE_LEVEL level{};
    RECT client{};GetClientRect(window,&client);DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=UINT(client.right);desc.BufferDesc.Height=UINT(client.bottom);desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferCount=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    Hr(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&desc,&swap,&device,&level,&context),"D3D WARP device and swap chain");
    if(!device||!context||!swap){Release(context);Release(swap);Release(device);return;}
    Check(level==D3D_FEATURE_LEVEL_9_1&&device->GetFeatureLevel()==level,"D3D feature negotiation");
    Hr(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"DXGI back buffer");if(!buffer){Release(context);Release(swap);Release(device);return;}
    Hr(device->CreateRenderTargetView(buffer,nullptr,&view),"D3D render target view");
    if(view){
        const float color[]{.25f,.5f,.75f,1};context->OMSetRenderTargets(1,&view,nullptr);D3D11_VIEWPORT viewport{0,0,FLOAT(client.right),FLOAT(client.bottom),0,1};context->RSSetViewports(1,&viewport);context->ClearRenderTargetView(view,color);Hr(swap->Present(0,0),"DXGI software present");
        D3D11_TEXTURE2D_DESC texture{};buffer->GetDesc(&texture);texture.Usage=D3D11_USAGE_STAGING;texture.BindFlags=0;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Hr(device->CreateTexture2D(&texture,nullptr,&staging),"D3D staging texture");if(staging){
            context->CopyResource(staging,buffer);D3D11_MAPPED_SUBRESOURCE mapping{};Hr(context->Map(staging,0,D3D11_MAP_READ,0,&mapping),"D3D synchronized read");
            if(mapping.pData){uint64_t hash=14695981039346656037ull;for(unsigned y=0;y<texture.Height;++y){const auto* row=static_cast<const BYTE*>(mapping.pData)+size_t(y)*mapping.RowPitch;for(size_t i=0;i<size_t(texture.Width)*4;++i){hash^=row[i];hash*=1099511628211ull;}}context->Unmap(staging,0);std::printf("SOFTWARE_D3D %u %u %llu\n",texture.Width,texture.Height,static_cast<unsigned long long>(hash));}
        }
        const D3D11_QUERY_DESC event{D3D11_QUERY_EVENT,0};Hr(device->CreateQuery(&event,&query),"D3D event query");if(query){context->End(query);context->Flush();BOOL ready=FALSE;HRESULT state=S_FALSE;const auto start=GetTickCount64();while(state==S_FALSE&&GetTickCount64()-start<3000){state=context->GetData(query,&ready,sizeof(ready),0);if(state==S_FALSE)Sleep(1);}Check(state==S_OK&&ready,"D3D event synchronization");}
        IUnknown* identity=nullptr;IDXGIDevice* dxgi=nullptr;IUnknown* other=nullptr;Hr(device->QueryInterface(IID_PPV_ARGS(&identity)),"D3D identity");Hr(device->QueryInterface(IID_PPV_ARGS(&dxgi)),"DXGI device interface");if(dxgi)Hr(dxgi->QueryInterface(IID_PPV_ARGS(&other)),"DXGI identity");Check(identity&&identity==other,"IUnknown identity across DXGI");Release(other);Release(dxgi);Release(identity);
        Check(FAILED(swap->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0)),"DXGI refuses live buffer references");context->ClearState();
    }
    Release(query);Release(staging);Release(view);Release(buffer);Hr(swap->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0),"DXGI resize after release");Release(swap);
    Hr(device->GetDeviceRemovedReason(),"D3D device healthy");Release(context);Release(device);
}
void Draw(HWND window,HDC dc){
    RECT rect{};GetClientRect(window,&rect);if(rect.right<1||rect.bottom<1)return;
    if(!target){const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);Hr(d2d->CreateDCRenderTarget(&props,&target),"D2D software DC target");}
    if(!target)return;Hr(target->BindDC(dc,&rect),"D2D bind owned paint DC");
    if(!brush)Hr(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DarkBlue),&brush),"D2D brush");
    if(!bitmap&&images[0])Hr(target->CreateBitmapFromWicBitmap(images[0],nullptr,&bitmap),"D2D WIC bitmap");
    target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());target->Clear(D2D1::ColorF(D2D1::ColorF::White));
    if(brush){
        target->FillRectangle(D2D1::RectF(8,8,48,38),brush);target->DrawRectangle(D2D1::RectF(55,8,110,40),brush,2);
        target->DrawLine(D2D1::Point2F(8,48),D2D1::Point2F(120,58),brush,2);target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(150,30),18,14),brush);
        const wchar_t text[]=L"MiniNT Direct2D / DirectWrite: ação";
        if(format)target->DrawText(text,UINT32(sizeof(text)/2-1),format,D2D1::RectF(8,72,FLOAT(rect.right-8),140),brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    target->PushAxisAlignedClip(D2D1::RectF(190,8,260,65),D2D1_ANTIALIAS_MODE_ALIASED);if(bitmap)target->DrawBitmap(bitmap,D2D1::RectF(180,0,270,72),.75f,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);target->PopAxisAlignedClip();
    Hr(target->EndDraw(),"D2D EndDraw");++paints;SetWindowTextW(window,failed?L"14.10F Software Graphics FAIL":L"14.10F Software Graphics ready");
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);if(d2d)Draw(window,dc);EndPaint(window,&paint);return 0;}
    if(message==WM_PRINTCLIENT){if(d2d)Draw(window,reinterpret_cast<HDC>(wp));return 0;}
    if(message==WM_SIZE){InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_CHAR){InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,wp,lp);
}
}
int main(int argc,char**){
    automatic=argc>1;Hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"COM initialize");Images();
    Hr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&d2d),"D2D factory");Hr(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&write)),"DirectWrite factory");
    if(write){Hr(write->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,20,L"pt-BR",&format),"DirectWrite text format");if(format)Hr(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP),"DirectWrite wrapping");}
    WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.lpszClassName=L"Aegis.SoftwareGraphics.Scene";cls.hInstance=GetModuleHandleW(nullptr);cls.style=CS_HREDRAW|CS_VREDRAW;cls.cbWndExtra=16;
    Check(RegisterClassW(&cls)!=0,"Register window class");const auto window=CreateWindowExW(0,cls.lpszClassName,L"14.10F software graphics",WS_OVERLAPPEDWINDOW,120,100,360,280,nullptr,nullptr,cls.hInstance,nullptr);
    Check(window!=nullptr,"Create scene window");if(window){
        Check(GetWindowLongPtrW(window,0)==0&&GetWindowLongPtrW(window,8)==0,"Window extra bytes start at zero");
        Check(SetWindowLongPtrW(window,0,0x12345678)==0&&SetWindowLongPtrW(window,8,0x23456789)==0,"Store guest window extra bytes");
        Check(GetWindowLongPtrW(window,0)==0x12345678&&GetWindowLongPtrW(window,8)==0x23456789,"Independent window extra slots");
        SetLastError(0);Check(SetWindowLongPtrW(window,16,9)==0&&GetLastError()==ERROR_INVALID_INDEX,"Window extra bounds");
        Check(!UnregisterClassW(cls.lpszClassName,cls.hInstance),"Active window class cannot be unregistered");
        Direct3D(window);ShowWindow(window,SW_SHOW);UpdateWindow(window);if(automatic)PostMessageW(window,WM_CLOSE,0,0);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
    Release(bitmap);Release(brush);Release(target);Release(format);Release(write);Release(d2d);for(auto& image:images)Release(image);Release(wic);CoFreeUnusedLibraries();CoUninitialize();if(window)Check(UnregisterClassW(cls.lpszClassName,cls.hInstance)!=FALSE,"Unregister scene class");
    std::printf("SOFTWARE_SCENE checks=%u paints=%u %s\n",checks,paints,failed?"FAIL":"PASS");return failed?91:0;
}
