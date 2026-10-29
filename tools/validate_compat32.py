"""Validate only the 14.12 x86 core and its used dependencies. No external input runs on the host."""
from pathlib import Path
import argparse,json,os,shutil,struct,subprocess,sys
import compat32_runtime as runtime
import compat32_corpus as corpus
import validate_frameworks as snapshots
import nt_compatibility as nt
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'test'))
from NativeUserIntegrationTests import own_helper
EXPECTED=18
def binaries(build,config):
    paths=[build/config/(name+'.exe') for name in ('guardian-desktop','guardian-native-user-host','guardian-compat32-tests','guardian-whp-memory')]+[build/'guest'/config/'guardian-kernel-compat32.gk']
    for name in ('kernel32','user32','win32u','msvcrt'):
        for suffix in ('.dll','.lib'):paths.append(build/'guest/compat32'/config/(name+suffix))
    for name in ('console','gui','input','crash','gui_crash','exit'):paths.append(build/'guest/compat32'/config/('guardian-x86-'+name+'.exe'))
    return {p.relative_to(build).as_posix():snapshots.sha(p) for p in sorted(paths)}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output','corpus','asan-runtime'):parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--config',choices=('Release','Debug'),required=True);a=parser.parse_args();build=a.build.resolve();out=a.output.resolve();nt.require(not out.exists(),'Fresh output required');out.mkdir(parents=True);inputs=out/'inputs';inputs.mkdir()
    sources=snapshots.inputs();artifacts=binaries(build,a.config);rows=[];dlls=build/'guest/compat32'/a.config;runner=build/a.config/'guardian-desktop.exe';kernel=build/'guest'/a.config/'guardian-kernel-compat32.gk'
    def record(name,ok,**fields):
        rows.append({'case':name,'passed':bool(ok),**fields});(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8');print(json.dumps(rows[-1]),flush=True)
    file=corpus.source(a.corpus);record('independent-source-pin',snapshots.sha(file)==json.loads((ROOT/'config/compat32-corpus.json').read_bytes())['sha256']);external=corpus.compile(file,inputs,dlls)
    environment=os.environ.copy();environment['PATH']=str(a.asan_runtime.resolve())+os.pathsep+environment.get('PATH','');environment['ASAN_OPTIONS']='halt_on_error=1:symbolize=0'
    parser_test=subprocess.run([str(build/a.config/'guardian-compat32-tests.exe'),str(dlls),str(dlls/'guardian-x86-gui.exe')],env=environment,capture_output=True,encoding='utf-8',errors='replace',timeout=90)
    (out/'parser.log').write_text(parser_test.stdout+parser_test.stderr,encoding='utf-8');record('PE32-loader-ASAN',parser_test.returncode==0,output=parser_test.stdout.strip())
    def case(name,app,success=True,input=False,marker='X86_ACCEPTANCE_PASS'):
        folder=out/name;folder.mkdir();bundle=folder/'bundle'
        try:
            runtime.bundle(build,a.config,bundle,app);command=[str(runner),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'run'),'--compat32','--seconds','15']
            p=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace');helper=None
            try:
                if input:helper=own_helper(p.pid,'input')
                stdout,stderr=p.communicate(timeout=35)
            finally:
                if p.poll() is None:p.kill();p.communicate()
            (folder/'stdout.log').write_text(stdout+stderr,encoding='utf-8')
            if not (folder/'run/boot.json').exists():raise RuntimeError('Guest launch failed: '+(stdout+stderr)[-512:])
            boot=json.loads((folder/'run/boot.json').read_bytes())['boots'][0];gui=json.loads((folder/'run/native-user32.json').read_bytes());trace=json.loads((folder/'run/analysis.json').read_bytes());objects=boot['processes']
            ok=(p.returncode==0)==success and boot['success']==success and not boot['timedOut'] and not boot['memoryFault'] and not boot['cpu']['fatal']
            ok=ok and all(objects[k]==0 for k in ('liveProcesses','liveObjects','liveHandles','liveReferences')) and gui['completed'] and gui['helperExited'] and gui['helperExitCode']==0 and not gui['helperKilled'] and gui['liveAtLastReply']==[0,0,0] and not gui['transportFailed'] and gui['restrictions']==31 and gui['inheritedHandles']==2 and gui['processLimit']==1 and not gui['hostHandlesInGuest'] and not gui['guestWindowPixelsTransferred']
            if success:ok=ok and marker in stdout and trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected')) and gui['nativeCalls'][0]==gui['nativeCalls'][1]
            else:ok=ok and not trace['complete'] and any(f.get('classification')=='inconclusive' for f in trace.get('findings',[])) and objects['faulted']==(0 if name=='x86-exit' else 1)
            if input:ok=ok and gui['helperPid']==helper and gui['nativeCalls'][0]>=4
            record(name,ok,exit=p.returncode,nativeWindows=gui['nativeCalls'][0],nativeCompleted=gui['completed'])
            if not ok:print((stdout+stderr)[-2000:],flush=True)
        except (RuntimeError,OSError,ValueError,AssertionError,KeyError,subprocess.TimeoutExpired) as e:record(name,False,diagnostic=str(e))
    for name in ('console','gui'):case('x86-'+name,dlls/('guardian-x86-'+name+'.exe'))
    case('x86-native-input',dlls/'guardian-x86-input.exe',input=True)
    case('external-C-console',external,marker='Hello World')
    for name in ('crash','gui_crash','exit'):case('x86-'+name,dlls/('guardian-x86-'+name+'.exe'),False)
    def rejection(name,operation):
        try:operation()
        except (RuntimeError,OSError,ValueError,struct.error) as e:record(name,True,diagnostic=str(e));return
        record(name,False)
    app=dlls/'guardian-x86-gui.exe'
    for profile in ('win7','win11'):rejection('unsupported-profile-'+profile,lambda profile=profile:runtime.bundle(build,a.config,out/('bad-'+profile),app,profile=profile))
    # The current trusted x64 helper is inspected as an invalid guest image.
    x64=build/a.config/'guardian-native-user-host.exe'
    rejection('x64-root-refused',lambda:runtime.Pe32(x64))
    mixed=inputs/'mixed';mixed.mkdir()
    for file in dlls.glob('*.dll'):shutil.copy2(file,mixed/file.name)
    shutil.copy2(x64,mixed/'kernel32.dll');rejection('mixed-DLL-refused',lambda:runtime.bundle(build,a.config,out/'bad-mixed',app,runtime=mixed))
    for label,change in (('writable-code',lambda raw,opt:struct.pack_into('<I',raw,opt+224+36,0xE0000020)),('CLR',lambda raw,opt:struct.pack_into('<II',raw,opt+96+14*8,0x1000,72)),('truncated',None)):
        raw=bytearray(app.read_bytes());opt=struct.unpack_from('<I',raw,60)[0]+24
        if change:change(raw,opt)
        else:raw=raw[:100]
        bad=inputs/(label+'.exe');bad.write_bytes(raw);rejection('refuse-'+label,lambda bad=bad:runtime.Pe32(bad))
    for label,flag,forge in (('wrong-mode','--native-user32',False),('wrong-architecture','--compat32',True)):
        folder=out/label;folder.mkdir();runtime.bundle(build,a.config,folder/'bundle',app);bundle=folder/'bundle'
        if forge:
            manifest=json.loads((bundle/'manifest.json').read_bytes());manifest['architecture']='x64';raw=nt.canonical(manifest);(bundle/'manifest.json').write_bytes(raw);binding=bytearray((bundle/'binding.gnt').read_bytes());binding[48:80]=bytes.fromhex(nt.sha(raw));(bundle/'binding.gnt').write_bytes(binding)
        p=subprocess.run([str(runner),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'run'),flag],capture_output=True,timeout=15);record(label,p.returncode==2 and b'x86 kernel/manifest/mode mismatch' in p.stderr and not (folder/'run/boot.json').exists())
    stable=sources==snapshots.inputs() and artifacts==binaries(build,a.config);passed=sum(r['passed'] for r in rows);ok=passed==EXPECTED==len(rows) and stable
    proof={'phase':'14.12','scope':'initial-x86-core','phaseComplete':False,'status':'PASS' if ok else 'FAIL','configuration':a.config,'tests':len(rows),'passed':passed,'stableSourcesAndBinaries':stable,'inputs':sources,'binaries':artifacts,'earlierPhasesRevalidated':False,'hostInputExecution':False,'sanitizer':'MSVC AddressSanitizer','targetNt':'10.0.19045','architecture':'x86'}
    (out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps({'passed':passed,'tests':len(rows),'stableSourcesAndBinaries':stable}));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
