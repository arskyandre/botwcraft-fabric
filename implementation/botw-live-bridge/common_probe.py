"""Stationary real-MC common-view fixture proof, explicit renewing short leases."""
import launch
import argparse,json,math,pathlib,time,threading
import combined,rig,modules
ROOT=pathlib.Path(__file__).resolve().parents[2]
LAB=combined.LAB
_latest={}
def latest(event):
    for row in rig.tail(LAB):
        if row.get('event')=='player_model' and row.get('kind')!=0:continue
        _latest[row.get('event')]=row
    if event not in _latest:raise RuntimeError('Required diagnostic absent: '+event)
    result=_latest[event]
    if 'qpc' in result:assert rig.qpc()-result['qpc']<20000000,'Required diagnostic stale: '+event
    return result
def begin():
    body=combined.latest();assert int(body['write'])==0
    model=latest('player_model');state=latest('ipc_mc');stats=latest('stats');composition=latest('composition')
    assert stats['ready'] and stats['screen']==0,'Native gameplay is not ready'
    assert state['camera_mode']==0 and state['flags']&1
    return {'mc_origin':[0,100,0],'native_anchor':[float(body['body_'+v]) for v in 'xyz'],
        'scale':1,'basis':[[1,0,0],[0,1,0],[0,0,1]],'yaw_zero_native_forward':[0,0,1],
        'yaw_positive_turns_toward':[-1,0,0],'pitch_positive':'down',
        'body_epoch':stats['body_epoch'],'target_epoch':composition['target_epoch'],'proc_id':model['proc_id'],
        'calibration_scope':'authored fixture geometry unit scale, independently checked by projected cube corners; no physics/terrain claim',
        'source':state,'body_authority':False,'lease_ms':750,'renew_ms':150}
def renew(plan):
    stats=latest('stats');assert stats['body_epoch']==plan['body_epoch'],'Body lifecycle changed; fresh manual begin required'
    assert stats['ready'] and stats['screen']==0,'Native gameplay left its ready state'
    state=latest('ipc_mc');assert state['flags']&1 and state['camera_mode']==0
    # The host independently rejects stale MC samples, proc IDs and both epochs.
    values=[time.time_ns(),plan['scale'],*plan['mc_origin'],*plan['native_anchor'],750,
        plan['body_epoch'],plan['target_epoch'],plan['proc_id']]
    (LAB/'common-view.txt').write_text(' '.join(map(str,values)))
def look(yaw,pitch): (LAB/'common-look.txt').write_text(f'{time.time_ns()} {yaw} {pitch}')
def hold(plan,seconds):
    until=time.monotonic()+seconds
    while time.monotonic()<until:renew(plan);time.sleep(.15)
def release(plan):
    (LAB/'common-view.txt').write_text(' '.join(map(str,[time.time_ns(),plan['scale'],*plan['mc_origin'],*plan['native_anchor'],0,plan['body_epoch'],plan['target_epoch'],plan['proc_id']])))
def leased_capture(plan,path):
    # Screenshot IPC can exceed a short lease. This same test owner renews only
    # while taking an active-view capture; expiry/menu captures use rig directly.
    stopped=threading.Event();errors=[]
    def renewer():
        try:
            while not stopped.is_set():renew(plan);stopped.wait(.15)
        except BaseException as error:errors.append(error)
    worker=threading.Thread(target=renewer,daemon=True);worker.start()
    try:rig.capture(LAB,path)
    finally:stopped.set();worker.join(timeout=2)
    assert not worker.is_alive()
    if errors:raise errors[0]
def input(type,code,a=0,b=0):
    (LAB/'live-input.txt').write_text(f'{time.time_ns()} {type} {code} {a} {b}')
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--seconds',type=int,default=46);a=p.parse_args()
    import local
    out=a.output.resolve();assert out.is_relative_to(local.evidence('first-person'));out.mkdir(parents=True,exist_ok=False)
    (LAB/'observer.txt').write_text('1');(LAB/'camera-probe.txt').write_text('1');time.sleep(1)
    plan=begin();plan['planned_views']=[{'yaw':y,'pitch':p} for y,p in [(0,0),(0,65),(0,-65),(90,0),(180,0),(270,0),(35,0)]]
    plan['markers']='One authored unit cube at four cardinal bearings, close cube below eye, elevated cube above eye; anchors fixed per case before capture.'
    (out/'plan.json').write_text(json.dumps(plan,indent=2));(out/'modules.json').write_text(json.dumps(modules.audit(),indent=2))
    child,log=combined.record(out,a.seconds);cases=[]
    try:
        for index,(yaw,pitch) in enumerate([(0,0),(0,65),(0,-65),(90,0),(180,0),(270,0),(35,0)]):
            look(yaw,pitch)
            angle=math.radians(yaw);anchor=[plan['native_anchor'][0]-4*math.sin(angle)-.5,plan['native_anchor'][1]+.7,plan['native_anchor'][2]+4*math.cos(angle)-.5]
            if pitch==65:anchor=[plan['native_anchor'][0]-.5,plan['native_anchor'][1]+.1,plan['native_anchor'][2]+.05]
            if pitch==-65:anchor=[plan['native_anchor'][0]-.5,plan['native_anchor'][1]+6,plan['native_anchor'][2]+2]
            rig.configure(LAB,True,True,anchor);hold(plan,2);leased_capture(plan,out/f'{index:02}-yaw-{yaw}-pitch-{pitch}.png')
            cases.append({'index':index,'yaw':yaw,'pitch':pitch,'fixture_min_corner':anchor,'qpc':rig.qpc(),'source_mc':latest('ipc_mc'),'applied':latest('applied_scene')})
        # Real client sneak changes its eye height; no fabricated native eye.
        look(0,0);input(1,225,1);hold(plan,2);leased_capture(plan,out/'07-real-sneak.png')
        cases.append({'label':'real-MC-sneak','source_mc':latest('ipc_mc'),'applied':latest('applied_scene')})
        input(1,225,0);hold(plan,1);release(plan);time.sleep(1);rig.capture(LAB,out/'08-released.png')
        child.wait(timeout=a.seconds+10);assert child.returncode==0
    finally:
        release(plan);input(5,0);rig.configure(LAB,False,False,plan['native_anchor'])
        if child.poll() is None:child.terminate();child.wait(timeout=10)
        launch.finish(child);log.close();combined.archive(out)
        (out/'cases.json').write_text(json.dumps(cases,indent=2));(out/'client-process.json').write_bytes((LAB/'client-process.json').read_bytes())
        (out/'recorder-cleanup.json').write_text(json.dumps({'pid':child.pid,'creation_filetime':combined.session.b.creation(int(child._handle)),'exit_code':child.returncode,'qpc':rig.qpc()}))
if __name__=='__main__':main()
