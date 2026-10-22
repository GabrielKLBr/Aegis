"""Independent phase-13 oracle over actual managed VM runs and exported reports."""
from pathlib import Path
import json,os,subprocess,sys,time

vm,kernel,output=map(Path,sys.argv[1:]);output=output.resolve();output.mkdir(parents=True,exist_ok=True)
root=output/('catalog-'+str(time.monotonic_ns()));commands=[];checks=0
canary='Guardian13-private-host-canary-9cdfa03d-1d3b-46b2'
private=output/'host-private-control.txt';private.write_text(canary)

def check(ok,message):
    global checks
    checks+=1
    if not ok:raise AssertionError(message)

def run(*args,ok=True):
    args=list(map(str,args))
    if args[0]=='create':args.extend(['--host-reserve-mib','1024'])
    env=dict(os.environ);env['GUARDIAN_TEST_PRIVATE_TOKEN']=canary
    p=subprocess.run([str(vm),'--root',str(root),*args],capture_output=True,text=True,env=env,timeout=40)
    commands.append({'args':args,'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
    check((p.returncode==0)==ok,str(args)+p.stdout+p.stderr)
    return json.loads(p.stdout) if p.returncode==0 else None

def settled(name,success=True):
    until=time.monotonic()+35
    while time.monotonic()<until:
        s=run('status',name)
        if not s['resident'] and s['state'] in ('stopped','fault'):
            check((s['state']=='stopped')==success,str(s))
            boot=json.loads((root/name/'last-boot.json').read_text())['boots'][0]
            check(boot['success']==success and not boot['timedOut'],str(boot))
            return boot
        time.sleep(.07)
    raise AssertionError('Analysis VM did not finish')

def report(name,pattern,complete=True,decision='allow'):
    data=run('analysis',name);check(data['schema']==1 and data['rulesVersion']==1,'Unversioned report')
    check(data['complete']==complete,'Coverage state disagrees with actual execution')
    check(canary not in json.dumps(data),'Host private data leaked to analysis')
    events=data['events'];check([e['sequence'] for e in events]==list(range(1,len(events)+1)),'Timeline lost or reordered an event')
    if not complete:
        check(data['dropped']>0 and any(f['classification']=='inconclusive' for f in data['findings']),'Loss was hidden');return data
    check(not data['dropped'] and not data['rejected'] and not data['pending'],'Complete report lost a request')
    pending={};results={}
    for e in events:
        if e['stage']=='intent':pending[e['operationId']]=e
        else:
            before=pending.pop(e['operationId']);check(all(before[k]==e[k] for k in ('pid','tid','actorPid','actorTid','domain','operation','resource','decision')),'Event causality/identity changed')
            if e['decision']=='deny':check(e['status']!=0,'Blocked operation claimed success')
            results[e['sequence']]=e
    check(not pending,'An operation has no result')
    for f in data['findings']:
        check(f['classification'] in ('legitimate','suspicious','confirmed','inconclusive'),'Unknown classification')
        check(all(seq in results for seq in f['evidence']),'Finding cites a missing/non-result event')
        check(f['version']==1 and f['reason'],'Untraceable rule or reason')
    bulk=[f for f in data['findings'] if f['rule']=='bulk-rewrite-rename-v1']
    check(bool(bulk)==pattern,'Incorrect bulk rewrite classification')
    for finding in bulk:
        proof=[results[seq] for seq in finding['evidence']];writes={e['resource'].lower():e for e in proof if e['operation']=='write'}
        renames=[e for e in proof if e['operation']=='rename'];check(len(writes)>=3 and len(renames)>=3,'Bulk evidence is not three distinct files')
        check(len({e['pid'] for e in proof})==1,'Bulk correlation mixed processes')
        check(max(e['observedNs'] for e in proof)-min(e['observedNs'] for e in proof)<=10_000_000_000,'Bulk correlation ignored time bound')
        for e in renames:
            w=writes[e['resource'].lower()];check(e['secondary'].endswith('.locked') and not e['status'] and not w['status'] and w['oldHash']!=w['newHash'] and w['beforeBytes']>0,'Claimed rewrite lacks real before/after data')
    network=[e for e in results.values() if e['domain']=='network'];check(network and all(e['actorPid']==e['pid'] and e['pid'] for e in network),'Application network attribution missing')
    for e in results.values():
        if e['domain']=='registry' and e['operation']=='write' and e['secondary'].lower()=='ownedstartup' and not e['status']:
            payload=b'guardian-analysis-child.exe\0';digest=14695981039346656037
            for byte in payload:digest=((digest^byte)*1099511628211)&((1<<64)-1)
            check(e['afterBytes']==len(payload) and e['newHash']==digest and e['flags']&4,'Registry diff disagrees with the real mixed-case value')
    file_results=[e for e in results.values() if e['domain']=='file' and e['operation']=='write' and e['resource'].lower() in ('c:\\a.txt','c:\\b.txt','c:\\c.txt')]
    check(file_results and all(e['decision']==decision for e in file_results),'Requested file policy was not applied')
    if decision=='deny':check(all(e['status'] and e['newHash']==e['oldHash'] and e['afterBytes']==e['beforeBytes'] for e in file_results),'Denied write changed the guest state')
    else:check(any(d['oldHash']!=d['newHash'] and d['beforeBytes'] for d in data['stateDifferences'] if d['operation']=='write'),'Applied mutations have no state differences')
    return data

proof={'status':'RUNNING','phase':13,'wine':False}
try:
    for name in ('patterns','denied','simulated','benign','overflow'):
        run('create',name,kernel,'storage')
        run('network',name,'simulated')
        if name=='denied':
            run('analysis-policy',name,'file','deny');check(run('analysis-policy',name)['denyMask']==8,'Stored file policy read-back differs')
        if name=='simulated':run('analysis-policy',name,'file','simulate')
        if name=='benign':run('analysis-policy',name,'file','allow','--scenario','benign')
        if name=='overflow':run('analysis-policy',name,'file','allow','--event-limit','16')
        run('start',name);boot=settled(name,success=name!='overflow')
        if name!='overflow':check('13 DONE\n' in boot['io']['console'],'Owned program did not complete')
        data=report(name,pattern=name in ('patterns','simulated'),complete=name!='overflow',decision={'denied':'deny','simulated':'simulate'}.get(name,'allow'))
        (output/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n')
        if name=='benign':check(any(f['classification']=='legitimate' for f in data['findings']),'Benign workload was not distinguished')
    run('analysis-export','patterns',output/'exported')
    exported=json.loads((output/'exported/analysis.json').read_text());check(exported==run('analysis','patterns'),'Export differs from recorded report')
    html=(output/'exported/analysis.html').read_text();check('Content-Security-Policy' in html and '<script' not in html.lower(),'Unsafe active report content')
    first=run('analysis','benign')['boot'];run('restart','benign');settled('benign');check(run('analysis','benign')['boot']!=first,'Boot identity was reused')
    run('create','snapshot',kernel,'storage');run('start','snapshot','--paused');run('snapshot','snapshot','before');run('clone','snapshot','branch');run('resume','snapshot');settled('snapshot');report('snapshot',True)
    run('resume','branch');settled('branch');report('branch',True)
    run('restore','snapshot','before');run('resume','snapshot');settled('snapshot');report('snapshot',True)
    run('analysis-policy','snapshot','file','deny');run('restore','snapshot','before',ok=False)
    run('create','unsupported',kernel.parent/'guardian-kernel-storage.gk','storage');run('analysis-policy','unsupported','file','deny',ok=False)
    run('analysis-policy','patterns','host-shell','allow',ok=False);run('analysis-policy','patterns','file','execute-host',ok=False)
    run('analysis-policy','patterns','file','allow','--event-limit','8193',ok=False)
    check(private.read_text()==canary,'Host control file changed')
    proof.update(status='PASS',checks=checks,scenarios=5,realManagedVMs=True,exportVerified=True,
                 independentRuleOracle=True,policyEffectsVerified=True,lossVisible=True,
                 cloneAndRestore=True,changedPolicyRejected=True,hostPrivateFileIntact=True)
    print(str(checks)+' managed VM, independent correlation, policy, export, coverage and restoration checks PASS')
except BaseException as error:
    proof.update(status='FAIL',checks=checks,error=str(error));raise
finally:
    try:
        for row in run('list'):
            if row.get('resident'):run('shutdown',row['id'])
    except BaseException:pass
    (output/'validation.json').write_text(json.dumps(proof,indent=2)+'\n')
    (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
