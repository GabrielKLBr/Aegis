"""Package an accepted native Linux phase-11 build, with per-file hashes."""
import argparse,datetime,hashlib,json,pathlib,shutil,subprocess,sys,tarfile,tempfile
from validate_linux_native import ROOT,require,sha,source_hashes

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('evidence',type=pathlib.Path);args=parser.parse_args();evidence=args.evidence.resolve();accepted=json.loads((evidence/'validation.json').read_text())
 require(accepted['phase']==11 and accepted['status']=='passed' and accepted['backend']=='KVM','Accepted native Linux evidence required')
 require(accepted['sources']==source_hashes(),'Accepted source tree changed')
 host=evidence/'host-Release';guest=evidence/'guest-Release';date=datetime.datetime.now().strftime('%Y%m%d');name='guardianav-phase11-linux-x86_64-'+date
 destination=ROOT/'dist'/(name+'.tar.gz');destination.parent.mkdir(exist_ok=True);require(not destination.exists(),'Refusing to overwrite package')
 with tempfile.TemporaryDirectory(prefix='guardian-phase11-package-') as temp:
  root=pathlib.Path(temp)/name;root.mkdir()
  def copy(src,rel):
   target=root/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target)
  for exe in ('guardianav','guardian-kvm','guardian-vm','guardian-gui'):
   copy(host/exe,exe);(root/exe).chmod(0o755)
  copy(ROOT/'signatures.txt','signatures.txt')
  images=list(guest.glob('*.gk'));require(len(images)==15,'Expected 13 production images + 2 fatal controls')
  for image in images:copy(image,pathlib.Path('guest')/image.name)
  for doc in (ROOT/'docs').glob('*.md'):copy(doc,pathlib.Path('docs')/doc.name)
  for file in ('validation.json','changes.json','changes.patch','windows-cross-precheck.json'):
   copy(evidence/file,pathlib.Path('evidence')/file)
  for folder in ('graphics-application','graphics-boots','graphics-manager'):
   source=host/folder
   for p in source.rglob('*'):
    if p.is_file() and p.suffix in ('.json','.ppm'):copy(p,pathlib.Path('evidence')/folder/p.relative_to(source))
  copy(ROOT/'test/GraphicsApplicationTests.py','tools/GraphicsApplicationTests.py')
  copy(ROOT/'test/test_vm_graphics.py','tools/test_vm_graphics.py')
  text='GuardianAV phase 11 — Linux x86-64 native\n\n./guardian-gui guest/guardian-kernel-graphics.gk resultados\n./guardian-gui guest/guardian-kernel-graphics.gk resultados --interactive\n\nEnter does not require dialogue. Click Paint to draw, or Notes to type.\nEscape/fechar ends. Interactive limit: 30 seconds.\nRequirements: KVM with NX, access to /dev/kvm, libcrypto.so.3 and libSDL2-2.0.so.0.\nThe windowless mode dispenses a video server; the interactive one needs it.\n\nThey are proprietary graphics applications with explicit API, not USER32/GDI compatibility.\nWindows/MSVC/WHP product preserved; native Windows validation pending.\nSee docs/PHASE-11.md for limits and tests.\n'
  (root/'README.txt').write_text(text)
  hashes={p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
  (root/'manifest.json').write_text(json.dumps({'phase':11,'files':hashes,'sources':accepted['sources']},indent=2)+'\n')
  with tarfile.open(destination,'w:gz') as archive:archive.add(root,arcname=name)
 digest=sha(destination);destination.with_suffix(destination.suffix+'.sha256').write_text(digest+'  '+destination.name+'\n')
 print(str(destination)+'\nSHA256 '+digest)
if __name__=='__main__':main()
