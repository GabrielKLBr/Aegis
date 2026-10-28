"""Build protocol 7 guest playback bundles, with no host paths or capture."""
from pathlib import Path
import argparse,json
import internet_runtime
def bundle(build,config,output,reference,schema_reference,application=None,assets=()):
    build=Path(build)
    return internet_runtime.bundle(build,config,output,reference,schema_reference,application or build/'guest/audio'/config/'guardian-audio-client.exe',assets=assets,kernel=build/'guest'/config/'guardian-kernel-audio.gk',native_audio=True)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','reference','schema-reference','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--application',type=Path);p.add_argument('--asset',type=Path,action='append',default=[]);a=p.parse_args()
    print(json.dumps(bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.asset)))
if __name__=='__main__':main()
