"""Phase 14.10F: bounded software Direct2D/DirectWrite, WIC and WARP commands."""
from pathlib import Path
import argparse,json
import internet_runtime

def bundle(build,config,output,reference,schema_reference,application=None,assets=(),extras=()):
    build=Path(build)
    return internet_runtime.bundle(build,config,output,reference,schema_reference,application or build/'guest/software-graphics'/config/'guardian-software-graphics-scene.exe',extras=extras,assets=assets,kernel=build/'guest'/config/'guardian-kernel-software-graphics.gk',native_software=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','output','reference','schema-reference'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--application',type=Path);p.add_argument('--asset',type=Path,action='append',default=[]);a=p.parse_args()
    print(json.dumps(bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.asset)))
if __name__=='__main__':main()
