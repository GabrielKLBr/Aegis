"""Frozen, independent-program compatibility measurements for phase 14.14.

Inputs are data. Only owned controllers and helpers execute on Windows.
Unsupported profiles and unverified workflows never become compatible results.
"""
from pathlib import Path
from collections import Counter
import argparse,ctypes,hashlib,json,re,shutil,struct,subprocess,time
import provision_runtime
import validate_runtime_integration as runtime_build
import validate_frameworks as sources

ROOT=Path(__file__).resolve().parents[1]
OUTCOMES=('functional','partial','incompatible','not-evaluated')

def require(ok,message):
    if not ok:raise ValueError(message)

def canonical(value):return json.dumps(value,ensure_ascii=True,sort_keys=True,indent=2,allow_nan=False).encode()+b'\n'
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1<<20),b''):h.update(block)
    return h.hexdigest()
def identity(value):return hashlib.sha256(canonical(value)).hexdigest()
def write(path,value):Path(path).write_bytes(canonical(value))
def ordinary(path):
    path=Path(path).absolute()
    for part in (path,*path.parents):
        require(not part.is_symlink() and not getattr(part,'is_junction',lambda:False)(),'Corpus links/reparse points rejected')
    require(path.is_file() and path.stat().st_size<=512<<20,'Corpus member missing or too large')
    return path

def architecture(path):
    """Inventory only, without loading the PE or guessing AnyCPU as native x86."""
    with Path(path).open('rb') as f:
        data=f.read(65536);require(len(data)>=64 and data[:2]==b'MZ','Corpus PE required')
        at=struct.unpack_from('<I',data,60)[0];require(at+264<=len(data) and data[at:at+4]==b'PE\0\0','Corpus PE header quota')
        machine=struct.unpack_from('<H',data,at+4)[0];require(machine in (0x14C,0x8664),'Unsupported corpus architecture')
        opt=at+24;magic=struct.unpack_from('<H',data,opt)[0];x86=machine==0x14C
        require(magic==(0x10B if x86 else 0x20B),'Corpus optional header mismatch')
        clr=struct.unpack_from('<I',data,opt+(96 if x86 else 112)+14*8)[0]
        if clr and x86:
            count=struct.unpack_from('<H',data,at+6)[0];table=opt+struct.unpack_from('<H',data,at+20)[0]
            require(count<=96 and table+count*40<=len(data),'Corpus section quota')
            for i in range(count):
                s=table+i*40;size,rva,raw_size,raw=struct.unpack_from('<4I',data,s+8)
                if rva<=clr and clr-rva+20<=raw_size:
                    offset=raw+clr-rva;require(offset+20<=Path(path).stat().st_size,'Corpus CLR bounds');f.seek(offset+16);flags=struct.unpack('<I',f.read(4))[0]
                    if flags&1 and not flags&2:return 'anycpu'
                    break
    return 'x86' if x86 else 'x64'

def file_version(path):
    # Read a version resource as data; do not start the selected application.
    v=ctypes.WinDLL('version',use_last_error=True)
    v.GetFileVersionInfoSizeW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p];v.GetFileVersionInfoSizeW.restype=ctypes.c_uint32
    v.GetFileVersionInfoW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
    v.VerQueryValueW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint)]
    size=v.GetFileVersionInfoSizeW(str(path),None)
    if not size:return {'value':None,'method':'no-version-resource; SHA-256 is the exact identity'}
    require(size<=4<<20,'Version resource quota');buffer=ctypes.create_string_buffer(size)
    require(v.GetFileVersionInfoW(str(path),0,size,buffer),'Read version resource failed')
    pointer=ctypes.c_void_p();length=ctypes.c_uint()
    require(v.VerQueryValueW(buffer,'\\',ctypes.byref(pointer),ctypes.byref(length)) and length.value>=52,'Version fixed resource missing')
    values=struct.unpack('<13I',ctypes.string_at(pointer,52));require(values[0]==0xFEEF04BD,'Version signature rejected')
    return {'value':'.'.join(map(str,(values[2]>>16,values[2]&65535,values[3]>>16,values[3]&65535))),'method':'VS_FIXEDFILEINFO, self-reported metadata'}

