"""Pinned independent source cache and x86 compilation; inputs never execute on the host."""
from pathlib import Path
import base64,hashlib,json,os,subprocess,urllib.request
ROOT=Path(__file__).resolve().parents[1]
def source(folder):
    origin=json.loads((ROOT/'config/compat32-corpus.json').read_bytes());folder=Path(folder);folder.mkdir(parents=True,exist_ok=True);file=folder/'hello.c'
    if not file.exists():
        url='https://api.github.com/repos/'+origin['repository']+'/git/blobs/'+origin['gitBlobSha1']
        with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'Aegis-x86-corpus'}),timeout=30) as response:raw=response.read(65537)
        if len(raw)>65536:raise ValueError('External source budget')
        record=json.loads(raw);content=base64.b64decode(''.join(record['content'].split()),validate=True)
        if hashlib.sha1(b'blob '+str(len(content)).encode()+b'\0'+content).hexdigest()!=origin['gitBlobSha1']:raise ValueError('Source blob mismatch')
        if hashlib.sha256(content).hexdigest()!=origin['sha256']:raise ValueError('Source hash mismatch')
        file.write_bytes(content)
    if hashlib.sha256(file.read_bytes()).hexdigest()!=origin['sha256']:raise ValueError('Cached source mismatch')
    return file
def compile(file,output,runtime):
    output=Path(output);output.mkdir(parents=True,exist_ok=True);runtime=Path(runtime)
    vs=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    install=subprocess.check_output([str(vs),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    vc=max((Path(install)/'VC/Tools/MSVC').iterdir(),key=lambda p:tuple(map(int,p.name.split('.'))))
    sdk=max((Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/Include').iterdir(),key=lambda p:tuple(map(int,p.name.split('.'))))
    common=['/nologo','/c','/O2','/GS-','/Gs2147483647','/Zl','/D_NO_CRT_STDIO_INLINE=1','/I'+str(vc/'include'),'/I'+str(sdk/'ucrt')]
    cl=vc/'bin/Hostx64/x86/cl.exe';objects=[];logs=[]
    for name,path in (('external-hello',Path(file)),('owned-entry',ROOT/'guest/fixtures/Compat32ConsoleEntry.cpp')):
        target=output/(name+'.obj');result=subprocess.run([str(cl),*common,'/Fo'+str(target),str(path)],capture_output=True);logs.append(result.stdout+result.stderr)
        (output/'external-compile.log').write_bytes(b'\n'.join(logs))
        if result.returncode:raise RuntimeError('External source compile failed; see log')
        objects.append(target)
    target=output/'external-hello.exe';result=subprocess.run([str(vc/'bin/Hostx64/x86/link.exe'),'/nologo','/MACHINE:X86','/NODEFAULTLIB','/DYNAMICBASE','/NXCOMPAT','/SAFESEH:NO','/MANIFEST:NO','/ENTRY:Compat32ConsoleEntry','/SUBSYSTEM:CONSOLE','/OUT:'+str(target),*map(str,objects),str(runtime/'msvcrt.lib')],capture_output=True);logs.append(result.stdout+result.stderr);(output/'external-compile.log').write_bytes(b'\n'.join(logs))
    if result.returncode:raise RuntimeError('External source link failed; see log')
    return target
