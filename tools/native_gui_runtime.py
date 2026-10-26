"""Phase 14.10A: an explicit native GUI command fixture, not general USER32 passthrough."""
from pathlib import Path
import argparse,json
import internet_runtime
def bundle(build,config,output,reference,schema_reference,application=None):
    build=Path(build)
    return internet_runtime.bundle(build,config,output,reference,schema_reference,application or build/'guest/native-gui'/config/'guardian-native-gui-demo.exe',kernel=build/'guest'/config/'guardian-kernel-native-gui.gk',native_gui=True)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','output','reference','schema-reference'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',choices=('Release','Debug'),required=True);p.add_argument('--application',type=Path);a=p.parse_args();print(json.dumps(bundle(a.build,a.config,a.output,a.reference,a.schema_reference,a.application)))
if __name__=='__main__':main()
