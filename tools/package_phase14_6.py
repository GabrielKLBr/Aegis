"""Package owned phase 14.6 components and stable acceptance evidence, excluding Microsoft input DLLs."""
from pathlib import Path
import argparse,json,shutil,zipfile
import nt_compatibility as nt
from validate_phase14_6 import REQUIRED,TOTAL_TESTS
APPS=('guardian-security','guardian-security-read','guardian-security-service','guardian-security-orphan','guardian-security-peer','guardian-security-crash','guardian-security-fault')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--debug',type=Path,required=True);p.add_argument('--regression',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();proofs={}
    for config,suite,folder in (('Release','acceptance',a.release),('Debug','acceptance',a.debug),('Release','regression',a.regression)):
        proof=json.loads((folder/'validation.json').read_bytes());required=REQUIRED if suite=='acceptance' else None
        covers_acceptance=suite=='acceptance' and config=='Release' and proof['suite']=='regression'
        expected=TOTAL_TESTS if proof['suite']=='regression' else len(REQUIRED)
        nt.require(proof['phase']=='14.6' and proof['status']=='PASS' and (proof['suite']==suite or covers_acceptance) and proof['configuration']==config and proof['tests']==expected and not proof['failures'] and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Stable accepted evidence required')
        if required:nt.require(required<=set(proof['testNames']) if covers_acceptance else set(proof['testNames'])==required,'Acceptance identities changed')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((a.build/name).read_bytes())==digest,'Binary changed: '+name)
        proofs[config+'-'+suite]=proof
    out=a.output;archive=out.with_name(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'New package path required');out.mkdir(parents=True)
    def copy(source,name):
        destination=out/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    for config,evidence in (('Release',a.release),('Debug',a.debug)):
        for name in ('guardian-pe','guardian-whp-memory','security_contract_tests','security_core_tests','security_windows_reference'):copy(a.build/config/(name+'.exe'),config+'/'+name+'.exe')
        copy(a.build/'guest'/config/'guardian-kernel-security.gk','guest/'+config+'/guardian-kernel-security.gk')
        for name in ('ntdll.dll','kernelbase.dll','kernel32.dll','guardian-runtime-api.dll'):copy(a.build/'guest/crt'/config/name,'guest/crt/'+config+'/'+name)
        for name in ('advapi32.dll','sechost.dll','userenv.dll',*(n+'.exe' for n in APPS)):copy(a.build/'guest/security'/config/name,'guest/security/'+config+'/'+name)
        for name in ('validation.json','ctest.log','ctest.xml'):copy(evidence/name,'validation/'+config+'/'+name)
    for name in ('validation.json','ctest.log','ctest.xml'):copy(a.regression/name,'validation/regression-Release/'+name)
    copy(a.build/'security-generated/coverage.json','validation/security-export-coverage.json');copy(nt.ROOT/'config/crt-references.json','config/crt-references.json')
    for name in ('security_runtime.py','crt_runtime.py','win32_runtime.py','native_nt.py','api_set.py','nt_compatibility.py','inspect_gui_compatibility.py','pe_volume.py','smoke_phase14_6.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    copy(nt.ROOT/'test/SecurityIntegrationTests.py','test/SecurityIntegrationTests.py');copy(nt.ROOT/'docs/PHASE-14.6.md','PHASE-14.6.md')
    (out/'README.txt').write_text('Aegis 14.6 - registration, accounts and services of MiniNT x64\n\nRequires Windows x64, WHP/NX and Visual C++ runtime installed on the host.\nIt contains own components, MSVC /MD and evidence testing programs.\nDo not contain Microsoft input DLLs, ISOs, personal disks or GPV volumes.\nThe services created by the tests belong only to the guest.\n\nProvides your exact local legal references of ucrtbase.dll and vcruntime140.dll\n(config/crt-references.json) and the apisetschema.dll Win10 phase 14.4.\nIn PowerShell, in this folder and using new outputs:\n\npython tools/security_runtime.py bundle --build . --config Release --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output execucao\n.\\Release\\guardian-pe.exe guest/Release/guardian-kernel-security.gk execucao/runtime.gpv execucao/manifest.json execucao/binding.gnt sessao\n\nA good session.-sucedida export security-state.bin64 KiB). To recover-lo,\nmanages a volume with --application guardian-security-read.exe and passes the file\nas the last argument of the Guardian-pe, after the new session folder.\nKeep generated volumes and your data locally.\n\nTo check both package configurations:\npython tools/smoke_phase14_6.py --package . --reference CRT-DIRECTORY --schema-reference LEGAL-SCHEMA --output NEW-EVIDENCE\n\nRead PHASE-14.6.md for storage limits, tokens, ACLs and services.\nIt does not equal complete ADVAPI32/Windows. 14.7 has not been started.\n',encoding='utf-8')
    files={p.relative_to(out).as_posix():nt.sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file()};(out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.6','files':files,'acceptance':{'Release':len(REQUIRED),'Debug':len(REQUIRED)},'regressionTests':TOTAL_TESTS,'microsoftInputBinariesIncluded':False,'completeAdvapi32':False,'phase14_7Started':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(out.rglob('*')):
            if path.is_file():z.write(path,path.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii');print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
if __name__=='__main__':main()
