"""Exercise only windows owned by this test's restricted native helper child."""
import ctypes,json,time
from ctypes import wintypes as w
class Entry(ctypes.Structure):
    _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
class Driver:
    def __init__(self,parent,helper="guardian-native-controls-host.exe",direct=False):
        self.parent=parent;self.helper=helper;self.pid=parent if direct else None;self.history=[];self.start=time.monotonic();self.inputs=0
        self.k=ctypes.WinDLL('kernel32',use_last_error=True);self.u=ctypes.WinDLL('user32',use_last_error=True)
        self.callback=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
        signatures={'EnumWindows':([self.callback,w.LPARAM],w.BOOL),'GetWindowThreadProcessId':([w.HWND,ctypes.POINTER(w.DWORD)],w.DWORD),'GetWindowTextW':([w.HWND,w.LPWSTR,ctypes.c_int],ctypes.c_int),'GetClassNameW':([w.HWND,w.LPWSTR,ctypes.c_int],ctypes.c_int),'GetWindowRect':([w.HWND,ctypes.POINTER(w.RECT)],w.BOOL),'GetClientRect':([w.HWND,ctypes.POINTER(w.RECT)],w.BOOL),'IsWindowVisible':([w.HWND],w.BOOL),'IsWindowEnabled':([w.HWND],w.BOOL),'IsWindow':([w.HWND],w.BOOL),'GetDpiForWindow':([w.HWND],w.UINT),'GetDlgItem':([w.HWND,ctypes.c_int],w.HWND),'PostMessageW':([w.HWND,w.UINT,w.WPARAM,w.LPARAM],w.BOOL),'SendMessageTimeoutW':([w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,ctypes.POINTER(ctypes.c_size_t)],ctypes.c_size_t),'SetWindowPos':([w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT],w.BOOL),'GetMenu':([w.HWND],w.HMENU),'GetSubMenu':([w.HMENU,ctypes.c_int],w.HMENU),'GetMenuItemID':([w.HMENU,ctypes.c_int],w.UINT)}
        for name,(args,result) in signatures.items():f=getattr(self.u,name);f.argtypes=args;f.restype=result
        self.k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];self.k.CreateToolhelp32Snapshot.restype=w.HANDLE
        self.k.Process32FirstW.argtypes=[w.HANDLE,ctypes.POINTER(Entry)];self.k.Process32NextW.argtypes=self.k.Process32FirstW.argtypes;self.k.CloseHandle.argtypes=[w.HANDLE]
    def discover(self):
        handle=self.k.CreateToolhelp32Snapshot(2,0);entry=Entry();entry.size=ctypes.sizeof(entry)
        try:
            found=self.k.Process32FirstW(handle,ctypes.byref(entry))
            while found:
                if entry.parent==self.parent and entry.exe==self.helper:self.pid=entry.pid;return
                found=self.k.Process32NextW(handle,ctypes.byref(entry))
        finally:self.k.CloseHandle(handle)
    def own(self,hwnd):
        pid=w.DWORD();self.u.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));return bool(self.pid and pid.value==self.pid)
    def windows(self):
        if not self.pid:self.discover()
        rows=[]
        def read(hwnd,_):
            if self.own(hwnd) and self.u.IsWindowVisible(hwnd):
                title=ctypes.create_unicode_buffer(256);kind=ctypes.create_unicode_buffer(128);r=w.RECT();self.u.GetWindowTextW(hwnd,title,256);self.u.GetClassNameW(hwnd,kind,128);self.u.GetWindowRect(hwnd,ctypes.byref(r));dpi=self.u.GetDpiForWindow(hwnd);assert dpi
                rows.append({'hwnd':hwnd,'title':title.value,'class':kind.value,'enabled':bool(self.u.IsWindowEnabled(hwnd)),'dpi':dpi,'x':round(r.left*96/dpi),'y':round(r.top*96/dpi),'width':round((r.right-r.left)*96/dpi),'height':round((r.bottom-r.top)*96/dpi)})
            return True
        self.u.EnumWindows(self.callback(read),0);self.history.append({'seconds':time.monotonic()-self.start,'windows':rows});return rows
    def child(self,hwnd,id):
        assert self.own(hwnd);child=self.u.GetDlgItem(hwnd,id);assert child and self.own(child);return child
    def send(self,hwnd,msg,wp=0,lp=0):
        assert self.own(hwnd);result=ctypes.c_size_t();assert self.u.SendMessageTimeoutW(hwnd,msg,wp,lp,2,1000,ctypes.byref(result)),(msg,ctypes.get_last_error());return ctypes.c_ssize_t(result.value).value
    def post(self,hwnd,msg,wp=0,lp=0):
        assert self.own(hwnd) and self.u.PostMessageW(hwnd,msg,wp,lp);self.inputs+=1
    def click(self,hwnd):self.post(hwnd,0x201,1,8|(8<<16));self.post(hwnd,0x202,0,8|(8<<16))
    def text(self,hwnd):
        buf=ctypes.create_unicode_buffer(256);self.send(hwnd,0xD,256,ctypes.addressof(buf));return buf.value
    def set_text(self,hwnd,text):
        buf=ctypes.create_unicode_buffer(text);self.send(hwnd,0xC,0,ctypes.addressof(buf));self.inputs+=1
    def resize(self,hwnd,width,height):
        assert self.own(hwnd);dpi=self.u.GetDpiForWindow(hwnd);assert self.u.SetWindowPos(hwnd,None,160,120,round(width*dpi/96),round(height*dpi/96),0x14);self.inputs+=1
    def capture(self,hwnd,path,presented=False):
        assert self.own(hwnd)
        class Header(ctypes.Structure):_fields_=[('size',w.DWORD),('width',w.LONG),('height',w.LONG),('planes',w.WORD),('bits',w.WORD),('compression',w.DWORD),('bytes',w.DWORD),('x',w.LONG),('y',w.LONG),('used',w.DWORD),('important',w.DWORD)]
        r=w.RECT();assert self.u.GetClientRect(hwnd,ctypes.byref(r));width,height=r.right,r.bottom;assert 0<width<=2048 and 0<height<=2048
        g=ctypes.WinDLL('gdi32',use_last_error=True);g.CreateCompatibleDC.argtypes=[w.HDC];g.CreateCompatibleDC.restype=w.HDC;g.CreateDIBSection.argtypes=[w.HDC,ctypes.POINTER(Header),w.UINT,ctypes.POINTER(ctypes.c_void_p),w.HANDLE,w.DWORD];g.CreateDIBSection.restype=w.HBITMAP;g.SelectObject.argtypes=[w.HDC,w.HANDLE];g.SelectObject.restype=w.HANDLE;g.DeleteObject.argtypes=[w.HANDLE];g.DeleteDC.argtypes=[w.HDC];self.u.PrintWindow.argtypes=[w.HWND,w.HDC,w.UINT]
        dc=g.CreateCompatibleDC(None);header=Header();header.size=ctypes.sizeof(header);header.width=width;header.height=-height;header.planes=1;header.bits=32;pixels=ctypes.c_void_p();bitmap=g.CreateDIBSection(None,ctypes.byref(header),0,ctypes.byref(pixels),None,0);assert dc and bitmap
        old=g.SelectObject(dc,bitmap)
        try:
            if presented:
                self.u.GetDC.argtypes=[w.HWND];self.u.GetDC.restype=w.HDC;self.u.ReleaseDC.argtypes=[w.HWND,w.HDC]
                g.BitBlt.argtypes=[w.HDC,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.HDC,ctypes.c_int,ctypes.c_int,w.DWORD]
                assert self.u.SetWindowPos(hwnd,None,0,0,0,0,0x13)
                source=self.u.GetDC(hwnd);assert source
                try:assert g.BitBlt(dc,0,0,width,height,source,0,0,0x00CC0020)
                finally:self.u.ReleaseDC(hwnd,source)
            else:assert self.u.PrintWindow(hwnd,dc,1)
            g.GdiFlush();raw=ctypes.string_at(pixels,width*height*4);rgb=bytearray(width*height*3)
            for i in range(width*height):rgb[i*3:i*3+3]=raw[i*4:i*4+3][::-1]
            path.write_bytes(f'P6\n{width} {height}\n255\n'.encode()+rgb);return sum(max(raw[i:i+3])<90 for i in range(0,len(raw),4))
        finally:g.SelectObject(dc,old);g.DeleteObject(bitmap);g.DeleteDC(dc)
