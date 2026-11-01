"""Package phase 17 owned sources and a self-contained WinUI build; no user state."""
from pathlib import Path
import argparse,hashlib,json,shutil,zipfile
import aegis_git as ag
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend-build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 proof=json.loads((ROOT/'build-phase17'/('validation-'+a.config+'.json')).read_bytes());assert proof['status']=='PASS' and proof['sourceStable'] and proof['integrationTests']==50 and proof['administrativeTests']==7 and len(proof['ui'])==3
 sources=ag.source_files(ROOT);current={f.relative_to(ROOT).as_posix():sha(f) for f in sources if f.suffix!='.md' and 'docs' not in f.relative_to(ROOT).parts}
 assert all(current.get(k)==v for k,v in proof['inputs'].items()),'Tested source changed'
 assert set(current)-set(proof['inputs'])<={'tools/package_winui.py'},'Untested code added'
 binary=ROOT/'build-phase17/bin'/a.config/'Aegis.exe';assert sha(binary)=={k.replace('\\','/'):v for k,v in proof['binaries'].items()}[binary.relative_to(ROOT).as_posix()]
 native=a.backend_build/a.config/'winui_backend_tests.exe';assert sha(native)==proof['binaries']['native-'+a.config]
 out=a.output.resolve();assert not out.exists() and not out.with_suffix('.zip').exists();out.mkdir(parents=True)
 def copy(source,name):
  dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest);assert sha(source)==sha(dest)
 for f in sources:copy(f,'source/'+f.relative_to(ROOT).as_posix())
 for f in sorted(binary.parent.rglob('*')):
  if f.is_file() and f.suffix.lower() not in {'.pdb','.ilk','.lib','.exp','.obj','.log'}:copy(f,'app/'+f.relative_to(binary.parent).as_posix())
 # Preserve the license/notice files supplied by the resolved NuGet packages.
 assets=json.loads((ROOT/'build-phase17/nuget/project.assets.json').read_bytes())
 for identity,library in assets['libraries'].items():
  if library.get('type')!='package':continue
  for folder in assets['packageFolders']:
   package=Path(folder)/library['path']
   if package.is_dir():
    for notice in package.iterdir():
     if notice.is_file() and ('license' in notice.name.lower() or 'notice' in notice.name.lower()) and notice.suffix.lower() in {'.txt','.md','.rtf'}:copy(notice,'notices/'+identity.replace('/','-')+'/'+notice.name)
    break
 copy(native,'tests/winui_backend_tests.exe')
 for kind,folder in [('integration','accepted-'),('administrative','accepted-admin-')]:copy(ROOT/'build-phase17'/(folder+a.config)/'results.json','validation/'+kind+'.json')
 ui=[{k:v for k,v in row.items()} for row in proof['ui']]
 (out/'validation/ui.json').write_text(json.dumps({'status':'PASS','config':a.config,'runs':ui},indent=2),'utf-8')
 (out/'README.txt').write_text('Aegis phase 17 - WinUI 3 desktop client\nRun app/Aegis.exe from its complete folder. Existing phase 16 services for the same user are required.\nThe app SDK runtime is self-contained. Debug requires the Visual Studio debug CRT; it is not redistributed here.\nSources, English/Portuguese/Spanish embedded resources and current phase evidence are included. No user state, private vault, runtime reference catalog or disk images are included.\nSee source/docs/PHASE-17.md. This is not a phase 18 installer or malware-containment certification.\n',encoding='utf-8')
 manifest={'schema':1,'phase':'17','configuration':a.config,'integrationTests':50,'administrativeTests':7,'uiRuns':ui,'files':{f.relative_to(out).as_posix():sha(f) for f in sorted(out.rglob('*')) if f.is_file()}}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n','utf-8')
 archive=out.with_suffix('.zip')
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for f in sorted(out.rglob('*')):
   if f.is_file():z.write(f,f.relative_to(out).as_posix())
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None
  for name,digest in manifest['files'].items():assert hashlib.sha256(z.read(name)).hexdigest()==digest
 print(json.dumps({'package':str(archive),'sha256':sha(archive),'bytes':archive.stat().st_size,'files':len(manifest['files'])}))
if __name__=='__main__':main()
