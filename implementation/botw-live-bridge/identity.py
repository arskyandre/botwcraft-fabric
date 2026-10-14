"""Owned Cemu lab launch/injection and identity-scoped cleanup. No UI automation."""
import argparse, ctypes as C, ctypes.wintypes as W, json, pathlib, os
k=C.WinDLL('kernel32',use_last_error=True)
def api(name, restype, args):
    f=getattr(k,name); f.restype=restype; f.argtypes=args; return f
openp=api('OpenProcess',W.HANDLE,[W.DWORD,W.BOOL,W.DWORD])
alloc=api('VirtualAllocEx',C.c_void_p,[W.HANDLE,C.c_void_p,C.c_size_t,W.DWORD,W.DWORD])
write=api('WriteProcessMemory',W.BOOL,[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)])
remote=api('CreateRemoteThread',W.HANDLE,[W.HANDLE,C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,W.DWORD,C.c_void_p])
getmodule=api('GetModuleHandleW',W.HMODULE,[W.LPCWSTR])
getproc=api('GetProcAddress',C.c_void_p,[W.HMODULE,C.c_char_p])
wait=api('WaitForSingleObject',W.DWORD,[W.HANDLE,W.DWORD])
close=api('CloseHandle',W.BOOL,[W.HANDLE])
free=api('VirtualFreeEx',W.BOOL,[W.HANDLE,C.c_void_p,C.c_size_t,W.DWORD])
terminate=api('TerminateProcess',W.BOOL,[W.HANDLE,W.UINT])
times=api('GetProcessTimes',W.BOOL,[W.HANDLE,C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME)])
imagepath=api('QueryFullProcessImageNameW',W.BOOL,[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)])
def creation(h):
    ts=[W.FILETIME() for _ in range(4)]
    if not times(h,*[C.byref(t) for t in ts]): raise C.WinError(C.get_last_error())
    return ts[0].dwLowDateTime+(ts[0].dwHighDateTime<<32)
def verify(h,data):
    n=W.DWORD(32768); buf=C.create_unicode_buffer(n.value)
    if not imagepath(h,0,buf,C.byref(n)): raise C.WinError(C.get_last_error())
    if creation(h)!=data['creation_filetime'] or pathlib.Path(buf.value).resolve()!=pathlib.Path(data['exe']).resolve():
        raise RuntimeError('Process identity differs; cleanup refused')
def inject(h,path):
    buf=C.create_unicode_buffer(str(path))
    mem=alloc(h,None,C.sizeof(buf),0x3000,4)
    if not mem: raise C.WinError(C.get_last_error())
    thread=None
    try:
        if not write(h,mem,buf,C.sizeof(buf),None): raise C.WinError(C.get_last_error())
        thread=remote(h,None,0,getproc(getmodule('kernel32'),b'LoadLibraryW'),mem,0,None)
        if not thread: raise C.WinError(C.get_last_error())
        if wait(thread,10000)!=0: raise RuntimeError('DLL load thread timeout')
    finally:
        if thread: close(thread)
        # Released on process exit; tiny per-session buffer intentionally retained.
PathLab=pathlib.Path(__file__).resolve().parent/'lab'

def main():
    p=argparse.ArgumentParser(); p.add_argument('action',choices=['stop','self-check']); p.add_argument('--lab',type=pathlib.Path,default=pathlib.Path(__file__).parent/'lab')
    a=p.parse_args(); lab=a.lab.resolve(); identity=lab/'process.json'
    assert lab==PathLab.resolve(),'Only the lab of the bridge is supported'
    if a.action=='stop':
        data=json.loads(identity.read_text()); h=openp(0x100001,False,data['pid'])
        result={'identity':data,'stopped':False}
        if h:
            close(h); h=openp(0x100401,False,data['pid'])
            try:
                verify(h,data)
                if wait(h,0)==0: result['already_exited']=True
                else: result['stopped']=bool(terminate(h,0)); result['wait']=wait(h,10000)
            finally: close(h)
        else: result['already_exited']=True
        (lab/'cleanup.json').write_text(json.dumps(result,indent=2)); print(json.dumps(result))
    else:
        import sys
        h=openp(0x400,False,os.getpid())
        try:
            data={'creation_filetime':creation(h),'exe':sys.executable}; verify(h,data)
            for changed in [dict(data,creation_filetime=0),dict(data,exe=str(lab/'another.exe'))]:
                try: verify(h,changed)
                except RuntimeError: pass
                else: raise AssertionError('wrong identity passed')
        finally: close(h)
        print('creation-time and executable identity rejection passed; no process stopped')
if __name__=='__main__': main()
