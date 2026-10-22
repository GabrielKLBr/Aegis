"""Fresh native builds, real KVM boots and application/persistence audits.

Does not declare phase 8 complete or replace the native Windows oracles.
Requires /dev/kvm with NX and the native LLVM toolchain used by build_guest_linux.
"""
import datetime
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
from validate_linux_native import ROOT, require, sha, source_hashes, tests, elf

WORKLOADS = [('basic','basic'),('cpu','cpu'),('memory','memory'),('threads','threads'),
             ('processes','processes'),('io','io'),('storage','storage'),
             ('loader','storage'),('runtime','storage'),('lifetime','storage'),('complete','storage')]
MARKERS = {
    'loader':['PE loader mapping checks PASS','PE console TLS DLL execution PASS'],
    'runtime':['PE loader mapping checks PASS','PE console TLS DLL execution PASS',
        'PE private TEB TLS thread lifecycle contention PASS','PE runtime services heap locks PASS','PE RUNTIME OK'],
    'lifetime':['PE loader mapping checks PASS','PE console TLS DLL execution PASS',
        'PE child creation rollback 10 fault budgets PASS','PE private TEB TLS thread lifecycle contention PASS',
        'PE x64 virtual stack capture PASS','PE child processes explicit exits last-thread lifetime PASS',
        'PE runtime services heap locks PASS','PE RUNTIME OK']}

MARKERS['complete']=MARKERS['lifetime']+['PE x64 exception dispatch recovery nonlocal unwind PASS','PE dynamic DLL TLS references rollback unload PASS']

