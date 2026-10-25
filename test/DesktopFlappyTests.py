"""Acceptance of the unmodified user-provided Flappy Window 1.5, inside MiniNT only."""
from pathlib import Path
import argparse,json,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import desktop_runtime as runtime
import nt_compatibility as nt
import pe_volume
from DesktopIntegrationTests import execute
APPLICATION_SHA='f6f41a43ccaab59dc278d90127f76d96b75d6369dd5283a234cb5ec5765fefa6'
INPUT='400 down 32 57 0\n480 up 32 57 0\n900 down 32 57 0\n980 up 32 57 0\n1400 down 32 57 0\n1480 up 32 57 0\n1900 down 32 57 0\n1980 up 32 57 0\n2400 close\n2900 close\n3400 close\n3900 close\n4400 close\n4900 close\n5400 close\n5900 close\n6400 close\n'

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--config',required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--application',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();original=nt.regular(a.application,8<<20);assert nt.sha(original)==APPLICATION_SHA,'The accepted Flappy input changed';settings=a.application.with_name('settings.txt');icon=a.application.with_name('icon.ico');input_hashes={str(path):nt.sha(path.read_bytes()) for path in (a.application,settings,icon)};a.evidence.mkdir(parents=True,exist_ok=True);folder=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));script=folder/'input.txt';script.write_text(INPUT,encoding='ascii');variant=folder/'variant';variant.mkdir();alternate=variant/'settings.txt';text=settings.read_text();assert 'BIRD_SIZE=20' in text and 'PIPE_WIDTH=80' in text;alternate.write_text(text.replace('BIRD_SIZE=20','BIRD_SIZE=28').replace('PIPE_WIDTH=80','PIPE_WIDTH=64').replace('PIPE_SPEED=4','PIPE_SPEED=2'),encoding='ascii');rows=[]
    for index,(source,bird_width,pipe_width) in enumerate([(settings,20,80),(alternate,28,64),(settings,20,80)]):
        bundle=folder/str(index);manifest=runtime.bundle(a.build,a.config,bundle,a.reference,a.schema_reference,a.application,[source,icon]);assert manifest['applicationSha256']==APPLICATION_SHA;pe_volume.verify(bundle);session=folder/(str(index)+'-session');boot,trace,desktop=execute(a.build,a.config,bundle,session,script);assert 9<=desktop['scriptConsumed']<=len(INPUT.splitlines());frames=desktop['history'];birds=[(f['ns'],w) for f in frames for w in f['windows'] if w['title']=='Bird'];pipes=[w for f in frames for w in f['windows'] if w['title']=='Top Pipe'];assert birds and pipes and all(w['width']==bird_width for _,w in birds) and all(w['width']==pipe_width for w in pipes)
        assert min(w['x'] for w in pipes)<max(w['x'] for w in pipes)
        after=[w['y'] for ns,w in birds if 500000000<=ns-desktop['scriptOriginNs']<=1400000000];assert len(after)>=2 and any(b<a for a,b in zip(after,after[1:])),'Physical flap must produce upward movement'
        assert any(any(w['title']=='Score' for w in f['windows']) and any(w['title']=='Bottom Pipe' for w in f['windows']) for f in frames)
        captured=list(session.glob('frame-*.ppm'));assert captured
        glyphs=False
        for path in captured:
            raw=path.read_bytes();header=b'P6\n1280 720\n255\n';assert raw.startswith(header) and len(raw)==len(header)+1280*720*3;pixels=raw[len(header):]
            dark=sum(pixels[(y*1280+x)*3:(y*1280+x)*3+3]==bytes(3) for y in range(10,70) for x in range(10,110));glyphs|=dark>=12
        assert glyphs,'Score text must produce visible pixels'
        assert any(e['domain']=='file' and e['operation']=='read' and e['resource'].lower().endswith('settings.txt') and not e['status'] for e in trace['events'])
        rows.append({'status':'PASS','applicationSha256':APPLICATION_SHA,'settingsSha256':nt.sha(source.read_bytes()),'birdWidth':bird_width,'pipeWidth':pipe_width,'frames':desktop['frames'],'events':len(trace['events']),'inputs':desktop['inputs'],'capturedFrames':len(captured),'exitReproducible':True,'executedOnHost':False})
    assert input_hashes=={path:nt.sha(Path(path).read_bytes()) for path in input_hashes}
    (folder/'result.json').write_bytes(nt.canonical({'schema':1,'phase':'14.7','configuration':a.config,'status':'PASS','cases':rows,'inputs':input_hashes,'completeWindowsGui':False,'originalApplicationRedistributed':False}));print(json.dumps({'status':'PASS','phase':'14.7','flappyCases':len(rows)}))
if __name__=='__main__':main()
