"""Package owned CRT compatibility components and evidence, without input DLLs."""
from pathlib import Path
import argparse,json,shutil,zipfile
import xml.etree.ElementTree as ET
import nt_compatibility as nt
from validate_phase14_5 import REQUIRED
APPS=('guardian-crt-probe','guardian-crt','guardian-crt-fh4','guardian-crt-launcher','guardian-crt-legacy','guardian-crt-unsupported','guardian-crt-rethrow')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();proofs={}
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        proof=json.loads((evidence/'validation.json').read_bytes())
        nt.require(set(proof['testNames'])==REQUIRED,'Acceptance test identities changed')
        nt.require(proof['phase']=='14.5' and proof['status']=='PASS' and proof['configuration']==config and proof['tests']==len(REQUIRED) and not proof['failures'] and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Accepted evidence required')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        proofs[config]=proof
    cases=ET.parse(a.regression).getroot().findall('.//testcase');names={c.attrib['name'] for c in cases};nt.require(len(cases)==len(names)==104 and REQUIRED<=names and all(c.find('failure') is None and c.find('error') is None and c.find('skipped') is None for c in cases),'Complete 104-test regression required')
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'New package path required');out.mkdir(parents=True)
    def copy(source,name):
        target=out/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-pe','guardian-whp-memory','crt_contract_tests'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-crt.gk','guest/'+config+'/guardian-kernel-crt.gk')
        for name in ('ntdll.dll','kernelbase.dll','kernel32.dll','guardian-runtime-api.dll',*(n+'.exe' for n in APPS)):
            copy(a.build/'guest/crt'/config/name,'guest/crt/'+config+'/'+name)
        for name in ('validation.json','ctest.log','ctest.xml'):copy(evidence/name,'validation/'+config+'/'+name)
    copy(a.regression,'validation/regression-Release.xml')
    copy(a.build/'crt-generated/coverage.json','validation/crt-export-coverage.json')
    copy(nt.ROOT/'config/crt-references.json','config/crt-references.json')
    for name in ('crt_runtime.py','win32_runtime.py','native_nt.py','api_set.py','nt_compatibility.py','inspect_gui_compatibility.py','pe_volume.py','smoke_phase14_5.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    copy(nt.ROOT/'test/CrtIntegrationTests.py','test/CrtIntegrationTests.py')
    copy(nt.ROOT/'docs/PHASE-14.5.md','PHASE-14.5.md')
    (out/'README.txt').write_text('Aegis 14.5 - components of CRT compatibility in MiniNT x64\n\nRequires Windows x64, WHP, NX and Visual C++ runtime installed on the host.\nThis package contains kernel, own DLLs and compiled test applications\nby MSVC /MD, apart from evidence Release/Debug. Do not contain inbound DLLs\nMicrosoft, ISOs or volumes incorporating these DLLs.\n\nTo execute, provides the local directory of policy exact legal references\nconfig/crt-references.json and apisetschema.dll Win10 legal 14.4 phase.\nIn PowerShell, in this folder, using output folders that do not yet exist:\n\npython tools/crt_runtime.py bundle --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --application guardian-crt.exe --output execucao\n.\\Release\\guardian-pe.exe guest/Release/guardian-kernel-crt.gk execucao/runtime.gpv execucao/manifest.json execucao/binding.gnt sessao\n\nTo repeat the entire matrix of the package, including the two refusal cases:\npython tools/smoke_phase14_5.py --package . --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output NEW-EVIDENCE\n\nThe generated volume contains the supplied DLLs; keep local-o. The entries never\nare executed as programs in the host. Compatibility is limited to the corpus\ndocumented. GUI, rethrow/nested C++ and CRT general are not yet accepted.\nThe 14.6 phase was not initiated.\n',encoding='utf-8')
    files={p.relative_to(out).as_posix():nt.sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.5','files':files,'acceptance':{c:p['tests'] for c,p in proofs.items()},'regressionTests':len(cases),'microsoftInputBinariesIncluded':False,'fullCrtCompatibility':False,'phase14_6Started':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
