"""Managed graphical guest: pause, durable snapshot, clone and restore."""
import json,pathlib,subprocess,sys,time
exe,image,output=map(pathlib.Path,sys.argv[1:]);exe=exe.resolve();image=image.resolve();output=output.resolve();output.mkdir(parents=True,exist_ok=True)
root=output/('catalog-'+str(time.time_ns()));commands=[];boots=[]
def run(*args):
 # Explicit fixture policy; production automatic reserve stays unchanged.
 if args and args[0]=="create": args=(*args,"--host-reserve-mib","1024")
 p=subprocess.run([str(exe),'--root',str(root),*map(str,args)],capture_output=True,text=True,timeout=35)
 commands.append({'args':list(map(str,args)),'returncode':p.returncode,'out':p.stdout,'error':p.stderr})
 assert p.returncode==0,(args,p.stdout,p.stderr)
 return json.loads(p.stdout)
def settled(id):
 deadline=time.monotonic()+20
 while time.monotonic()<deadline:
  s=run('status',id)
  if s['state'] in ('stopped','fault'):
   assert s['state']=='stopped',s
   b=json.loads((root/id/'last-boot.json').read_text())['boots'][0];assert b['success'] and b['graphicsFrames']==6 and b['graphicsInputs']==12
   boots.append(b);return
  time.sleep(.05)
 raise AssertionError('guest did not finish')
try:
 run('create','paint',image,'graphics','--ram-mib','32','--timeout-ms','15000')
 run('start','paint','--paused');run('snapshot','paint','initial');run('clone','paint','notes');run('resume','paint');settled('paint')
 run('resume','notes');settled('notes');run('restore','paint','initial');run('resume','paint');settled('paint')
 assert len({b['graphicsHash'] for b in boots})==1
 captures=list(root.glob('*/graphics-history/*/graphics.json'));assert len(captures)>=3
 for p in captures:
  f=json.loads(p.read_text());assert f['permissions']==0 and f['accelerated'] is False
 result={'status':'passed','boots':len(boots),'capture_histories':len(captures),'restore_clone_pixels_identical':True,'clipboard_filedrop_printer_usb_shared_drive_disabled':True}
 (output/'graphics-manager.json').write_text(json.dumps(result,indent=2)+'\n');print('3 managed graphical boots; clone/restore and isolated capture histories PASS')
finally:
 for id in ('paint','notes'):
  subprocess.run([str(exe),'--root',str(root),'shutdown',id],capture_output=True,timeout=35)
 (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
