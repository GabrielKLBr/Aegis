"""Signed portable distribution, phase-scoped validation and private build export."""
from pathlib import Path
import argparse,hashlib,json,subprocess
import package_portable as portable
import aegis_git as ag
ROOT=portable.ROOT
def run(args):
    result=subprocess.run([str(x) for x in args],capture_output=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if result.returncode:raise RuntimeError(result.stderr.decode('utf-8','replace'))
    return json.loads(result.stdout)
def stage(backend,config,out,sequence,version,phase="18"):
    portable.stage(backend,config,out)
    # Replace the previous unsigned inventory in this newly created output only.
    (out/'manifest.json').unlink()
    definitions=out/'engines/definitions';definitions.mkdir()
    portable.copy(ROOT/'signatures.txt',definitions/'signatures.txt')
    result=subprocess.run([str(backend/config/'guardian-engine-service.exe'),'--default-definitions',str(definitions)],capture_output=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));assert result.returncode==0,result.stderr
    rules=json.loads((definitions/'rules.json').read_bytes());rules['source']='rule Aegis_Harmless_Test { meta: classification = "detected" strings: $a = "AEGIS_SCANNER_TEST_POSITIVE_V1" condition: $a at 0 and filesize <= 4096 }';(definitions/'rules.json').write_text(json.dumps(rules,indent=2)+'\n','utf-8')
    assets=json.loads((ROOT/'build-phase17/nuget/project.assets.json').read_bytes())
    inventory={'schema':1,'product':'Aegis','version':version,'configuration':config,'nugetPackages':sorted(k for k,v in assets['libraries'].items() if v.get('type')=='package'),'nativeLibraries':['SQLite','YARA','libarchive','zlib','nlohmann/json'],'licenseNotices':'notices/','windowsGuestReferencesDistributed':False,'publisherSignature':'ECDSA-P256-SHA256','authenticodeCertificateIncluded':False}
    (out/'notices/components.json').write_text(json.dumps(inventory,indent=2)+'\n','utf-8')
    (out/'README.txt').write_text(
        'Aegis phase '+phase+' - '+version+' - '+config+' x64\nExtract the entire ZIP and open Aegis.exe. No app installer or Python is required.\n'
        'The bootstrap verifies the publisher signature and member hashes before starting WinUI. Preserve runtime/, engines/, notices/ and the two aegis.manifest files.\n'
        'Settings installs/updates/removes the required Windows services after an explicit button click and Windows administrator authorization.\n'
        'Portable updates use an extracted signed package folder, preserve the previous version and take effect after restarting Aegis. Engine services update separately through Update / repair services.\n'
        'Settings also supports signed definition updates, definition rollback and previous engine services. Uninstall retains settings, local runtime references and the encrypted vault.\n'
        'The antivirus does not require virtualization. MiniNT needs the running Windows hypervisor, WHP and NX. System features are never changed automatically.\n'
        'This publisher manifest signature is independent of Windows Authenticode. No CA-backed code-signing certificate is bundled. Verify the ZIP checksum against the publisher record before first use.\n'
        'User settings, cache and installed services live outside the portable folder. Debug is for development and requires the Visual Studio debug CRT.\n'
        'Guest Windows DLLs, user data and private keys are not distributed. See notices/components.json and source/docs/PHASE-'+phase+'.md in the private source archive.\nhttps://github.com/GabrielKLBr/Aegis\n','utf-8')
    signer=backend/config/'guardian-publisher.exe'
    for folder,kind in [(definitions,'definitions'),(out/'engines','engines'),(out,'portable')]:run([signer,'--sign',folder,kind,sequence,version,config])
    result=run([backend/config/'guardian-distribution.exe','verify',out,'portable'])
    return {'identity':result['identity'],'files':len(result['manifest']['files'])}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--phase',choices=['18','19A'],default='18');p.add_argument('--backend-build',type=Path,required=True);p.add_argument('--config',choices=['Release','Debug'],required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--sequence',type=int,default=1800001);p.add_argument('--version',default='18.0.0');p.add_argument('--stage-only',action='store_true');p.add_argument('--seal',action='store_true',help='Archive the exact previously staged and validated signed folder');p.add_argument('--proof',type=Path);p.add_argument('--build-archive',type=Path);a=p.parse_args();backend=a.backend_build.resolve();out=a.output.resolve()
    if not a.stage_only:
        assert a.proof,'Current-phase validation proof is required';proof=json.loads(a.proof.read_bytes());assert proof['phase']==a.phase and proof['status']=='PASS' and proof['configuration']==a.config and proof['inputs']==portable.inputs()
        for path,digest in proof['binaries'].items():assert portable.sha(Path(path))==digest,'Validated binary changed'
    if a.seal:
        assert not a.stage_only and a.proof and out.is_dir()
        verified=run([backend/a.config/'guardian-distribution.exe','verify',out,'portable']);assert verified['identity']==proof['packageIdentity'],'Staged signature differs from tested package';result={'identity':verified['identity'],'files':len(verified['manifest']['files'])}
    else:result=stage(backend,a.config,out,a.sequence,a.version,a.phase)
    if not a.stage_only:
        result.update(portable.archive(out))
        if a.build_archive:
            full=a.build_archive.resolve();assert not full.exists();full.mkdir(parents=True)
            for file in ag.source_files(ROOT):portable.copy(file,full/'source'/file.relative_to(ROOT))
            for file in out.rglob('*'):
                if file.is_file():portable.copy(file,full/'app'/file.relative_to(out))
            portable.copy(a.proof,full/'validation/results.json')
            for name in (['engine_services_tests.exe','isolation_boundary_tests.exe','runtime_catalog_tests.exe','native_gui_broker_tests.exe','audio_broker_tests.exe','reputation_tests.exe','internet_core_tests.exe','guardian-native-gui-host.exe'] if a.phase=='19A' else ['distribution_tests.exe','deployment_fault_driver.exe']):portable.copy(backend/a.config/name,full/'tests'/name)
            if a.phase=='19A':
                portable.copy(backend/'guest/isolation'/a.config/'guardian-isolation-guest.exe',full/'tests/guardian-isolation-guest.exe')
                portable.copy(backend/'guest/isolation'/a.config/'guardian-isolation-network-guest.exe',full/'tests/guardian-isolation-network-guest.exe')
                for name in ['msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll']:
                    if (out/'engines'/name).exists():portable.copy(out/'engines'/name,full/'tests'/name)
            result['privateBuild']=portable.archive(full)
    print(json.dumps(result))
if __name__=='__main__':main()
