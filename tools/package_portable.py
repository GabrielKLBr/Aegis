"""Build the portable Windows folder/ZIP. No registration or user data is included."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, zipfile
import aegis_git as ag
ROOT=Path(__file__).resolve().parents[1]
def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):digest.update(block)
    return digest.hexdigest()
def inputs():
    return {f.relative_to(ROOT).as_posix():sha(f) for f in ag.source_files(ROOT) if f.suffix!='.md' and 'docs' not in f.relative_to(ROOT).parts}
def copy(source,dest):
    assert source.is_file() and not source.is_symlink(),source
    dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest);assert sha(source)==sha(dest)
def notices(out):
    assets=json.loads((ROOT/'build-phase17/nuget/project.assets.json').read_bytes())
    for identity,library in assets['libraries'].items():
        if library.get('type')!='package':continue
        for folder in assets['packageFolders']:
            package=Path(folder)/library['path']
            if package.is_dir():
                for f in package.iterdir():
                    if f.is_file() and ('license' in f.name.lower() or 'notice' in f.name.lower()) and f.suffix.lower() in {'.txt','.md','.rtf'}:copy(f,out/'notices'/identity.replace('/','-')/f.name)
                break
    for f in (ROOT/'docs/third-party').rglob('*'):
        if f.is_file():copy(f,out/'notices/scanner'/f.relative_to(ROOT/'docs/third-party'))
def stage(backend,config,out):
    assert not out.exists(),'Use a new output directory'
    out.mkdir(parents=True)
    copy(backend/config/'aegis-portable.exe',out/'Aegis.exe')
    ui=ROOT/'build-phase17/bin'/config
    for f in sorted(ui.rglob('*')):
        if f.is_file() and f.suffix.lower() not in {'.pdb','.ilk','.lib','.exp','.obj','.log'}:copy(f,out/'runtime'/f.relative_to(ui))
    cli=backend/config/'guardian-engine-service.exe'
    names=json.loads(subprocess.check_output([str(cli),'--package-files'],creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0)))
    assert all(Path(n).name==n for n in names)
    for name in names:copy(backend/config/name,out/'engines'/name)
    # Release redistributables are licensed for app-local deployment. Debug CRT
    # is a development prerequisite and is never redistributed.
    if config=='Release':
        vc=Path(os.environ.get('ProgramFiles',r'C:\Program Files'))/'Microsoft Visual Studio/2022/Community/VC/Redist/MSVC'
        candidates=sorted(vc.glob('*/x64/Microsoft.VC143.CRT'))
        assert candidates,'Visual C++ Release redistributables are missing'
        for f in candidates[-1].glob('*.dll'):
            for dest in ('runtime','engines'):copy(f,out/dest/f.name)
    notices(out)
    (out/'README.txt').write_text(
        'Aegis portable - phase 17.1 - '+config+' x64\n'
        'Extract the whole ZIP, then open Aegis.exe. There is no app installer.\n'
        'Settings > Install engine services registers the required Windows services after Windows administrator authorization. Use Update / repair when services already exist.\n'
        'Removing services retains installed versions, protection settings and the encrypted vault. Moving the portable folder does not move installed service files.\n'
        'Hash queries are opt-in and transmit only SHA-256. Configure the optional MalwareBazaar key in Settings; Windows Credential Manager keeps it outside this folder. CIRCL needs no key.\n'
        'The self-contained Windows App SDK and its localization resources are in runtime/. Keep that folder intact. Guest Windows DLLs, private runtime catalogs, credentials and user data are not distributed.\n'
        +('Debug builds require the Visual Studio debug CRT and are for development only.\n' if config=='Debug' else '')
        +'https://github.com/GabrielKLBr/Aegis\n',encoding='utf-8')
    manifest={'schema':1,'phase':'17.1','configuration':config,'layout':'portable','files':{f.relative_to(out).as_posix():sha(f) for f in sorted(out.rglob('*')) if f.is_file()}}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n','utf-8')
    return manifest
def archive(folder):
    target=Path(str(folder)+'.zip');assert not target.exists()
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for f in sorted(folder.rglob('*')):
            if f.is_file():z.write(f,f.relative_to(folder).as_posix())
    with zipfile.ZipFile(target) as z:assert z.testzip() is None
    return {'archive':str(target),'sha256':sha(target),'bytes':target.stat().st_size}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend-build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--stage-only',action='store_true');p.add_argument('--proof',type=Path);p.add_argument('--build-archive',type=Path);a=p.parse_args()
    backend=a.backend_build.resolve();out=a.output.resolve()
    if not a.stage_only:
        assert a.proof,'Validation evidence is required to publish a ZIP'
        proof=json.loads(a.proof.read_bytes());assert proof['status']=='PASS' and proof['configuration']==a.config
        assert proof['inputs']==inputs(),'Tested sources changed'
        for path,digest in proof['binaries'].items():assert sha(Path(path))==digest,'Tested binary changed'
    manifest=stage(backend,a.config,out)
    if a.stage_only:print(json.dumps({'stage':str(out),'files':len(manifest['files'])}));return
    result=archive(out)
    if a.build_archive:
        full=a.build_archive.resolve();assert not full.exists();full.mkdir(parents=True)
        for f in ag.source_files(ROOT):copy(f,full/'source'/f.relative_to(ROOT))
        for f in out.rglob('*'):
            if f.is_file():copy(f,full/'app'/f.relative_to(out))
        copy(a.proof,full/'validation/results.json');copy(backend/a.config/'reputation_tests.exe',full/'tests/reputation_tests.exe')
        (full/'README.txt').write_text('Aegis phase 17.1 '+a.config+'\nPortable app: app/Aegis.exe\nSource and current-phase validation evidence included. No personal state or guest Windows references.\n','utf-8')
        result['privateBuild']=archive(full)
    print(json.dumps(result))
if __name__=='__main__':main()
