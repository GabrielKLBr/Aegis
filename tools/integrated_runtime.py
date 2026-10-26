"""Build a phase 12F package; native HWNDs remain confined to its host presenter."""
from pathlib import Path
import argparse,json
import internet_runtime,nt_compatibility as nt


def bundle(build,config,output,reference,schema_reference,application=None,extras=(),assets=()):
    build=Path(build);application=Path(application) if application else build/'guest/integrated'/config/'guardian-integrated-demo.exe'
    kernel=build/'guest'/config/'guardian-kernel-integrated.gk'
    nt.require(b'GuardianWindowBridge/1/guest' in nt.regular(kernel,4<<20),'Integrated kernel required')
    return internet_runtime.bundle(build,config,output,reference,schema_reference,application,extras,assets,kernel=kernel,presentation=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--schema-reference',type=Path,required=True);p.add_argument('--application',type=Path);p.add_argument('--extra',type=Path,action='append',default=[]);p.add_argument('--asset',type=Path,action='append',default=[]);a=p.parse_args()
    print(json.dumps(bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application,a.extra,a.asset)))
if __name__=='__main__':main()
