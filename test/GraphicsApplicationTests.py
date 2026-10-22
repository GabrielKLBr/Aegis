"""Independent PPM/pixel oracle for the two real CPL3 reference applications."""
import hashlib,json,pathlib,subprocess,sys

def verify(output):
    root=pathlib.Path(output);boot=json.loads((root/'boot.json').read_text())['boots'][0]
    frame=json.loads((root/'graphics.json').read_text());raw=(root/'desktop.ppm').read_bytes()
    header=b'P6\n320 240\n255\n';assert raw.startswith(header) and len(raw)==len(header)+320*240*3
    pixels=raw[len(header):]
    def color(x,y):return tuple(pixels[(y*320+x)*3:(y*320+x)*3+3])
    expected={(319,239):(23,37,54),(12,12):(86,97,109),(126,92):(40,108,165),
              (40,65):(208,74,50),(39,65):(225,237,245),(141,140):(37,54,68),
              (155,180):(245,243,232),(126,91):(225,237,245)}
    for point,rgb in expected.items():assert color(*point)==rgb,(point,color(*point),rgb)
    assert boot['success'] and not boot['timedOut'] and boot['graphicsReportValid'] and boot['graphicsChecks']==63
    assert boot['graphicsInputs']==12 and boot['graphicsFrames']==6 and boot['graphicsDenied']==7
    assert frame['windows']==2 and frame['focus']==2 and frame['permissions']==0 and frame['accelerated'] is False
    assert [(w['handle'],w['title'],w['dpi'],w['modal']) for w in frame['windowInfo']]==[(1,'PAINT',96,0),(2,'NOTES',144,0)]
    # Independent FNV-1a over the declared BGRA wire layout, no C++ helper.
    h=14695981039346656037
    for at in range(0,len(pixels),3):
        for b in (pixels[at+2],pixels[at+1],pixels[at],255):h=((h^b)*1099511628211)&0xFFFFFFFFFFFFFFFF
    assert h==frame['pixelHash']==boot['graphicsHash']
    return {'status':'passed','pixel_checks':len(expected),'ppm_sha256':hashlib.sha256(raw).hexdigest(),'pixel_hash':h}

def main():
    exe,image,output=sys.argv[1:];p=subprocess.run([exe,image,output],capture_output=True,text=True,timeout=25)
    assert p.returncode==0,p.stdout+p.stderr
    result=verify(output);pathlib.Path(output,'oracle.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Two graphical CPL3 applications: 8 independent pixels, full framebuffer hash, input/focus/dialog/bridge behavior PASS')
if __name__=='__main__':main()
