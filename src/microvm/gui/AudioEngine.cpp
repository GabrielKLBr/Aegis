#include "AudioEngine.h"
#include <Windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cstring>
namespace av::microvm::native_gui {
namespace a=guardian::audio;namespace g=guardian::nativegdi;
using Microsoft::WRL::ComPtr;
struct AudioEngine::State {
    struct Stream {uint64_t id=0;uint32_t pid=0,tid=0,align=0,rate=0,capacity=0;HWAVEOUT wave=nullptr;ComPtr<IAudioClient> client;ComPtr<IAudioRenderClient> render;ComPtr<IAudioClock> clock;};
    struct Buffer {uint64_t id=0,stream=0;uint32_t written=0;std::vector<uint8_t> data;WAVEHDR header{};bool queued=false,submitted=false;};
    uint64_t session,next=1,bytes=0,submitted=0;uint32_t calls=0,failures=0,backends=0;bool com=false;
    std::array<Stream,a::MaxStreams> streams{};std::array<Buffer,a::MaxBuffers> buffers{};
    explicit State(uint64_t value):session(value){com=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));}
    ~State(){if(com)CoUninitialize();}
    uint64_t Id(){return (uint64_t(60)<<56)|((session&0xFFFFFF)<<32)|next++;}
    Stream* Find(uint64_t id,uint32_t pid){for(auto& s:streams)if(id&&s.id==id&&s.pid==pid)return &s;return nullptr;}
    Buffer* FindBuffer(uint64_t id,uint32_t pid){for(auto& b:buffers)if(id&&b.id==id&&Find(b.stream,pid))return &b;return nullptr;}
    void Drop(Buffer& b){if(auto* s=Owner(b.stream);s&&s->wave&&b.header.dwFlags&WHDR_PREPARED)if(waveOutUnprepareHeader(s->wave,&b.header,sizeof(b.header)))__fastfail(7);bytes-=b.data.size();b={};}
    Stream* Owner(uint64_t id){for(auto& s:streams)if(id&&s.id==id)return &s;return nullptr;}
    void Reset(Stream& s){if(s.wave){if(waveOutReset(s.wave))__fastfail(7);}else if(s.client){s.client->Stop();s.client->Reset();}for(auto& b:buffers)if(b.id&&b.stream==s.id){b.queued=false;b.header.dwFlags&=~WHDR_INQUEUE;b.header.dwFlags|=WHDR_DONE;}}
    void Close(Stream& s){Reset(s);for(auto& b:buffers)if(b.id&&b.stream==s.id)Drop(b);if(s.wave&&waveOutClose(s.wave))__fastfail(7);s={};}
};
AudioEngine::AudioEngine(uint64_t value):state_(std::make_unique<State>(value)){}
AudioEngine::~AudioEngine(){Clear();}
void AudioEngine::Clear(){for(auto& s:state_->streams)if(s.id)state_->Close(s);}
void AudioEngine::Exit(uint32_t pid,uint32_t tid){for(auto& s:state_->streams)if(s.id&&s.pid==pid&&s.tid==tid)state_->Close(s);}
uint32_t AudioEngine::Live() const{uint32_t n=0;for(auto& s:state_->streams)if(s.id)++n;for(auto& b:state_->buffers)if(b.id)++n;return n;}
uint64_t AudioEngine::Bytes() const{return state_->bytes;}
void AudioEngine::Final(g::Reply& r) const{r.metrics.height=int32_t(state_->calls);r.metrics.ascent=int32_t(state_->failures);r.metrics.descent=int32_t(state_->backends);r.metrics.internal=int32_t(state_->submitted);}
uint32_t AudioEngine::Execute(const g::Command& c,g::Reply& r,uint32_t pid,uint32_t tid){
    if(!a::Valid(c))return ERROR_INVALID_PARAMETER;auto& st=*state_;++st.calls;
    HRESULT hr=S_OK;const auto op=static_cast<a::Op>(c.op);auto* s=st.Find(c.dc,pid);auto* b=st.FindBuffer(c.dc,pid);
    const auto body=[&]()->HRESULT {
        if(op==a::Op::Stats){Final(r);return S_OK;}
        if(op==a::Op::Enumerate){ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDevice> d;auto result=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e));if(SUCCEEDED(result))result=e->GetDefaultAudioEndpoint(eRender,eMultimedia,&d);r.value=SUCCEEDED(result)?1:0;return result;}
        if(op==a::Op::Open){
            WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nSamplesPerSec=uint32_t(c.args[0]);f.nChannels=WORD(c.args[1]);f.wBitsPerSample=WORD(c.args[2]);f.nBlockAlign=WORD(f.nChannels*2);f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
            if(c.flags==2){const auto mm=waveOutOpen(nullptr,WAVE_MAPPER,&f,0,0,WAVE_FORMAT_QUERY);r.y=int32_t(mm);return mm?HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED):S_OK;}
            State::Stream* slot=nullptr;for(auto& v:st.streams)if(!v.id){slot=&v;break;}if(!slot)return E_OUTOFMEMORY;
            State::Stream temp;temp.pid=pid;temp.tid=tid;temp.align=f.nBlockAlign;temp.rate=f.nSamplesPerSec;
            if(!c.flags){const auto mm=waveOutOpen(&temp.wave,WAVE_MAPPER,&f,0,0,CALLBACK_NULL);if(mm){r.y=int32_t(mm);return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);}st.backends|=1;}
            else {ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDevice> d;auto result=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&e));if(SUCCEEDED(result))result=e->GetDefaultAudioEndpoint(eRender,eMultimedia,&d);if(SUCCEEDED(result))result=d->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(temp.client.GetAddressOf()));if(SUCCEEDED(result))result=temp.client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,REFERENCE_TIME(c.args[3])*10000,0,&f,nullptr);if(SUCCEEDED(result))result=temp.client->GetBufferSize(&temp.capacity);if(SUCCEEDED(result))result=temp.client->GetService(IID_PPV_ARGS(&temp.render));if(SUCCEEDED(result))result=temp.client->GetService(IID_PPV_ARGS(&temp.clock));if(FAILED(result))return result;st.backends|=2;}
            temp.id=st.Id();*slot=std::move(temp);r.value=slot->id;r.rect.right=int32_t(slot->capacity);return S_OK;
        }
        if(op==a::Op::Upload||op==a::Op::Poll||op==a::Op::Free){
            if(!b)return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
            auto* owner=st.Find(b->stream,pid);if(!owner)return E_UNEXPECTED;
            if(op==a::Op::Poll){r.value=owner->wave?(b->submitted&&(b->header.dwFlags&WHDR_DONE)?1:0):(b->submitted?1:0);return S_OK;}
            if(op==a::Op::Free){if(owner->wave&&b->queued&&!(b->header.dwFlags&WHDR_DONE))return HRESULT_FROM_WIN32(ERROR_BUSY);st.Drop(*b);return S_OK;}
            if(b->submitted||c.offset!=b->written||c.count>b->data.size()-b->written)return E_INVALIDARG;std::memcpy(b->data.data()+b->written,c.pixels,c.count);b->written+=c.count;return S_OK;
        }
        if(!s)return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
        switch(op){
        case a::Op::Allocate:{if(c.count%s->align||st.bytes+c.count>a::MaxBytes)return E_INVALIDARG;for(auto& buffer:st.buffers)if(!buffer.id){buffer.data.resize(c.count);buffer.stream=s->id;buffer.id=st.Id();st.bytes+=c.count;r.value=buffer.id;return S_OK;}return E_OUTOFMEMORY;}
        case a::Op::Submit:{auto* buffer=st.FindBuffer(c.object,pid);if(!buffer||buffer->stream!=s->id)return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);if(buffer->written!=buffer->data.size()||buffer->submitted)return E_INVALIDARG;if(st.submitted+buffer->data.size()>(16u<<20))return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
            if(s->wave){auto& h=buffer->header;h.lpData=reinterpret_cast<LPSTR>(buffer->data.data());h.dwBufferLength=DWORD(buffer->data.size());auto mm=waveOutPrepareHeader(s->wave,&h,sizeof(h));if(!mm)mm=waveOutWrite(s->wave,&h,sizeof(h));if(mm){r.y=int32_t(mm);return E_FAIL;}buffer->queued=true;}
            else {UINT32 padding=0;auto result=s->client->GetCurrentPadding(&padding);const auto frames=UINT32(buffer->data.size()/s->align);if(FAILED(result))return result;if(padding>s->capacity)return E_UNEXPECTED;if(frames>s->capacity-padding)return AUDCLNT_E_BUFFER_TOO_LARGE;BYTE* dest=nullptr;result=s->render->GetBuffer(frames,&dest);if(FAILED(result))return result;std::memcpy(dest,buffer->data.data(),buffer->data.size());result=s->render->ReleaseBuffer(frames,0);if(FAILED(result))return result;}
            buffer->submitted=true;st.submitted+=buffer->data.size();return S_OK;}
        case a::Op::Position:if(s->wave){MMTIME time{};time.wType=TIME_BYTES;const auto mm=waveOutGetPosition(s->wave,&time,sizeof(time));if(mm)return E_FAIL;r.value=time.u.cb;return S_OK;}else {UINT32 padding=0;auto result=s->client->GetCurrentPadding(&padding);r.rect.right=int32_t(s->capacity);r.rect.bottom=int32_t(padding);UINT64 position=0,qpc=0,freq=0;if(SUCCEEDED(result))result=s->clock->GetPosition(&position,&qpc);if(SUCCEEDED(result))result=s->clock->GetFrequency(&freq);r.value=position;r.selected[0]=qpc;r.selected[1]=freq;return result;}
        case a::Op::Pause:return s->wave?(waveOutPause(s->wave)?E_FAIL:S_OK):s->client->Stop();
        case a::Op::Start:return s->wave?(waveOutRestart(s->wave)?E_FAIL:S_OK):s->client->Start();
        case a::Op::Reset:if(s->wave){const auto mm=waveOutReset(s->wave);if(mm)return E_FAIL;}else {auto result=s->client->Stop();if(FAILED(result)&&result!=AUDCLNT_E_NOT_INITIALIZED)return result;result=s->client->Reset();if(FAILED(result))return result;}for(auto& buffer:st.buffers)if(buffer.id&&buffer.stream==s->id){buffer.queued=false;buffer.header.dwFlags|=WHDR_DONE;}return S_OK;
        case a::Op::Close:st.Close(*s);return S_OK;
        default:return E_NOTIMPL;
        }
    };
    hr=body();r.x=hr;if(FAILED(hr))++st.failures;return 0;
}
}
