"""Pin unchanged independent Microsoft sources; local executable receipts are not implementation proof."""
from pathlib import Path
import argparse,json
import nt_compatibility as nt
CONFIG=nt.ROOT/'config/software-graphics-corpus.json'
def stamp(source,binary):
    expected=json.loads(CONFIG.read_bytes());directory=Path(source)
    files={name:nt.sha(nt.regular(directory/name,1<<20)) for name in expected['files']}
    nt.require(files=={k:v['sha256'] for k,v in expected['files'].items()},'Independent Direct2D source changed')
    receipt={'phase':'14.10F','application':'Microsoft-Direct2DHelloWorld','commit':expected['commit'],'files':expected['files'],'binarySha256':nt.sha(nt.regular(binary,8<<20)),'unchangedSources':True}
    Path(binary).with_suffix('.exe.source.json').write_bytes(nt.canonical(receipt));return receipt
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--binary',type=Path,required=True);a=p.parse_args();print(json.dumps(stamp(a.source,a.binary)))
