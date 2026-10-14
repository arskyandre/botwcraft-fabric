"""OS input into E's one physical capture route; no diagnostic ring/look writes."""
import launch
import argparse,ctypes as C,json,pathlib,struct,time
import local,rig,session
class Mouse(C.Structure):_fields_=[('dx',C.c_long),('dy',C.c_long),('data',C.c_uint32),('flags',C.c_uint32),('time',C.c_uint32),('extra',C.c_size_t)]
class Keyboard(C.Structure):_fields_=[('vk',C.c_uint16),('scan',C.c_uint16),('flags',C.c_uint32),('time',C.c_uint32),('extra',C.c_size_t)]
class Union(C.Union):_fields_=[('mouse',Mouse),('key',Keyboard)]
class Input(C.Structure):_fields_=[('type',C.c_uint32),('value',Union)]
assert C.sizeof(Input)==40
u=C.WinDLL('user32',use_last_error=True);u.SendInput.argtypes=[C.c_uint,C.POINTER(Input),C.c_int];u.SendInput.restype=C.c_uint
u.GetForegroundWindow.restype=C.c_void_p;u.SetForegroundWindow.argtypes=[C.c_void_p];u.GetWindowThreadProcessId.argtypes=[C.c_void_p,C.POINTER(C.c_uint)]
ownerHWND=None;ownedSession=None
# A background session (lab/background.txt, rig.background) takes this module's input over the
# bridge's pipe instead of the operating system: nothing is brought to the front, and the player's
# keyboard, mouse and focus are left alone (physical.h, `injected`).
PIPE={'pid':None,'file':None}
def inject(kind,code,a=0,b=0):
    record=struct.pack('<HHii',kind,code&0xffff,int(a),int(b))
    for attempt in range(20):
        try:
            if PIPE['pid']!=ownedSession['pid']:
                if PIPE['file']:PIPE['file'].close()
                PIPE.update(pid=None,file=None);PIPE.update(file=open(rf"\\.\pipe\botw-bridge-input-{ownedSession['pid']}",'wb',buffering=0),pid=ownedSession['pid'])
            PIPE['file'].write(record);return
        except OSError:
            PIPE['pid']=None
            if attempt==19:raise
            time.sleep(.1)
def focus():
    global ownerHWND,ownedSession
    if rig.background(session.LAB):
        ownerHWND,ownedSession=rig.window(session.LAB);return ownedSession
    hwnd,owned=rig.window(session.LAB);u.SetForegroundWindow(hwnd);time.sleep(.15)
    # The selector is made from the window title, which carries the frame rate and changes every
    # second: between the two calls it can be gone (route-102, the session was torn down for it).
    import subprocess
    for attempt in range(6):
        if u.GetForegroundWindow()==hwnd:break
        exe=local.tool('winapp','winapp')
        if not exe:
            # Windows gives the front only to a process that has just had input: an Alt key down and up counts.
            u.keybd_event(0x12,0,0,0);u.keybd_event(0x12,0,2,0);u.SetForegroundWindow(hwnd);time.sleep(.15);continue
        try:
            tree=json.loads(launch.run([exe,'ui','inspect','-w',str(hwnd),'--json'],check=True,capture_output=True,text=True).stdout)
            assert tree['windows'][0]['hwnd']==hwnd
            launch.run([exe,'ui','focus',tree['windows'][0]['elements'][0]['selector'],'-w',str(hwnd),'--json'],check=True,capture_output=True)
        except subprocess.CalledProcessError:
            if attempt==5:raise
        time.sleep(.15);u.SetForegroundWindow(hwnd);time.sleep(.15)
    assert u.GetForegroundWindow()==hwnd;ownerHWND=hwnd;ownedSession=owned;return owned
def send(value):
    assert ownerHWND and ownedSession;owned=ownedSession
    if rig.background(session.LAB):
        if value.type:
            k=value.value.key
            if k.flags&4:inject(2,k.scan,0 if k.flags&2 else 1)
            else:inject(1,k.vk,0 if k.flags&2 else 1)
        else:
            m=value.value.mouse;buttons={2:(1,1),4:(1,0),8:(3,1),16:(3,0)}
            if m.flags==1:inject(3,0,m.dx,m.dy)
            elif m.flags in buttons:inject(4,*buttons[m.flags])
            elif m.flags==0x800:inject(5,0,C.c_int32(m.data).value)
            else:raise AssertionError(f'no background form of mouse flags {m.flags:#x}')
        launch.record({'event':'injected-input','type':value.type,'vk':value.value.key.vk if value.type else None,'scan':value.value.key.scan if value.type else None,'flags':value.value.key.flags if value.type else value.value.mouse.flags,'dx':value.value.mouse.dx if not value.type else None,'dy':value.value.mouse.dy if not value.type else None,'qpc':rig.qpc(),'owned_cemu':owned})
        return
    assert u.GetForegroundWindow()==ownerHWND,'Refuse OS input while Cemu is not foreground'
    assert u.SendInput(1,C.byref(value),C.sizeof(value))==1,C.WinError(C.get_last_error())
    launch.record({'event':'physical-OS-input','type':value.type,'vk':value.value.key.vk if value.type else None,'scan':value.value.key.scan if value.type else None,'flags':value.value.key.flags if value.type else value.value.mouse.flags,'dx':value.value.mouse.dx if not value.type else None,'dy':value.value.mouse.dy if not value.type else None,'qpc':rig.qpc(),'owned_cemu':owned})
def key(vk,down):send(Input(1,Union(key=Keyboard(vk,0,0 if down else 2,0,0))))
def tap(vk,seconds=.06):
    key(vk,True)
    try:time.sleep(seconds)
    finally:key(vk,False)
def move(dx,dy):send(Input(0,Union(mouse=Mouse(dx,dy,0,1,0,0))))
def move_exact(dx,dy):
    # Absolute OS injection avoids the user's pointer-speed scaling in repeatable diagnostics.
    # It still enters the production Windows mouse hook; no host look/ring command is authored.
    if rig.background(session.LAB):return move(dx,dy) # the pipe's movement is exact as it is
    point=(C.c_long*2)();assert u.GetCursorPos(C.byref(point))
    x0,y0,w,h=[u.GetSystemMetrics(i) for i in (76,77,78,79)]
    x=int((point[0]+dx-x0+.5)*65536/w);y=int((point[1]+dy-y0+.5)*65536/h)
    send(Input(0,Union(mouse=Mouse(x,y,0,0xE001,0,0))))
def button(code,down):send(Input(0,Union(mouse=Mouse(0,0,0,{(1,True):2,(1,False):4,(3,True):8,(3,False):16}[(code,down)],0,0))))
def click(code=1):
    button(code,True)
    try:time.sleep(.1)
    finally:button(code,False)
def wheel(value):send(Input(0,Union(mouse=Mouse(0,0,value&0xffffffff,0x800,0,0))))
def text(value):
    for char in value:
        for flag in (4,6):send(Input(1,Union(key=Keyboard(0,ord(char),flag,0,0))))
def command(value):
    tap(ord('T'));time.sleep(.35);text(value);tap(13);time.sleep(.4)
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['claim','release','key','move','click','wheel','command']);p.add_argument('values',nargs='*');a=p.parse_args();focus()
    if a.action in ('claim','release'):tap(0x77)
    elif a.action=='key':tap(int(a.values[0],0))
    elif a.action=='move':move(*map(int,a.values))
    elif a.action=='click':click(int(a.values[0]) if a.values else 1)
    elif a.action=='wheel':wheel(int(a.values[0]))
    else:command(' '.join(a.values))
if __name__=='__main__':main()
