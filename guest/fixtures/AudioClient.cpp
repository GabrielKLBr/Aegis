#include <Windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstdio>
#include <cstring>
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
template<class T>void Free(T*& value){if(value)value->Release();value=nullptr;}
void Wave(bool negative){
    WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=44100;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=176400;
    HWAVEOUT h=nullptr;Check(waveOutGetNumDevs()>0,"output endpoint");
    Check(!waveOutOpen(nullptr,WAVE_MAPPER,&f,0,0,WAVE_FORMAT_QUERY),"native format query");
    auto mm=waveOutOpen(&h,WAVE_MAPPER,&f,0,0,0);Check(!mm&&h,"waveOutOpen");if(!h)return;
    short data[4410*2]{};for(unsigned i=0;i<4410;++i)data[i*2]=data[i*2+1]=short((i%100<50)?300:-300);
    WAVEHDR header{};header.lpData=reinterpret_cast<LPSTR>(data);header.dwBufferLength=sizeof(data);
    Check(!waveOutPrepareHeader(h,&header,sizeof(header)),"prepare");Check(!waveOutPause(h),"pause");
    Check(!waveOutWrite(h,&header,sizeof(header)),"write");Check(waveOutUnprepareHeader(h,&header,sizeof(header))==WAVERR_STILLPLAYING,"cannot free queued data");
    Check(!waveOutRestart(h),"restart");const auto start=GetTickCount64();while(!(header.dwFlags&WHDR_DONE)&&GetTickCount64()-start<3000)Sleep(5);
    Check((header.dwFlags&WHDR_DONE)&&!(header.dwFlags&WHDR_INQUEUE),"asynchronous completion flags");
    MMTIME time{};time.wType=TIME_BYTES;Check(!waveOutGetPosition(h,&time,sizeof(time))&&time.u.cb>=sizeof(data),"real device position");
    Check(!waveOutUnprepareHeader(h,&header,sizeof(header)),"unprepare");
    header={};header.lpData=reinterpret_cast<LPSTR>(data);header.dwBufferLength=sizeof(data);Check(!waveOutPrepareHeader(h,&header,sizeof(header)),"prepare after playback");Check(!waveOutWrite(h,&header,sizeof(header)),"write after playback");Check(!waveOutReset(h),"reset");Check(header.dwFlags&WHDR_DONE,"reset completes pending buffers");Check(!waveOutUnprepareHeader(h,&header,sizeof(header)),"unprepare reset");Check(!waveOutClose(h),"close");
    if(negative){f.wBitsPerSample=32;Check(waveOutOpen(&h,WAVE_MAPPER,&f,0,0,0)==WAVERR_BADFORMAT,"unsupported format");f.wBitsPerSample=16;Check(waveOutOpen(&h,WAVE_MAPPER,&f,1,0,CALLBACK_FUNCTION)==MMSYSERR_NOTSUPPORTED,"host callbacks denied");Check(!PlaySoundW(L"C:\\Windows\\Media\\Windows Notify.wav",nullptr,SND_FILENAME|SND_ASYNC),"unsupported PlaySound mode denied");}
}
void Wasapi(bool negative){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialize");IMMDeviceEnumerator* e=nullptr;
    auto hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e));Check(SUCCEEDED(hr)&&e,"MMDeviceEnumerator activation");if(!e){CoUninitialize();return;}
    IMMDeviceCollection* collection=nullptr;Check(SUCCEEDED(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&collection)),"render enumeration");if(collection){UINT n=0;Check(SUCCEEDED(collection->GetCount(&n))&&n,"active render device count");Free(collection);}
    IMMDevice* d=nullptr;Check(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eMultimedia,&d))&&d,"default render endpoint");
    if(negative){IMMDevice* capture=nullptr;Check(e->GetDefaultAudioEndpoint(eCapture,eMultimedia,&capture)==E_ACCESSDENIED&&!capture,"microphone denied");}
    if(d){LPWSTR id=nullptr;Check(SUCCEEDED(d->GetId(&id))&&id,"device ID");CoTaskMemFree(id);IAudioClient* client=nullptr;Check(SUCCEEDED(d->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(&client)))&&client,"audio client activation");
        if(client){WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=2;f.nSamplesPerSec=48000;f.wBitsPerSample=16;f.nBlockAlign=4;f.nAvgBytesPerSec=192000;
            hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,1000000,0,&f,nullptr);Check(SUCCEEDED(hr),"shared PCM initialize");
            if(SUCCEEDED(hr)){UINT32 capacity=0,padding=0;Check(SUCCEEDED(client->GetBufferSize(&capacity))&&capacity>=480,"device buffer size");IAudioRenderClient* render=nullptr;Check(SUCCEEDED(client->GetService(IID_PPV_ARGS(&render))),"render service");
                if(render){BYTE* bytes=nullptr;Check(SUCCEEDED(render->GetBuffer(480,&bytes))&&bytes,"render buffer");if(bytes){std::memset(bytes,0,480*4);Check(SUCCEEDED(render->ReleaseBuffer(480,0)),"release PCM buffer");}
                    if(negative){BYTE* denied=nullptr;Check(render->GetBuffer(capacity+1,&denied)==AUDCLNT_E_BUFFER_TOO_LARGE&&!denied,"WASAPI capacity");Check(SUCCEEDED(render->GetBuffer(1,&bytes))&&bytes,"guest lease");Check(render->GetBuffer(1,&denied)==AUDCLNT_E_OUT_OF_ORDER&&!denied,"nested lease denied");Check(render->ReleaseBuffer(2,0)==E_INVALIDARG,"oversized release denied");Check(SUCCEEDED(render->ReleaseBuffer(0,0)),"cancel guest lease");IAudioCaptureClient* capture=nullptr;Check(client->GetService(IID_PPV_ARGS(&capture))==E_NOINTERFACE&&!capture,"capture service absent");}
                    Check(SUCCEEDED(client->Start()),"start WASAPI");Sleep(80);Check(SUCCEEDED(client->GetCurrentPadding(&padding))&&padding<capacity,"device consumes frames");
                    IAudioClock* clock=nullptr;Check(SUCCEEDED(client->GetService(IID_PPV_ARGS(&clock))),"clock service");if(clock){UINT64 freq=0,position=0,qpc=0;Check(SUCCEEDED(clock->GetFrequency(&freq))&&freq,"clock frequency");Check(SUCCEEDED(clock->GetPosition(&position,&qpc))&&position&&qpc,"device clock advances");Free(clock);}
                    Check(SUCCEEDED(client->Stop()),"stop WASAPI");Check(SUCCEEDED(client->Reset()),"reset WASAPI");Free(render);}
            }Free(client);}
        Free(d);}
    Free(e);CoUninitialize();
}
void Wav(){
    BYTE wav[44+1920]{};const auto put=[&](unsigned offset,DWORD value,unsigned bytes){for(unsigned i=0;i<bytes;++i)wav[offset+i]=BYTE(value>>(8*i));};
    put(0,0x46464952,4);put(4,sizeof(wav)-8,4);put(8,0x45564157,4);put(12,0x20746D66,4);put(16,16,4);put(20,1,2);put(22,2,2);put(24,48000,4);put(28,192000,4);put(32,4,2);put(34,16,2);put(36,0x61746164,4);put(40,1920,4);
    Check(PlaySoundW(reinterpret_cast<LPCWSTR>(wav),nullptr,SND_MEMORY|SND_SYNC|SND_NODEFAULT)!=FALSE,"PCM WAV memory playback");
    Check(PlaySoundW(L"tone.wav",nullptr,SND_FILENAME|SND_SYNC|SND_NODEFAULT)!=FALSE,"guest WAV file playback");
}
}
int main(int argc,char** argv){bool negative=argc==2&&!std::strcmp(argv[1],"--negative");
#ifdef AUDIO_NEGATIVE
negative=true;
#endif
Wave(negative);Wasapi(negative);Wav();std::printf("AUDIO_CLIENT checks=%u failures=%u\n",checks,failures);return failures?1:0;}
