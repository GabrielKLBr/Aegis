"""Independent seeded-content oracle plus actual managed profile evolution."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

vm, kernel, legacy, docs, matrix_file, output = map(Path, sys.argv[1:])
vm, kernel, legacy, docs, matrix_file = [p.resolve() for p in (vm,kernel,legacy,docs,matrix_file)]
output=output.resolve();output.mkdir(parents=True,exist_ok=True)
commands=[];checks=0
canary='Guardian-private-canary-12E-lifecycle-1f7501f8-no-automatic-import'
(output/'host-personal-data.txt').write_text(canary)
root=output/('catalog-'+str(time.time_ns()))


def check(value,message):
    global checks
    checks+=1
    if not value:raise AssertionError(message)


def fnv(data):
    h=14695981039346656037
    for b in data:h=((h^b)*1099511628211)&0xffffffffffffffff
    return h


def verify_profiles(matrix):
    check(matrix['status']=='PASS' and matrix['personalCanaryAbsent'] and matrix['restores']==12,'Incomplete actual profile coverage')
    check(len(matrix['profiles'])==12,'Profile count differs')
    program=docs.read_bytes();check(program[:2]==b'MZ','Reader must be the owned PE artifact')
    program_hash=fnv(program);seen={};digests={}
    for row in matrix['profiles']:
        seed,nt=row['seed'],row['ntBuild'];check(nt in (7601,19045,22631) and seed in (0,42),'Unsupported matrix profile')
        nodes=row['nodes'];files={n['name']:n for n in nodes if n['kind']==2}
        account='lab-'+str(seed);base=133485408000000000+(seed%365)*864000000000
        expected={
            'welcome.txt':f'Guardian synthetic workspace v1\nAccount: {account}\nNT build: {nt}\nSeed: {seed}\nThis document contains no imported personal data.\n',
            'budget.csv':f'item,units\npaper,{1+seed%90}\nink,{1+(seed//7)%20}\n',
            'notes.txt':f'Synthetic notes for {account}\nEditable inside this guest.\n',
            'apps.gac':f'GuardianCatalog/1\nGuardian Docs 1\nimage=guardian-docs.exe\nbacking=immutable guest catalog\nhash64={program_hash}\nformats=.txt,.csv\n',
            'welcome.gsl':'GuardianShortcut/1\nC:\\welcome.txt\n',
            'profile.dat':f'GuardianProfile/1\nsynthetic=true\nseed={seed}\nnt={nt}\npreserve=1\ncatalog={program_hash}\n',
        }
        for name,text in expected.items():
            check(bytes.fromhex(files[name]['hex'])==text.encode('ascii'),'Generated document/catalog differs: '+name)
            stamp=base-36000000000 if name in ('welcome.txt','budget.csv','notes.txt') else base
            check(files[name]['times']==[stamp-864000000000,stamp,stamp],'Stored file dates disagree: '+name)
            check(files[name]['owner']==100,'Profile file account differs')
        history=bytes.fromhex(files['recent.dat']['hex'])
        check(len(history)==320 and int.from_bytes(history[:8],'little')==0x31545349484547 and
              int.from_bytes(history[8:12],'little')==1 and not any(history[12:]),'Initial history claims actions which did not run yet')
        lab=next(n for n in nodes if n['kind']==3 and n['name']=='lab')
        values={n['name']:n for n in nodes if n['parent']==lab['id'] and n['kind']==4}
        check(bytes.fromhex(values['user']['hex'])==account.encode()+b'\0','Account registry label differs')
        for name,value in [('userid',100),('seed',seed),('nt',nt),('version',1)]:
            check(values[name]['type']==4 and int.from_bytes(bytes.fromhex(values[name]['hex']),'little')==value,'Registry number differs: '+name)
        for name in ('docs','txthandler','csvhandler'):
            check(bytes.fromhex(values[name]['hex'])==b'guardian-docs.exe\0','Association is not the installed reader')
        check(values['cataloghash']['type']==11 and int.from_bytes(bytes.fromhex(values['cataloghash']['hex']),'little')==program_hash,'Installed executable hash differs')
        raw=b''.join(bytes.fromhex(n['hex']) for n in nodes)
        check(canary.encode() not in raw and b'password=' not in raw,'Host/private account material found in guest data')
        pair=(nt,seed);seen.setdefault(pair,set()).add(row['repeat']);digests.setdefault(pair,set()).add(row['initialHash'])
    check(len(seen)==6 and all(v=={0,1} for v in seen.values()) and all(len(v)==1 for v in digests.values()),'Seed reproduction differs')
    check(len({next(iter(v)) for v in digests.values()})==6,'Different scenario seeds/NT versions collided')


def run(*args,ok=True):
    if args[0]=='create-environment':args=(*args,'--host-reserve-mib','1024')
    env=dict(os.environ);env['GUARDIAN_TEST_PRIVATE_TOKEN']=canary
    p=subprocess.run([str(vm),'--root',str(root),*map(str,args)],env=env,capture_output=True,text=True,timeout=40)
    commands.append({'args':list(map(str,args)),'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
    check((p.returncode==0)==ok,str(args)+p.stdout+p.stderr)
    return json.loads(p.stdout) if p.returncode==0 else None


def settled(name):
    until=time.monotonic()+35
    while time.monotonic()<until:
        s=run('status',name)
        if not s['resident'] and s['state'] in ('stopped','fault'):
            check(s['state']=='stopped',str(s));b=json.loads((root/name/'last-boot.json').read_text())['boots'][0]
            check(b['success'] and not b['timedOut'],str(b));check(canary not in b['io']['console'],'Host canary reached guest console');return b
        time.sleep(.07)
    raise AssertionError('Synthetic guest did not finish')


def completed(name,visits,old_note=False):
    b=settled(name);text=b['io']['console']
    check(f'12E DONE visits={visits} opens=3\n' in text,'Wrong durable visit count')
    check(text.count('12E DOC OPEN\n')==3,'Installed program did not open all documents')
    check(('Visit 1\n' in text)==old_note,'Notes were reset or leaked across profiles')
    return b


proof={'status':'RUNNING','phase':'12E','wine':False}
try:
    matrix=json.loads(matrix_file.read_text());verify_profiles(matrix)
    corrupted=deepcopy(matrix);corrupted['profiles'][0]['nodes'][0]['owner']=0
    # A real content mutation must be detected by the independent oracle.
    file=next(n for n in corrupted['profiles'][0]['nodes'] if n['name']=='welcome.txt');file['hex']=canary.encode().hex()
    refused=False
    try:verify_profiles(corrupted)
    except AssertionError:refused=True
    check(refused,'Independent content oracle did not detect private/incorrect data')
    run('create-environment','unknown',kernel,'--seed','1','--nt','xp',ok=False)
    run('create-environment','overflow',kernel,'--seed','4294967296',ok=False)
    run('create-environment','old',legacy,'--seed','1',ok=False)
    run('create-environment','import',kernel,'--base',output/'host-personal-data.txt',ok=False)
    run('create-environment','lab',kernel,'--seed','42','--nt','win7','--ram-mib','32')
    config=run('environment','lab');check(config['synthetic'] and not config['hostImports'] and config['account']=='lab-42','Profile reports imported/incorrect identity')
    run('hardware-config','lab','--nt','win11',ok=False)
    run('start','lab');first=completed('lab',1)
    run('restart','lab');second=completed('lab',2,True)
    check(first['bootId']!=second['bootId'],'Cold reboot reused boot identity')
    run('start','lab','--paused');run('snapshot','lab','evolved');run('clone','lab','branch')
    run('resume','lab');completed('lab',3,True)
    run('resume','branch');completed('branch',3,True)
    run('restore','lab','evolved');run('resume','lab');completed('lab',3,True)
    run('create-environment','temporary',kernel,'--seed','7','--nt','win11','--disposable')
    for _ in range(2):
        run('start','temporary');completed('temporary',1)
    check((output/'host-personal-data.txt').read_text()==canary,'Host personal canary was modified')
    proof.update(status='PASS',checks=checks,profiles=12,scenarioPairs=6,seedReproduction=True,
                 documentsOpened=True,executableCatalogVerified=True,persistentEvolution=True,
                 disposableReset=True,cloneAndRestore=True,personalCanaryAbsent=True,
                 readerSha256=hashlib.sha256(docs.read_bytes()).hexdigest())
    print(str(checks)+' independent profile/oracle, actual document execution, persistence, clone, restore and host privacy checks PASS')
except BaseException as error:
    proof.update(status='FAIL',checks=checks,error=str(error));raise
finally:
    try:
        for s in run('list'):
            if s.get('resident'):run('shutdown',s['id'])
    except BaseException:pass
    (output/'lifecycle.json').write_text(json.dumps(proof,indent=2)+'\n')
    (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
