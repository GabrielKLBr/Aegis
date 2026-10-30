"""Administrative half of the phase 16 inert-fixture integration campaign.

Run explicitly from an authorized administrator terminal. This tool never elevates.
"""
from pathlib import Path
import argparse,ctypes,json,subprocess,time,winreg,hashlib
from ctypes import wintypes as w
from validate_protection import invoke,MARKER

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--stage',choices=('configure','release','schedule','crash','restart','cleanup'),required=True);a=p.parse_args();out=a.output.resolve();plan=json.loads((out/'plan.json').read_bytes());assert Path(plan['root'])==out and plan['inertOnly'];exe=Path(plan['build'])/plan['configuration']/'guardian-engine-service.exe';rows=[]
    shell=ctypes.WinDLL('shell32');assert shell.IsUserAnAdmin(),'Explicit administrator authorization required'
    def record(name,ok):
        rows.append({'case':name,'passed':bool(ok)});(out/(a.stage+'-admin-results.json')).write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows[-1]),flush=True);assert ok,name
    def call(action,request=None):
        code,r=invoke(exe,action,request,out);assert code==0,r;return r
    if a.stage=='configure':
        current=call('--protection-config',json.loads((out/'enable.json').read_bytes()));record('admin-policy-committed-to-protected-service',current['enabled'] and current['policy']['autoQuarantine'])
        security=ctypes.WinDLL('advapi32',use_last_error=True);security.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(w.DWORD)];security.SetFileSecurityW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.c_void_p];kernel=ctypes.WinDLL('kernel32');kernel.LocalFree.argtypes=[ctypes.c_void_p]
        protected=out/'sys-fixture';protected.mkdir();(protected/'owned.txt').write_bytes(MARKER);descriptor=ctypes.c_void_p();assert security.ConvertStringSecurityDescriptorToSecurityDescriptorW('D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)',1,ctypes.byref(descriptor),None)
        try:
            for path in (protected/'owned.txt',protected):assert security.SetFileSecurityW(str(path),0x80000004,descriptor)
        finally:kernel.LocalFree(descriptor)
        policy=json.loads((out/'enable.json').read_bytes());bad=[{**policy,'roots':[]},{**policy,'schema':2},{**policy,'roots':[r'\\server\share']},{**policy,'quickRoots':['..\\escape']},{**policy,'maxFiles':0},{**policy,'maxFiles':100001},{**policy,'maxSeconds':0},{**policy,'exclusions':'*'},{**policy,'unknown':True},{**policy,'schedules':[{'id':'bad','mode':'quick','intervalSeconds':0}]}]
        for i,value in enumerate(bad):
            code,_=invoke(exe,'--protection-config',value,out);record('invalid-policy-atomic-rejection-'+str(i),code!=0 and call('--protection-status')['policy']==current['policy'])

    elif a.stage=='release':
        entry=json.loads((out/'owned-entry.json').read_bytes());assert Path(entry['source']).parent==out/'watch';r=call('--q-restore',{'schema':2,'id':entry['id'],'directory':str(out/'restore'),'filename':'verified.txt'});record('admin-restores-through-restricted-caller-rights',r['copyRestored'] and r['historyUpdated'] and (out/'restore/verified.txt').read_bytes()==MARKER)
        _,denied=invoke(exe,'--q-restore',{'schema':2,'id':entry['id'],'directory':str(out/'sys-fixture'),'filename':'forbidden.txt'},out);record('restore-keeps-caller-destination-permissions',not denied.get('complete',False) and not (out/'sys-fixture/forbidden.txt').exists())
        q=call('--q-list');record('protected-copy-retained-until-explicit-erase',any(e['id']==entry['id'] for e in q['entries']))
        _,collision=invoke(exe,'--q-restore',{'schema':2,'id':entry['id'],'directory':str(out/'restore'),'filename':'verified.txt'},out);record('broker-never-overwrites-existing-destination',not collision.get('complete',False) and (out/'restore/verified.txt').read_bytes()==MARKER)
        call('--q-erase',{'schema':2,'id':entry['id']});record('admin-erases-only-owned-vault-entry',(out/'restore/verified.txt').read_bytes()==MARKER and not any(e['id']==entry['id'] for e in call('--q-list')['entries']))
    elif a.stage=='schedule':
        policy=json.loads((out/'enable.json').read_bytes());schedule_id='phase16-'+hashlib.sha256(str(out).encode()).hexdigest()[:12];policy['schedules']=[{'id':schedule_id,'mode':'quick','intervalSeconds':3600,'nextRun':int(time.time())-1}];call('--protection-config',policy)
        end=time.monotonic()+50;found=None
        while time.monotonic()<end:
            state=call('--protection-status');found=next((j for j in state['jobs'] if j['origin']=='schedule:'+schedule_id and j['state']=='completed'),None)
            if found:break
            time.sleep(.2)
        record('persistent-schedule-dispatches-without-gui',found and found['complete'] and found['filesScanned']==1)
        state=call('--protection-status');record('schedule-next-run-advanced-atomically',state['policy']['schedules'][0]['nextRun']>int(time.time()))
    elif a.stage in ('crash','restart'):
        before=call('--protection-status');pid=before['servicePid']
        if a.stage=='restart':
            for action in ('--stop','--start'):
                r=subprocess.run([str(exe),action,'antivirus'],capture_output=True,timeout=30);assert r.returncode==0,r.stderr
        else:
            id=(out/'recovery-id.txt').read_text();assert any(j['id']==id and j['state']=='running' for j in before['jobs']),'Recovery fixture already finished; submit a fresh running job'
            kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE;kernel.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,ctypes.POINTER(w.DWORD)];kernel.TerminateProcess.argtypes=[w.HANDLE,w.UINT];kernel.CloseHandle.argtypes=[w.HANDLE]
            h=kernel.OpenProcess(0x1001,False,pid);assert h
            try:
                size=w.DWORD(32768);name=ctypes.create_unicode_buffer(size.value);assert kernel.QueryFullProcessImageNameW(h,0,name,ctypes.byref(size))
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'SYSTEM\CurrentControlSet\Services\AegisAntivirus') as key:image=winreg.QueryValueEx(key,'ImagePath')[0]
                assert Path(name.value)==Path(image.split('"')[1]),'Refusing to terminate an unrelated process';assert kernel.TerminateProcess(h,77)
            finally:kernel.CloseHandle(h)
        end=time.monotonic()+45;after=None
        while time.monotonic()<end:
            code,report=invoke(exe,'--protection-status',None,out)
            if code==0:after=report;break
            time.sleep(.2)
        record('scm-recovery' if a.stage=='crash' else 'administrator-service-restart',after and after['servicePid']!=pid)
        record('policy-survives-service-restart',after['policy']==before['policy'])
    else:
        baseline=json.loads((out/'baseline-policy.json').read_bytes());state=call('--protection-config',baseline);record('original-protection-policy-restored',state['policy']==baseline)
        if (out/'owned-entry.json').exists():
            entry=json.loads((out/'owned-entry.json').read_bytes());assert Path(entry['source']).parent==out/'watch'
            if any(e['id']==entry['id'] for e in call('--q-list')['entries']):call('--q-erase',{'schema':2,'id':entry['id']})
        for e in call('--q-list')['entries']:
            if Path(e.get('source','')).is_relative_to(out):call('--q-erase',{'schema':2,'id':e['id']})
        record('antivirus-service-left-running',call('--protection-status')['complete'])

if __name__=='__main__':main()
