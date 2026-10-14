"""Bounded combined fixture proof; fixed targets, full traces, owned recorder."""
import argparse,csv,io,json,pathlib,shutil,time,subprocess
import launch,rig,session
LAB=session.LAB
def rows():
    p=LAB/'poses.csv'
    with p.open('rb') as f:
        header=f.readline().decode();f.seek(max(len(header),p.stat().st_size-16384));data=f.read().decode()
    lines=data.splitlines()
    if not data.endswith('\n'):lines=lines[:-1]
    return list(csv.DictReader(io.StringIO(header+'\n'.join(lines[1:]))))
def latest():return next(r for r in reversed(rows()) if r['phase']=='0')
def body(value):
    (LAB/'command.txt').write_text(value+'-'+str(time.time_ns()))
def wait(mode,step,after,seconds=75):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        r=latest()
        if int(r['seq'])>after and int(r['mode'])==mode and int(r['step'])>=step:return r
        time.sleep(.025)
    raise TimeoutError(f'No mode {mode} step {step} after {after}: {latest()}')
def archive(out):
    out.mkdir(parents=True,exist_ok=True)
    for name in ('process.json','frames.jsonl','poses.csv','native.txt','log.txt','settings.xml','pristine-save-manifest.json','process-ledger.jsonl'):
        shutil.copy2(LAB/name,out/name)
def segment(out,mode,command,capture=True):
    before=latest();body(command);start=wait(mode,0,int(before['seq']))
    if capture:
        active=wait(mode,120,int(before['seq']));rig.capture(LAB,out/'during.png')
        (out/'active.json').write_text(json.dumps(active,indent=2))
    end=wait(mode,480,int(before['seq']));time.sleep(.15)
    (out/'segment.json').write_text(json.dumps({'before':before,'start':start,'end':end},indent=2));archive(out)
    return end
def record(out,seconds=42,fragmented=False):
    hwnd,identity=rig.window(LAB)
    import local
    command=[local.needed('ffmpeg','ffmpeg'),'-hide_banner','-loglevel','warning','-f','lavfi','-i',f'gfxcapture=hwnd={hwnd}:capture_cursor=0:display_border=0:max_framerate=30:resize_mode=scale','-t',str(seconds),'-vf','hwdownload,format=bgra,pad=ceil(iw/2)*2:ceil(ih/2)*2,format=yuv420p','-c:v','libx264','-preset','ultrafast','-crf','23',str(out/'combined.mp4')]
    if fragmented:command[-1:-1]=['-movflags','+frag_keyframe+empty_moov']
    log=(out/'recorder.log').open('w');child=launch.start(command,stdout=log,stderr=log,stdin=subprocess.PIPE)
    creation=session.b.creation(int(child._handle))
    (out/'recorder-process.json').write_text(json.dumps({'pid':child.pid,'creation_filetime':creation,'command':command,'owned_cemu':identity,'qpc':rig.qpc()},indent=2))
    return child,log
def proof(out):
    (LAB/'observer.txt').write_text('1');rig.configure(LAB,True,True,rig.ANCHORS['front']);time.sleep(1)
    # Prescribe a fresh small Gatepost floor target before any acceptance command.
    before=latest();a=[float(before['body_'+x]) for x in 'xyz'];b=[a[0]+.25,a[1],a[2]]
    (out/'fixture-declaration.json').write_text(json.dumps({'qpc':rig.qpc(),'initial':before,'floor_spec':a+b,'terrain_tolerance':.03,'air_tolerance':.15,'no_live_target_adjustment':True},indent=2))
    child,log=record(out,65)
    try:
        origin=latest();body('hold-forward');start=wait(1,0,int(origin['seq']))
        sweep=[]
        for index,(anchor,direction,ms) in enumerate([('front','right',300),('front','left',300),('edge','right',700),('edge','left',700),('behind','right',300),('behind','left',300)]):
            rig.configure(LAB,True,True,rig.ANCHORS[anchor]);time.sleep(.3);rig.pulse(LAB,direction,ms);time.sleep(.3)
            item={'qpc':rig.qpc(),'anchor_name':anchor,'anchor':rig.ANCHORS[anchor],'body':latest()};sweep.append(item)
            rig.capture(LAB,out/f'orbit-{index:02}-{anchor}.png')
        hold_end=wait(1,480,int(origin['seq']));(out/'hold.json').write_text(json.dumps({'start':start,'end':hold_end,'orbits':sweep},indent=2));archive(out/'hold')
        rig.configure(LAB,True,True,rig.ANCHORS['front'])
        (LAB/'path_spec.txt').write_text(' '.join(format(v,'.12g') for v in a+b))
        floor=out/'floor';floor.mkdir();segment(floor,4,'external')
        # Fixed anchor orbit after the bounded move; no anchor follows Link.
        rig.pulse(LAB,'right',300);rig.capture(LAB,out/'after-move-orbit.png');rig.pulse(LAB,'left',300)
        prior=latest();body('fall');wait(3,0,int(prior['seq']),5);body('hold');wait(1,0,int(prior['seq']),5)
        air=out/'air';air.mkdir();segment(air,2,'path');release=latest();body('release');time.sleep(3)
        landed=latest();rig.capture(LAB,out/'released-landed.png')
        rig.pulse(LAB,'forward',500);time.sleep(1);moved=latest();rig.capture(LAB,out/'restored-native-input.png')
        (out/'release.json').write_text(json.dumps({'release':release,'landed':landed,'moved':moved},indent=2))
        child.wait(timeout=70)
        if child.returncode:raise RuntimeError('Owned recorder failed')
    finally:
        body('release');(LAB/'E-control.txt').write_text('release-'+str(time.time_ns()))
        if child.poll() is None:child.terminate();child.wait(timeout=10)
        launch.finish(child);log.close();archive(out)
        (out/'recorder-cleanup.json').write_text(json.dumps({'pid':child.pid,'creation_filetime':session.b.creation(int(child._handle)),'exit_code':child.returncode},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    try:proof(a.output)
    except BaseException as error:
        (a.output/'failure.json').write_text(json.dumps({'error':repr(error),'qpc':rig.qpc()},indent=2));raise
