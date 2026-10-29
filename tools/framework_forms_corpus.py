"""Fetch a pinned source example into a local corpus, never execute or redistribute it."""
from pathlib import Path
import base64
import hashlib
import json
import re
import urllib.request

ROOT=Path(__file__).resolve().parents[1]

def materialize(folder):
    origin=json.loads((ROOT/'config/framework-forms-corpus.json').read_bytes())
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True);source=folder/'Window.cs'
    if not source.exists():
        url='https://api.github.com/repos/'+origin['sourceRepository']+'/git/blobs/'+origin['gitBlobSha1']
        request=urllib.request.Request(url,headers={'User-Agent':'Aegis-phase14.11-corpus','Accept':'application/vnd.github+json'})
        with urllib.request.urlopen(request,timeout=30) as response:raw=response.read(256*1024+1)
        if len(raw)>256*1024:raise ValueError('Corpus download exceeds its limit')
        envelope=json.loads(raw)
        if envelope.get('encoding')!='base64':raise ValueError('Unexpected corpus encoding')
        blob=base64.b64decode(''.join(envelope['content'].split()),validate=True)
        digest=hashlib.sha1(b'blob '+str(len(blob)).encode()+b'\0'+blob).hexdigest()
        if digest!=origin['gitBlobSha1']:raise ValueError('Corpus Git blob does not match its pin')
        match=re.search(r'## C#\s+```c#\r?\n([\s\S]*?)\r?\n```',blob.decode('utf-8'))
        if not match:raise ValueError('C# example missing')
        content=(match[1]+'\n').encode('utf-8')
        if hashlib.sha256(content).hexdigest()!=origin['sourceSha256']:raise ValueError('Extracted source pin mismatch')
        source.write_bytes(content)
    if hashlib.sha256(source.read_bytes()).hexdigest()!=origin['sourceSha256']:raise ValueError('Local corpus pin mismatch')
    return source

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path)
    print(materialize(parser.parse_args().folder))
