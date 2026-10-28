"""Versioned PCM playback surface; local state is distinct from native operations."""
from pathlib import Path
import argparse,json
ROOT=Path(__file__).resolve().parents[1]
def entries():
    rows=[]
    def add(dll,names,contract,status='implemented_with_restrictions',execution='Native operation through copied PCM and owned IDs'):
        for name in names.split():rows.append(dict(dll=dll,name=name,status=status,contract=contract,execution=execution,phase='14.10G',category='Áudio e mídia',kind='Método COM' if '::' in name else 'Export'))
    add('winmm.dll','waveOutOpen waveOutWrite waveOutReset waveOutPause waveOutRestart waveOutGetPosition waveOutClose','PCM 16-bit, mono/stereo, 22050/44100/48000 Hz; default output only; CALLBACK_NULL; max four streams, eight headers/stream, 256 KiB/header and 1 MiB/session. Position bytes/samples/ms. No capture, loop or driver messages.')
    add('winmm.dll','waveOutPrepareHeader waveOutUnprepareHeader waveOutGetDevCapsW timeGetTime','Guest header/state management and virtual output capabilities; guest clock for timeGetTime.',execution='Guest-local state; native completion is polled by a guest worker')
    add('winmm.dll','waveOutGetNumDevs','One virtual output backed by current host default render endpoint; zero on failure.')
    add('winmm.dll','PlaySoundW','Synchronous PCM RIFF/WAV, guest filename or guest memory; max 256 KiB PCM. No async, aliases, resources, loops or default host sounds.',execution='Guest WAV parsing/read; native PCM playback through WinMM')
    add('mmdevapi.dll','IMMDeviceEnumerator::EnumAudioEndpoints IMMDeviceEnumerator::GetDefaultAudioEndpoint IMMDeviceEnumerator::GetDevice IMMDeviceCollection::GetCount IMMDeviceCollection::Item IMMDevice::GetId IMMDevice::GetState IMMDevice::Activate','Only virtual default eRender, active state, IAudioClient; synthetic device ID, no host identifiers/properties.',execution='Guest COM objects and virtual device identity; host checks default render endpoint availability')
    add('mmdevapi.dll','IAudioClient::Initialize IAudioClient::GetBufferSize IAudioClient::GetCurrentPadding IAudioClient::Start IAudioClient::Stop IAudioClient::Reset IAudioRenderClient::GetBuffer IAudioRenderClient::ReleaseBuffer IAudioClock::GetPosition IAudioClock::GetFrequency','Native shared WASAPI PCM conversion; 20–500 ms buffer; PCM subset above; copied buffers max 256 KiB; polling. No exclusive/loopback/events/custom sessions. Failures/device invalidation propagate; reopen via a new client.')
    add('mmdevapi.dll','IAudioClient::IsFormatSupported IAudioClient::GetMixFormat IAudioClient::GetService IAudioClock::GetCharacteristics','Guest virtual device contract: preferred PCM stereo 48 kHz; render/clock services; fixed-frequency characteristic.',execution='Guest-local validation, allocations and service wrappers')
    add('mmdevapi.dll','IMMDevice::OpenPropertyStore','Host endpoint properties are private; returns E_ACCESSDENIED.','denied_by_policy','No host access')
    add('mmdevapi.dll','IMMDeviceEnumerator::RegisterEndpointNotificationCallback IMMDeviceEnumerator::UnregisterEndpointNotificationCallback IAudioClient::GetStreamLatency IAudioClient::GetDevicePeriod IAudioClient::SetEventHandle','Not implemented; E_NOTIMPL, not a successful no-op.','pending','No implementation')
    add('mmdevapi.dll','IAudioCaptureClient::GetBuffer','Capture/loopback not available; capture endpoint request returns E_ACCESSDENIED, capture service E_NOINTERFACE.','denied_by_policy','No host access')
    add('mfplat.dll','MFStartup MFShutdown MFCreateMemoryBuffer','Media Foundation not available; imports rejected before boot.','pending','No implementation')
    add('mfreadwrite.dll','MFCreateSourceReaderFromByteStream','Compressed media/video codecs remain a later lot; imports rejected before boot.','pending','No implementation')
    return rows
def validate(build,config):
    import nt_compatibility as nt
    expected=json.loads((ROOT/'config/audio-api-matrix.json').read_bytes())
    nt.require(expected=={'schema':1,'phase':'14.10G','entries':entries()},'Audio inventory changed without generator update')
    exports={name for row in nt.InventoryPe(Path(build)/'guest/audio'/config/'winmm.dll').export_records() for name in row['names']}
    wanted={r['name'] for r in entries() if r['dll']=='winmm.dll'};nt.require(exports==wanted,'WinMM export inventory mismatch')
    return {'status':'PASS','entries':len(entries()),'winmmExports':len(wanted),'scope':'Declared PCM subset; not complete Windows audio'}
def main():
    p=argparse.ArgumentParser();p.add_argument('--write',action='store_true');p.add_argument('--build',type=Path);p.add_argument('--config');a=p.parse_args()
    if a.write:(ROOT/'config/audio-api-matrix.json').write_text(json.dumps({'schema':1,'phase':'14.10G','entries':entries()},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    else:print(json.dumps(validate(a.build,a.config)))
if __name__=='__main__':main()
