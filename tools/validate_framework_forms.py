"""Phase 14.11 managed forms and their used dependencies only; no mouse/keyboard automation."""
from pathlib import Path
from ctypes import wintypes as w
import argparse
import ctypes
import json
import os
import struct
import subprocess
import time
import framework_runtime as runtime
import framework_forms_corpus as corpus
import nt_compatibility as nt
import validate_frameworks as base

EXPECTED_TESTS=20

def control_owned_window(parent,process,caption,action):
    """Target only the restricted helper created by this exact launcher PID."""
    class Entry(ctypes.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);user=ctypes.WinDLL('user32',use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype=w.HANDLE;kernel.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Entry)];kernel.Process32NextW.argtypes=kernel.Process32FirstW.argtypes;kernel.CloseHandle.argtypes=[w.HANDLE]
    callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);user.EnumWindows.argtypes=[callback_type,w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
    user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int];user.IsWindowVisible.argtypes=[w.HWND];user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    user.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];user.GetDlgItem.restype=w.HWND;user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]
    user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)];user.SendMessageTimeoutW.restype=ctypes.c_ssize_t
    user.GetDC.argtypes=[w.HWND];user.GetDC.restype=w.HDC;user.ReleaseDC.argtypes=[w.HWND,w.HDC]
    gdi=ctypes.WinDLL('gdi32');gdi.GetTextFaceW.argtypes=[w.HDC,ctypes.c_int,w.LPWSTR]
    deadline=time.monotonic()+8
    while time.monotonic()<deadline and process.poll() is None:
        snapshot=kernel.CreateToolhelp32Snapshot(2,0);entry=Entry();entry.size=ctypes.sizeof(entry);pids=[]
        if snapshot==w.HANDLE(-1).value:raise RuntimeError('Process snapshot failed')
        try:
            found=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
            while found:
                if entry.parent==parent and entry.exe=='guardian-audio-host.exe':pids.append(entry.pid)
                found=kernel.Process32NextW(snapshot,ctypes.byref(entry))
        finally:kernel.CloseHandle(snapshot)
        windows=[]
        def inspect(hwnd,_):
            owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value in pids and user.IsWindowVisible(hwnd):
                text=ctypes.create_unicode_buffer(256);user.GetWindowTextW(hwnd,text,256)
                if text.value=='[MiniNT audio 14.10G] '+caption:windows.append((hwnd,owner.value))
            return True
        user.EnumWindows(callback_type(inspect),0)
        for hwnd,pid in windows:
            evidence={'helperPid':pid,'caption':caption,'action':action,'desktopInputUsed':False}
            if action=='close':
                nt.require(user.PostMessageW(hwnd,0x10,0,0)!=0,'Owned close failed');return evidence
            # Managed object IDs are this fixture's deterministic child IDs.
            button=user.GetDlgItem(hwnd,3);edit=user.GetDlgItem(hwnd,4)
            if not button or not edit:continue
            classes=[]
            for child in (button,edit):
                name=ctypes.create_unicode_buffer(256);nt.require(user.GetClassNameW(child,name,256)>0,'Native class missing');classes.append(name.value.lower())
            text=ctypes.create_unicode_buffer(256);user.GetWindowTextW(edit,text,256)
            if text.value!='Input ready':continue
            nt.require(classes==['button','edit'],'Managed controls are not native Windows controls')
            dc=user.GetDC(edit);nt.require(bool(dc),'Native edit DC missing');face=ctypes.create_unicode_buffer(128)
            try:nt.require(gdi.GetTextFaceW(dc,128,face)>0,'Native font missing')
            finally:user.ReleaseDC(edit,dc)
            returned=ctypes.c_size_t();nt.require(user.SendMessageTimeoutW(button,0xF5,0,0,2,1000,ctypes.byref(returned))!=0,'Owned native button input failed')
            return {**evidence,'classes':classes,'fontFace':face.value}
        time.sleep(.02)
    raise RuntimeError('Owned managed window was not observed')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output','reference','schema-reference','asan-runtime','corpus'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True)
    p.add_argument('--csc',type=Path,default=Path(os.environ.get('WINDIR','C:/Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe')
    a=p.parse_args();root=base.ROOT;build=a.build.resolve();out=a.output.resolve();nt.require(not out.exists(),'Use a fresh output');out.mkdir(parents=True);fixtures=out/'inputs';fixtures.mkdir()
    sources=base.inputs();binaries=base.binaries(build,a.config);rows=[]
    def record(name,ok,**fields):
        rows.append({'case':name,'passed':bool(ok),**fields});(out/'results.json').write_text(json.dumps({'phase':'14.11','scope':'managed-forms and used dependencies only','rows':rows},ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(rows[-1],ensure_ascii=False),flush=True)
    def compile_cs(name,source,define=None):
        target=fixtures/(name+'.exe');args=[str(a.csc),'/nologo','/codepage:65001','/platform:x64','/optimize+','/out:'+str(target),'/reference:'+str(a.csc.parent/'System.Windows.Forms.dll')]
        if define:args+=['/define:'+define]
        r=subprocess.run(args+[str(source)],capture_output=True,encoding='utf-8',errors='replace',timeout=30);(fixtures/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8');nt.require(r.returncode==0,'Compilation failed: '+name);return target
    external_source=corpus.materialize(a.corpus);record('external-source-pin',base.sha(external_source)==json.loads((root/'config/framework-forms-corpus.json').read_bytes())['sourceSha256'])
    forms_source=root/'guest/fixtures/frameworks/Forms.cs'
    applications={'controls':compile_cs('forms-controls',forms_source)}
    for label,define in (('run','RUN'),('input','INPUT'),('null-run','NULL_RUN'),('event-quota','QUOTA'),('callback-failure','CALLBACK_FAILURE'),('text-quota','TEXT_QUOTA')):applications[label]=compile_cs('forms-'+label,forms_source,'FORMS_'+define)
    applications['external']=compile_cs('external-window',external_source)
    invalid=fixtures/'invalid-forms.exe'
    il=subprocess.run([str(a.csc.parent/'ilasm.exe'),str(root/'guest/fixtures/frameworks/InvalidForms.il'),'/exe','/pe64','/x64','/output='+str(invalid)],capture_output=True,encoding='utf-8',errors='replace',timeout=30)
    (fixtures/'invalid-forms-compile.log').write_text(il.stdout+il.stderr,encoding='utf-8');nt.require(il.returncode==0,'Invalid delegate fixture failed to assemble')
    # Remove the unused native ILASM bootstrap, as in the existing CIL negatives.
    data=bytearray(invalid.read_bytes());optional=struct.unpack_from('<I',data,60)[0]+24;struct.pack_into('<I',data,optional+16,0)
    for directory in (1,5,12):struct.pack_into('<II',data,optional+112+directory*8,0,0)
    invalid.write_bytes(data);applications['invalid-delegate']=invalid
    unsupported=compile_cs('unsupported-browser',root/'guest/fixtures/frameworks/Negative.cs','CIL_WINFORMS')
    probe=runtime.inspect(build,a.config,unsupported);record('unsupported-browser',probe['status']=='UNSUPPORTED',diagnostic=probe['diagnostic'])
    record('forms-contract',all(runtime.inspect(build,a.config,applications[k]).get('runtime')=='minint-cil-forms-v2' for k in ('controls','run','external')))
    environment=os.environ.copy();environment['PATH']=str(a.asan_runtime.resolve())+os.pathsep+environment.get('PATH','');environment['ASAN_OPTIONS']='halt_on_error=1:symbolize=0'
    fuzz=subprocess.run([str(build/a.config/'guardian-framework-tests.exe'),str(applications['controls'])],env=environment,capture_output=True,encoding='utf-8',errors='replace',timeout=60)
    (out/'parser-forms.log').write_text(fuzz.stdout+fuzz.stderr,encoding='utf-8');record('parser-forms-asan',fuzz.returncode==0,output=fuzz.stdout.strip())
    kernel=build/'guest'/a.config/'guardian-kernel-frameworks.gk';runner=build/a.config/'guardian-desktop.exe'
    def case(name,application,marker='',success=True,control=None,portable=None,state=None):
        folder=out/name;folder.mkdir();bundle=folder/'bundle';evidence=None
        try:
            args=(build,a.config,bundle,a.reference,a.schema_reference,application)
            if portable:runtime.portable_bundle(*args,mode=portable)
            else:runtime.bundle(*args)
            command=[str(runner),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'run'),'--frameworks','--seconds','25']
            if state:command+=['--state',str(state)]
            process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
            try:
                if control:evidence=control_owned_window(process.pid,process,*control)
                stdout,stderr=process.communicate(timeout=45)
            finally:
                if process.poll() is None:process.kill();process.communicate()
            (folder/'stdout.log').write_text(stdout+stderr,encoding='utf-8')
            if evidence:(folder/'window.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
            boot=json.loads((folder/'run/boot.json').read_bytes())['boots'][0];native=json.loads((folder/'run/audio.json').read_bytes())
            trace=json.loads((folder/'run/analysis.json').read_bytes());resources=boot.get('processes',{})
            ok=(process.returncode==0)==success and boot['success']==success and marker in stdout and not boot['timedOut'] and not boot['memoryFault']
            ok=ok and all(resources.get(key,1)==0 for key in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
            ok=ok and native['completed'] and native['helperExited'] and not native['helperKilled'] and native['helperExitCode']==0 and native['restrictions']==31 and native['inheritedHandles']==2 and native['processLimit']==1
            ok=ok and native['liveAtLastReply']==[0,0,0] and native['gdiLive']==[0,0,0] and not native['hostHandlesInGuest'] and not native['guestPixelsTransferred'] and not native['transportFailed']
            ok=ok and native['nativeCalls'][0]==native['nativeCalls'][1]
            if evidence:ok=ok and evidence['helperPid']==native['helperPid'] and native['nativeCalls'][0]>=1
            if success:ok=ok and trace['complete'] and not any(trace[key] for key in ('dropped','pending','rejected'))
            else:ok=ok and any(item.get('classification')=='inconclusive' for item in trace.get('findings',[]))
            record(name,ok,exit=process.returncode,expectedSuccess=success,nativeWindows=native['nativeCalls'][0],helperCompleted=native['completed'])
            if not ok:print((stdout+stderr)[-1800:],flush=True)
            return folder/'run/security-state.bin'
        except (RuntimeError,OSError,ValueError,AssertionError,KeyError,subprocess.TimeoutExpired) as error:
            record(name,False,diagnostic=str(error));return None
    case('managed-native-controls',applications['controls'],'WINFORMS_CONTROLS_PASS')
    case('managed-run-form',applications['run'],'WINFORMS_RUN_PASS',control=('Aegis WinForms acceptance','close'))
    case('managed-native-input',applications['input'],'WINFORMS_NATIVE_INPUT_PASS',control=('Aegis WinForms acceptance','button'))
    case('external-rosetta-window',applications['external'],control=('Window','close'))
    for name,diagnostic in (('null-run','cli-null-reference'),('event-quota','cli-quota'),('callback-failure','cli-arithmetic-error'),('text-quota','cli-quota'),('invalid-delegate','unsupported-cli-signature')):case('managed-'+name,applications[name],diagnostic,False)
    state=None
    for mode in ('install','resume','uninstall'):state=case('portable-forms-'+mode,applications['controls'],'PORTABLE_'+mode.upper()+'_PASS',portable=mode,state=state)
    state=None
    for mode in ('install','resume','uninstall'):
        state=case('portable-external-'+mode,applications['external'],'PORTABLE_'+mode.upper()+'_PASS',portable=mode,state=state,control=None if mode=='uninstall' else ('Window','close'))
    first=out/'managed-native-controls/bundle';wrong=out/'wrong-mode'
    result=subprocess.run([str(runner),str(kernel),str(first/'runtime.gpv'),str(first/'manifest.json'),str(first/'binding.gnt'),str(wrong),'--audio'],capture_output=True,timeout=15)
    record('phase-mode-binding',result.returncode!=0 and not (wrong/'boot.json').exists())
    stable=sources==base.inputs() and binaries==base.binaries(build,a.config);passed=sum(row['passed'] for row in rows)
    ok=len(rows)==EXPECTED_TESTS and passed==len(rows) and stable
    proof={'schema':1,'phase':'14.11','scopeKey':'managed-forms','status':'PASS' if ok else 'FAIL','configuration':a.config,'tests':len(rows),'passed':passed,'stableSourcesAndBinaries':stable,'inputs':sources,'binaries':binaries,'earlierPhasesRevalidated':False,'hostManagedExecution':False,'externalSourceRedistributed':False,'sanitizer':'MSVC AddressSanitizer','completeWinForms':False,'completePhase14_11':False}
    (out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps({'passed':passed,'total':len(rows),'stableSourcesAndBinaries':stable,'output':str(out)},ensure_ascii=False));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
