#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <array>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("WIC encoder failed: "+std::to_string(uint32_t(result)));}
}
int main(int argc,char** argv){try{
    if(argc!=2)return 2;const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
    {
        ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        std::array<BYTE,8*8*4> pixels{};for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto i=(y*8+x)*4;pixels[i]=BYTE(x*31);pixels[i+1]=BYTE(y*31);pixels[i+2]=BYTE((x+y)*15);pixels[i+3]=BYTE((x+y)%3?255:128);}
        ComPtr<IWICBitmap> source;Check(factory->CreateBitmapFromMemory(8,8,GUID_WICPixelFormat32bppBGRA,32,UINT(pixels.size()),pixels.data(),&source));
        for(unsigned kind=0;kind<3;++kind){
            const auto file=output/(kind==0?"image.png":kind==1?"image.bmp":"image.jpg");
            ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream));Check(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE));
            ComPtr<IWICBitmapEncoder> encoder;const auto container=kind==0?GUID_ContainerFormatPng:kind==1?GUID_ContainerFormatBmp:GUID_ContainerFormatJpeg;
            Check(factory->CreateEncoder(container,&GUID_VendorMicrosoft,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
            ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr));Check(frame->Initialize(nullptr));Check(frame->SetSize(8,8));Check(frame->SetResolution(96,96));
            auto format=kind==0?GUID_WICPixelFormat32bppBGRA:GUID_WICPixelFormat24bppBGR;Check(frame->SetPixelFormat(&format));
            ComPtr<IWICFormatConverter> converter;Check(factory->CreateFormatConverter(&converter));Check(converter->Initialize(source.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
            Check(frame->WriteSource(converter.Get(),nullptr));Check(frame->Commit());Check(encoder->Commit());
            std::cout<<file.filename().string()<<' '<<std::filesystem::file_size(file)<<'\n';
        }
    }
    CoUninitialize();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
