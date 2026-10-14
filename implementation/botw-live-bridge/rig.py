import launch
"""Small E-only scene controls, capture brackets and bounded window recorder."""
import argparse,ctypes,json,pathlib,subprocess,time,winsound
import local,session
def shutter():
    """The sound of a picture being taken, when local.json names one ("shutter_sound")."""
    sound=local.path('shutter_sound')
    if sound and sound.exists():winsound.PlaySound(str(sound),winsound.SND_FILENAME|winsound.SND_NODEFAULT)

ROOT=pathlib.Path(__file__).resolve().parent
ANCHORS={'front':(-560.1,127.5,1423.5),'edge':(-556.6,127.5,1426.8),'behind':(-556.31,127.5,1430.166)}
def qpc():
    value=ctypes.c_int64();ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(value));return value.value

def configure(lab,enabled,overlay,anchor):
    (lab/'render.txt').write_text(' '.join(map(str,(int(enabled),int(overlay),*anchor))))

def tail(lab):
    identity=json.loads((lab/'process.json').read_text())
    if not identity['probe_injected']:return []
    p=lab/'frames.jsonl'
    with p.open('rb') as f:
        # Full renderer evidence can exceed 32 KiB between one-second stats.
        f.seek(max(0,p.stat().st_size-524288));lines=f.read().decode(errors='replace').splitlines()[1:]
    return [json.loads(x) for x in lines if x.startswith('{') and x.endswith('}')]

def rects(hwnd):
    """The window and its client area (where the game is drawn) in screen pixels."""
    u=ctypes.windll.user32;w=(ctypes.c_long*4)();c=(ctypes.c_long*4)();origin=(ctypes.c_long*2)()
    u.GetWindowRect(ctypes.c_void_p(hwnd),ctypes.byref(w));u.GetClientRect(ctypes.c_void_p(hwnd),ctypes.byref(c));u.ClientToScreen(ctypes.c_void_p(hwnd),ctypes.byref(origin))
    return {'window':list(w),'client':[origin[0],origin[1],origin[0]+c[2],origin[1]+c[3]]}

def background(lab):
    """An automatic run while the computer is in use (native_route writes lab/background.txt): its window
    is on the secondary display and is never brought to the front, its input goes over the bridge's
    pipe (os_input.inject) and it is silent."""
    return mode(lab)==1

def mode(lab):
    """lab/background.txt: 0 a play session (main display, in front, the operating system's
    input, sound); 1 a background run; 2 an automatic run on the secondary display that still takes
    the front and the operating system's input (`--foreground`, should the pipe fail)."""
    try:return int((lab/'background.txt').read_text().split()[0])
    except (OSError,IndexError,ValueError):return 0

def displays():
    u=ctypes.windll.user32;found=[]
    class Info(ctypes.Structure):_fields_=[('size',ctypes.c_uint32),('monitor',ctypes.c_long*4),('work',ctypes.c_long*4),('flags',ctypes.c_uint32)]
    callback=ctypes.WINFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ssize_t)
    def each(monitor,dc,rect,data):
        info=Info(size=ctypes.sizeof(Info));u.GetMonitorInfoW(ctypes.c_void_p(monitor),ctypes.byref(info));found.append({'primary':bool(info.flags&1),'work':list(info.work)});return 1
    u.EnumDisplayMonitors(None,None,callback(each),0);return found

def work_area(lab):
    """Where the session's window belongs, as [left, top, right, bottom]: the secondary display for
    a background run, the main one otherwise or when there is one only."""
    found=displays();other=[d for d in found if not d['primary']]
    return (other[0] if mode(lab) and other else next(d for d in found if d['primary']))['work']

def place(hwnd,x,y,w,h):
    """Position and size without bringing the window to the front."""
    u=ctypes.WinDLL('user32',use_last_error=True);u.SetWindowPos.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
    u.ShowWindow(ctypes.c_void_p(hwnd),4) # shown as it was before any maximizing, not activated
    if not u.SetWindowPos(hwnd,None,x,y,w,h,0x14):raise ctypes.WinError(ctypes.get_last_error())

def maximize(lab):
    """Maximized, not fullscreen: every run is shown that way."""
    if background(lab):
        # Maximizing would bring the window to the front: it is laid over its display's work area instead.
        hwnd,identity=window(lab);x0,y0,x1,y1=work_area(lab);place(hwnd,x0,y0,x1-x0,y1-y0);return hwnd
    # Restored first: a window sized by `resize` while Cemu had opened it maximized still counts as
    # maximized, and maximizing it again does nothing (routes 85 to 87 stayed at 1280x760).
    hwnd,identity=window(lab);show=ctypes.windll.user32.ShowWindow;show(ctypes.c_void_p(hwnd),9);show(ctypes.c_void_p(hwnd),3);return hwnd

def capture(lab,out):
    before={'unix':time.time(),'qpc':qpc(),'tail':tail(lab)}
    hwnd,identity=window(lab);before['rects']=rects(hwnd)
    # The window's own rectangle of the screen. WinAppCLI takes it when local.json names it ("winapp"), as in the
    # runs the menu references come from; otherwise the same rectangle is read from the screen.
    exe=local.tool('winapp','winapp')
    if exe:
        result=launch.run([exe,'ui','screenshot','-w',str(hwnd),'--capture-screen','--output',str(out.resolve()),'--json'],check=True,capture_output=True,text=True)
        before['capture_metadata']=json.loads(result.stdout)
    else:
        from PIL import ImageGrab
        ImageGrab.grab(bbox=tuple(before['rects']['window']),all_screens=True).save(out.resolve());before['capture_metadata']={'method':'PIL ImageGrab of the window rectangle'}
    shutter();before['owned_cemu']=identity
    after={'unix':time.time(),'qpc':qpc(),'tail':tail(lab)}
    out.with_suffix('.json').write_text(json.dumps({'before':before,'after':after,'note':'Asynchronous window capture bracket; logged IDs bound the capture, not an exact presented-frame claim.'},indent=2))

