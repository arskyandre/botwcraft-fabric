"""One owned OS-input/live-GPU proof. Setup is explicit; recorded actions use physical input."""
import argparse,json,pathlib,time,shutil,ctypes
import launch,session,rig,combined,common_probe as c,os_input as i,modules
import local
EVIDENCE=local.evidence('live-proof')
def archive(out):
    combined.archive(out)
    for name in ('client-process.json','minecraft/console.log'):
        shutil.copy2(session.LAB/name,out/pathlib.Path(name).name)
def claim(out):
    (session.LAB/'observer.txt').write_text('1');(session.LAB/'live-render.txt').write_text('1');rig.configure(session.LAB,False,False,[0,0,0]);time.sleep(1)
    plan=c.begin();c.renew(plan);time.sleep(.15);i.focus();i.tap(0x77);time.sleep(1)
    (out/'plan.json').write_text(json.dumps(plan,indent=2));return plan
def move(dx,dy):
    while dx or dy:
        x=max(-140,min(140,dx));y=max(-140,min(140,dy));i.move_exact(x,y);dx-=x;dy-=y;time.sleep(.025)
def mine(seconds=1.7):
    i.button(1,True)
    try:time.sleep(seconds)
    finally:i.button(1,False)
    time.sleep(.5)
def gui(expected):
    deadline=time.monotonic()+4
    while time.monotonic()<deadline:
        state=c.latest('ipc_mc')
        if bool(state['flags']&2)==expected:return state
        time.sleep(.1)
    raise RuntimeError('MC GUI state did not confirm')
def swap():
    i.tap(ord('E'));gui(True);time.sleep(.3)
    move(-216,28);time.sleep(.2);i.click();time.sleep(.3)
    move(216,176);time.sleep(.2);i.click();time.sleep(.3)
    move(-216,-176);time.sleep(.2);i.click();time.sleep(.3)
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['claim','setup','record','archive','normalize']);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--full',action='store_true');a=p.parse_args()
    out=a.output.resolve();assert out.is_relative_to(EVIDENCE);out.mkdir(exist_ok=False);i.focus()
    if a.action=='claim':claim(out);rig.capture(session.LAB,out/'claimed.png');return
    if a.action=='archive':archive(out);return
    if a.action=='normalize':
        state=c.latest('ipc_mc');assert state['flags']&1 and not state['flags']&2
        move(round(-state['yaw']/.15),round(-state['pitch']/.15));time.sleep(.5)
        (out/'normalization.json').write_text(json.dumps({'before':state,'after':c.latest('ipc_mc'),'route':'actual state -> finite OS mouse displacement through physical capture, no host look file'},indent=2));return
    if a.action=='setup':
        commands=['/fill -3 100 0 3 103 5 minecraft:air','/fill -2 100 3 2 102 3 minecraft:stone']
        (out/'setup.json').write_text(json.dumps({'commands':commands,'method':'ordinary MC chat via same physical route; BEFORE acceptance recording','qpc':rig.qpc()},indent=2))
        for command in commands:i.command(command);time.sleep(1)
        time.sleep(15);rig.capture(session.LAB,out/'warm-seed.png');archive(out);return
    # Prospective criteria must already exist; no setup command is issued inside this recording.
    assert (EVIDENCE/'prospective-continuity.json').is_file()
    (session.LAB/'observer.txt').write_text('1' if a.full else '0');time.sleep(1)
    (out/'modules.json').write_text(json.dumps(modules.audit(),indent=2));child,log=combined.record(out,65,fragmented=True);start=time.monotonic();qpc=rig.qpc();actions=[];gui_intervals=[]
    def at(seconds,label,fn):
        time.sleep(max(0,start+seconds-time.monotonic()));item={'label':label,'qpc_start':rig.qpc(),'seconds_start':time.monotonic()-start};fn();item.update(qpc_end=rig.qpc(),seconds_end=time.monotonic()-start);actions.append(item)
    try:
        at(2,'stone-bricks-two-placements',lambda:(i.tap(ord('9')),i.click(3),time.sleep(.6),i.click(3),time.sleep(1),rig.capture(session.LAB,out/'placed.png')))
        at(7,'pickaxe-two-breaks',lambda:(i.tap(ord('2')),mine(),mine(),rig.capture(session.LAB,out/'broken.png')))
        at(14,'strong-up',lambda:(move(0,-467),time.sleep(1),rig.capture(session.LAB,out/'up.png')))
        at(18,'strong-down',lambda:(move(0,934),time.sleep(1),rig.capture(session.LAB,out/'down.png')))
        at(22,'level-and-yaw-turn',lambda:(move(0,-467),move(600,0),time.sleep(1),rig.capture(session.LAB,out/'yaw90.png'),move(-600,0)))
        def inventory():
            before=c.latest('ipc_mc');opened=time.monotonic()-start;swap();rig.capture(session.LAB,out/'inventory.png');i.tap(ord('E'));after=gui(False);gui_intervals.append([opened,time.monotonic()-start])
            assert abs(before['yaw']-after['yaw'])<.01 and abs(before['pitch']-after['pitch'])<.01,'GUI cursor changed world look; retain and reject interrupted run'
        at(28,'inventory-real-item-swap',inventory)
        def held():
            for vk in (ord('W'),0xA0,0xA2):i.key(vk,True)
            time.sleep(.5)
            rig.capture(session.LAB,out/'held-movement-sneak.png')
            for vk in (0xA2,0xA0,ord('W')):i.key(vk,False)
        at(35,'movement-shift-control-delivery-stationary-hold',held)
        at(39,'wheel-and-two-selected-items',lambda:(i.wheel(120),time.sleep(.5),i.tap(ord('1')),time.sleep(.5),i.tap(ord('5')),time.sleep(.5),rig.capture(session.LAB,out/'selected-item.png')))
        at(44,'repeat-two-place-two-break',lambda:(i.tap(ord('9')),i.click(3),time.sleep(.6),i.click(3),time.sleep(.5),i.tap(ord('2')),mine(),mine(),rig.capture(session.LAB,out/'repeat-removed.png')))
        at(54,'responsive-small-turn',lambda:(move(180,0),time.sleep(.5),move(-180,0)))
        child.wait(timeout=max(1,75-(time.monotonic()-start)));assert child.returncode==0
    finally:
        if child.poll() is None:child.communicate(b'q',timeout=10)
        launch.finish(child);log.close();archive(out)
        (out/'actions.json').write_text(json.dumps({'record_qpc':qpc,'actions':actions,'gui_intervals':gui_intervals,'full':a.full},indent=2))
        (out/'recorder-cleanup.json').write_text(json.dumps({'pid':child.pid,'creation_filetime':session.b.creation(int(child._handle)),'exit_code':child.returncode,'qpc':rig.qpc()}))
if __name__=='__main__':main()
