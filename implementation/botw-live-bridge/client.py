"""Launch unchanged retained A classes with all runtime output in the E lab."""
import launch
import argparse, hashlib, json, os, pathlib, time
import identity, retained
ROOT=pathlib.Path(__file__).resolve().parent
LAB=ROOT/'lab'
RETAINED=retained.RETAINED
import local
JAVA=local.java('java')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['start','stop']);args=parser.parse_args()
    record=LAB/'client-process.json'
    if args.action=='stop':
        data=json.loads(record.read_text());h=identity.openp(0x100401,False,data['pid'])
        result={'identity':data}
        if h:
            try:
                result['already_exited']=identity.wait(h,0)==0
                if not result['already_exited']:
                    identity.verify(h,data);result['stopped']=bool(identity.terminate(h,0));result['wait']=identity.wait(h,10000)
            finally:identity.close(h)
        else:result['already_exited']=True
        (LAB/'client-cleanup.json').write_text(json.dumps(result,indent=2));print(json.dumps(result));return
    if record.exists():
        data=json.loads(record.read_text());h=identity.openp(0x100400,False,data['pid'])
        if h:
            try:
                # An exited process that something still holds a handle to has no image path to
                # verify (route-133: error 31 after play sessions): only a running one is asked.
                if identity.wait(h,0)!=0:identity.verify(h,data);raise RuntimeError('Owned client still running')
            except RuntimeError as error:
                if str(error)=='Owned client still running':raise
            finally:identity.close(h)
    game=LAB/'minecraft';game.mkdir(exist_ok=True)
    tmp=game/'tmp';tmp.mkdir(exist_ok=True)
    # Development outputs/classpaths are read-only dependencies. No Gradle run
    # in A, and no copying its world/settings into this fresh private profile.
    configSrc=RETAINED/'.gradle/loom-cache/launch.cfg'
    argSrc=RETAINED/'build/loom-cache/argFiles/runClient'
    logSrc=RETAINED/'.gradle/loom-cache/log4j.xml'
    config=game/'launch.cfg';argfile=game/'runClient.args';log=game/'log4j.xml'
    log.write_bytes(logSrc.read_bytes())
    # This worktree's adapted mod classes (mod_build.py) replace the retained
    # ones on the class path; the retained build itself is never written.
    overrides=ROOT/'build/mod-overrides.json';mod=json.loads(overrides.read_text()) if overrides.exists() else None
    def redirect(text):
        if not mod:return text
        for part in ('classes','resources'):
            old=RETAINED/'build'/part;new=pathlib.Path(mod['build'])/part
            for form in (str,lambda p:str(p).replace('\\','\\\\'),lambda p:p.as_posix()):
                text=text.replace(form(old),form(new))
        return text
    # The retained launch files name the folder they were made in; retained.here reads them as this folder's.
    argfile.write_text(redirect(retained.here(argSrc.read_text())))
    config.write_text(redirect(retained.here(configSrc.read_text()).replace(str(logSrc).replace('/','\\'),str(log))))
    assert not retained.elsewhere(argfile.read_text()+config.read_text()),'a launch file still names the folder the retained build was made in'
    if mod:assert str(pathlib.Path(mod['build'])/'classes') in config.read_text() and str(RETAINED/'build'/'classes') not in argfile.read_text(),'mod class path was not redirected'
    # Walkable slope limit shared with the bridge (live_target.h); 60 degrees unless the lab file says otherwise.
    try:degrees=float((LAB/'walkable-degrees.txt').read_text().split()[0])
    except (OSError,ValueError,IndexError):degrees=60.0
    # Blocks a second up a wall of the game's (BotwClimb): Minecraft's ladder, 2.35, unless the lab file says otherwise; 0: walls are not climbed.
    try:climb=float((LAB/'climb-speed.txt').read_text().split()[0])
    except (OSError,ValueError,IndexError):climb=2.35
    # A background run (rig.background) is silent; a play session gets its volume back.
    import rig
    options=game/'options.txt';kept=LAB/'mc-volume.txt'
    if options.exists():
        lines=options.read_text(encoding='utf-8').splitlines()
        for n,line in enumerate(lines):
            if not line.startswith('soundCategory_master:'):continue
            value=line.split(':',1)[1]
            if rig.mode(LAB):
                if float(value)!=0:kept.write_text(value)
                lines[n]='soundCategory_master:0.0'
            elif kept.exists():lines[n]='soundCategory_master:'+kept.read_text().strip()
        # The lab player's skin, if one is given: this client is offline and has a
        # default skin, picked by its name. lab/skin/skin.png, a 64x64 skin of the classic model, is put in place of every
        # default skin by a resource pack of the lab's own (the game's resource format 97, Minecraft 26.3). Without the
        # file the pack is only taken out of the options.
        skin=LAB/'skin/skin.png';pack=game/'resourcepacks/lab-skin';wanted=skin.exists()
        if wanted:
            (pack/'pack.mcmeta').parent.mkdir(parents=True,exist_ok=True)
            (pack/'pack.mcmeta').write_text(json.dumps({'pack':{'description':"The lab player's skin",'min_format':97,'max_format':97}}))
            for model in ('wide','slim'):
                for name in ('alex','ari','efe','kai','makena','noor','steve','sunny','zuri'):
                    target=pack/'assets/minecraft/textures/entity/player'/model/(name+'.png');target.parent.mkdir(parents=True,exist_ok=True)
                    if not target.exists() or target.read_bytes()!=skin.read_bytes():target.write_bytes(skin.read_bytes())
        for n,line in enumerate(lines):
            for key in ('resourcePacks:','incompatibleResourcePacks:'):
                if not line.startswith(key):continue
                try:packs=[x for x in json.loads(line[len(key):]) if x!='file/lab-skin']
                except ValueError:packs=[]
                # Only the list of chosen packs: a pack that is also in the other list is left out by the game for that start (route-324).
                if wanted and key=='resourcePacks:':packs=(packs or ['vanilla'])+['file/lab-skin']
                lines[n]=key+json.dumps(packs,separators=(',',':'))
        options.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    # The player's name. An offline client takes its default skin, and with it the model of the arms (classic or slim),
    # from the name: BotwLab falls on a slim one, and a skin of the classic model is then drawn with slim arms
    # (route-325). lab/skin/name.txt holds the name to use; pick one whose default model matches the skin.
    try:player=(LAB/'skin/name.txt').read_text(encoding='utf-8').split()[0]
    except (OSError,IndexError):player='BotwLab'
    assert player.replace('_','').isalnum() and len(player)<=16,'not a Minecraft player name'
    # The game's own names of its creatures, read from the player's copy of the game (actor_names.py): the mod's
    # death messages name the creature that killed the player.
    import actor_names
    names=actor_names.ensure()
    command=[str(JAVA),f'-Dfabric.dli.config={config}','-Dfabric.dli.env=client',*([f'-Dbotwcraft.actorNames={names}'] if names else []),f'-Dbotwcraft.walkableDegrees={degrees:g}',f'-Dbotwcraft.climbSpeed={climb:g}',
        '-Dfabric.dli.main=net.fabricmc.loader.impl.launch.knot.KnotClient',
        '-Dbotwcraft.link=Local\\BotwCraft_v1','-Dbotwcraft.quitWithBotw=true',
        '-Dbotwcraft.startHidden=true',f'-Djava.io.tmpdir={tmp}','-Duser.language=en',
        '-Dfile.encoding=UTF-8','--enable-native-access=ALL-UNNAMED',
        '--sun-misc-unsafe-memory-access=allow','-XX:StackShadowPages=32',
        '@'+str(argfile),'net.fabricmc.devlaunchinjector.Main','--username',player,
        '--gameDir',str(game)]
    env=os.environ.copy();env.pop('JAVA_TOOL_OPTIONS',None);env.pop('_JAVA_OPTIONS',None)
    with (game/'console.log').open('wb') as output:
        child=launch.start(command,cwd=game,env=env,stdout=output,stderr=output)
    data={'pid':child.pid,'creation_filetime':identity.creation(int(child._handle)),
        'exe':str(JAVA),'command':command,'cwd':str(game),'started_unix':time.time(),
        'mod_overrides':mod,'walkable_degrees':degrees,'readonly_inputs':[{ 'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in (configSrc,argSrc,logSrc)]}
    record.write_text(json.dumps(data,indent=2));print(json.dumps({'pid':data['pid'],'creation_filetime':data['creation_filetime'],'cwd':data['cwd']}))
if __name__=='__main__':main()
