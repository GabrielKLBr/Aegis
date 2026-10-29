"""Targeted phase 14.11 validation. Untrusted/managed inputs run only in MiniNT."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import framework_runtime as runtime
import nt_compatibility as nt

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs():
    files=[ROOT/'CMakeLists.txt']
    for directory in ('src','guest','shared','test','tools','config','cmake'):
        files.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.pyo'))
    return {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(files)}
def binaries(build,config):
    files=[build/config/(name+'.exe') for name in ('guardian-desktop','guardian-whp-memory','guardian-audio-host','guardian-cli-inspect','guardian-framework-tests')]
    files.append(build/'guest'/config/'guardian-kernel-frameworks.gk')
    for group in ('frameworks','audio','desktop','crt','security','components','internet'):
        files.extend(sorted((build/'guest'/group/config).glob('*.dll')))
    files.extend(sorted((build/'guest/frameworks'/config).glob('*.exe')))
    return {p.relative_to(build).as_posix():sha(p) for p in files}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True);p.add_argument('--config',default='Release')
    p.add_argument('--output',type=Path,required=True);p.add_argument('--reference',type=Path,required=True)
    p.add_argument('--schema-reference',type=Path,required=True)
    p.add_argument('--csc',type=Path,default=Path(os.environ.get('WINDIR','C:/Windows'))/'Microsoft.NET/Framework64/v4.0.30319/csc.exe')
    p.add_argument('--asan-runtime',type=Path)
    a=p.parse_args();root=Path(__file__).resolve().parents[1];build=a.build.resolve();out=a.output.resolve()
    nt.require(not out.exists(),'Use a fresh validation output directory');out.mkdir(parents=True);fixtures=out/'inputs';fixtures.mkdir()
    rows=[];kernel=build/'guest'/a.config/'guardian-kernel-frameworks.gk';runner=build/a.config/'guardian-desktop.exe';fw=build/'guest/frameworks'/a.config
    source_inputs=inputs();binary_inputs=binaries(build,a.config)
    def record(name,ok,**fields):
        row={'case':name,'passed':bool(ok),**fields};rows.append(row)
        (out/'results.json').write_text(json.dumps({'schema':1,'phase':'14.11','scope':'current phase and used dependencies only','rows':rows},ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(row,ensure_ascii=False),flush=True);return ok
    def compile_cs(name,source,define=None,references=(),platform='x64'):
        target=fixtures/(name+'.exe');args=[str(a.csc),'/nologo','/codepage:65001','/platform:'+platform,'/optimize+','/out:'+str(target)]
        if define:args+=['/define:'+define]
        args+=['/reference:'+str(file) for file in references];args+=[str(source)]
        r=subprocess.run(args,capture_output=True,encoding='utf-8',errors='replace',timeout=30)
        (fixtures/(name+'-compile.log')).write_text(r.stdout+r.stderr,encoding='utf-8');nt.require(r.returncode==0,'C# compilation failed: '+name);return target
    console=compile_cs('console',root/'guest/fixtures/frameworks/Console.cs')
    mono_source=root/'test/frameworks/mono/test-1.cs';content=mono_source.read_bytes();origin=json.loads((mono_source.parent/'ORIGIN.json').read_bytes())
    record('external-source-pin',hashlib.sha1(b'blob '+str(len(content)).encode()+b'\0'+content).hexdigest()==origin['gitBlobSha1'])
    license=(mono_source.parent/'LICENSE.txt').read_bytes()
    record('external-license-pin',hashlib.sha1(b'blob '+str(len(license)).encode()+b'\0'+license).hexdigest()==origin['licenseGitBlobSha1'])
    mono=compile_cs('mono-console',mono_source)
    x86=compile_cs('x86',mono_source,platform='x86');record('x86-diagnostic',runtime.inspect(build,a.config,x86)['status']=='UNSUPPORTED')
    negative={};source=root/'guest/fixtures/frameworks/Negative.cs'
    for name in ('DIVZERO','LOOP','DEPTH','I8','FILE','PINVOKE','WINFORMS','WPF','STATIC','OUTPUT','CONTROL'):
        refs=[]
        if name=='WINFORMS':refs=[a.csc.parent/'System.Windows.Forms.dll']
        if name=='WPF':refs=[a.csc.parent/'WPF'/dll for dll in ('PresentationFramework.dll','PresentationCore.dll','WindowsBase.dll')]+[a.csc.parent/'System.Xaml.dll']
        negative[name]=compile_cs(name.lower(),source,'CIL_'+name,refs)
    for name in ('I8','FILE','PINVOKE','WINFORMS','WPF','STATIC'):
        result=runtime.inspect(build,a.config,negative[name]);record('unsupported-'+name.lower(),result['status']=='UNSUPPORTED',diagnostic=result['diagnostic'])
    for name in ('msi','msix','appx','winmd'):
        fake=fixtures/('unsupported.'+name);fake.write_bytes(b'not-an-executable')
        result=runtime.inspect(build,a.config,fake);record('unsupported-'+name,result['status']=='UNSUPPORTED',diagnostic=result['diagnostic'])
    environment=os.environ.copy()
    if a.asan_runtime:environment['PATH']=str(a.asan_runtime.resolve())+os.pathsep+environment.get('PATH','')
    environment['ASAN_OPTIONS']='halt_on_error=1:symbolize=0'
    fuzz=subprocess.run([str(build/a.config/'guardian-framework-tests.exe'),str(console)],env=environment,capture_output=True,encoding='utf-8',errors='replace',timeout=60)
    (out/'parser.log').write_text(fuzz.stdout+fuzz.stderr,encoding='utf-8');record('parser-and-utf8',fuzz.returncode==0,output=fuzz.stdout.strip())
    # Only these owned SDK clients run natively, from a directory with no guest DLL.
    native=out/'native-reference';native.mkdir()
    for name,marker in (('winrt','WINRT_SCALARS_PASS'),('version-client','VERSION_RESOURCE_PASS')):
        executable=native/('guardian-framework-'+name+'.exe');shutil.copy2(fw/executable.name,executable)
        nt.require(not list(native.glob('*.dll')),'Native reference directory contains a guest DLL')
        result=subprocess.run([str(executable)],cwd=native,capture_output=True,encoding='utf-8',errors='replace',timeout=20)
        (native/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8');record('native-sdk-'+name,result.returncode==0 and marker in result.stdout)
    def case(name,application,marker,profile='win10',portable=None,state=None,success=True,also=()):
        folder=out/name;folder.mkdir();bundle=folder/'bundle'
        args=(build,a.config,bundle,a.reference,a.schema_reference,application)
        try:
            if portable:runtime.portable_bundle(*args,mode=portable,profile=profile)
            else:runtime.bundle(*args,profile=profile)
        except (RuntimeError,OSError,ValueError,struct.error) as error:
            record(name,False,stage='bundle-preparation',diagnostic=str(error),target=profile)
            return None
        command=[str(runner),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'run'),'--frameworks','--seconds','25']
        if state:command+=['--state',str(state)]
        result=subprocess.run(command,capture_output=True,encoding='utf-8',errors='replace',timeout=45)
        (folder/'stdout.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        report_path=folder/'run/boot.json';report=json.loads(report_path.read_bytes())['boots'][0] if report_path.exists() else {}
        ok=(result.returncode==0)==success and marker in result.stdout and all(text in result.stdout for text in also) and bool(report.get('success'))==success
        if report:
            ok=ok and not report.get('memoryFault') and not report.get('timedOut')
            process=report.get('processes',{});ok=ok and all(process.get(key,1)==0 for key in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
        if not success:
            analysis_path=folder/'run/analysis.json';analysis=json.loads(analysis_path.read_bytes()) if analysis_path.exists() else {}
            ok=ok and any(item.get('classification')=='inconclusive' for item in analysis.get('findings',[]))
        if not record(name,ok,exit=result.returncode,expectedSuccess=success,target=profile):print((result.stdout+result.stderr)[-1800:],flush=True)
        return folder/'run/security-state.bin'
    case('cil-console',console,'CIL_CONSOLE_PASS',also=('ação e Unicode 🎉','55\n120\n59\nTrue\n-2147483648\n'))
    for profile in ('win10','win7','win11'):case('external-cil-'+profile,mono,'Hello, World!',profile)
    case('winrt',fw/'guardian-framework-winrt.exe','WINRT_SCALARS_PASS')
    def properties(text):return [line for line in text.splitlines() if line.startswith('PROPERTY_')]
    native_properties=properties((native/'winrt.log').read_text(encoding='utf-8'))
    guest_properties=properties((out/'winrt/stdout.log').read_text(encoding='utf-8')) if (out/'winrt/stdout.log').exists() else []
    record('winrt-native-metadata',len(native_properties)==4 and guest_properties==native_properties)
    case('winrt-negative',fw/'guardian-framework-winrt-negative.exe','WINRT_SCALARS_PASS')
    case('winrt-nt61',fw/'guardian-framework-winrt.exe','WINRT_NT61_UNSUPPORTED_PASS','win7')
    case('version-resource',fw/'guardian-framework-version-client.exe','VERSION_RESOURCE_PASS')
    case('package-copy-boundary',fw/'guardian-framework-copy-client.exe','PACKAGE_COPY_BOUNDARY_PASS')
    for label,payload in (('cil',mono),('native',fw/'guardian-framework-version-client.exe')):
        state=None
        for mode in ('install','resume','uninstall'):
            child_marker='Hello, World!' if label=='cil' else 'VERSION_RESOURCE_PASS'
            state=case('portable-'+label+'-'+mode,payload,'PORTABLE_'+mode.upper()+'_PASS',portable=mode,state=state,also=() if mode=='uninstall' else (child_marker,))
    for name,diagnostic in (('DIVZERO','cli-arithmetic-error'),('LOOP','cli-instruction-limit'),('DEPTH','cli-quota'),('OUTPUT','cli-output-failed'),('CONTROL','cli-output-failed')):
        case('runtime-'+name.lower(),negative[name],diagnostic,success=False)
    # The phase gate must reject the earlier launcher mode before creating a VM.
    bundle=out/'cil-console/bundle';wrong=out/'wrong-mode'
    command=[str(runner),str(kernel),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(wrong),'--audio']
    result=subprocess.run(command,capture_output=True,encoding='utf-8',errors='replace',timeout=15)
    record('phase-mode-binding',result.returncode!=0 and not (wrong/'boot.json').exists())
    passed=sum(row['passed'] for row in rows);stable=source_inputs==inputs() and binary_inputs==binaries(build,a.config)
    ok=passed==len(rows) and stable
    receipt={'schema':1,'phase':'14.11','status':'PASS' if ok else 'FAIL','configuration':a.config,'tests':len(rows),'passed':passed,'stableSourcesAndBinaries':stable,'inputs':source_inputs,'binaries':binary_inputs,'earlierPhasesRevalidated':False,'hostManagedExecution':False,'sanitizer':'MSVC AddressSanitizer' if a.asan_runtime else 'not-requested'}
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'passed':passed,'total':len(rows),'stableSourcesAndBinaries':stable,'output':str(out)},ensure_ascii=False));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
