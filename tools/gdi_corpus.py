"""Pin independently compiled, reviewed window-only reference programs; never runs an input."""
from pathlib import Path
import argparse,json
import nt_compatibility as nt
CONFIG=nt.ROOT/'config/gdi-corpus.json'
def configuration():return json.loads(CONFIG.read_bytes())
def stamp(source,binary,application,variant):
    meta=configuration();expected=meta['applications'][application];source=Path(source);binary=Path(binary)
    nt.require(nt.sha(nt.regular(source,1<<20))==expected['sha256'],'Independent source changed: '+application)
    receipt={'phase':'14.10E','application':application,'variant':variant,'sourceSha256':expected['sha256'],'binarySha256':nt.sha(nt.regular(binary,8<<20)),'source':expected['source'],'reviewedWindowOnlyReference':True}
    binary.with_suffix('.exe.source.json').write_bytes(nt.canonical(receipt));return receipt
def verify(folder,application,variant):
    folder=Path(folder);name=application+('_a' if variant=='ANSI' else '')+'.exe';binary=folder/name
    receipt=json.loads(binary.with_suffix('.exe.source.json').read_bytes());expected=configuration()['applications'][application]
    nt.require(receipt['phase']=='14.10E' and receipt['application']==application and receipt['variant']==variant and receipt['sourceSha256']==expected['sha256'] and receipt['binarySha256']==nt.sha(nt.regular(binary,8<<20)) and receipt['reviewedWindowOnlyReference'] is True,'Independent binary/receipt mismatch')
    return binary,receipt
def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--binary',type=Path,required=True);p.add_argument('--application',choices=tuple(configuration()['applications']),required=True);p.add_argument('--variant',choices=('ANSI','Unicode'),required=True);a=p.parse_args();print(json.dumps(stamp(a.source,a.binary,a.application,a.variant)))
if __name__=='__main__':main()

