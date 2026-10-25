"""Desktop package exports, bounded data assets and tamper rejection without executing inputs."""
from pathlib import Path
import argparse,json,struct,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import desktop_runtime as runtime
import nt_compatibility as nt
import pe_volume

def rejected(action):
    try:action()
    except (ValueError,KeyError,OSError,AssertionError):return
    raise AssertionError('Malformed input was accepted')

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='aegis-desktop-tools-') as temp:
        folder=Path(temp);exports=folder/'exports';runtime.exports(exports,a.reference);coverage=json.loads((exports/'coverage.json').read_bytes());assert len(coverage['user32Exports'])>=150 and not coverage['completeWindowsGui'];assert {'BeginPaint','CreateWindowExA','CreateWindowExW','GetMessageA','GetMessageW','TextOutA','TextOutW','DwmSetWindowAttribute','SetWindowLongPtrW'}<=set(coverage['user32Exports'])
        mandatory=set('CreateSemaphoreA GetProcessAffinityMask GetStartupInfoA GetThreadContext GetThreadPriority InitializeCriticalSection IsDBCSLeadByteEx OpenProcess ReleaseSemaphore SetProcessAffinityMask SetThreadContext SetThreadPriority SuspendThread'.split());assert mandatory<=set(coverage['extraKernelExports'])
        for dll in ('gdi32','gdi32full','dwmapi'):
            functions=nt.InventoryPe(a.build/'guest/desktop'/a.config/(dll+'.dll')).export_records();assert functions and all(e.get('forwarder','').lower().startswith('user32.') for e in functions)
        source=folder/'settings.txt';source.write_text('value=7\n',encoding='ascii');bundle=folder/'bundle';manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,assets=[source]);assert nt.canonical(manifest)==nt.canonical(pe_volume.verify(bundle));assert manifest['desktopRuntime']['hostCallbacks'] is False and manifest['phase']=='14.7';assert any(f['kind']==3 for f in manifest['files'])
        data=bytearray((bundle/'runtime.gpv').read_bytes());count=struct.unpack_from('<I',data,24)[0];index=next(i for i,f in enumerate(manifest['files']) if f['kind']==3);at=48+index*152
        for name in ('c:\\windows\\system32\\settings.txt','c:\\app\\malformed.dll'):
            bad=bytearray(data);bad[at:at+128]=pe_volume.text(name,128);struct.pack_into('<Q',bad,40,pe_volume.fnv(bad,True));rejected(lambda:pe_volume.validate_volume(bad))
        assert count==len(manifest['files'])
        (bundle/'runtime.gpv').write_bytes(data[:-1]);rejected(lambda:pe_volume.verify(bundle))
        invalid=folder/'program.dll';invalid.write_bytes(b'bounded data');rejected(lambda:runtime.bundle(a.build,a.config,folder/'bad-data',a.reference,a.schema_reference,assets=[invalid]))
        rejected(lambda:runtime.bundle(a.build,a.config,folder/'duplicate-data',a.reference,a.schema_reference,assets=[source,source]))
    print(json.dumps({'status':'PASS','phase':'14.7','exports':len(coverage['user32Exports']),'dataTamperCases':5}))
if __name__=='__main__':main()
