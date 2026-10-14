"""Task F ordinary Windows-input actions with recorded state; never writes body targets."""
import launch
import argparse,json,math,pathlib,time
import common_probe as c,combined,os_input as i,live_proof as p,rig,session
import local
ROOT=local.evidence('walking')

def state():return c.latest('ipc_mc')
def aim(yaw,pitch=0):
    s=state();delta=(yaw-s['yaw']+180)%360-180
    p.move(round(delta/.15),round((pitch-s['pitch'])/.15));time.sleep(.2)
def hold(keys,seconds):
    try:
        for k in keys:i.key(k,True)
        time.sleep(seconds)
    finally:
        for k in reversed(keys):i.key(k,False)
    time.sleep(.35)
def walk_to(x,z,check=None):
    # Test driver chooses keyboard direction; the unchanged MC simulation
    # determines movement and collision. Short bounded steps keep the lab safe.
    for _ in range(16):
        s=state();dx=x-s['pos'][0];dz=z-s['pos'][2];distance=math.hypot(dx,dz)
        if distance<.13:return
        assert combined.latest()['mode']=='5','Authority released; stop test input'
        aim(math.degrees(math.atan2(-dx,dz)))
        hold([87],min(.2,max(.05,distance/4.32-.04)))
        if check:check()
    raise RuntimeError(f'Keyboard route blocked before {(x,z)}: {state()}')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['setup','walk','wall','record']);parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--x',type=float,default=-.5);parser.add_argument('--z',type=float,default=.6);a=parser.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT);out.mkdir(exist_ok=False);i.focus();actions=[]
    def run(label,fn):
        item={'label':label,'qpc_start':rig.qpc(),'before':state()};actions.append(item)
        fn();time.sleep(.35);item.update(qpc_end=rig.qpc(),after=state(),body=combined.latest())
        (out/'actions.json').write_text(json.dumps(actions,indent=2))
    if a.action=='setup':
        commands=['/fill -2 100 -1 2 105 6 minecraft:air','/fill -1 100 4 0 102 5 minecraft:gold_block']
        for command in commands:run(command,lambda command=command:i.command(command))
        (out/'setup.json').write_text(json.dumps({'commands':commands,'scope':'Private MC world only; gold fixture straddles native wall for moving occlusion. Ordinary place/break used in proof.'},indent=2))
        run('start-position',lambda:walk_to(-.5,.6));aim(0,45);rig.capture(session.LAB,out/'setup.png');return
    if a.action=='walk':run('walk',lambda:walk_to(a.x,a.z));rig.capture(session.LAB,out/'walk.png');return
    if a.action=='wall':
        run('approach',lambda:walk_to(0,3.25));run('native-wall',lambda:(aim(-51),hold([87],.6)))
        rig.capture(session.LAB,out/'wall.png');return
    # Prospective actions. No chat, teleport or setup commands in this recording.
    record,log=combined.record(out,85,fragmented=True);start=time.monotonic();record_qpc=rig.qpc()
    def at(seconds,label,fn):
        time.sleep(max(0,start+seconds-time.monotonic()));run(label,fn)
    def shot(name):rig.capture(session.LAB,out/(name+'.png'))
    try:
        at(2,'place-step',lambda:(aim(0,45),i.tap(ord('9')),i.click(3),time.sleep(.5),shot('placed')))
        at(7,'walk-against-built-step',lambda:(hold([87],.5),shot('blocked')))
        def ascend():
            i.key(32,True)
            try:time.sleep(.15);hold([87],.2)
            finally:i.key(32,False)
            time.sleep(.7);shot('on-step')
        at(12,'jump-ascend-step',ascend)
        at(18,'crouch-on-step',lambda:(hold([0xA0],.8),aim(0,85),shot('above-step')))
        at(23,'descend-step',lambda:(aim(0,30),hold([83],.28),time.sleep(.6),shot('descended')))
        at(28,'break-step',lambda:(aim(0,30),i.tap(ord('2')),p.mine(2),shot('broken')))
        at(34,'walk-through-removed-step',lambda:(aim(0,0),hold([87],.35),shot('no-ghost')))
        at(39,'standing-jump-land',lambda:(hold([32],.15),time.sleep(.8)))
        at(43,'look-up-down-turn',lambda:(aim(30,-65),shot('up'),aim(-30,65),shot('down'),aim(0,0)))
        at(49,'short-sprint',lambda:(hold([0xA2,87],.15),shot('sprint')))
        at(53,'item-and-inventory',lambda:(i.tap(ord('5')),i.tap(ord('E')),p.gui(True),shot('inventory'),i.tap(ord('E')),p.gui(False)))
        at(59,'approach-native-wall',lambda:walk_to(0,3.25))
        at(64,'native-wall-stop',lambda:(aim(-51),hold([87],.6),shot('wall-stop')))
        at(69,'moving-wall-occlusion',lambda:(aim(-51),hold([83],.25),aim(0,0),shot('occlusion-back'),walk_to(-.5,2.8),shot('occlusion-side')))
        at(78,'return-supported-idle',lambda:(walk_to(-.5,1.4),aim(0,0),i.tap(ord('1'))))
        record.wait(timeout=max(1,95-(time.monotonic()-start)));assert record.returncode==0
    finally:
        for vk in (87,83,65,68,32,0xA0,0xA2):i.key(vk,False)
        for b in (1,3):i.button(b,False)
        if record.poll() is None:record.communicate(b'q',timeout=10)
        launch.finish(record);log.close();p.archive(out)
        (out/'record.json').write_text(json.dumps({'qpc_start':record_qpc,'qpc_end':rig.qpc(),'recorder_pid':record.pid,'exit_code':record.returncode,'seconds':time.monotonic()-start},indent=2))
if __name__=='__main__':main()
