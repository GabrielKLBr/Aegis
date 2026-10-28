"""Validate only phase 14.10G, preserving Windows playback evidence."""
from pathlib import Path
import argparse,hashlib,json,subprocess,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
AUDIO={'audio_broker','audio_integration','audio_inventory','audio_runtime_tools'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs():
    files=[ROOT/'CMakeLists.txt']
    for directory in ('src','guest','shared','test','tools','config','cmake'):
        files.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.pyo'))
    return {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(files)}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.build=a.build.resolve();a.output.mkdir(parents=True,exist_ok=False)
    tests=json.loads(subprocess.check_output(['ctest','--test-dir',str(a.build),'-C',a.config,'--show-only=json-v1'],text=True))['tests'];names={r['name'] for r in tests}
    assert AUDIO<=names and all(t.get('command') for t in tests if t['name'] in AUDIO)
    selected=AUDIO
    files=[a.build/a.config/(name+'.exe') for name in ('guardian-desktop','guardian-audio-host','audio_broker_tests')]
    files.append(a.build/'guest'/a.config/'guardian-kernel-audio.gk')
    for group in ('audio','desktop','crt','security','components','internet'):
        files.extend(p for p in (a.build/'guest'/group/a.config).glob('*.dll'))
    files.extend(p for p in (a.build/'guest/audio'/a.config).glob('*.exe'))
    files.append(a.build/'external/audio'/a.config/'whisky-audio-test.exe')
    source=inputs();binaries={p.relative_to(a.build).as_posix():sha(p) for p in files}
    import ctypes
    ctypes.windll.kernel32.SetThreadExecutionState(0x80000003)
    try:
        with (a.output/'ctest.log').open('w',encoding='utf-8') as log:
            run=subprocess.run(['ctest','--test-dir',str(a.build),'-C',a.config,'-j','1','-R','^('+'|'.join(sorted(selected))+')$','--output-on-failure','--output-junit',str(a.output.resolve()/'ctest.xml')],stdout=log,stderr=subprocess.STDOUT)
    finally:ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)
    cases=ET.parse(a.output/'ctest.xml').getroot().findall('.//testcase');failed=[c.attrib['name'] for c in cases if c.find('failure') is not None or c.find('error') is not None];skipped=[c.attrib['name'] for c in cases if c.find('skipped') is not None]
    stable=source==inputs() and all(sha(a.build/n)==h for n,h in binaries.items());ok=run.returncode==0 and not failed and not skipped and stable and {c.attrib['name'] for c in cases}==selected
    result={'phase':'14.10G','status':'PASS' if ok else 'FAIL','configuration':a.config,'suite':'phase','tests':len(cases),'testNames':sorted(selected),'failures':failed,'skipped':skipped,'exitCode':run.returncode,'stableSourcesAndBinaries':stable,'inputs':source,'binaries':binaries,'capture':False,'mediaFoundation':False,'fullWindowsAudio':False,'earlierPhasesRevalidated':False,'sourceScope':'Source/configuration/test/generator hashes; only audio_* tests execute'}
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps({k:result[k] for k in ('phase','status','configuration','suite','tests','failures','skipped','stableSourcesAndBinaries')}));return 0 if ok else 1
if __name__=='__main__':raise SystemExit(main())
