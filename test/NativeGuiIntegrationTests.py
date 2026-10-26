"""Actual MiniNT native-command applications and supervised Win32 helper processes."""
from pathlib import Path
import argparse,ctypes,json,subprocess,sys,time
from ctypes import wintypes as w
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_gui_runtime as runtime
import nt_compatibility as nt
import pe_volume

def kill_helper(parent):
    class Entry(ctypes.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateToolhelp32Snapshot.restype=w.HANDLE;kernel.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Entry)];kernel.Process32NextW.argtypes=kernel.Process32FirstW.argtypes;kernel.CloseHandle.argtypes=[w.HANDLE];kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE;kernel.TerminateProcess.argtypes=[w.HANDLE,w.UINT]
    user=ctypes.WinDLL('user32',use_last_error=True);callback_type=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);user.EnumWindows.argtypes=[callback_type,w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)];user.IsWindowVisible.argtypes=[w.HWND]
    def visible(pid):
        found=[]
        def inspect(hwnd,_):
            owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
            if owner.value==pid and user.IsWindowVisible(hwnd):found.append(hwnd)
            return True
        user.EnumWindows(callback_type(inspect),0);return bool(found)
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        snapshot=kernel.CreateToolhelp32Snapshot(2,0);entry=Entry();entry.size=ctypes.sizeof(entry)
        try:
            found=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
            while found:
                if entry.parent==parent and entry.exe=='guardian-native-gui-host.exe' and visible(entry.pid):
                    handle=kernel.OpenProcess(1|0x1000,False,entry.pid)
                    assert handle
                    try:assert kernel.TerminateProcess(handle,19)
                    finally:kernel.CloseHandle(handle)
                    return entry.pid
                found=kernel.Process32NextW(snapshot,ctypes.byref(entry))
        finally:kernel.CloseHandle(snapshot)
        time.sleep(.01)
    raise AssertionError('Own helper process not observed')

def main():
    p=argparse.ArgumentParser()
    for name in ('build','reference','schema-reference','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);cases=[];sessions=[]
    for label,mode,seconds,expected in [('normal-0','demo',8,True),('normal-1','demo',8,True),('guest-crash','crash',5,False),('guest-deadline','hang',1,False),('helper-death','demo',5,False)]:
        folder=out/label;folder.mkdir();bundle=folder/'bundle';session=folder/'session';manifest=runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/native-gui'/a.config/('guardian-native-gui-'+mode+'.exe'));assert manifest['phase']=='14.10A' and manifest['nativeGuiRuntime']['backend']=='win32-gdi-helper';pe_volume.verify(bundle)
        command=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-native-gui.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(session),'--native-gui','--seconds',str(seconds)]
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        try:
            killed=kill_helper(process.pid) if label=='helper-death' else None
            stdout,stderr=process.communicate(timeout=seconds+18)
        finally:
            if process.poll() is None:process.kill();process.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8');native=json.loads((session/'native-gui.json').read_bytes());assert native['helperExited'] and native['restrictions']==31 and native['inheritedHandles']==2 and native['processLimit']==1 and not native['guestPixelsTransferred'] and not native['hostHandlesInGuest']
        if expected:
            assert not process.returncode,stdout+stderr
            boot=json.loads((session/'boot.json').read_bytes())['boots'][0];trace=json.loads((session/'analysis.json').read_bytes());assert boot['success'] and 'Native GUI host FillRect/GetPixel PASS' in boot['console'];assert trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'));assert native['completed'] and native['helperExitCode']==0 and native['liveAtLastReply']==[0,0,0] and native['nativeCalls']==[1,2,1,2,2,1,2,1]
            assert any(e['status']==6 for e in native['events']) and all(e['pid'] and e['tid'] for e in native['events']);sessions.append(native['session'])
        else:
            assert process.returncode and not native['completed'],stdout+stderr
            if killed:assert native['helperPid']==killed and native['helperExitCode']==19
        cases.append({'case':label,'status':'PASS','completed':native['completed'],'helperExited':True,'nativeCalls':native['nativeCalls']})
    assert len(set(sessions))==2
    result={'phase':'14.10A','status':'PASS','configuration':a.config,'cases':cases,'guestPixelsTransferred':0,'generalUser32Supported':False};(out/'result.json').write_bytes(nt.canonical(result));print(json.dumps({'status':'PASS','cases':len(cases),'evidence':str(out)}))
if __name__=='__main__':main()
