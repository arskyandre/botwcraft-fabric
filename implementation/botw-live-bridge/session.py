import launch
"""One launcher: the user's own Cemu, with the bridge's layer and patches for one session.

Cemu is started from where it is installed (local.json "cemu"), with its own settings, shader cache and graphic
packs. For the session its settings.xml is changed (the bridge's graphic pack switched on, see `packs`) and a copy
of it as it was is kept in lab/cemu-settings-before.xml; `stop` and `restore` put that copy back once Cemu has
ended. The saves are Cemu's own, unless --private-saves is given: an automatic run then uses the lab's own mlc01
(Cemu's --mlc), so that it never writes a save of the user's.
"""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, time
import xml.etree.ElementTree as E
from ctypes import WinError, get_last_error
import identity as b
from identity import api,openp,creation,verify,close,times
import local
api=b.api  # Existing title-FPS/window helper imports the active session module.
LAB=local.LAB
PACK='graphicPacks/NativeBridge/rules.txt'
# A session for play keeps the player's own graphic packs (packs()). An automatic run draws BOTW at this size through
# the community "Graphics" pack when Cemu has it (its other choices at their defaults). lab/render-size.txt may name
# another of the pack's 16:9 presets, or `native` for the game's own 1280x720 without the pack.
GRAPHICS='graphicPacks/downloadedGraphicPacks/BreathOfTheWild/Graphics/rules.txt'
GRAPHICS_PRESET='1920x1080 (Full HD)'
BEFORE=LAB/'cemu-settings-before.xml';ACTIVE=LAB/'cemu-settings-active.txt'

def cemu_running():
    query=launch.run(['powershell','-NoProfile','-Command',"@(Get-CimInstance Win32_Process -Filter \"Name='Cemu.exe'\").Count"],capture_output=True,text=True,check=True)
    return int(query.stdout.strip())>0

def restore_settings():
    """Cemu's settings.xml as it was before the bridge's session. True when it was put back."""
    if not ACTIVE.exists() or ACTIVE.read_text().strip()!='1':return False
    if cemu_running():raise RuntimeError('Cemu is still running: its settings are put back after it has ended')
    shutil.copy2(BEFORE,local.cemu_data()/'settings.xml');ACTIVE.write_text('0');return True

def packs(settings,data,lab,play):
    """The graphic packs of a bridge session.

    A session for play (the player's own saves): the player's packs stay as they are in Cemu (resolution, graphics,
    FPS++, cheats), and the bridge's own is added. None of the community packs for this game
    patches an instruction the bridge patches; FPS++'s two patches inside the game's wait for the next frame are
    not reached, because the bridge replaces that function, so the bridge's pace holds.
    local.json "keep_my_graphic_packs": false gives the set of an automatic run instead, for finding a fault.

    An automatic run (the lab's saves): the bridge's own pack and the Graphics pack for the picture's size, the
    same on every machine. BRIDGE_PACKS=mine in the environment: the player's packs, to try them.

    A pack the player switched off stays off in both (a pack that is on by its own rules.txt, as BCML's pack of
    merged mods, is off only by its entry here)."""
    block=settings.find('GraphicPack')
    if block is None:block=E.SubElement(settings.getroot(),'GraphicPack')
    if local.flag('keep_my_graphic_packs',True) if play else os.environ.get('BRIDGE_PACKS')=='mine':
        for entry in [e for e in block if e.get('filename')==PACK]:block.remove(entry)
        E.SubElement(block,'Entry',filename=PACK)
        return {'preset':"the player's own packs",'packs':[e.get('filename') for e in block if e.get('disabled')!='true']}
    for entry in [e for e in block if e.get('disabled')!='true' or e.get('filename') in (PACK,GRAPHICS)]:block.remove(entry)
    E.SubElement(block,'Entry',filename=PACK)
    try:preset=(lab/'render-size.txt').read_text().strip() or GRAPHICS_PRESET
    except OSError:preset=GRAPHICS_PRESET
    rules=data/GRAPHICS
    if preset=='native' or not rules.exists():return {'preset':'native'}
    text=rules.read_text(encoding='utf-8',errors='replace')
    assert f'name = {preset}\ncategory = Resolution' in text.replace('\r\n','\n'),f'Graphics pack has no Resolution preset {preset!r}'
    entry=E.SubElement(block,'Entry',filename=GRAPHICS);chosen=E.SubElement(entry,'Preset')
    E.SubElement(chosen,'category').text='Resolution';E.SubElement(chosen,'preset').text=preset
    return {'preset':preset,'rules_sha256':hashlib.sha256(rules.read_bytes()).hexdigest()}

