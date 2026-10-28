from pathlib import Path
import argparse,json,re,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import expanded_gdi_runtime as runtime,nt_compatibility as nt
from NativeControlsDriver import Driver
from NativeUserIntegrationTests import own_helper

def main():
    p=argparse.ArgumentParser()
    for name in ('build','reference','schema-reference','evidence'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--config',required=True);a=p.parse_args();build=a.build.resolve();out=a.evidence/str(time.time_ns());out.mkdir(parents=True);rows=[]
    reference=subprocess.run([str(build/a.config/'expanded_gdi_widgets_reference.exe')],capture_output=True,text=True,timeout=20);assert reference.returncode==0,reference.stdout+reference.stderr;expected=re.search(r'CONTROL_RESULT .+',reference.stdout).group();(out/'reference.log').write_text(reference.stdout+reference.stderr)
    pad_ref=subprocess.run([str(build/a.config/'expanded_gdi_pad_reference.exe')],capture_output=True,text=True,timeout=20);assert not pad_ref.returncode,pad_ref.stdout+pad_ref.stderr
    gdi_ref=subprocess.run([str(build/a.config/'native_gdi_reference.exe')],capture_output=True,text=True,timeout=20);assert not gdi_ref.returncode;gdi_expected=re.search(r'GDI_RESULT .+',gdi_ref.stdout).group()
    sentinel=out/'native-host-only.txt';sentinel.write_text('HOST PRIVATE SENTINEL',encoding='ascii');sentinel_hash=nt.sha(sentinel.read_bytes())
    ansi_mode=out/'ansi-mode.txt';ansi_mode.write_text('ANSI dialog fixture',encoding='ascii')
    data=out/'native-data.txt';data.write_text('MiniNT native file fixture\n',encoding='ascii')
    cases=[('gdi-normal','demo',True),('widgets-host-path','widgets',True),('widgets-open','widgets',True),('widgets-ansi','widgets',True),('widgets-cancel','widgets',True),('pad-open','pad',True),('pad-cancel','pad',True),('threads','threads',True),('guest-crash','crash',False),('guest-deadline','hang',False),('helper-death','hang',False)]
    for label,mode,passed in cases:
        folder=out/label;folder.mkdir();bundle=folder/'bundle';manifest=runtime.bundle(build,a.config,bundle,a.reference,a.schema_reference,build/'guest/expanded-gdi'/a.config/('guardian-expanded-gdi-'+mode+'.exe'),assets=([data,ansi_mode] if label=='widgets-ansi' else [data]));assert manifest['phase']=='14.10E' and manifest['nativeGuiRuntime']['protocol']==5
        seconds=1 if label=='guest-deadline' else 20 if passed else 5
        cmd=[str(build/a.config/'guardian-desktop.exe'),str(build/'guest'/a.config/'guardian-kernel-expanded-gdi.gk'),str(bundle/'runtime.gpv'),str(bundle/'manifest.json'),str(bundle/'binding.gnt'),str(folder/'session'),'--expanded-gdi','--seconds',str(seconds)]
        proc=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace');driver=Driver(proc.pid,helper='guardian-expanded-gdi-host.exe');stage=0;started=time.monotonic();captured=False;disabled=False;killed=None
        try:
            if label=='helper-death':killed=own_helper(proc.pid,'kill','guardian-expanded-gdi-host.exe')
            elif label=='threads':own_helper(proc.pid,'burst','guardian-expanded-gdi-host.exe')
            elif passed and mode in ('widgets','pad'):
                while proc.poll() is None and time.monotonic()-started<seconds+3:
                    windows=driver.windows();main=next((w for w in windows if w['title'].endswith('14.10D Widgets' if mode=='widgets' else '14.10D Pad')),None);modal=next((w for w in windows if w['title'].endswith('14.10D Guest files') or w['title'].endswith('14.10D Confirm') or w['title'].endswith('14.10D Pad confirm')),None)
                    if main and not main['enabled']:disabled=True
                    if stage==0 and main and main['enabled']:
                        hwnd=main['hwnd'];driver.resize(hwnd,560,380 if mode=='widgets' else 260)
                        if mode=='widgets':
                            assert driver.text(driver.child(hwnd,303))=='Input ready';menu=driver.u.GetMenu(hwnd);assert menu and driver.u.GetMenuItemID(driver.u.GetSubMenu(menu,0),0)==31;driver.post(hwnd,0x111,31,0)
                        else:
                            edit=driver.child(hwnd,202)
                            for ch in 'abc':driver.post(edit,0x102,ord(ch),1)
                            driver.click(driver.child(hwnd,201))
                        stage=1
                    elif stage==1 and modal and driver.u.GetDlgItem(modal['hwnd'],2 if label.endswith('cancel') else 1):
                        if mode=='widgets':
                            if label=='widgets-host-path':driver.set_text(driver.child(modal['hwnd'],10),str(sentinel.resolve()))
                            driver.click(driver.child(modal['hwnd'],2 if label.endswith('cancel') else 1));stage=3 if label.endswith('cancel') or label=='widgets-host-path' else 2
                        else:driver.click(driver.child(modal['hwnd'],2 if label.endswith('cancel') else 1));stage=3
                    elif stage==2 and modal and modal['title'].endswith('14.10D Confirm') and driver.u.GetDlgItem(modal['hwnd'],1):
                        driver.click(driver.child(modal['hwnd'],1));stage=3
                    elif stage==3 and main and main['enabled'] and not modal:
                        driver.capture(main['hwnd'],folder/'native-client.ppm');captured=True;driver.post(main['hwnd'],0x10);stage=4
                    time.sleep(.02)
            stdout,stderr=proc.communicate(timeout=seconds+18)
        finally:
            if proc.poll() is None:proc.kill();proc.communicate()
        (folder/'host.log').write_text(stdout+stderr,encoding='utf-8');(folder/'native-history.json').write_bytes(nt.canonical({'inputs':driver.inputs,'history':driver.history}))
        report=json.loads((folder/'session/expanded-gdi.json').read_bytes());assert report['helperExited'] and report['restrictions']==31 and not report['hostHandlesInGuest'] and report['nativeGuiBackend']=='win32-expanded-gdi' and not report['pixelPresenterFallback'] and report['guestWindowPixelsTransferred']==0
        if passed:
            assert proc.returncode==0,stdout+stderr;assert report['completed'] and not report['helperKilled'] and report['liveAtLastReply']==[0,0,0] and report['gdiLive']==[0,0,0] and report['bitmapBytes']==0
            boot=json.loads((folder/'session/boot.json').read_bytes())['boots'][0];trace=json.loads((folder/'session/analysis.json').read_bytes());assert boot['success'] and trace['complete'] and not any(trace[k] for k in ('dropped','pending','rejected'))
            if mode=='widgets':assert expected in stdout and 'failures=0 PASS' in stdout and ('DIALOG_RESULT opened=0 confirmed=0' if label.endswith('cancel') or label=='widgets-host-path' else 'DIALOG_RESULT opened=1 confirmed=1') in stdout,stdout
            elif mode=='pad':assert ('PAD_RESULT text=abc confirmed=0' if label.endswith('cancel') else 'PAD_RESULT text=abc confirmed=1') in stdout,stdout
            elif mode=='demo':assert gdi_expected in stdout,stdout
            else:assert 'Native USER32 interthread return' in stdout,stdout
            if mode in ('widgets','pad'):assert stage==4 and captured and disabled,(stage,captured,disabled)
            if label=='widgets-open':assert any(e['domain']=='file' and e['operation']=='read' and e['resource'].lower().endswith('native-data.txt') and not e['status'] for e in trace['events'])
        else:assert proc.returncode and not report['completed']
        if killed:assert report['helperPid']==killed and report['helperExitCode']==19
        assert nt.sha(sentinel.read_bytes())==sentinel_hash
        rows.append({'case':label,'status':'PASS','inputEvents':driver.inputs,'completed':report['completed']})
    (out/'result.json').write_bytes(nt.canonical({'status':'PASS','phase':'14.10E','configuration':a.config,'reference':expected,'cases':rows,'guestCodeExecutedOnHost':False,'pixelFallback':False}));print(json.dumps({'status':'PASS','cases':len(rows),'evidence':str(out)}))
if __name__=='__main__':main()
