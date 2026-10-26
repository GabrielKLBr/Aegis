"""SDK USER32 imports in MiniNT with real, restricted host controls and guest callbacks."""
from pathlib import Path
import argparse,ctypes,json,subprocess,sys,time
from ctypes import wintypes as w
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_user32_runtime as runtime
import nt_compatibility as nt
import pe_volume

def own_helper(parent,action):
    class Entry(ctypes.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateToolhelp32Snapshot.restype=w.HANDLE;kernel.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Entry)];kernel.Process32NextW.argtypes=kernel.Process32FirstW.argtypes;kernel.CloseHandle.argtypes=[w.HANDLE];kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE;kernel.TerminateProcess.argtypes=[w.HANDLE,w.UINT]
    user=ctypes.WinDLL('user32',use_last_error=True);callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);user.EnumWindows.argtypes=[callback_type,w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];user.IsWindowVisible.argtypes=[w.HWND];user.GetDlgItem.argtypes=[w.HWND,ctypes.c_int];user.GetDlgItem.restype=w.HWND;user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int];user.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)]
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        snapshot=kernel.CreateToolhelp32Snapshot(2,0);entry=Entry();entry.size=ctypes.sizeof(entry);pids=[]
        try:
            found=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
            while found:
                if entry.parent==parent and entry.exe=='guardian-native-user-host.exe':pids.append(entry.pid)
                found=kernel.Process32NextW(snapshot,ctypes.byref(entry))
        finally:kernel.CloseHandle(snapshot)
        windows=[]
        def inspect(hwnd,_):
            owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value in pids and user.IsWindowVisible(hwnd):windows.append((hwnd,owner.value))
            return True
        user.EnumWindows(callback_type(inspect),0)
        for hwnd,pid in windows:
            if action=='kill':
                handle=kernel.OpenProcess(1|0x1000,False,pid);assert handle
                try:assert kernel.TerminateProcess(handle,19)
                finally:kernel.CloseHandle(handle)
                return pid
            if action=='burst':
                caption=ctypes.create_unicode_buffer(256);user.GetWindowTextW(hwnd,caption,256)
                if caption.value.endswith('Receiver'):
                    for _ in range(200):assert user.PostMessageW(hwnd,0x102,ord('x'),1)
                    return pid
                continue
            edit=user.GetDlgItem(hwnd,102);button=user.GetDlgItem(hwnd,101);label=user.GetDlgItem(hwnd,103)
            if edit and button and label:
                # The fixture clears the initial text after its other checks. Its helper is the only target.
                text=ctypes.create_unicode_buffer(256);copied=ctypes.c_size_t();assert user.SendMessageTimeoutW(edit,0xD,256,ctypes.addressof(text),2,1000,ctypes.byref(copied))
                ready=ctypes.create_unicode_buffer(256);assert user.SendMessageTimeoutW(label,0xD,256,ctypes.addressof(ready),2,1000,ctypes.byref(copied))
                if not text.value and ready.value=='Input ready':
                    for ch in 'abc':assert user.PostMessageW(edit,0x102,ord(ch),1)
                    assert user.PostMessageW(button,0x201,1,(10<<16)|10)
                    assert user.PostMessageW(button,0x202,0,(10<<16)|10)
                    return pid
        time.sleep(.01)
    raise AssertionError('Own native helper/control not observed')

def main():
    p=argparse.ArgumentParser()
    for name in ('build','reference','schema-reference','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);cases=[];sessions=[]
    corpus=[('normal-0','demo',8,True),('normal-1','demo',8,True),('threads','threads',8,True),('input','input',8,True),('guest-crash','crash',5,False),('guest-deadline','hang',1,False),('helper-death','hang',5,False)]
    for label,mode,seconds,expected in corpus:
        folder=out/label;folder.mkdir();bundle=folder/'bundle';session=folder/'session';manifest=runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/native-user32'/a.config/('guardian-native-user-'+mode+'.exe'));assert manifest['phase']=='14.10B' and manifest['nativeGuiRuntime']['backend']=='win32-user32-helper';pe_volume.verify(bundle)
        command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-native-user32.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--native-user32','--seconds',str(seconds)]
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        try:
            helper=own_helper(process.pid,'kill' if label=='helper-death' else 'burst' if label=='threads' else 'input') if label in ('input','helper-death','threads') else None
            stdout,stderr=process.communicate(timeout=seconds+18)
        finally:
            if process.poll() is None:process.kill();process.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8');native=json.loads((session/'native-user32.json').read_bytes());assert native['helperExited'] and native['restrictions']==31 and native['inheritedHandles']==2 and native['processLimit']==1 and not native['guestPixelsTransferred'] and not native['hostHandlesInGuest']
        if expected:
            assert not process.returncode,stdout+stderr
            boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());marker='Native USER32 interthread return' if mode=='threads' else 'Native USER32 SDK controls'
            assert boot['success'] and marker in boot['console'],boot['console'];assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'));assert native['completed'] and not native['helperKilled'] and native['helperExitCode']==0 and native['liveAtLastReply']==[0,0,0];assert native['nativeCalls'][0]>=2 and native['nativeCalls'][0]==native['nativeCalls'][1];sessions.append(native['session'])
            if mode=='threads':assert helper==native['helperPid'] and max(e['queued'] for e in native['events'])>=200 and any(e['operation']==15 and e['status']==1460 for e in native['events'])
        else:
            assert process.returncode and not native['completed'],stdout+stderr
            if label=='helper-death':assert native['helperPid']==helper and native['helperExitCode']==19
        cases.append({'case':label,'status':'PASS','completed':native['completed'],'helperExited':True,'nativeCalls':native['nativeCalls']})
    assert len(set(sessions))==4
    result={'phase':'14.10B','status':'PASS','configuration':a.config,'cases':cases,'guestPixelsTransferred':0,'standardUser32Subset':True,'completeWindowsGui':False};(out/'result.json').write_bytes(nt.canonical(result));print(json.dumps({'status':'PASS','cases':len(cases),'evidence':str(out)}))
if __name__=='__main__':main()
