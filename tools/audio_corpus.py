"""Verify independent sources/binaries without redistributing or executing inputs."""
from pathlib import Path
import argparse,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def receipt(source,binary):
    record=json.loads((ROOT/'config/audio-corpus.json').read_bytes())
    if sha(source)!=record['sha256']:raise ValueError('Independent audio source changed')
    result={'source':record,'binarySha256':sha(binary),'sourceUnchanged':True}
    Path(binary).with_suffix('.exe.source.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result
def verify(binary):
    record=json.loads(Path(binary).with_suffix('.exe.source.json').read_bytes())
    if record['source']!=json.loads((ROOT/'config/audio-corpus.json').read_bytes()) or record['binarySha256']!=sha(binary) or not record['sourceUnchanged']:raise ValueError('Independent audio receipt mismatch')
    return record
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--binary',type=Path,required=True);a=p.parse_args();print(json.dumps(receipt(a.source,a.binary)))
