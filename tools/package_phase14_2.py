"""Package accepted Windows 14.2 binaries, volumes and evidence; no host DLL collection."""
from pathlib import Path
import argparse
import json
import shutil
import zipfile
import nt_compatibility as nt
import pe_volume

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--release',type=Path,required=True)
    parser.add_argument('--debug',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();proofs={}
    for config,evidence in (('Release',args.release),('Debug',args.debug)):
        proof=json.loads((evidence/'validation.json').read_bytes())
        nt.require(proof['phase']=='14.2' and proof['status']=='PASS' and proof['configuration']==config and not proof['skipped'] and proof['stableSourcesAndBinaries'],'Accepted evidence required')
        for name,digest in proof['inputs'].items():nt.require(nt.sha((nt.ROOT/name).read_bytes())==digest,'Source changed: '+name)
        for name,digest in proof['binaries'].items():nt.require(nt.sha((args.build/name).read_bytes())==digest,'Binary changed: '+name)
        proofs[config]=proof
    out=args.output;archive=out.parent/(out.name+'.zip');nt.require(not out.exists() and not archive.exists(),'Use a new package path');out.mkdir(parents=True)
    def copy(source,target):
        destination=out/target;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    for config,evidence in (('Release',args.release),('Debug',args.debug)):
        copy(args.build/config/'guardian-pe.exe',config+'/guardian-pe.exe')
        copy(args.build/config/'guardian-whp-memory.exe',config+'/guardian-whp-memory.exe')
        copy(args.build/'guest'/config/'guardian-kernel-external-pe.gk',config+'/guardian-kernel-external-pe.gk')
        for name in ('validation.json','ctest.xml','ctest.log'):copy(evidence/name,'validation/'+config+'/'+name)
        profiles=out/config/'profiles';profiles.mkdir()
        for profile in nt.PROFILES:
            pe_volume.sample(args.build,config,out/config/'guardian-kernel-external-pe.gk',profiles/profile,profile)
            pe_volume.verify(profiles/profile)
    for name in ('pe_volume.py','nt_compatibility.py','inspect_gui_compatibility.py'):copy(nt.ROOT/'tools'/name,'tools/'+name)
    copy(nt.ROOT/'docs/PHASE-14.2.md','PHASE-14.2.md')
    (out/'README.txt').write_text('Aegis phase 14.2 - external charger MiniNT x64\n\nWindows x64 development build. Requires WHP, NX and C++ runtime from Visual Studio 2022.\nOriginal Windows DLLs and overall application compatibility remain pending.\nThe reference program is within the guest volume; use the CLI below.\n\nIn PowerShell, in this folder, choose a session folder that does not yet exist:\npython tools/pe_volume.py verify Release/profiles/win10\n.\\Release\\guardian-pe.exe Release/guardian-kernel-external-pe.gk Release/profiles/win10/runtime.gpv Release/profiles/win10/manifest.json Release/profiles/win10/binding.gnt sessao-nova\n\nSwitch win10 to win7 or win11 in all arguments for another target profile.\nSwitch Release by Debug for debugging compilation. boot.json records the result.\nRead PHASE-14.2.md for limits and reproduction. The 14.3 phase has not been initiated.\n',encoding='utf-8')
    files={str(p.relative_to(out)).replace('\\','/'):nt.sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'manifest.json').write_bytes(nt.canonical({'schema':1,'phase':'14.2','files':files,
        'acceptance':{c:p['tests'] for c,p in proofs.items()},'windowsBinaryCompatibility':False,'phase14_3Started':False}))
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out))
    digest=nt.sha(archive.read_bytes());archive.with_suffix('.zip.sha256').write_text(digest+'  '+archive.name+'\n',encoding='ascii')
    print(json.dumps({'folder':str(out),'zip':str(archive),'sha256':digest,'files':len(files)+1}))
    return 0

if __name__=='__main__':raise SystemExit(main())
