#include <Windows.h>
#include <mmsystem.h>
#include <intrin.h>
#include "guardian/Audio.h"
namespace a=guardian::audio;namespace g=guardian::nativegdi;
extern "C" __declspec(dllimport) DWORD GuardianNativeGdiCall(g::Call*) noexcept;
extern "C" const int GuardianAudioAnchor=147;
const void* volatile GuardianAudioRelocation=&GuardianAudioAnchor;
namespace {
void Copy(void* to,const void* from,size_t bytes){auto* d=static_cast<BYTE*>(to);const auto* s=static_cast<const BYTE*>(from);for(size_t i=0;i<bytes;++i)d[i]=s[i];}
volatile LONG gate=0;struct Guard {Guard(){while(_InterlockedCompareExchange(&gate,1,0))_mm_pause();}~Guard(){_InterlockedExchange(&gate,0);}};
g::Call C(a::Op op,uint64_t id=0){g::Call c;c.draw.op=static_cast<g::Op>(op);c.draw.dc=id;return c;}
MMRESULT Send(g::Call& c){const auto error=GuardianNativeGdiCall(&c);if(error||c.graphics.status)return MMSYSERR_ERROR;if(FAILED(HRESULT(c.graphics.x)))return c.graphics.y?UINT(c.graphics.y):MMSYSERR_ERROR;return MMSYSERR_NOERROR;}
struct Header {WAVEHDR* guest=nullptr;uint64_t buffer=0;DWORD length=0;};
struct Stream {uint64_t handle=0,id=0;DWORD align=0,rate=0;Header headers[8]{};};
Stream streams[a::MaxStreams]{};uint64_t nextHandle=0xA7000000;HANDLE worker=nullptr;
Stream* Find(HWAVEOUT h){for(auto& s:streams)if(s.handle&&s.handle==uint64_t(h))return &s;return nullptr;}
Header* Find(Stream& s,WAVEHDR* header){for(auto& h:s.headers)if(h.guest==header)return &h;return nullptr;}
bool Poll(Header& h){if(!h.buffer)return true;auto c=C(a::Op::Poll,h.buffer);if(Send(c))return false;if(c.graphics.value){h.guest->dwFlags=(h.guest->dwFlags&~WHDR_INQUEUE)|WHDR_DONE;return true;}return false;}
DWORD WINAPI Pump(void*){for(;;){{Guard lock;for(auto& s:streams)if(s.handle)for(auto& h:s.headers)if(h.guest&&h.buffer)Poll(h);}Sleep(5);}}
bool Supported(const WAVEFORMATEX* f){return f&&f->wFormatTag==WAVE_FORMAT_PCM&&a::Format(f->nSamplesPerSec,f->nChannels,f->wBitsPerSample)&&f->nBlockAlign==f->nChannels*2&&f->nAvgBytesPerSec==f->nSamplesPerSec*f->nBlockAlign&&!f->cbSize;}
MMRESULT Control(HWAVEOUT h,a::Op op){Guard lock;auto* s=Find(h);if(!s)return MMSYSERR_INVALHANDLE;auto c=C(op,s->id);const auto result=Send(c);if(!result&&op==a::Op::Reset)for(auto& header:s->headers)if(header.guest&&header.buffer)Poll(header);return result;}
}
extern "C" BOOL WINAPI AudioEntry(HINSTANCE,DWORD,LPVOID){return TRUE;}
extern "C" MMRESULT WINAPI AudioWaveOutOpen(LPHWAVEOUT out,UINT device,LPCWAVEFORMATEX format,DWORD_PTR callback,DWORD_PTR instance,DWORD flags){
    if(out)*out=nullptr;if(device!=WAVE_MAPPER&&device!=0)return MMSYSERR_BADDEVICEID;if(!Supported(format))return WAVERR_BADFORMAT;
    if(callback||instance||flags&~WAVE_FORMAT_QUERY)return MMSYSERR_NOTSUPPORTED;
    if(flags&WAVE_FORMAT_QUERY){auto c=C(a::Op::Open);c.draw.flags=2;c.draw.args[0]=int32_t(format->nSamplesPerSec);c.draw.args[1]=format->nChannels;c.draw.args[2]=format->wBitsPerSample;c.draw.args[3]=100;return Send(c);}
    if(!out)return MMSYSERR_INVALPARAM;Guard lock;Stream* slot=nullptr;for(auto& s:streams)if(!s.handle){slot=&s;break;}if(!slot)return MMSYSERR_ALLOCATED;
    auto c=C(a::Op::Open);c.draw.args[0]=int32_t(format->nSamplesPerSec);c.draw.args[1]=format->nChannels;c.draw.args[2]=format->wBitsPerSample;c.draw.args[3]=100;
    const auto result=Send(c);if(result)return result;slot->handle=++nextHandle;slot->id=c.graphics.value;slot->align=format->nBlockAlign;slot->rate=format->nSamplesPerSec;
    if(!worker){worker=CreateThread(nullptr,0,Pump,nullptr,0,nullptr);if(!worker){auto close=C(a::Op::Close,slot->id);Send(close);*slot={};return MMSYSERR_NOMEM;}}
    *out=reinterpret_cast<HWAVEOUT>(slot->handle);return MMSYSERR_NOERROR;
}
extern "C" MMRESULT WINAPI AudioWaveOutPrepareHeader(HWAVEOUT handle,LPWAVEHDR h,UINT bytes){Guard lock;auto* s=Find(handle);if(!s)return MMSYSERR_INVALHANDLE;if(!h||bytes!=sizeof(*h)||!h->lpData||!h->dwBufferLength||h->dwBufferLength>a::MaxBuffer||h->dwBufferLength%s->align||h->dwFlags&~(WHDR_DONE|WHDR_PREPARED)||h->dwLoops)return MMSYSERR_INVALPARAM;if(Find(*s,h))return MMSYSERR_NOERROR;for(auto& slot:s->headers)if(!slot.guest){slot.guest=h;slot.length=h->dwBufferLength;h->dwFlags=WHDR_PREPARED;return MMSYSERR_NOERROR;}return MMSYSERR_NOMEM;}
extern "C" MMRESULT WINAPI AudioWaveOutWrite(HWAVEOUT handle,LPWAVEHDR h,UINT bytes){
    Guard lock;auto* s=Find(handle);if(!s)return MMSYSERR_INVALHANDLE;if(!h||bytes!=sizeof(*h))return MMSYSERR_INVALPARAM;auto* slot=Find(*s,h);if(!slot)return WAVERR_UNPREPARED;if(h->dwBufferLength!=slot->length||h->dwFlags&~(WHDR_DONE|WHDR_PREPARED)||!h->lpData||h->dwLoops)return MMSYSERR_INVALPARAM;
    if(slot->buffer){if(!Poll(*slot))return WAVERR_STILLPLAYING;auto free=C(a::Op::Free,slot->buffer);if(Send(free))return MMSYSERR_ERROR;slot->buffer=0;}
    auto c=C(a::Op::Allocate,s->id);c.draw.count=h->dwBufferLength;auto result=Send(c);if(result)return result;slot->buffer=c.graphics.value;
    for(DWORD offset=0;offset<h->dwBufferLength;){c=C(a::Op::Upload,slot->buffer);c.draw.offset=offset;c.draw.count=h->dwBufferLength-offset;if(c.draw.count>sizeof(c.draw.pixels))c.draw.count=sizeof(c.draw.pixels);Copy(c.draw.pixels,h->lpData+offset,c.draw.count);result=Send(c);if(result)break;offset+=c.draw.count;}
    if(!result){c=C(a::Op::Submit,s->id);c.draw.object=slot->buffer;result=Send(c);}if(result){auto free=C(a::Op::Free,slot->buffer);Send(free);slot->buffer=0;return result;}
    h->dwFlags=(h->dwFlags&~WHDR_DONE)|WHDR_INQUEUE;return MMSYSERR_NOERROR;
}
extern "C" MMRESULT WINAPI AudioWaveOutUnprepareHeader(HWAVEOUT handle,LPWAVEHDR h,UINT bytes){Guard lock;auto* s=Find(handle);if(!s)return MMSYSERR_INVALHANDLE;if(!h||bytes!=sizeof(*h))return MMSYSERR_INVALPARAM;auto* slot=Find(*s,h);if(!slot)return MMSYSERR_NOERROR;if(slot->buffer){if(!Poll(*slot))return WAVERR_STILLPLAYING;auto c=C(a::Op::Free,slot->buffer);if(Send(c))return MMSYSERR_ERROR;}h->dwFlags&=~WHDR_PREPARED;*slot={};return MMSYSERR_NOERROR;}
extern "C" MMRESULT WINAPI AudioWaveOutReset(HWAVEOUT h){return Control(h,a::Op::Reset);}
extern "C" MMRESULT WINAPI AudioWaveOutPause(HWAVEOUT h){return Control(h,a::Op::Pause);}
extern "C" MMRESULT WINAPI AudioWaveOutRestart(HWAVEOUT h){return Control(h,a::Op::Start);}
extern "C" MMRESULT WINAPI AudioWaveOutClose(HWAVEOUT h){Guard lock;auto* s=Find(h);if(!s)return MMSYSERR_INVALHANDLE;for(auto& slot:s->headers)if(slot.buffer&&!Poll(slot))return WAVERR_STILLPLAYING;auto c=C(a::Op::Close,s->id);const auto result=Send(c);if(!result){for(auto& slot:s->headers)if(slot.guest)slot.guest->dwFlags&=~WHDR_PREPARED;*s={};}return result;}
extern "C" MMRESULT WINAPI AudioWaveOutGetPosition(HWAVEOUT h,LPMMTIME time,UINT bytes){Guard lock;auto* s=Find(h);if(!s)return MMSYSERR_INVALHANDLE;if(!time||bytes!=sizeof(*time))return MMSYSERR_INVALPARAM;auto c=C(a::Op::Position,s->id);const auto result=Send(c);if(result)return result;if(time->wType==TIME_SAMPLES)time->u.sample=DWORD(c.graphics.value/s->align);else if(time->wType==TIME_MS)time->u.ms=DWORD(c.graphics.value*1000/(uint64_t(s->align)*s->rate));else{time->wType=TIME_BYTES;time->u.cb=DWORD(c.graphics.value);}return MMSYSERR_NOERROR;}
extern "C" UINT WINAPI AudioWaveOutGetNumDevs(){auto c=C(a::Op::Enumerate);return Send(c)?0:1;}
extern "C" MMRESULT WINAPI AudioWaveOutGetDevCapsW(UINT_PTR id,LPWAVEOUTCAPSW caps,UINT bytes){if((id!=0&&id!=WAVE_MAPPER)||!caps||bytes!=sizeof(*caps))return MMSYSERR_INVALPARAM;WAVEOUTCAPSW value{};value.vDriverVersion=0x100;value.wChannels=2;value.dwFormats=WAVE_FORMAT_2M16|WAVE_FORMAT_2S16|WAVE_FORMAT_4M16|WAVE_FORMAT_4S16;const wchar_t name[]=L"MiniNT PCM Output";Copy(value.szPname,name,sizeof(name));*caps=value;return MMSYSERR_NOERROR;}
extern "C" DWORD WINAPI AudioTimeGetTime(){return GetTickCount();}
extern "C" BOOL WINAPI AudioPlaySoundW(LPCWSTR name,HMODULE module,DWORD flags){
    if(!name||module||flags&~(SND_FILENAME|SND_MEMORY|SND_NODEFAULT)||bool(flags&SND_FILENAME)==bool(flags&SND_MEMORY))return FALSE;
    BYTE* owned=nullptr;const BYTE* data=reinterpret_cast<const BYTE*>(name);DWORD length=0;
    if(flags&SND_FILENAME){auto file=CreateFileW(name,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return FALSE;LARGE_INTEGER fileSize{};if(!GetFileSizeEx(file,&fileSize)||fileSize.QuadPart<44||fileSize.QuadPart>a::MaxBuffer+4096){CloseHandle(file);return FALSE;}length=DWORD(fileSize.QuadPart);if(length<44||length>a::MaxBuffer+4096){CloseHandle(file);return FALSE;}owned=static_cast<BYTE*>(HeapAlloc(GetProcessHeap(),0,length));DWORD read=0;const bool ok=owned&&ReadFile(file,owned,length,&read,nullptr)&&read==length;CloseHandle(file);if(!ok){if(owned)HeapFree(GetProcessHeap(),0,owned);return FALSE;}data=owned;}
    auto word=[&](DWORD offset){DWORD v=0;Copy(&v,data+offset,4);return v;};bool result=false;
    if(word(0)==0x46464952&&word(8)==0x45564157&&word(4)<=a::MaxBuffer+4088){const auto end=word(4)+8;if(!length)length=end;if(end==length){WAVEFORMATEX format{};const BYTE* pcm=nullptr;DWORD size=0;bool valid=true;for(DWORD at=12;at<end;){if(end-at<8){valid=false;break;}const auto count=word(at+4);if(count>end-at-8){valid=false;break;}if(word(at)==0x20746D66&&count>=16&&count<=18)Copy(&format,data+at+8,16);else if(word(at)==0x61746164){pcm=data+at+8;size=count;}const auto next=uint64_t(at)+8+count+(count&1);if(next>end){valid=false;break;}at=DWORD(next);}if(valid&&pcm&&size&&size<=a::MaxBuffer&&Supported(&format)){HWAVEOUT h=nullptr;WAVEHDR header{};header.lpData=reinterpret_cast<LPSTR>(const_cast<BYTE*>(pcm));header.dwBufferLength=size;if(!AudioWaveOutOpen(&h,WAVE_MAPPER,&format,0,0,0)){result=!AudioWaveOutPrepareHeader(h,&header,sizeof(header))&&!AudioWaveOutWrite(h,&header,sizeof(header));const auto start=GetTickCount64();while(result&&!(header.dwFlags&WHDR_DONE)&&GetTickCount64()-start<7000)Sleep(5);result=result&&(header.dwFlags&WHDR_DONE);AudioWaveOutReset(h);AudioWaveOutUnprepareHeader(h,&header,sizeof(header));AudioWaveOutClose(h);}}}}
    if(owned)HeapFree(GetProcessHeap(),0,owned);return result;
}
