"""Package only the Windows engines, user app, owned sample and runtime dependencies."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--redist',type=Path,required=True,help='Licensed Visual Studio x64 Microsoft.VC143.CRT directory')
    a=p.parse_args();root=Path(__file__).resolve().parents[1]
    source=a.build.resolve()/'Release';output=a.output.resolve()
    if output.exists():raise ValueError('Use a new output directory')
    names=json.loads(subprocess.check_output([str(source/'guardian-engine-service.exe'),'--package-files'],encoding='utf-8'))
    allowed={'guardian-engine-service.exe','guardian-scan-worker.exe','guardian-whp-memory.exe','guardian-service-sample.exe',
        'signatures.txt','guardian-desktop.exe','guardian-audio-host.exe','guardian-kernel-audio.gk'}
    if not isinstance(names,list) or not set(names)<=allowed or len(names)!=len(set(names)):raise ValueError('Package file list rejected')
    output.mkdir(parents=True)
    for name in [*names,'avui.exe']:shutil.copy2(source/name,output/name)
    for name in ('concrt140.dll','msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll',
                 'vccorlib140.dll','vcruntime140.dll','vcruntime140_1.dll','vcruntime140_threads.dll'):
        file=a.redist/name
        if file.is_file():shutil.copy2(file,output/name)
    shutil.copytree(root/'docs/third-party',output/'licenses')
    shutil.copy2(root/'docs/WINDOWS_ENGINE_SERVICES.md',output/'LEIA-ME.md')
    manifest={str(f.relative_to(output)).replace('\\','/'):{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
              for f in sorted(output.rglob('*')) if f.is_file()}
    proof={'schema':1,'feature':'windows-engine-services','configuration':'Release','files':manifest,'microsoftRuntimeSource':'Visual Studio licensed x64 redistributables'}
    (output/'package.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'output':str(output),'files':len(manifest)}))
if __name__=='__main__':main()
