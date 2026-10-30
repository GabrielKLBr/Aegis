"""Validate only the installed 14.13A service flow; never installs or elevates."""
from pathlib import Path
import argparse,ctypes,json,struct,subprocess,sys,time,winreg
from ctypes import wintypes as w
import nt_compatibility as nt,validate_frameworks as frozen
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'test'))
from NativeUserIntegrationTests import own_helper

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();build=a.build.resolve();out=a.output.resolve();nt.require(not out.exists(),'Fresh service evidence required');out.mkdir(parents=True);exe=build/'Release/guardian-engine-service.exe';rows=[]
    sources=frozen.inputs();binary=frozen.sha(exe)
    def record(name,ok,**extra):
        rows.append({'case':name,'passed':bool(ok),**extra});(out/'results.json').write_bytes(nt.canonical(rows));print(json.dumps(rows[-1]),flush=True);nt.require(ok,name)
    def control(*args):
        r=subprocess.run([str(exe),*map(str,args)],capture_output=True,timeout=25);nt.require(not r.returncode,r.stderr.decode(errors='replace'));return json.loads(r.stdout) if r.stdout.strip() else None
    def run(name,action,path,interact=False):
        process=subprocess.Popen([str(exe),action,str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace');lines=[]
        try:
            identity=json.loads(process.stdout.readline());lines.append(identity)
            while True:
                line=process.stdout.readline()
                if not line:break
                value=json.loads(line);lines.append(value)
                if interact and value.get('stage')=='running':own_helper(identity['pid'],'input')
            _,err=process.communicate(timeout=70)
        finally:
            if process.poll() is None:process.kill();process.communicate()
        (out/(name+'.json')).write_bytes(nt.canonical(lines));nt.require(lines and 'complete' in lines[-1],err if 'err' in locals() else 'Service result missing');return lines
    installed=[]
    for kind in ('Antivirus','Sandbox'):
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,'SYSTEM\\CurrentControlSet\\Services\\Aegis'+kind) as key:image=winreg.QueryValueEx(key,'ImagePath')[0]
        path=Path(image.split('"')[1]);installed.append(path)
    record('installed-current-binaries',all(frozen.sha(path)==binary for path in installed))
    for path in installed:
        nt.require((path.parent/'runtime/index.json').is_file(),'Installed automatic templates missing')
        for helper in ('guardian-native-user-host.exe','guardian-audio-host.exe','guardian-whp-memory.exe'):nt.require(frozen.sha(path.parent/helper)==frozen.sha(build/'Release'/helper),'Installed helper stale')
    record('installed-templates-and-helpers',True)
    for kind in ('antivirus','sandbox'):control('--start',kind)
    console=build/'guest/compat32/Release/guardian-x86-console.exe'
    values=run('automatic','--sandbox',console);identity=values[0];report=values[-1]
    record('restricted-worker',identity.get('restricted') and identity.get('privilegesRestricted') and not identity.get('elevated') and not identity.get('administrator'))
    record('service-auto-package',report.get('complete') and report.get('automaticPackage') and 'X86_ACCEPTANCE_PASS' in report.get('console',''))
    stages=[v['stage'] for v in values if v.get('type')=='runtime-progress']
    record('service-live-progress',stages==['preparing','validating','running','completed'])
    clone=out/'new-owned-GUI.exe';raw=bytearray((build/'guest/unified/Release/guardian-unified-probe.exe').read_bytes());raw[struct.unpack_from('<I',raw,60)[0]+8]^=0x7A;clone.write_bytes(raw)
    values=run('checkpoint','--checkpoint',clone);report=values[-1]
    record('service-USER32-save',report.get('checkpointCreated') and report.get('automaticPackage') and not report['complete'])
    values=run('restore','--resume',Path(report['output'])/'recovery.json',True);report=values[-1]
    record('service-USER32-resume',report.get('complete') and report.get('restored') and 'UNIFIED_RECOVERY_PASS' in report.get('console',''))
    process=subprocess.Popen([str(exe),'--sandbox',str(clone)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8');identity=json.loads(process.stdout.readline())
    try:
        while json.loads(process.stdout.readline()).get('stage')!='running':pass
        process.terminate();process.communicate(timeout=10)
    finally:
        if process.poll() is None:process.kill();process.communicate()
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE;kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];kernel.CloseHandle.argtypes=[w.HANDLE]
    handle=kernel.OpenProcess(0x100000,False,identity['pid']);gone=not handle or kernel.WaitForSingleObject(handle,5000)==0
    if handle:kernel.CloseHandle(handle)
    record('cancel-disconnect-kills-job',gone and control('--status')['sandbox']['running'])
    control('--stop','sandbox');states=control('--status');record('independent-engine-switches',not states['sandbox']['running'] and states['antivirus']['running']);control('--start','sandbox')
    document=out/'ordinary.txt';document.write_text('Ordinary harmless validation document.',encoding='utf-8');values=run('antivirus','--scan',document);record('antivirus-still-works',values[-1].get('complete') and values[-1].get('filesScanned')==1)
    stable=sources==frozen.inputs() and binary==frozen.sha(exe);record('stable-sources-and-service',stable)
    proof={'phase':'14.13A','status':'PASS','scope':'installed-unified-service-current-lot','tests':len(rows),'passed':sum(r['passed'] for r in rows),'sourceStable':stable,'serviceSha256':binary,'earlierPhasesRevalidated':False,'hostInputExecution':False}
    (out/'validation.json').write_bytes(nt.canonical(proof));print(json.dumps(proof))
if __name__=='__main__':main()
