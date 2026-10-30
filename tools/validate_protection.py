"""Phase 16 integration stages. Only fresh inert fixtures may be isolated/restored.

Run prepare/basic as the desktop user; configure/release/restart/crash/cleanup
use validate_protection_admin.py from an authorized administrator terminal.
"""
from pathlib import Path
import argparse, ctypes, hashlib, json, os, struct, subprocess, time, winreg
from ctypes import wintypes as w
import validate_frameworks as frozen

MARKER=b'AEGIS_SCANNER_TEST_POSITIVE_V1\nInert phase 16 fixture.\n'

def invoke(exe,action,request=None,folder=None):
    args=[str(exe),action]
    if request is not None:
        f=folder/('request-'+os.urandom(8).hex()+'.json');f.write_text(json.dumps(request));args.append(str(f))
    r=subprocess.run(args,capture_output=True,timeout=35)
    values=[json.loads(line) for line in r.stdout.splitlines() if line.strip()]
    value=values[-1] if values else {'complete':False,'error':r.stderr.decode('utf-8','replace')}
    return r.returncode,value

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--stage',choices=('prepare','basic','monitor','recovery-submit','recovery-check','final'),required=True);p.add_argument('--config',default='Release',choices=('Release','Debug'));a=p.parse_args()
    build=a.build.resolve();out=a.output.resolve();exe=build/a.config/'guardian-engine-service.exe';rows=[]
    def record(name,ok):
        rows.append({'case':name,'passed':bool(ok)});(out/(a.stage+'-results.json')).write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows[-1]),flush=True);assert ok,name
    def call(action,request=None):return invoke(exe,action,request,out)
    def status():
        c,j=call('--protection-status');assert c==0 and j.get('complete'),j;return j
    def until(test,seconds=60):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=test()
            if value:return value
            time.sleep(.2)
        raise TimeoutError('Phase16 condition did not become true')
    def job(id):return next((j for j in status()['jobs'] if j['id']==id),None)
    def wait_job(id):return until(lambda:(j if (j:=job(id)) and j['state'] in ('completed','cancelled','failed') else None))
    if a.stage=='prepare':
        assert not out.exists();out.mkdir(parents=True)
        current=status();(out/'baseline-policy.json').write_text(json.dumps(current['policy'],indent=2)+'\n')
        assert not current['enabled'],'Use an isolated validation install or explicitly pause existing protection first'
        for n in ('watch','watch/excluded','custom','quick','recovery','restore'): (out/n).mkdir()
        for i in range(512):(out/'recovery'/f'inert-{i:03}.txt').write_bytes(b'Inert recovery fixture.\n')
        policy={**current['policy'],'schema':1,'enabled':True,'roots':[str(out/'watch')],'quickRoots':[str(out/'quick')],'exclusions':[str(out/'watch/excluded')],'autoQuarantine':True,'maxFiles':1000,'maxSeconds':120,'schedules':[]}
        (out/'enable.json').write_text(json.dumps(policy,indent=2)+'\n');(out/'plan.json').write_text(json.dumps({'schema':1,'root':str(out),'build':str(build),'configuration':a.config,'inertOnly':True},indent=2)+'\n');(out/'source-inputs.json').write_text(json.dumps(frozen.inputs(),indent=2)+'\n')
        record('fresh-inert-fixture-plan',True);return
    plan=json.loads((out/'plan.json').read_bytes());assert Path(plan['root'])==out and plan['inertOnly']
    if a.stage=='basic':
        current=status();record('real-owner-session-and-protected-broker',current['sessionAvailable'] and current['protectedVault'] and not current['administrator'])
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'SYSTEM\CurrentControlSet\Services\AegisAntivirus') as k:image,start=winreg.QueryValueEx(k,'ImagePath')[0],winreg.QueryValueEx(k,'Start')[0]
        installed=Path(image.split('"')[1]);record('current-service-autostart-installed',start==2 and frozen.sha(installed)==frozen.sha(exe))
        denied=False
        try:
            with installed.open('r+b'):pass
        except PermissionError:denied=True
        record('owner-cannot-modify-installed-service',denied)
        code,_=call('--protection-config',json.loads((out/'enable.json').read_bytes()));record('nonadmin-policy-change-denied',code!=0 and status()['policy']==current['policy'])
        r=subprocess.run([str(exe),'--stop','antivirus'],capture_output=True,timeout=20);record('nonadmin-protection-stop-denied',r.returncode!=0 and status()['servicePid']==current['servicePid'])
        code,q=call('--q-list');record('protected-vault-list-through-authenticated-service',code==0 and q['protectedVault'])
        denied=False
        try:Path(os.environ['ProgramData'],'AegisProtection16/state.sqlite').read_bytes()
        except PermissionError:denied=True
        record('same-account-cannot-read-protected-state',denied)
        code,q=call('--q-list');denied=False
        try:Path(os.environ['ProgramData'],'AegisProtection16/quarantine/vault.key').read_bytes()
        except PermissionError:denied=True
        record('same-account-cannot-read-vault-key',denied)
        # Raw public IPC must reject the worker-only operation before it can run.
        kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE];kernel.CreateFileW.restype=w.HANDLE;kernel.WriteFile.argtypes=[w.HANDLE,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.c_void_p];kernel.ReadFile.argtypes=kernel.WriteFile.argtypes;kernel.CloseHandle.argtypes=[w.HANDLE]
        handle=kernel.CreateFileW(r'\\.\pipe\Aegis.Engine.v1.AegisAntivirus',0x12008b,0,None,3,0x100000,None);assert handle!=w.HANDLE(-1).value
        try:
            path=str(exe);payload=struct.pack('<6I',0x53474541,1,1,25,len(path),5000)+path.encode('utf-16-le');count=w.DWORD();assert kernel.WriteFile(handle,payload,len(payload),ctypes.byref(count),None);head=ctypes.create_string_buffer(16);assert kernel.ReadFile(handle,head,16,ctypes.byref(count),None);magic,version,kind,length=struct.unpack('<4I',head.raw);body=ctypes.create_string_buffer(length);assert kernel.ReadFile(handle,body,length,ctypes.byref(count),None);record('public-ipc-rejects-internal-parser-operation',magic==0x53474541 and version==1 and kind==3 and 'Internal' in json.loads(body.raw)['error'])
        finally:kernel.CloseHandle(handle)
        f=out/'custom/positive.txt';f.write_bytes(MARKER);(out/'custom/benign.txt').write_bytes(b'Inert ordinary text.');(out/'custom/embedded.txt').write_bytes(b'Example source code: '+MARKER);code,r=call('--protection-scan',{'schema':1,'mode':'custom','paths':[str(out/'custom')]});id=r['job']['id'];record('queued-scan-outlives-submitting-client',code==0 and r['job']['state']=='queued');done=wait_job(id);record('restricted-static-scan-confirms-inert-marker',done['complete'] and done['filesScanned']==3 and done['threatsFound']==1 and f.read_bytes()==MARKER)
        record('embedded-test-marker-not-treated-as-a-sentinel',(out/'custom/embedded.txt').exists() and done['threatsFound']==1)
        code,r=call('--protection-scan',{'schema':1,'mode':'full'});id=r['job']['id'];code,_=call('--protection-cancel',{'schema':1,'id':id});done=wait_job(id);record('full-local-volume-plan-cancellable',code==0 and done['state']=='cancelled' and not done['complete'])
        code,_=call('--protection-scan',{'schema':1,'mode':'custom','paths':['..\\escape']});record('scan-path-traversal-rejected',code!=0)
        code,_=call('--protection-scan',{'schema':1,'mode':'custom','paths':[r'\\server\share']});record('network-source-rejected',code!=0)
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(4) as pool:values=list(pool.map(lambda _:call('--protection-status'),range(12)))
        record('bounded-concurrent-status-clients',all(c==0 and v['complete'] for c,v in values))
    elif a.stage=='monitor':
        until(lambda:status()['monitoringActive']);f=out/'watch/live-ação.txt';f.write_bytes(MARKER);excluded=out/'watch/excluded/retained.txt';excluded.write_bytes(MARKER)
        until(lambda:not f.exists());code,q=call('--q-list');entries=[e for e in q['entries'] if e['source']==str(f)];assert len(entries)==1;entry=entries[0];(out/'owned-entry.json').write_text(json.dumps(entry,indent=2)+'\n')
        record('realtime-detection-without-gui',entry['state']=='isolated' and entry['sha256']==hashlib.sha256(MARKER).hexdigest())
        record('admin-exclusion-retains-inert-source',excluded.read_bytes()==MARKER)
        # A manual scan must not occupy the worker reserved for automatic detection.
        code,r=call('--protection-scan',{'schema':1,'mode':'custom','paths':[str(out/'recovery')]});manual_id=r['job']['id'];until(lambda:(j:=job(manual_id)) and j['state']=='running');parallel=out/'watch/parallel.txt';parallel.write_bytes(MARKER);until(lambda:not parallel.exists(),seconds=12);record('automatic-protection-progresses-during-manual-scan',True);call('--protection-cancel',{'schema':1,'id':manual_id});wait_job(manual_id)
        legacy=out/'legacy-positive.txt';legacy.write_bytes(MARKER);cleaned=subprocess.run([str(build/a.config/'av.exe'),'clean',str(legacy)],capture_output=True,timeout=30);record('legacy-clean-uses-protected-service',cleaned.returncode==2 and not legacy.exists() and any(e['source']==str(legacy) for e in call('--q-list')[1]['entries']))

        blocked=out/'sys-fixture/owned.txt';code,_=call('--q-isolate',{'schema':2,'source':str(blocked)});record('broker-cannot-read-an-admin-only-source',code!=0)
        record('native-windows-threat-notification',any(e.get('windowsEventPublished') and e['action']=='threat-detected' for e in call('--protection-events',{'schema':1,'after':0})[1]['events']))
        code,_=call('--q-restore',{'schema':2,'id':entry['id'],'directory':str(out/'restore'),'filename':'denied.bin'});record('nonadmin-threat-release-denied',code!=0 and not (out/'restore/denied.bin').exists())
        code,_=call('--q-erase',{'schema':2,'id':entry['id']});record('nonadmin-vault-erase-denied',code!=0 and any(e['id']==entry['id'] for e in call('--q-list')[1]['entries']))
        automatic=until(lambda:next((j for j in status()['jobs'] if j['origin']!='manual'),None));code,_=call('--protection-cancel',{'schema':1,'id':automatic['id']});record('nonadmin-automatic-scan-cancellation-denied',code!=0)
        code,events=call('--protection-events',{'schema':1,'after':0});record('durable-notifications-and-gap-recovery',code==0 and any(e['action']=='threat-detected' and e.get('sha256')==entry['sha256'] for e in events['events']) and any(e['action']=='coverage-gap' and e['rescanRequired'] for e in events['events']))
        code,later=call('--protection-events',{'schema':1,'after':events['next']});record('notification-cursor-monotonic',code==0 and all(e['seq']>events['next'] for e in later['events']))
        (out/'quick/quick.txt').write_bytes(b'Inert quick scan.');code,r=call('--protection-scan',{'schema':1,'mode':'quick'});done=wait_job(r['job']['id']);record('quick-scan-configured-scope',code==0 and done['complete'] and done['filesScanned']==1)
        (out/'watch/rename.tmp').write_bytes(b'Inert renamed text.');(out/'watch/rename.tmp').rename(out/'watch/renamed.txt');until(lambda:any(j.get('currentFile')==str(out/'watch/renamed.txt') for j in status()['jobs']));record('renamed-file-notification',True)
        until(lambda:not status().get('activeAutomaticJob'));previous=out/'watch-previous';assert not previous.exists();source=(out/'watch').resolve();target=previous.resolve();assert source.is_relative_to(out) and target.is_relative_to(out);source.rename(target);(out/'watch').mkdir();replacement=out/'watch/replacement.txt';replacement.write_bytes(MARKER);until(lambda:not replacement.exists(),seconds=15);record('watch-rebinds-when-the-root-directory-is-replaced',True)

    elif a.stage=='recovery-submit':
        code,r=call('--protection-scan',{'schema':1,'mode':'custom','paths':[str(out/'recovery')]});id=r['job']['id'];until(lambda:(j:=job(id)) and j['state']=='running');(out/'recovery-id.txt').write_text(id);record('durable-running-job-before-service-failure',code==0)
    elif a.stage=='recovery-check':
        id=(out/'recovery-id.txt').read_text();done=wait_job(id);record('scm-restarts-and-recovers-queued-work',done.get('recovered') and done['attempts']>=1 and done['complete'] and done['filesScanned']==512);record('monitor-rearms-after-service-restart',until(lambda:status()['monitoringActive']))
    else:
        files=list(out.glob('*-results.json'));all_rows=[]
        for f in files:all_rows+=json.loads(f.read_bytes())
        assert all_rows and all(r['passed'] for r in all_rows)
        native=subprocess.run([str(build/a.config/'protection_events_tests.exe')],capture_output=True,timeout=20);native_result=json.loads(native.stdout);assert not native.returncode and native_result['status']=='PASS';(out/'native.json').write_text(json.dumps(native_result,indent=2)+'\n')
        proof={'schema':1,'phase':'16','configuration':a.config,'status':'PASS','tests':len(all_rows),'passed':len(all_rows),'inputs':frozen.inputs(),'sourceStable':json.loads((out/'source-inputs.json').read_bytes())==frozen.inputs(),'binaries':binaries(build,a.config),'earlierPhasesRevalidated':False,'malwareExecuted':False,'guiFullClickTested':False,'nativeTests':native_result['tests'],'osLogoffOrPhysicalPowerCutTested':False}
        assert proof['sourceStable'],'Sources changed during integration validation';(out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n');(out/'results.json').write_text(json.dumps(all_rows,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k not in ('inputs','binaries')}))

def binaries(build,config):
    return {config+'/'+n:frozen.sha(build/config/n) for n in ('guardian-engine-service.exe','guardian-quarantine.exe','guardian-scan-worker.exe','guardian-whp-memory.exe','guardian-service-sample.exe','guardian-runtime.exe','guardian-desktop.exe','guardian-native-user-host.exe','guardian-audio-host.exe','guardian-kernel-audio.gk','avui.exe','av.exe','protection_events_tests.exe')}

if __name__=='__main__':main()
