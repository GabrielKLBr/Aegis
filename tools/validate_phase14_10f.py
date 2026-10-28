"""Validate phase 14.10F native command broker with pinned external inputs and stable build evidence."""
from pathlib import Path
import argparse,json,subprocess
import xml.etree.ElementTree as ET
import api_set,crt_runtime,native_nt,nt_compatibility as nt
from validate_phase14_1 import inputs
from validate_phase14_10e import REQUIRED as PREVIOUS
REQUIRED=PREVIOUS|{'software_graphics_protocol','software_graphics_broker','software_graphics_integration','software_graphics_failures','software_graphics_runtime_tools','software_graphics_inventory'}
TOTAL_TESTS=173

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--suite',choices=('acceptance','regression'),default='acceptance')
    p.add_argument('--application',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--original',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    flappy={str(path.resolve()):nt.sha(nt.regular(path,8<<20)) for path in (a.application,a.application.with_name('settings.txt'),a.application.with_name('icon.ico'))}
    nt.require(flappy[str(a.application.resolve())]=='f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6','Flappy acceptance input changed')
    _,native=native_nt.syscall_map(a.original,'win10',True);schema_raw=nt.regular(a.schema_reference,8<<20);version,contracts=api_set.parse(api_set.section(schema_raw));schema={'sha256':nt.sha(schema_raw),'fileVersion':native_nt.file_version(nt.InventoryPe(a.schema_reference,schema_raw)),'version':version,'contracts':len(contracts),'inputExecutedOnHost':False};crt=crt_runtime.references(a.reference)
    tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(a.build),'-C',a.config,'--show-only=json-v1'],text=True));commands={t['name']:t.get('command',[]) for t in tests['tests']};nt.require(len(commands)==TOTAL_TESTS and REQUIRED<=commands.keys() and all(commands.values()),'Required test matrix not configured/built')
    for name,arg,expected in [('native_nt_original_boot','--original',a.original),('win32_runtime_integration','--reference',a.schema_reference),('crt_runtime_integration','--reference',a.reference),('security_runtime_integration','--reference',a.reference),('security_runtime_integration','--schema-reference',a.schema_reference),('desktop_flappy','--application',a.application),('desktop_flappy','--reference',a.reference),('desktop_runtime_integration','--schema-reference',a.schema_reference),('component_com_integration','--reference',a.reference),('component_com_integration','--schema-reference',a.schema_reference),('component_controls_integration','--reference',a.reference),('component_rpc_integration','--schema-reference',a.schema_reference),('internet_tls_integration','--reference',a.reference),('internet_tls_integration','--schema-reference',a.schema_reference),('internet_reference_integration','--reference',a.reference),('window_bridge_flappy_integration','--application',a.application),('window_bridge_programs_integration','--reference',a.reference),('window_bridge_components_integration','--schema-reference',a.schema_reference)]:
        command=commands[name];nt.require(Path(command[command.index(arg)+1]).resolve()==expected.resolve(),'CTest reference mismatch: '+name+' '+arg)
    for name in ('native_gui_integration','native_gui_runtime_tools','native_user32_integration','native_user32_runtime_tools','native_gdi_integration','native_gdi_runtime_tools','native_controls_integration','native_controls_flappy','native_controls_runtime_tools','expanded_gdi_compatibility','expanded_gdi_flappy','expanded_gdi_runtime_tools','expanded_gdi_integration','expanded_gdi_external'):
        for arg,expected in (('--build',a.build),('--reference',a.reference),('--schema-reference',a.schema_reference)):
            command=commands[name];nt.require(Path(command[command.index(arg)+1]).resolve()==expected.resolve(),'Native GUI CTest input mismatch: '+name+' '+arg)
    import gdi_corpus,gdi_inventory
    inventory=gdi_inventory.validate(a.build,a.config)
    import software_graphics_matrix
    software_inventory=software_graphics_matrix.validate(a.build,a.config)
    external_files={}
    for application in ('bezier','clover','whatsize'):
        for variant in ('Unicode','ANSI'):
            path,receipt=gdi_corpus.verify(a.build/'external/gdi'/a.config,application,variant)
            for file in (path,path.with_suffix('.exe.source.json')):external_files[str(file.resolve())]=nt.sha(file.read_bytes())
    for file in ((a.build/'external/software-graphics'/a.config/'direct2d-hello.exe'),(a.build/'external/software-graphics'/a.config/'direct2d-hello.exe.source.json')):
        external_files[str(file.resolve())]=nt.sha(file.read_bytes())
    cache=(a.build/'CMakeCache.txt').read_text(encoding='utf-8')
    corpus=Path(next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('GUARDIAN_EXPANDED_GDI_CORPUS:PATH=')))
    independent_sources={}
    for name,item in gdi_corpus.configuration()['applications'].items():
        path=corpus/(name+'.c');digest=nt.sha(nt.regular(path,1<<20));nt.require(digest==item['sha256'],'Independent source changed')
        independent_sources[str(path.resolve())]=digest
    software_corpus=Path(next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('GUARDIAN_SOFTWARE_GRAPHICS_CORPUS:PATH=')))
    for name,record in json.loads((nt.ROOT/'config/software-graphics-corpus.json').read_bytes())['files'].items():
        path=software_corpus/name;digest=nt.sha(path.read_bytes());nt.require(digest==record['sha256'],'Independent software source changed');independent_sources[str(path.resolve())]=digest
    for name in ('software_graphics_integration','software_graphics_failures','software_graphics_runtime_tools'):
        for arg,expected in (('--build',a.build),('--reference',a.reference),('--schema-reference',a.schema_reference)):
            command=commands[name];nt.require(Path(command[command.index(arg)+1]).resolve()==expected.resolve(),'Software CTest input mismatch')
    command=commands['software_graphics_integration'];nt.require(Path(command[command.index('--external')+1]).resolve()==(a.build/'external/software-graphics'/a.config/'direct2d-hello.exe').resolve(),'Independent software corpus input mismatch')
    command=commands['expanded_gdi_external'];nt.require(Path(command[command.index('--corpus-binaries')+1]).resolve()==(a.build/'external/gdi').resolve(),'Independent corpus input mismatch')
    command=commands['expanded_gdi_flappy'];nt.require(Path(command[command.index('--application')+1]).resolve()==a.application.resolve(),'Expanded Flappy input mismatch')
    legal_files={str(path.resolve()):nt.sha(nt.regular(path,8<<20)) for path in (a.original,a.schema_reference,*(a.reference/record['name'] for record in crt))}
    required=REQUIRED if a.suite=='acceptance' else set(commands);before=inputs();paths=set()
    for directory in (a.build/a.config,*(a.build/'guest'/name/a.config for name in ('fixtures','external','native','win32','crt','security','desktop','components','internet','integrated','native-gui','native-user32','native-gdi','native-controls','expanded-gdi','software-graphics')),a.build/'guest'/a.config,a.build/'external/gdi'/a.config,a.build/'external/software-graphics'/a.config,a.build/'software-assets'/a.config):paths.update(p for p in directory.rglob('*') if p.suffix.lower() in ('.exe','.dll','.gk','.png','.bmp','.jpg'))
    binaries={p.relative_to(a.build).as_posix():nt.sha(p.read_bytes()) for p in sorted(paths)}
    run=subprocess.run(['ctest','--test-dir',str(a.build),'-C',a.config,'-j','1','-R','^('+'|'.join(sorted(required))+')$','--output-on-failure','--output-junit',str(a.output.resolve()/'ctest.xml')],capture_output=True,text=True,encoding='utf-8',errors='replace')
    (a.output/'ctest.log').write_text(run.stdout+run.stderr,encoding='utf-8');cases=ET.parse(a.output/'ctest.xml').getroot().findall('.//testcase');failures=sum(c.find('failure') is not None or c.find('error') is not None for c in cases);skipped=sum(c.find('skipped') is not None for c in cases)
    stable=all(nt.sha(Path(path).read_bytes())==digest for path,digest in {**flappy,**external_files,**independent_sources,**legal_files}.items()) and before==inputs() and all(nt.sha((a.build/n).read_bytes())==d for n,d in binaries.items()) and nt.sha(nt.regular(a.original,8<<20))==native['dllSha256'] and nt.sha(nt.regular(a.schema_reference,8<<20))==schema['sha256'] and crt==crt_runtime.references(a.reference)
    passed=run.returncode==failures==skipped==0 and len(cases)==len(required) and {c.attrib['name'] for c in cases}==required and stable
    proof={'schema':1,'phase':'14.10F','configuration':a.config,'suite':a.suite,'status':'PASS' if passed else 'FAIL','tests':len(cases),'testNames':sorted(c.attrib['name'] for c in cases),'failures':failures,'skipped':skipped,'exitCode':run.returncode,'stableSourcesAndBinaries':stable,'scope':'bounded Direct2D software, DirectWrite fonts/text, builtin PNG/BMP/JPEG, WARP textures/clear/copy/map/query, DXGI owned-window copy presentation, identical independent SDK binary and exact client pixels; explicit failures and quotas; expanded native GDI curves, paths, regions, transformations, raster and text; identical independent SDK binaries in ANSI/Unicode with exact native client pixels; explicit pending imports; native controls, bounded menus and icons, guest namespace dialogs, native font metrics, original Flappy on host windows, privacy, resource and cleanup checks; native GDI commands, owned DC/object/bitmap resources, text metrics, native-reference pixel comparison, clipping/state, resize, resource limits and cleanup; inherited compatibility regression','flappyInputs':flappy,'nativeNtdllRegressionReference':native,'apiSetReference':schema,'crtReferences':crt,'windowsBinaryCompatibility':False,'phase14_8Started':True,'phase14_9Started':True,'phase14_10Started':True,'phase14_10AStarted':True,'phase14_10ACompleteForCorpus':passed,'phase14_10BStarted':True,'phase14_10BCompleteForCorpus':passed,'phase14_10CStarted':True,'phase14_10CCompleteForCorpus':passed,'phase14_10DStarted':True,'phase14_10DCompleteForCorpus':passed,'phase14_10EStarted':True,'phase14_10ECompleteForCorpus':passed,'phase14_10FStarted':True,'phase14_10FCompleteForCorpus':passed,'phase14_10GStarted':False,'softwareGraphicsInventory':software_inventory,'fullDirectX':False,'shadersAvailable':False,'guiApiInventory':inventory,'independentBinariesAndReceipts':external_files,'independentSources':independent_sources,'legalInputFiles':legal_files,'hostTls':False,'hostCredentials':False,'completeWindowsNetworking':False,'completeCOM':False,'completeNDR':False,'phase12FStarted':True,'phase12FCompleteForCorpus':passed,'arbitraryUser32Passthrough':False,'gpuAcceleration':False,'inputs':before,'binaries':binaries}
    (a.output/'validation.json').write_bytes(nt.canonical(proof));print(json.dumps({k:proof[k] for k in ('status','phase','configuration','suite','tests','failures','skipped','stableSourcesAndBinaries')}));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
