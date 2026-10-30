"""14.13 only: scanner handoff, runtime identity, attributed effects and recovery."""
from pathlib import Path
import argparse,json,os,shutil,struct,subprocess,sys
import nt_compatibility as nt,unified_runtime as runtime,validate_frameworks as snapshots
import compat32_corpus
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'test'))
from NativeUserIntegrationTests import own_helper
EXPECTED=23
def binaries(build,config):
    files=[build/config/(name+'.exe') for name in ('guardian-runtime','guardian-scan','guardian-scan-worker','guardian-engine-service','guardian-native-user-host','guardian-audio-host','guardian-whp-memory','guardian-cli-inspect','guardian-checkpoint-tests')]
    files+=[build/'guest'/config/('guardian-kernel-'+name+'.gk') for name in ('compat32','native-user32','frameworks')]
    for group in ('frameworks','audio','desktop','crt','security','components','internet','native-user32','compat32'):files+=list((build/'guest'/group/config).glob('*.dll'))
    files+=list((build/'guest/compat32'/config).glob('*.lib'))
    files+=[build/'guest/compat32'/config/('guardian-x86-'+mode+'.exe') for mode in ('console','input')]
    files+=[build/'guest/unified'/config/'guardian-unified-probe.exe',build/'guest/frameworks'/config/'guardian-framework-winrt.exe']
    return {p.relative_to(build).as_posix():snapshots.sha(p) for p in sorted(files)}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','output','reference','schema-reference','corpus','asan-runtime'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True);a=p.parse_args();build=a.build.resolve();out=a.output.resolve();nt.require(not out.exists(),'Fresh output required');out.mkdir(parents=True);inputs=snapshots.inputs();artifacts=binaries(build,a.config);rows=[]
    def record(name,ok,**fields):
        rows.append({'case':name,'passed':bool(ok),**fields});(out/'results.json').write_bytes(nt.canonical(rows));print(json.dumps(rows[-1]),flush=True)
    registry=out/'registry';apps={'console':build/'guest/compat32'/a.config/'guardian-x86-console.exe','gui32':build/'guest/compat32'/a.config/'guardian-x86-input.exe','gui64':build/'guest/unified'/a.config/'guardian-unified-probe.exe','framework':build/'guest/frameworks'/a.config/'guardian-framework-winrt.exe'}
    external=compat32_corpus.compile(compat32_corpus.source(a.corpus),out/'external',build/'guest/compat32'/a.config);apps['external']=external
    for name,app in apps.items():runtime.prepare(build,a.config,registry,app,out/('prepare-'+name),a.reference,a.schema_reference,'user32' if name=='gui64' else 'auto')
    record('hash-bound-enrollment',len(json.loads((registry/'index.json').read_bytes())['applications'])==5)
    env=os.environ.copy();env['PATH']=str(a.asan_runtime)+os.pathsep+env.get('PATH','');env['ASAN_OPTIONS']='halt_on_error=1:symbolize=0'
    parser=subprocess.run([str(build/a.config/'guardian-checkpoint-tests.exe')],capture_output=True,env=env,encoding='utf-8',errors='replace',timeout=60);(out/'parser.log').write_text(parser.stdout+parser.stderr,encoding='utf-8');record('native-state-verifier-ASAN',parser.returncode==0,output=parser.stdout.strip())
    def execute(name,app='console',extra=(),reg=None,success=True,input=False,marker=None):
        command=[str(build/a.config/'guardian-runtime.exe'),'--runtime-root',str(reg or registry),'--input',str(apps[app]),'--output',str(out/name),*map(str,extra)]
        proc=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        try:
            if input:own_helper(proc.pid,'input')
            stdout,stderr=proc.communicate(timeout=45)
        finally:
            if proc.poll() is None:proc.kill();proc.communicate()
        (out/(name+'.log')).write_text(stdout+stderr,encoding='utf-8');result=json.loads(stdout);ok=result['complete']==success and not result['inputExecutedOnHost'] and result['backend']=='MiniNT-unified-14.13'
        if success:
            boot=json.loads((out/name/'boot.json').read_bytes())['boots'][0];trace=json.loads((out/name/'analysis.json').read_bytes());ok=ok and proc.returncode==0 and trace['complete'] and all(boot['processes'][k]==0 for k in ('liveProcesses','liveObjects','liveHandles','liveReferences'))
            if marker:ok=ok and marker in result['console']
        else:ok=ok and result['classification']=='inconclusive'
        record(name,ok,diagnostic=result.get('error',''),restored=result.get('restored',False));return result
    execute('x86-console',marker='X86_ACCEPTANCE_PASS');execute('independent-C-console','external',marker='Hello World');execute('framework-family','framework')
    for app in ('gui32','gui64'):
        pause=execute('checkpoint-'+app,app,('--checkpoint-after','12'),success=False)
        nt.require(pause.get('checkpointCreated'),'Checkpoint creation failed')
        restored=execute('restore-'+app,app,('--restore',out/('checkpoint-'+app)/'recovery.json'),input=True,marker='X86_ACCEPTANCE_PASS' if app=='gui32' else 'UNIFIED_RECOVERY_PASS')
        if app=='gui64':
            events=restored['events'];record('file-registry-actor-continuity',all(any(e['domain']==d and e['stage']=='result' and e['operation']==op and not e['status'] and e['actorPid'] and e['actorTid'] for e in events) for d,op in (('file','write'),('file','read'),('registry','write'),('registry','read'))))
    config={'schema':1,'state':str(out/'scanner-state'),'offline':True,'queueUnknown':True,'sandboxRuntime':str(registry),'limits':{'milliseconds':15000}};cfg=out/'scanner.json';cfg.write_bytes(nt.canonical(config))
    def scan(args):
        result=subprocess.run([str(build/a.config/'guardian-scan.exe'),*map(str,args),'--config',str(cfg),'--json'],capture_output=True,encoding='utf-8',errors='replace',timeout=40);return result,json.loads(result.stdout)
    scan(('scan',apps['console']));process,result=scan(('sandbox-next',));record('scanner-queue-to-owned-kernel',process.returncode==0 and result.get('complete') and result.get('backend')=='MiniNT-unified-14.13' and 'X86_ACCEPTANCE_PASS' in result.get('console',''))
    config['state']=str(out/'scanner-unconfigured');config.pop('sandboxRuntime');cfg.write_bytes(nt.canonical(config));scan(('scan',apps['console']));process,result=scan(('sandbox-next',));record('scanner-no-legacy-fallback',process.returncode==3 and result.get('classification')=='inconclusive' and result.get('backend')=='MiniNT-unified-14.13')
    key=nt.sha(apps['console'].read_bytes())
    def altered(name,change):
        destination=out/('registry-'+name);shutil.copytree(registry,destination);index=json.loads((destination/'index.json').read_bytes());change(index['applications'][key],destination);(destination/'index.json').write_bytes(nt.canonical(index));return destination
    bad=altered('profile',lambda r,d:r.update(profile='win11'));execute('profile-swap-refused',reg=bad,success=False)
    bad=altered('architecture',lambda r,d:r.update(architecture='x64'));execute('architecture-swap-refused',reg=bad,success=False)
    def corrupt(record,folder):
        file=folder/record['volume'];raw=bytearray(file.read_bytes());raw[-1]^=1;file.write_bytes(raw)
    bad=altered('volume',corrupt);execute('volume-tamper-refused',reg=bad,success=False)
    def rogue(record,folder):
        helper=folder/record['helper'];helper.write_bytes((build/a.config/'guardian-runtime.exe').read_bytes());record['files'][record['helper']]=nt.sha(helper.read_bytes());mf=folder/record['manifest'];m=json.loads(mf.read_bytes());m['nativeGuiRuntime']['helperSha256']=nt.sha(helper.read_bytes());mf.write_bytes(nt.canonical(m));record['files'][record['manifest']]=nt.sha(mf.read_bytes());bf=folder/record['binding'];raw=bytearray(bf.read_bytes());raw[48:80]=bytes.fromhex(nt.sha(mf.read_bytes()));bf.write_bytes(raw);record['files'][record['binding']]=nt.sha(raw)
    bad=altered('host-helper',rogue);r=execute('external-host-executable-refused',reg=bad,success=False);nt.require('trusted helper' in r.get('error',''),'Untrusted helper refusal missing')
    receipt=out/'checkpoint-gui32/recovery.json'
    execute('recovery-other-input-refused','console',('--restore',receipt),success=False)
    execute('recovery-policy-swap-refused','gui32',('--restore',receipt,'--deny','4'),success=False)
    corrupted=json.loads(receipt.read_bytes());corrupted['snapshotSha256']='0'*64;bad=receipt.with_name('corrupt.json');bad.write_bytes(nt.canonical(corrupted));execute('recovery-corruption-refused','gui32',('--restore',bad),success=False)
    wrong=json.loads(receipt.read_bytes());wrong['controllerSha256']='0'*64;bad=receipt.with_name('controller.json');bad.write_bytes(nt.canonical(wrong));execute('recovery-controller-swap-refused','gui32',('--restore',bad),success=False)
    execute('deadline-inconclusive','gui32',('--milliseconds','1000'),success=False)
    denied=execute('module-policy-enforced',extra=('--deny','4'),success=False);nt.require(not denied['loaded'] and not denied['ran'],'Denied image must not be reported as loaded or executed')
    execute('unsupported-GDI-checkpoint-refused','framework',('--checkpoint-after','12'),success=False)
    stable=inputs==snapshots.inputs() and artifacts==binaries(build,a.config);passed=sum(r['passed'] for r in rows);ok=passed==len(rows)==EXPECTED and stable
    proof={'phase':'14.13','scope':'unified-handoff-and-basic-user32-recovery','configuration':a.config,'status':'PASS' if ok else 'FAIL','tests':len(rows),'passed':passed,'stableSourcesAndBinaries':stable,'inputs':inputs,'binaries':artifacts,'earlierPhasesRevalidated':False,'hostInputExecution':False,'liveGdiAudioSnapshots':False,'hotServiceUpdateTested':False}
    (out/'validation.json').write_bytes(nt.canonical(proof));print(json.dumps({'status':proof['status'],'passed':passed,'tests':len(rows),'stable':stable}));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