def validate_plan(plan):
    require(plan.get('schema')==1 and plan.get('phase')=='14.14','Campaign schema rejected')
    policy=plan['policy'];apps=plan['programs'];require(identity(policy)==plan['policySha256'],'Campaign policy hash mismatch')
    ids=[a['id'] for a in apps];keys=[a['independenceKey'] for a in apps];hashes=[a['sha256'] for a in apps]
    require(len(set(ids))==len(ids)==len(set(keys))==len(set(hashes)),'Duplicate application, version or executable')
    require(len(apps)>=policy['minimumIndependentPrograms'],'Independent program count below policy')
    counts=Counter(a['category'] for a in apps)
    require(set(counts)==set(policy['categories']),'Corpus categories differ from policy')
    for category,n in policy['categories'].items():require(counts[category]>=n,'Corpus category below policy: '+category)
    for arch in policy['architecture']:require(sum(a['architecture']==arch for a in apps)>=policy['minimumProgramsPerArchitecture'],'Native architecture quota not met: '+arch)
    for app in apps:
        require(re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}',app['id']) and app.get('ownedFixture') is False,'Owned fixture or unsafe corpus id')
        require(re.fullmatch('[0-9a-f]{64}',app['sha256']) and app['bytes']>0,'Corpus identity missing')
        require(app['scenario']['steps'] and app['scenario']['successCriteria'] and app['source'],'Scenario/source missing')
    return plan

def freeze(selection,bindings,policy,output,cache):
    require(not output.exists() and not cache.exists(),'Fresh corpus freeze required')
    mapping=json.loads(bindings.read_bytes());chosen=json.loads(selection.read_bytes());rules=json.loads(policy.read_bytes());cache.mkdir(parents=True)
    apps=[]
    for row in chosen['programs']:
        path=ordinary(mapping[row['id']]);before=digest(path);target=cache/row['id'];target.mkdir();destination=target/'input.exe';shutil.copyfile(path,destination)
        require(before==digest(destination)==digest(path),'Input changed during freeze')
        apps.append({**row,'sha256':before,'bytes':destination.stat().st_size,'architecture':architecture(destination),'version':file_version(path)})
    plan={'schema':1,'phase':'14.14','selectionSha256':digest(selection),'policy':rules,'policySha256':identity(rules),'sampling':'convenience sample of installed software; not representative of all Windows software','programs':apps,'inputExecutedOnHost':False,'redistributionOfInputs':False}
    validate_plan(plan);write(output,plan);write(cache/'freeze.json',{'planSha256':identity(plan),'policySha256':identity(rules),'programs':{a['id']:a['sha256'] for a in apps}})
    return {'status':'FROZEN','programs':len(apps),'planSha256':identity(plan),'architectures':dict(Counter(a['architecture'] for a in apps))}

def classify(app,report):
    require(report.get('inputExecutedOnHost') is False and report.get('type')=='sandbox','Runtime identity or host-execution invariant missing')
    require(isinstance(report.get('complete'),bool),'Runtime complete flag missing')
    error=report.get('error','')
    if 'File limit' in error or 'size limit' in error or app['bytes']>8<<20:return 'incompatible','input-size-limit'
    # Capacity/setup faults are infrastructure failures, not application incompatibility.
    for token in ('bad allocation','WHP','hypervisor','Cannot lock','trusted helper','SHA-256','New runtime session','Invalid boot timeout'):
        require(token.lower() not in error.lower(),'Campaign infrastructure failure: '+error)

    if report['complete'] and app['scenario']['oracle']=='hello-world-console-v1' and report.get('exitCode')==0 and not report.get('faulted') and any(line.strip()=='Hello World' for line in report.get('console','').splitlines()):
        return 'functional','workflow-observed'
    if report.get('loaded') or report.get('ran'):return 'partial','workflow-not-completed-or-not-verified'
    if report.get('unresolved'):return 'incompatible','missing-imports-in-template'
    require(report.get('error'),'Unexplained runtime result')
    return 'incompatible','loader-or-kernel-rejection'