def put(settings,path,value):
    node=settings.find(path)
    if node is not None:node.text=value

def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['start','stop','restore','self-check','watch']);p.add_argument('--pid',type=int,default=0);p.add_argument('--lab',type=pathlib.Path,default=LAB);p.add_argument('--no-probe',action='store_true');p.add_argument('--live-ipc',action='store_true');p.add_argument('--camera-probe',action='store_true');p.add_argument('--private-saves',action='store_true');a=p.parse_args();lab=a.lab.resolve()
    assert lab==LAB.resolve(),'Only the bridge\'s own lab folder is supported'
    if a.action=='stop':
        launch.run([os.sys.executable,'-B',str(pathlib.Path(b.__file__)),'stop','--lab',str(lab)],check=True)
        for attempt in range(20):
            if not cemu_running():break
            time.sleep(.5)
        print(json.dumps({'settings_restored':restore_settings()}));return
    if a.action=='restore':print(json.dumps({'settings_restored':restore_settings()}));return
    if a.action=='watch':
        # Started by `start`, on its own: waits for that Cemu to end and puts the settings back. So they come back
        # also when the launcher is gone first (its console closed, a crash) and the player closes Cemu later.
        import ctypes
        kernel=ctypes.windll.kernel32;kernel.OpenProcess.restype=ctypes.c_void_p
        handle=kernel.OpenProcess(0x00100000,False,a.pid)   # SYNCHRONIZE
        if handle:kernel.WaitForSingleObject(ctypes.c_void_p(handle),0xFFFFFFFF);kernel.CloseHandle(ctypes.c_void_p(handle))
        for attempt in range(40):
            try:restore_settings();break
            except RuntimeError:time.sleep(.5)   # another Cemu is running: its own session, or the player's
        return
    if a.action=='self-check':
        launch.run([os.sys.executable,'-B',str(pathlib.Path(b.__file__)),'self-check','--lab',str(lab)],check=True);return
    cemu=local.cemu();data=local.cemu_data();game=local.game()
    assert hashlib.sha256(cemu.read_bytes()).hexdigest()==local.CEMU_SHA256,'The bridge is made for Cemu 2.6 (Windows x64); another build is at '+str(cemu)
    if cemu_running():raise RuntimeError('Cemu is already running; close it first')
    if not a.no_probe and not (data/PACK).exists():raise SystemExit(f'The bridge\'s graphic pack is not in {data}: run build.ps1 -Install')
    if a.private_saves:assert (lab/'mlc01/usr/save').is_dir() and not (lab/'mlc01/usr/save').is_symlink(),'The lab has no saves of its own (prepare_lab.ps1)'
    restore_settings()   # a session before that ended without putting them back
    env=os.environ.copy()
    for key in ('VK_LAYER_PATH','VK_INSTANCE_LAYERS'):env.pop(key,None)
    if not a.no_probe:env['VK_LAYER_PATH']=str(lab);env['VK_INSTANCE_LAYERS']='VK_LAYER_LOCAL_botw_live_bridge'
    shutil.copy2(data/'settings.xml',BEFORE);ACTIVE.write_text('1')
    settings=E.parse(data/'settings.xml')
    graphics=packs(settings,data,lab,not a.private_saves) if not a.no_probe else {'preset':'unchanged'}
    # A background run opens on the secondary display, not maximized (rig.py lays it out without
    # bringing it to the front) and silent; a play session opens as before, with its volume back.
    import rig
    quiet=rig.mode(lab)!=0;x0,y0,x1,y1=rig.work_area(lab);volume=settings.find('Audio/TVVolume')
    put(settings,'window_position/x',str(x0+304));put(settings,'window_position/y',str(y0+143));put(settings,'window_maximized','false' if rig.background(lab) else 'true')
    # A window, not full screen: an automatic run reads the title and its menus in a window with its menu bar
    # (route-320 failed at the title in full screen). In play the player goes full screen by hand (Alt+Enter).
    put(settings,'fullscreen','false');put(settings,'check_update','false')
    if quiet and volume is not None:volume.text='0'
    settings.write(data/'settings.xml',encoding='utf-8',xml_declaration=True)
    (lab/'command.txt').write_text('release-start-'+str(time.time_ns()))
    (lab/'E-control.txt').write_text('release-start-'+str(time.time_ns()))
    (lab/'view-probe.txt').write_text('0 0 0 0 0 0 70 0')
    (lab/'camera-probe.txt').write_text('1' if a.camera_probe else '0')
    (lab/'live-ipc.txt').write_text('1' if a.live_ipc else '0')
    (lab/'live-render.txt').write_text('0')
    (lab/'live-input.txt').write_text('')
    (lab/'live-capture.txt').write_text('')
    (lab/'common-view.txt').write_text('')
    (lab/'common-look.txt').write_text('0 0 0')
    (lab/'walking-claim.txt').write_text('')
    (lab/'calibration-ray.txt').write_text('')
    (lab/'native-stream.txt').write_text('0')
    args=[str(cemu),'-g',str(game)]+(['--mlc',str(lab/'mlc01')] if a.private_saves else [])
    data_record={}
    try:
        child=launch.start(args,cwd=cemu.parent,env=env)
        h=b.openp(0x43B,False,child.pid)
        if not h: child.terminate();raise WinError(get_last_error())
    except BaseException:
        time.sleep(1)
        try:restore_settings()
        except RuntimeError:pass
        raise
    # Not through launch.py: it is no helper of the run to be stopped with it; it ends by itself with Cemu.
    subprocess.Popen([os.sys.executable,'-B',str(pathlib.Path(__file__).resolve()),'watch','--pid',str(child.pid)],cwd=pathlib.Path(__file__).resolve().parent,
        stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=0x00000008|0x00000200,close_fds=True)   # detached from the console, its own group
    data_record={'pid':child.pid,'creation_filetime':b.creation(h),'exe':args[0],'command':args,'started_unix':time.time(),
          'probe_injected':not a.no_probe,'graphics_pack':graphics,'private_saves':a.private_saves,'cemu_data':str(data),'background':quiet,'work_area':[x0,y0,x1,y1],'environment':{k:env[k] for k in ('VK_LAYER_PATH','VK_INSTANCE_LAYERS') if k in env}}
    (lab/'process.json').write_text(json.dumps(data_record,indent=2))
    try:
        if a.no_probe:print(json.dumps(data_record,indent=2));return
        time.sleep(.15);b.inject(h,lab/'botw_live_bridge.dll')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            log=lab/'native.txt'
            if log.exists() and log.stat().st_mtime>=data_record['started_unix'] and f'registered PID={child.pid} ' in log.read_text():break
            if child.poll() is not None:raise RuntimeError(f'Cemu exited during registration (exit code {child.returncode:#x})')
            time.sleep(.1)
        else:raise RuntimeError('The bridge did not register in Cemu')
        print(json.dumps(data_record,indent=2));print(log.read_text())
    except BaseException:
        try:
            b.verify(h,data_record);stopped=bool(b.terminate(h,1));wait=b.wait(h,10000)
            (lab/'startup-failure-cleanup.json').write_text(json.dumps({'identity':data_record,'stopped':stopped,'wait':wait},indent=2))
        except OSError:pass   # Cemu has ended by itself: nothing to stop
        time.sleep(1)
        try:restore_settings()
        except RuntimeError:pass
        raise
    finally:b.close(h)

if __name__=='__main__':main()
