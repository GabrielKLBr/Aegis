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
  text='''GuardianAV fase 11 — Linux x86-64 nativo\n\n./guardian-gui guest/guardian-kernel-graphics.gk resultados\n./guardian-gui guest/guardian-kernel-graphics.gk resultados --interactive\n\nEnter dispensa o diálogo. Clique no Paint para desenhar, ou no Notes para digitar.\nEscape/fechar termina. Limite interativo: 30 segundos.\nRequisitos: KVM com NX, acesso a /dev/kvm, libcrypto.so.3 e libSDL2-2.0.so.0.\nO modo sem janela dispensa um servidor de vídeo; o interativo precisa dele.\n\nSão aplicações gráficas próprias com API explícita, não compatibilidade USER32/GDI.\nProduto Windows/MSVC/WHP preservado; validação nativa Windows pendente.\nConsulte docs/PHASE-11.md para limites e testes.\n'''
  (root/'LEIA-ME.txt').write_text(text)
  hashes={p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
  (root/'manifest.json').write_text(json.dumps({'phase':11,'files':hashes,'sources':accepted['sources']},indent=2)+'\n')
  with tarfile.open(destination,'w:gz') as archive:archive.add(root,arcname=name)
 digest=sha(destination);destination.with_suffix(destination.suffix+'.sha256').write_text(digest+'  '+destination.name+'\n')
 print(str(destination)+'\nSHA256 '+digest)
if __name__=='__main__':main()
