"""Focused tests against the installed Aegis services; never installs other services.

Run from an ordinary desktop account after the package's UAC setup completes.
The fixtures are generated here and are never executed as Windows host programs.
"""
from pathlib import Path
import argparse,ctypes as c,json,os,queue,struct,subprocess,threading,time,winreg
from ctypes import wintypes as w

def require(condition,message):
    if not condition:raise RuntimeError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--wait-sample',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--bundle',type=Path)
    args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    exe=args.package.resolve()/'guardian-engine-service.exe'
    evidence=[]
    def run(*arguments,success=True,timeout=30):
        result=subprocess.run([str(exe),*map(str,arguments)],capture_output=True,encoding='utf-8',errors='replace',timeout=timeout)
        rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        if success:require(result.returncode==0,result.stderr or result.stdout or f'Command failed: {arguments}')
        return result,rows
    def states():return run('--status')[1][-1]
    def record(name,**extra):evidence.append({'case':name,'status':'PASS',**extra});print(name,'PASS',flush=True)
    k=c.WinDLL('kernel32',use_last_error=True);a=c.WinDLL('advapi32',use_last_error=True)
    k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
    k.CloseHandle.argtypes=[w.HANDLE];k.CloseHandle.restype=w.BOOL
    k.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];k.WaitForSingleObject.restype=w.DWORD
    a.OpenSCManagerW.argtypes=[w.LPCWSTR,w.LPCWSTR,w.DWORD];a.OpenSCManagerW.restype=w.HANDLE
    a.OpenServiceW.argtypes=[w.HANDLE,w.LPCWSTR,w.DWORD];a.OpenServiceW.restype=w.HANDLE
    a.CloseServiceHandle.argtypes=[w.HANDLE];a.CloseServiceHandle.restype=w.BOOL
    k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE
    k.WriteFile.argtypes=[w.HANDLE,w.LPCVOID,w.DWORD,c.POINTER(w.DWORD),w.LPVOID];k.WriteFile.restype=w.BOOL
    k.ReadFile.argtypes=[w.HANDLE,w.LPVOID,w.DWORD,c.POINTER(w.DWORD),w.LPVOID];k.ReadFile.restype=w.BOOL
    k.PeekNamedPipe.argtypes=[w.HANDLE,w.LPVOID,w.DWORD,w.LPVOID,c.POINTER(w.DWORD),w.LPVOID];k.PeekNamedPipe.restype=w.BOOL
    invalid=c.c_void_p(-1).value
    def gone(pid):
        handle=k.OpenProcess(0x100000,False,pid)
        if not handle:
            require(c.get_last_error()==87,'Could not verify worker termination');return True
        try:return k.WaitForSingleObject(handle,5000)==0
        finally:k.CloseHandle(handle)
    def worker_process(action,path):
        process=subprocess.Popen([str(exe),action,str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,encoding='utf-8',errors='replace')
        received=queue.Queue()
        threading.Thread(target=lambda:received.put(process.stdout.readline()),daemon=True).start()
        try:line=received.get(timeout=10)
        except queue.Empty:process.kill();process.communicate();raise RuntimeError('No worker identity received')
        require(line.startswith('{'),'Worker identity missing: '+line)
        info=json.loads(line);require(info.get('type')=='worker','Worker identity frame rejected')
        return process,info
    def read_pipe(handle,length):
        result=b'';deadline=time.monotonic()+6
        while len(result)<length:
            available=w.DWORD()
            require(k.PeekNamedPipe(handle,None,0,None,c.byref(available),None),'Pipe response unavailable')
            if not available.value:
                require(time.monotonic()<deadline,'Pipe response timed out');time.sleep(.01);continue
            size=min(available.value,length-len(result));buffer=c.create_string_buffer(size);got=w.DWORD()
            require(k.ReadFile(handle,buffer,size,c.byref(got),None),'Read rejected request response')
            result+=buffer.raw[:got.value]
        return result
    initial=states();require(all(row['installed'] for row in initial.values()),'Install both Aegis services before this test')
    try:
        for kind in ('antivirus','sandbox'):run('--stop',kind)
        document=output/'documentação-🧪.txt';document.write_text('An ordinary document.',encoding='utf-8')
        result,_=run('--scan',document,success=False);require(result.returncode!=0,'Stopped antivirus silently analyzed a file')
        result,_=run('--sandbox',args.wait_sample,success=False);require(result.returncode!=0,'Stopped sandbox silently executed a file')
        record('stopped_engines_fail_closed')
        run('--start','antivirus');require(states()['antivirus']['running'] and not states()['sandbox']['running'],'Engines are not independent')
        _,rows=run('--scan',document);identity=rows[0];report=rows[-1]
        require(identity['type']=='worker' and not identity['elevated'] and identity['restricted'] and
            identity['privilegesRestricted'] and not identity['administrator'],'Scanner worker privileges rejected')
        session=w.DWORD();k.ProcessIdToSessionId.argtypes=[w.DWORD,c.POINTER(w.DWORD)]
        require(k.ProcessIdToSessionId(os.getpid(),c.byref(session)) and identity['session']==session.value,'Worker left the caller session')
        require(report['filesScanned']==1 and report['unknown']==1 and report['complete'],'Scanner result or Unicode path failed')
        record('antivirus_worker_user_session_unicode',sessionMatches=True,restricted=True,elevated=False)
        corpus=output/'corpus';corpus.mkdir()
        (corpus/'positive.txt').write_text('AEGIS_SCANNER_TEST_'+'POSITIVE_V1',encoding='ascii')
        (corpus/'ordinary.txt').write_text('An ordinary document.',encoding='ascii')
        (corpus/'unsupported.msi').write_bytes(b'owned unsupported fixture')
        result,rows=run('--scan',corpus,success=False);report=rows[-1]
        require(result.returncode==3 and report['filesScanned']==3 and report['threatsFound']==1 and report['inconclusive']==1 and report['unknown']==1,
            'Service scan lost detections or incomplete coverage')
        record('scan_tree_detection_unknown_incomplete')
        scm=a.OpenSCManagerW(None,None,1);require(scm,'Open SCM')
        try:
            for name in ('AegisAntivirus','AegisSandbox'):
                service=a.OpenServiceW(scm,name,2|0x40000)
                if service:a.CloseServiceHandle(service)
                require(not service and c.get_last_error()==5,'Desktop user could change a privileged service configuration')
        finally:a.CloseServiceHandle(scm)
        record('service_configuration_not_writable_by_desktop_user')
        for service_name in ('AegisAntivirus','AegisSandbox'):
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,'SYSTEM\\CurrentControlSet\\Services\\'+service_name) as key:
                image=winreg.QueryValueEx(key,'ImagePath')[0]
            installed=Path(image.split('"')[1])
            require(installed.parent.parent.name=='AegisEngineServices','Service was not installed in its protected directory')
            handle=k.CreateFileW(str(installed),2,7,None,3,0,None)
            error=c.get_last_error()
            if handle!=invalid:k.CloseHandle(handle)
            require(handle==invalid and error==5,'Desktop user could write a privileged service executable')
        record('installed_executables_not_writable_by_desktop_user')
        name=r'\\.\pipe\Aegis.Engine.v1.AegisAntivirus'
        deadline=time.monotonic()+3
        while True:
            handle=k.CreateFileW(name,0x12008b,0,None,3,0x120000,None)
            if handle!=invalid:break
            require(c.get_last_error() in (2,231) and time.monotonic()<deadline,'Connect protocol rejection test')
            time.sleep(.025)
        require(handle!=invalid,'Connect protocol rejection test')
        try:
            bad=struct.pack('<6I',0x53474541,2,1,1,1,1000);count=w.DWORD()
            require(k.WriteFile(handle,bad,len(bad),c.byref(count),None),'Write malformed request')
            header=struct.unpack('<4I',read_pipe(handle,16));require(header[:3]==(0x53474541,1,3),'Malformed protocol was accepted')
            require(0<header[3]<=2<<20,'Rejected request response exceeded its bound')
            response=json.loads(read_pipe(handle,header[3]));require(response.get('error'),'Rejected request did not explain failure')
        finally:k.CloseHandle(handle)
        require(states()['antivirus']['running'],'Malformed request stopped antivirus')
        record('malformed_ipc_rejected_without_service_failure')
        run('--start','sandbox')
        _,rows=run('--sandbox',args.package/'guardian-service-sample.exe');report=rows[-1]
        require(rows[0]['session']==session.value and not rows[0]['elevated'],'Sandbox worker left user session')
        require(report['backend']=='MiniNT-WHP-legacy' and report['loaded'] and report['ran'] and report['exitCode']==0,'Owned WHP sample failed')
        record('owned_pe_runs_inside_whp')
        process,identity=worker_process('--sandbox',args.wait_sample)
        start=time.monotonic();run('--stop','sandbox');process.communicate(timeout=10)
        require(gone(identity['pid']),'Stopping sandbox left a worker alive')
        require(states()['antivirus']['running'] and not states()['sandbox']['running'],'Stopping sandbox affected antivirus')
        record('stop_kills_vm_worker_independent_antivirus',seconds=round(time.monotonic()-start,3))
        run('--start','sandbox')
        process,identity=worker_process('--sandbox',args.wait_sample)
        process.terminate();process.communicate(timeout=10)
        require(gone(identity['pid']) and states()['sandbox']['running'],'Client disconnect did not cancel only its job')
        record('client_disconnect_kills_owned_worker')
        if args.bundle:
            _,rows=run('--bundle',args.bundle,timeout=90);report=rows[-1]
            require(report['backend']=='MiniNT-14.10G' and rows[0]['session']==session.value and report['complete'],'Prepared desktop bundle failed through the service')
            session_report=Path(report['output'])/'native-gui.json'
            require((Path(report['output'])/'boot.json').is_file(),'Desktop session report missing')
            record('prepared_14_10G_bundle_in_user_session',output=report['output'])
        # Leave both engines ready for use after validating independent controls.
        for kind in ('antivirus','sandbox'):run('--start',kind)
        (output/'validation.json').write_text(json.dumps({'schema':1,'status':'PASS','scope':'Windows engine services only','cases':evidence},indent=2)+'\n',encoding='utf-8')
    except BaseException:
        (output/'validation-partial.json').write_text(json.dumps({'schema':1,'status':'INCOMPLETE','cases':evidence},indent=2)+'\n',encoding='utf-8')
        raise
    print('Windows engine services integration PASS',flush=True)

if __name__=='__main__':main()