def metrics(process):
    from ctypes import wintypes as w
    class Memory(ctypes.Structure):
        _fields_=[('cb',w.DWORD),('faults',w.DWORD)]+[(n,ctypes.c_size_t) for n in ('peakWorkingSet','workingSet','peakPagedPool','pagedPool','peakNonPagedPool','nonPagedPool','pagefile','peakPagefile')]
    class Io(ctypes.Structure):_fields_=[(n,ctypes.c_uint64) for n in ('readOps','writeOps','otherOps','readBytes','writeBytes','otherBytes')]
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);psapi=ctypes.WinDLL('psapi',use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes=[w.HANDLE,ctypes.POINTER(Memory),w.DWORD];kernel.GetProcessIoCounters.argtypes=[w.HANDLE,ctypes.POINTER(Io)];kernel.GetProcessTimes.argtypes=[w.HANDLE,*([ctypes.POINTER(ctypes.c_uint64)]*4)]
    m=Memory();m.cb=ctypes.sizeof(m);io=Io();times=[ctypes.c_uint64() for _ in range(4)]
    require(psapi.GetProcessMemoryInfo(int(process._handle),ctypes.byref(m),m.cb) and kernel.GetProcessIoCounters(int(process._handle),ctypes.byref(io)) and kernel.GetProcessTimes(int(process._handle),*[ctypes.byref(t) for t in times]),'Controller measurement unavailable')
    return {'scope':'owned controller only; excludes RAM owner/helper, includes preparation and reports','cpuMs':(times[2].value+times[3].value)/10000,'peakWorkingSetBytes':m.peakWorkingSet,'peakPrivateCommitBytes':m.peakPagefile,'ioReadBytes':io.readBytes,'ioWriteBytes':io.writeBytes}

def summarize(plan,rows):
    validate_plan(plan);expected={(p,a['id']) for p in plan['policy']['profiles'] for a in plan['programs']}
    require(len(rows)==len(expected) and {(r['profile'],r['program']) for r in rows}==expected,'Missing/duplicate campaign cells')
    require(all(r['outcome'] in OUTCOMES for r in rows),'Unknown campaign outcome')
    for r in rows:
        require(not (r['outcome']=='functional' and not r.get('workflowVerified')),'Unverified workflow promoted')
        app=next(a for a in plan['programs'] if a['id']==r['program']);require(r['inputSha256']==app['sha256'],'Row input differs from frozen corpus')
    results={}
    by_id={a['id']:a for a in plan['programs']}
    def group(subset):
        counts={o:sum(r['outcome']==o for r in subset) for o in OUTCOMES};measured=len(subset)-counts['not-evaluated']
        return {'selected':len(subset),'measured':measured,**counts,'observedFunctionalPercent':100*counts['functional']/measured if measured else None,'functionalPercentOfSelected':100*counts['functional']/len(subset) if subset else None}
    for profile in plan['policy']['profiles']:
        selected=[r for r in rows if r['profile']==profile];result=group(selected)
        result['categories']={c:group([r for r in selected if by_id[r['program']]['category']==c]) for c in plan['policy']['categories']}
        result['architectures']={arch:group([r for r in selected if by_id[r['program']]['architecture']==arch]) for arch in sorted({a['architecture'] for a in plan['programs']})}
        result['acceptanceMet']=result['measured']==result['selected'] and result['functionalPercentOfSelected']>=plan['policy']['requiredOverallFunctionalPercentPerProfile'] and all(g['functionalPercentOfSelected']>=plan['policy']['requiredFunctionalPercentPerCategory'] for g in result['categories'].values())
        results[profile]=result
    return {'profiles':results,'broadCompatibilityAccepted':all(p['acceptanceMet'] for p in results.values()),'majorityClaimAllowed':False,'malwareDetectionEvaluated':False,'nativeVersionConformanceEvaluated':False}

def run(plan_path,cache,build,config,output,reference,schema_reference):
    plan=validate_plan(json.loads(plan_path.read_bytes()));require(not output.exists(),'Fresh campaign output required');output.mkdir(parents=True)
    seal=json.loads((cache/'freeze.json').read_bytes());require(seal['planSha256']==identity(plan),'Corpus freeze differs from campaign plan')
    for app in plan['programs']:require(digest(ordinary(cache/app['id']/'input.exe'))==app['sha256'],'Frozen input hash mismatch: '+app['id'])
    before=sources.inputs();binaries=runtime_build.binaries(build,config);registry=output/'runtime';provision_runtime.provision(build,config,registry,output/'provision',reference,schema_reference)
    index=json.loads((registry/'index.json').read_bytes());supported={r['profile'] for r in index['applications'].values()};require(supported=={'win10'},'Campaign profile adapter requires review')
    rows=[];exe=build/config/'guardian-runtime.exe'
    def launch(application,directory,milliseconds=4000):
        start=time.perf_counter();process=subprocess.Popen([str(exe),'--input',str(application),'--runtime-root',str(registry),'--output',str(directory),'--milliseconds',str(milliseconds)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            stdout,stderr=process.communicate(timeout=35);observed=metrics(process)
        except BaseException:
            process.kill();process.communicate();raise
        elapsed=(time.perf_counter()-start)*1000;log=directory.with_suffix('.log');log.write_bytes(stdout+stderr);require(stdout.strip(),'Controller returned no report');report=json.loads(stdout)
        require(report.get('backend')=='MiniNT-unified-14.13','Wrong runtime backend');return report,{'wallMs':elapsed,**observed},process.returncode
    # A fixture checks infrastructure only; it is not in the independent denominator.
    control,_,code=launch(build/'guest/compat32'/config/'guardian-x86-console.exe',output/'infrastructure-control')
    require(not code and control.get('complete') and 'X86_ACCEPTANCE_PASS' in control.get('console',''),'Campaign VM control failed')
    for profile in plan['policy']['profiles']:
        for app in plan['programs']:
            row={'program':app['id'],'profile':profile,'configuration':config,'workflowVerified':False,'inputSha256':app['sha256']}
            if profile not in supported:
                row.update(outcome='not-evaluated',barrier='profile-not-supported-by-unified-runtime',dispatchAttempted=False,loaded=False,ran=False,metrics=None)
            else:
                report,measurements,code=launch(cache/app['id']/'input.exe',output/(profile+'-'+app['id']))
                require(report.get('sha256',app['sha256'])==app['sha256'],'Controller input hash mismatch');outcome,barrier=classify(app,report);require(code==(0 if report['complete'] else 3),'Controller return code contradicts report')
                row.update(outcome=outcome,barrier=barrier,workflowVerified=outcome=='functional',dispatchAttempted=True,loaded=report.get('loaded',False),ran=report.get('ran',False),runtimeComplete=report['complete'],controllerReturnCode=code,missingImports=report.get('unresolved',[]),diagnostic=report.get('error',''),metrics=measurements,limitations=['Application workflow not exercised beyond startup probe'] if outcome!='functional' else [])
                boot=output/(profile+'-'+app['id'])/'boot.json'
                if boot.exists():
                    data=json.loads(boot.read_bytes())['boots'][0]
                    if outcome=='functional':require(all(data.get('processes',{}).get(k,-1)==0 for k in ('liveProcesses','liveObjects','liveHandles','liveReferences')) and not data['timedOut'] and not data['memoryFault'],'Functional program leaked resources or faulted')
                    row['guestConfiguredRamBytes']=data['ramBytes'];row['guestExecutionWallMs']=data['wallMs'];row['guestTimedOut']=data['timedOut'];row['guestFaulted']=data.get('memoryFault',False) or data.get('processes',{}).get('faulted',0)!=0
            rows.append(row);write(output/'rows.json',rows);print(json.dumps({k:row[k] for k in ('program','profile','outcome','barrier')}),flush=True)
    stable=before==sources.inputs() and binaries==runtime_build.binaries(build,config) and all(digest(cache/a['id']/'input.exe')==a['sha256'] for a in plan['programs'])
    require(stable,'Campaign source, binary or corpus changed')
    result={'schema':1,'phase':'14.14','status':'MEASUREMENT_COMPLETE','configuration':config,'planSha256':identity(plan),'policySha256':plan['policySha256'],'programs':len(plan['programs']),'cells':len(rows),'stableSourcesBinariesAndCorpus':stable,'inputs':before,'binaries':binaries,'earlierPhasesRevalidated':False,'hostInputExecution':False,'nativeReference':'not executed; reference NT builds unavailable; scenarios are frozen expectations','rowsSha256':identity(rows),'summary':summarize(plan,rows)}
    write(output/'validation.json',result);return {k:result[k] for k in ('status','configuration','programs','cells','summary')}

def report(plan_path,release,debug,output):
    plan=validate_plan(json.loads(plan_path.read_bytes()));campaigns={};all_rows=[]
    for config,folder in (('Release',release),('Debug',debug)):
        proof=json.loads((folder/'validation.json').read_bytes());rows=json.loads((folder/'rows.json').read_bytes())
        require(proof['status']=='MEASUREMENT_COMPLETE' and proof['configuration']==config and proof['planSha256']==identity(plan) and proof['stableSourcesBinariesAndCorpus'] and proof['rowsSha256']==identity(rows) and proof['summary']==summarize(plan,rows),'Campaign evidence mismatch')
        campaigns[config]=proof['summary'];all_rows.extend(rows)
    public={'schema':1,'phase':'14.14','planSha256':identity(plan),'campaigns':campaigns,'rows':all_rows,'interpretation':'Compatibility of this selected corpus only; unsupported profiles were not executed. No malware efficacy or escape-resistance certification.'}
    write(output,public)
    text=['# Resultados da campanha 14.14','', 'Amostra de conveniência congelada antes dos testes; não representa todo o ecossistema Windows.','', '| Configuração | Perfil | Avaliados | Funcionais | Parciais | Incompatíveis | Não avaliados | Meta atingida |','|---|---|---:|---:|---:|---:|---:|---|']
    for config,summary in campaigns.items():
        for profile,s in summary['profiles'].items():text.append(f"| {config} | {profile} | {s['measured']}/{s['selected']} | {s['functional']} | {s['partial']} | {s['incompatible']} | {s['not-evaluated']} | {'Sim' if s['acceptanceMet'] else 'Não'} |")
    text+=['','Perfis sem execução têm taxa observada indisponível, não uma taxa de compatibilidade medida de 0%.','', '| Aplicação | Arquitetura | Release / win10 | Debug / win10 |','|---|---|---|---|']
    for app in plan['programs']:
        states=[next(r['outcome'] for r in all_rows if r['configuration']==c and r['profile']=='win10' and r['program']==app['id']) for c in ('Release','Debug')]
        text.append(f"| {app['name']} | {app['architecture']} | {states[0]} | {states[1]} |")
    output.with_suffix('.md').write_text('\n'.join(text)+'\n',encoding='utf-8')
    return {'status':'REPORT_GENERATED','broadCompatibilityAccepted':all(c['broadCompatibilityAccepted'] for c in campaigns.values()),'programs':len(plan['programs']),'cells':len(all_rows)}

def main():
    p=argparse.ArgumentParser(description=__doc__);commands=p.add_subparsers(dest='action',required=True)
    f=commands.add_parser('freeze')
    for n in ('selection','bindings','policy','output','cache'):f.add_argument('--'+n,type=Path,required=True)
    r=commands.add_parser('run')
    for n in ('plan','cache','build','output','reference','schema-reference'):r.add_argument('--'+n,type=Path,required=True)
    r.add_argument('--config',choices=('Release','Debug'),required=True)
    r=commands.add_parser('report')
    for n in ('plan','release','debug','output'):r.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    if a.action=='freeze':result=freeze(a.selection,a.bindings,a.policy,a.output,a.cache)
    elif a.action=='run':result=run(a.plan.resolve(),a.cache.resolve(),a.build.resolve(),a.config,a.output.resolve(),a.reference.resolve(),a.schema_reference.resolve())
    else:result=report(a.plan,a.release,a.debug,a.output)
    print(json.dumps(result),flush=True)
if __name__=='__main__':main()
