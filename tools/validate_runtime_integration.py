"""14.13A only: automatic packages and the runtime's current UI/service flow.

Real-program incompatibility is recorded, never counted as a compatible run.
No selected EXE is launched on Windows. Only owned controllers/helpers run there.
"""
from pathlib import Path
import argparse,json,os,shutil,struct,subprocess,sys
import nt_compatibility as nt
import provision_runtime,validate_unified as original,validate_frameworks as frozen

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'test'))
from NativeUserIntegrationTests import own_helper
EXPECTED=20

def binaries(build,config):
    files=original.binaries(build,config)
    for name in ('avui.exe','guardian-automatic-tests.exe','engine_services_tests.exe',
                 'guardian-service-sample.exe','guardian-desktop.exe','guardian-kernel-audio.gk'):
        path=build/config/name;files[path.relative_to(build).as_posix()]=frozen.sha(path)
    path=build/'guest/crt'/config/'guardian-crt.exe';files[path.relative_to(build).as_posix()]=frozen.sha(path)
    return dict(sorted(files.items()))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output','reference','schema-reference','asan-runtime','external-console'):
        parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--real-program',type=Path,action='append',default=[])
    parser.add_argument('--config',choices=('Release','Debug'),required=True)
    a=parser.parse_args();build=a.build.resolve();out=a.output.resolve()
    nt.require(not out.exists(),'Fresh validation output required');out.mkdir(parents=True)
    sources=frozen.inputs();artifacts=binaries(build,a.config);rows=[];registry=out/'runtime'
    def record(name,ok,**extra):
        row={'case':name,'passed':bool(ok),**extra};rows.append(row)
        (out/'results.json').write_bytes(nt.canonical(rows));print(json.dumps(row),flush=True)
    provision_runtime.provision(build,a.config,registry,out/'provision',a.reference,a.schema_reference)
    index=json.loads((registry/'index.json').read_bytes());before={str(p.relative_to(registry)):frozen.sha(p) for p in registry.rglob('*') if p.is_file()}
    record('local-templates-provisioned',len(index['automaticTemplates'])==3)
    env=os.environ.copy();env['PATH']=str(a.asan_runtime)+os.pathsep+env.get('PATH','');env['ASAN_OPTIONS']='halt_on_error=1:symbolize=0'
    logs=[];asan=True
    for key in index['automaticTemplates']:
        row=index['applications'][key];manifest=json.loads((registry/row['manifest']).read_bytes())
        root=manifest['files'][0]['path'].rsplit('\\',1)[-1]
        group='compat32' if row['architecture']=='x86' else 'unified' if row['mode']=='user32' else 'frameworks'
        result=subprocess.run([str(build/a.config/'guardian-automatic-tests.exe'),str(build/'guest'/group/a.config/root),str(registry/row['volume'])],env=env,capture_output=True,timeout=60)
        logs.append(result.stdout+result.stderr);asan&=result.returncode==0
    (out/'parser.log').write_bytes(b'\n'.join(logs));record('PE-parser-rebinding-ASAN',asan,mutations=1500,truncations=192)
    result=subprocess.run([str(build/a.config/'engine_services_tests.exe')],capture_output=True,timeout=20)
    (out/'protocol.log').write_bytes(result.stdout+result.stderr);record('progress-recovery-protocol',result.returncode==0)
    def execute(name,app,extra=(),reg=registry,success=None,interact=False,close=False):
        command=[str(build/a.config/'guardian-runtime.exe'),'--input',str(app),'--runtime-root',str(reg),'--output',str(out/name),'--milliseconds','8000',*map(str,extra)]
        process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            if interact:own_helper(process.pid,'input')
            if close:
                try:own_helper(process.pid,'close','guardian-audio-host.exe')
                except AssertionError:pass
            stdout,stderr=process.communicate(timeout=35)
        finally:
            if process.poll() is None:process.kill();process.communicate()
        (out/(name+'.log')).write_bytes(stdout+stderr);report=json.loads(stdout)
        ok=not report['inputExecutedOnHost'] and (report['complete']==success if success is not None else True)
        return report,ok
    console=build/'guest/compat32'/a.config/'guardian-x86-console.exe'
    report,ok=execute('automatic-x86-console',console,success=True)
    record('automatic-x86-console',ok and report.get('automaticPackage') and 'X86_ACCEPTANCE_PASS' in report.get('console',''))
    report,ok=execute('automatic-x64-CRT',build/'guest/crt'/a.config/'guardian-crt.exe',success=True)
    record('automatic-x64-CRT',ok and report.get('automaticPackage'),diagnostic=report.get('error',''))
    # A derived owned probe exercises rebinding instead of exact-hash enrollment.
    clone=out/'new-owned-GUI.exe';raw=bytearray((build/'guest/unified'/a.config/'guardian-unified-probe.exe').read_bytes());raw[struct.unpack_from('<I',raw,60)[0]+8]^=0x5A;clone.write_bytes(raw)
    report,ok=execute('automatic-checkpoint',clone,('--checkpoint-after','12'))
    record('automatic-USER32-checkpoint',ok and report.get('automaticPackage') and report.get('checkpointCreated') and not report['complete'],diagnostic=report.get('error',''))
    receipt=out/'automatic-checkpoint/recovery.json'
    report,ok=execute('automatic-restore',clone,('--restore',receipt),success=True,interact=True)
    record('automatic-USER32-restore',ok and report.get('restored') and 'UNIFIED_RECOVERY_PASS' in report.get('console',''))
    record('templates-unchanged',before=={str(p.relative_to(registry)):frozen.sha(p) for p in registry.rglob('*') if p.is_file()})
    def altered(name,change):
        target=out/name;shutil.copytree(registry,target);idx=json.loads((target/'index.json').read_bytes());change(idx,target);(target/'index.json').write_bytes(nt.canonical(idx));return target
    bad=altered('no-templates',lambda i,p:i.pop('automaticTemplates'))
    report,ok=execute('no-host-fallback',console,reg=bad,success=False);record('unconfigured-fails-closed',ok and not report['ran'])
    def corrupt(i,p):
        key=next(k for k,v in i['applications'].items() if v['architecture']=='x86');file=p/i['applications'][key]['volume'];b=bytearray(file.read_bytes());b[-1]^=1;file.write_bytes(b)
    bad=altered('corrupt-template',corrupt);report,ok=execute('template-integrity',console,reg=bad,success=False);record('template-integrity',ok and 'SHA-256' in report.get('error',''))
    def traversal(i,p):
        for row in i['applications'].values():row['volume']='../index.json'
    bad=altered('traversal-template',traversal);report,ok=execute('template-path',console,reg=bad,success=False);record('template-path-traversal-refused',ok and not report['ran'])
    report,ok=execute('GDI-checkpoint',build/'guest/frameworks'/a.config/'guardian-framework-winrt.exe',('--checkpoint-after','12'),success=False);record('unsupported-GDI-checkpoint-refused',ok and 'unsupported' in report.get('error',''))
    report,ok=execute('restore-policy',clone,('--restore',receipt,'--deny','4'),success=False);record('recovery-policy-binding',ok and 'mismatch' in report.get('error',''))
    wrong=json.loads(receipt.read_bytes());wrong['snapshotSha256']='0'*64;badReceipt=receipt.with_name('corrupt.json');badReceipt.write_bytes(nt.canonical(wrong))
    report,ok=execute('restore-corrupt',clone,('--restore',badReceipt),success=False);record('recovery-snapshot-integrity',ok and 'SHA-256' in report.get('error',''))
    report,ok=execute('deadline',clone,('--milliseconds','1000'),success=False);record('deadline-inconclusive',ok and report['classification']=='inconclusive')
    config=out/'scanner.json';config.write_bytes(nt.canonical({'schema':1,'state':str(out/'scanner-state'),'offline':True,'queueUnknown':True,'sandboxRuntime':str(registry),'limits':{'milliseconds':15000}}))
    for args in (('scan',console),('sandbox-next',)):
        process=subprocess.run([str(build/a.config/'guardian-scan.exe'),*map(str,args),'--config',str(config),'--json'],capture_output=True,timeout=35)
    report=json.loads(process.stdout);record('scanner-queue-auto-package',process.returncode==0 and report.get('automaticPackage') and report.get('complete'))
    report,ok=execute('independent-console',a.external_console,success=True);record('independent-C-console',ok and 'Hello World' in report.get('console',''),sha256=frozen.sha(a.external_console))
    nt.require(len(a.real_program)==3,'Provide exactly three local harmless programs for this acceptance lot')
    for app in a.real_program:
        report,ok=execute('real-'+app.stem,app,close=app.name.lower()=='flappy.exe')
        supported=report['complete'];diagnosed=supported or (report['classification']=='inconclusive' and bool(report.get('error')) and bool(report['issues']))
        record('real-program-'+app.name,ok and diagnosed,compatible=supported,sha256=frozen.sha(app),missingImports=report['unresolved'])
    stable=sources==frozen.inputs() and artifacts==binaries(build,a.config);passed=sum(row['passed'] for row in rows)
    proof={'phase':'14.13A','scope':'automatic-packages-current-runtime-integration','configuration':a.config,'status':'PASS' if len(rows)==passed==EXPECTED and stable else 'FAIL','tests':len(rows),'passed':passed,'stableSourcesAndBinaries':stable,'inputs':sources,'binaries':artifacts,'earlierPhasesRevalidated':False,'hostInputExecution':False}
    (out/'validation.json').write_bytes(nt.canonical(proof));print(json.dumps({k:v for k,v in proof.items() if k not in ('inputs','binaries')}));return 0 if proof['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