def main():
    require(platform.system()=='Linux' and platform.machine().lower() in ('x86_64','amd64'),'Linux x86-64 required')
    root = ROOT/'build-linux-kvm'/('validation-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S-%f'))
    root.mkdir(parents=True,exist_ok=False)
    traps=root/'sentinels';traps.mkdir();sentinel=traps/'invoked'
    for name in ['wine','wine64','cmd.exe','powershell.exe','qemu-system-x86_64']:
        path=traps/name;path.write_text('#!/bin/sh\nprintf used >> "'+str(sentinel)+'"\nexit 97\n');path.chmod(0o700)
    environment=dict(os.environ, PATH=str(traps)+os.pathsep+os.environ.get('PATH',''),GUARDIAN_BUILD_JOBS='4')
    def run(command, log, env=None, expected=0):
        p=subprocess.run(list(map(str,command)),cwd=ROOT,env=env or environment,capture_output=True,text=True,timeout=240)
        log.write_text(json.dumps(list(map(str,command)))+'\n'+p.stdout+p.stderr)
        require(p.returncode==expected,'Unexpected exit '+str(p.returncode)+'; see '+str(log))
        return p
    before=source_hashes()
    report={'status':'running','phase':8,'phase_status':'in_progress','next_phase_started':False,
        'platform':platform.platform(),'backend':'KVM','wine_used':False,'qemu_used':False,
        'windows_validation':'not_run','second_stage_execute_protection':False,
        'sources':before,'configurations':[],'boots':[]}
    print('Evidence: '+str(root),flush=True)
    try:
        id=0
        for configuration in ['Debug','Release']:
            print('Building guest and ELF host: '+configuration,flush=True)
            guest=root/('guest-'+configuration)
            run([sys.executable,ROOT/'tools/build_guest_linux.py','--config',configuration,'--output',guest],root/(configuration+'-guest.log'))
            host=root/('host-'+configuration)
            env=dict(environment,GUARDIAN_LINUX_BUILD_DIR=str(host),GUARDIAN_TEST_KVM='ON',GUARDIAN_KVM_GUEST_DIR=str(guest))
            run(['bash',ROOT/'Build-Linux.sh',configuration],root/(configuration+'-host.log'),env)
            row={'configuration':configuration,'tests':tests(host/'tests.xml',20),
                 'guest':json.loads((guest/'build-manifest.json').read_text()),
                 'artifacts':[elf(p) for p in sorted(host.iterdir()) if p.is_file() and (p.name in ('guardianav','guardian-kvm') or p.name.endswith('_tests'))]}
            capabilities=run([host/'guardianav','--capabilities'],root/(configuration+'-capabilities.log'))
            row['capabilities']=json.loads(capabilities.stdout)
            require(row['capabilities']['vmAvailable'] and row['capabilities']['vmBackend']=='KVM','KVM unavailable')
            for name,contract in WORKLOADS:
                for ram in [16,32,64]:
                    disk=root/(configuration+'-'+name+'-'+str(ram))
                    previous=None
                    for iteration in range(2):
                        id+=1;tag=configuration+'-'+name+'-'+str(ram)+'-'+str(iteration)
                        target=guest/('guardian-kernel'+('' if name=='basic' else '-'+name)+'.gk')
                        output=root/(tag+'.json')
                        command=[host/'guardianav','boot-kernel',target,contract,output,'--ram-mib',ram,'--boot-id',id]
                        if contract=='storage':
                            command+=['--disk',disk.with_suffix('.gvd'),disk.with_suffix('.gvo'),'--create-disk']
                        run(command,root/(tag+'.log'))
                        boot=json.loads(output.read_text())['boots'][0]
                        require(boot['success'] and not boot['timedOut'] and not boot['error'] and boot['cpl']==0,'Guest did not complete')
                        for marker in MARKERS.get(name,[]):
                            require(marker in boot['console']+boot.get('io',{}).get('console',''),'Missing application evidence: '+marker)
                        if contract=='storage':
                            require(sha(disk.with_suffix('.gvd'))==hashlib.sha256(bytes(65536)).hexdigest(),'Base disk was modified')
                            if previous is not None:
                                require(boot['storage']['recovered']==1 and boot['storage']['sequenceBefore']==previous['storage']['sequenceAfter'],
                                        'Disk state did not survive reboot')
                                require(boot['storage']['hostRecords']>previous['storage']['hostRecords'],'Overlay did not advance')
                        previous=boot
                        report['boots'].append({'configuration':configuration,'workload':name,'ram_mib':ram,'iteration':iteration,
                            'report':output.relative_to(ROOT).as_posix(),'sha256':sha(output),'success':True})
                    print(configuration+' '+name+' '+str(ram)+' MiB: 2 boots PASS',flush=True)
            # Fatal/IST kernels must fail the normal-success predicate and
            # publish their fatal guest report, rather than appearing clean.
            for name in ['cpu-fatal','cpu-double-fault']:
                output=root/(configuration+'-'+name+'.json')
                run([host/'guardian-kvm',guest/('guardian-kernel-'+name+'.gk'),'cpu',output],root/(configuration+'-'+name+'.log'),expected=3)
                boot=json.loads(output.read_text())['boots'][0]
                require(not boot['success'] and boot['cpuReportValid'] and boot['cpu']['fatal']==1 and not boot['timedOut'],'Fatal/IST refusal was not observed')
                row.setdefault('negative_guests',[]).append({'name':name,'report':output.relative_to(ROOT).as_posix(),'sha256':sha(output)})
            report['configurations'].append(row)
        # Sanitizers cover both the portable algorithms and host device code;
        # their real-guest test uses the already freshly built Release images.
        host=root/'host-Sanitize';env=dict(environment,GUARDIAN_LINUX_BUILD_DIR=str(host),GUARDIAN_TEST_KVM='ON',GUARDIAN_KVM_GUEST_DIR=str(root/'guest-Release'))
        run(['bash',ROOT/'Build-Linux.sh','Sanitize'],root/'Sanitize-host.log',env)
        report['sanitizers']={'tests':tests(host/'tests.xml',20),'artifacts':[elf(host/'guardianav'),elf(host/'guardian-kvm')]}
        require(len(report['boots'])==132,'Incomplete boot matrix')
        require(not sentinel.exists(),'A prohibited compatibility/emulation program was invoked')
        require(before==source_hashes(),'Sources changed during verification')
        report['status']='passed'
    except BaseException as exc:
        report['status']='failed';report['error']=str(exc);raise
    finally:
        report['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
        (root/'validation.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    print('132 real KVM boots, 4 fatal/IST refusals and 60 CTest executions PASS. Native Windows validation pending. '+str(root),flush=True)
    return root

if __name__=='__main__':
    main()
