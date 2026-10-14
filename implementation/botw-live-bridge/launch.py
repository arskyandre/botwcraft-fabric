"""Track owned helper identities at launch, including early failures."""
import json,os,pathlib,subprocess,sys,time
import identity
LAB=pathlib.Path(__file__).resolve().parent/'lab'
def record(data):
    with (LAB/'process-ledger.jsonl').open('a',encoding='utf-8') as file:
        file.write(json.dumps(data)+'\n')
def self_record():
    h=identity.openp(0x100400,False,os.getpid())
    try:record({'event':'helper','pid':os.getpid(),'creation_filetime':identity.creation(h),'exe':sys.executable,'command':sys.argv,'unix':time.time()})
    finally:identity.close(h)
def start(command,**kwargs):
    child=subprocess.Popen(command,**kwargs)
    data={'event':'launch','pid':child.pid,'command':list(map(str,command)),'unix':time.time()}
    try:data['creation_filetime']=identity.creation(int(child._handle))
    except BaseException as error:data['identity_error']=repr(error)
    record(data)
    return child
def finish(child):
    record({'event':'exit','pid':child.pid,'exit_code':child.returncode,'unix':time.time()})
def run(command,check=False,**kwargs):
    capture_output=kwargs.pop('capture_output',False)
    if capture_output:kwargs.update(stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    child=start(command,**kwargs)
    try:out,err=child.communicate()
    finally:finish(child)
    result=subprocess.CompletedProcess(command,child.returncode,out,err)
    if check:result.check_returncode()
    return result
self_record()