def pulse(lab,action,ms=150):
    file=lab/'E-control.txt'
    try:file.write_text(action+'-'+str(time.time_ns()));time.sleep(ms/1000)
    finally:file.write_text('release-'+str(time.time_ns()))

def window(lab):
    identity=json.loads((lab/'process.json').read_text());h=session.b.openp(0x100400,False,identity['pid'])
    if not h:raise RuntimeError('Owned process absent')
    try:session.b.verify(h,identity)
    finally:session.b.close(h)
    # Cemu's main window: the visible top-level window of that process whose title begins with "Cemu"
    # (the largest, should a dialog of its own be open).
    u=ctypes.windll.user32;found=[]
    @ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
    def each(hwnd,_):
        pid=ctypes.c_ulong();u.GetWindowThreadProcessId(ctypes.c_void_p(hwnd),ctypes.byref(pid))
        if pid.value==identity['pid'] and u.IsWindowVisible(ctypes.c_void_p(hwnd)):
            title=ctypes.create_unicode_buffer(256);u.GetWindowTextW(ctypes.c_void_p(hwnd),title,256)
            if title.value.startswith('Cemu'):
                r=(ctypes.c_long*4)();u.GetWindowRect(ctypes.c_void_p(hwnd),ctypes.byref(r));found.append(((r[2]-r[0])*(r[3]-r[1]),hwnd))
        return True
    u.EnumWindows(each,0)
    assert found,'Cemu has no window yet'
    return int(max(found)[1]),identity

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['off','mesh','overlay','capture','resize','maximize','sweep']);p.add_argument('--anchor',choices=ANCHORS,default='edge');p.add_argument('--output',type=pathlib.Path);p.add_argument('--lab',type=pathlib.Path,default=session.LAB);p.add_argument('--width',type=int,default=1280);p.add_argument('--height',type=int,default=760);a=p.parse_args();lab=a.lab.resolve();hwnd,identity=window(lab)
    if a.action in ('off','mesh','overlay'):configure(lab,a.action!='off',a.action=='overlay',ANCHORS[a.anchor]);return
    if a.action=='maximize':maximize(lab);return
    if a.action=='resize':
        assert 640<=a.width<=1920 and 400<=a.height<=1080
        if background(lab):x0,y0,x1,y1=work_area(lab);place(hwnd,x0+304,y0+143,a.width,a.height);return
        u=ctypes.WinDLL('user32',use_last_error=True);u.SetWindowPos.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
        u.ShowWindow(ctypes.c_void_p(hwnd),9) # out of the maximized state Cemu may have reopened in
        if not u.SetWindowPos(hwnd,None,0,0,a.width,a.height,0x16):raise ctypes.WinError(ctypes.get_last_error())
        return
    assert a.output
    if a.action=='capture':capture(lab,a.output);return
    a.output.mkdir(parents=True,exist_ok=False)
    command=[local.needed('ffmpeg','ffmpeg'),'-hide_banner','-loglevel','warning','-f','lavfi','-i',f'gfxcapture=hwnd={hwnd}:capture_cursor=0:display_border=0:max_framerate=30:resize_mode=scale','-t','42','-vf','hwdownload,format=bgra,pad=ceil(iw/2)*2:ceil(ih/2)*2,format=yuv420p','-c:v','libx264','-preset','ultrafast','-crf','23',str(a.output/'sweep.mp4')]
    with (a.output/'recorder.log').open('w') as log:
        rec=launch.start(command,stdout=log,stderr=log)
        handle=session.b.openp(0x100400,False,rec.pid)
        try:creation=session.b.creation(handle)
        finally:session.b.close(handle)
        (a.output/'recorder-process.json').write_text(json.dumps({'pid':rec.pid,'creation_filetime':creation,'command':command,'owned_cemu':identity,'started_unix':time.time(),'started_qpc':qpc()},indent=2))
        try:
            for index,(anchor,action,ms) in enumerate([('front',None,0),('front','right',300),('front','left',300),('edge',None,0),('edge','right',800),('edge','left',800),('behind',None,0),('behind','right',300),('behind','left',300)]):
                configure(lab,True,False,ANCHORS[anchor]);time.sleep(.5)
                if action:pulse(lab,action,ms)
                time.sleep(1);capture(lab,a.output/f'{index:02}-{anchor}.png')
            configure(lab,True,True,ANCHORS['front']);time.sleep(1);capture(lab,a.output/'09-overlay.png')
            rec.wait(timeout=45)
            if rec.returncode:raise RuntimeError('Recorder failed; inspect recorder.log')
        finally:
            (lab/'E-control.txt').write_text('release-'+str(time.time_ns()))
            if rec.poll() is None:rec.terminate();rec.wait(timeout=10)
            (a.output/'recorder-cleanup.json').write_text(json.dumps({'pid':rec.pid,'exit_code':rec.returncode,'finished_unix':time.time()},indent=2))
            launch.finish(rec)

if __name__=='__main__':main()
