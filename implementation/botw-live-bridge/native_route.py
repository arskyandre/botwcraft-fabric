"""One owned Cemu + Fabric native-collision run: native stream, F8 claim, OS input.

python native_route.py probe        # support/landing/release batch
python native_route.py --summary DIR
Only identities this runner records are stopped. Rotated trace segments are
copied while the run continues so long routes keep every post-update row.
"""
import argparse, gzip, hashlib, json, math, os, pathlib, shutil, sys, time
import live_check as live
import local

ROOT = live.ROOT
LAB = live.LAB
EVIDENCE = live.EVIDENCE
# Runs of the rendering work (plan `render`, or --evidence render) are kept apart from the collision routes.
RENDER_EVIDENCE = local.evidence('render')
# Runs of the gameplay work (control, actors, damage, combat; from report 48 on).
GAMEPLAY_EVIDENCE = local.evidence('gameplay')
GAMEPLAY_PLANS = ('robust', 'swim', 'river', 'hits', 'actors', 'weapons', 'water', 'world', 'campfire', 'procs', 'boxwater', 'boats', 'airwall', 'pen', 'things', 'swing', 'axe', 'steve', 'mcwalls', 'padkeys', 'keys')
# lab/follow-states.txt as the bridge defaults to it (body.h): no release for a native state; the write continued
# through a hit and while Link swims, and left out while he climbs a wall, glides, rides or is a ragdoll, unless
# he is more than 25 units behind.
FOLLOW_DEFAULT = '0 188080 0 25'
DAMAGE_EVENT_DEFAULT = '1'   # hits on Link are refused where the game adds them and go to Minecraft without his armour (report 51)
ANCHOR = live.ANCHOR
SCALE = 1.3
# The two files a fresh Minecraft profile starts from, as the first live lab had them.
RETAINED_PROFILE = ROOT / 'minecraft-profile'
F8, SPACE, SHIFT, W = 0x77, 32, 0xA0, 87


def numbered(root=EVIDENCE):
    # One numbering for both folders: a route number names one run.
    number = max([int(p.name.split('-')[1]) for base in (EVIDENCE, RENDER_EVIDENCE, GAMEPLAY_EVIDENCE) for p in base.glob('route-[0-9]*') if p.name.split('-')[1].isdigit()] or [0]) + 1
    out = root / f'route-{number:02}'
    out.mkdir(parents=True, exist_ok=False)
    return out


class Keeper:
    """Copies each rotated 8 MiB frames/poses segment before the bridge drops it."""
    def __init__(self, out):
        self.out, self.seen = out, {'frame_rotations': 0, 'pose_rotations': 0}
    def poll(self):
        path = LAB / 'trace-stats.json'
        try: stats = json.loads(path.read_text())
        except (OSError, ValueError): return
        for key, name in (('frame_rotations', 'frames.jsonl'), ('pose_rotations', 'poses.csv')):
            while self.seen[key] < stats.get(key, 0):
                self.seen[key] += 1
                source = LAB / (name + '.1')
                if source.exists():
                    shutil.copy2(source, self.out / f'{name}.seg{self.seen[key]:03}')


def events_since(offset, names):
    """Events of the given names appended to frames.jsonl after a byte offset.

    After a rotation the current file restarts, so the previous segment's
    tail (from the same offset) is read as well. From offset 0 ("since the
    run began") the previous segment is always read: route-171 lost the one
    `ipc_connected` that was written 386 lines before the first rotation.
    """
    path = LAB / 'frames.jsonl'
    size = path.stat().st_size
    data = ''
    if (size < offset or not offset) and (LAB / 'frames.jsonl.1').exists():
        with (LAB / 'frames.jsonl.1').open('rb') as f:
            f.seek(offset); data = f.read().decode(errors='replace')
    with path.open('rb') as f:
        f.seek(offset if size >= offset else 0)
        data += chr(10) + f.read().decode(errors='replace')
    found = []
    for line in data.splitlines():
        if any(f'"event":"{n}"' in line for n in names):
            try: found.append(json.loads(line))
            except ValueError: pass
    return found


def offset():
    return (LAB / 'frames.jsonl').stat().st_size


def wait_event(name, since, seconds, keeper, predicate=lambda e: True):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        keeper.poll()
        hits = [e for e in events_since(since, [name]) if predicate(e)]
        if hits: return hits[-1]
        time.sleep(.5)
    raise RuntimeError(f'No {name} within {seconds}s')


def save_differences():
    """Lab save files that differ from the preserved checkpoint (autosaves)."""
    a = LAB / 'pristine-save'; b = LAB / 'mlc01/usr/save'
    h = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    return sorted(str(p.relative_to(a)) for p in a.rglob('*') if p.is_file() and (not (b / p.relative_to(a)).exists() or h(p) != h(b / p.relative_to(a))))


def restore_save(out, label):
    """Overwrite lab save slots from the preserved checkpoint (no deletion)."""
    import launch
    changed = save_differences()
    if changed:
        kept = out / f'save-{label}'
        for name in changed:
            target = kept / name; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(LAB / 'mlc01/usr/save' / name, target)
        launch.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'restore_fixture.ps1')], check=True)
    live.save(out / f'save-{label}.json', {'changed_before_restore': changed, 'differences_after': save_differences()})
    name = checkpoint_name()
    if name:
        # A kept save of the lab's (report 79: 'new-game-chamber', the new game after its opening scene) is laid
        # over the usual one: files are overwritten, none is removed.
        launch.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'restore_fixture.ps1'), '-CheckpointSource', str(LAB / 'saves' / name)], check=True)
        live.save(out / f'save-{label}-checkpoint.json', {'checkpoint': name, 'differs_from_usual': save_differences()})


def checkpoint_name():
    name = os.environ.get('BRIDGE_CHECKPOINT', '')
    if name and not (name.replace('-', '').isalnum() and (LAB / 'saves' / name).is_dir()): raise RuntimeError(f'No kept save lab/saves/{name}')
    return name


def prepare_profile(out):
    """Fresh private MC profile: copy only retained options/config (read-only source)."""
    game = LAB / 'minecraft'
    copied = []
    for relative in ('options.txt', 'config/botwcraft.properties'):
        target = game / relative
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(RETAINED_PROFILE / relative, target)
            copied.append(relative)
    live.save(out / 'profile.json', {'game_dir': str(game), 'copied_from': str(RETAINED_PROFILE), 'copied': copied,
        'files': {r: hashlib.sha256((game / r).read_bytes()).hexdigest() for r in ('options.txt', 'config/botwcraft.properties')}})


def backup_saves(out):
    """A copy of the user's own saves of the game, before a session they play (lab/save-backups, one folder a
    session): the game autosaves over them while Steve plays, and the mod is young."""
    source = local.cemu_mlc() / 'usr/save/00050000/101c9400'
    record = {'source': str(source), 'found': source.is_dir()}
    if record['found']:
        target = LAB / 'save-backups' / time.strftime('%Y-%m-%d_%H-%M-%S')
        shutil.copytree(source, target); record['copy'] = str(target)
    live.save(out / 'save-backup.json', record)


def start_cemu(out, title_only=False, own_saves=False):
    """own_saves: a session a player plays. Cemu runs with its own saves, and nothing is pressed for them:
    they choose New Game or a save in the game's own title menu. Every other run uses the lab's saves."""
    import launch, rig, combined
    with (out / 'start.log').open('w') as log:
        # Cemu now and then ends in its first half second, before its own log is begun (route-346, route-348; four
        # starts by hand right after were good). session.py has put the settings back by then: once more.
        for attempt in (1, 2):
            started = launch.run([sys.executable, '-B', str(ROOT / 'session.py'), 'start', '--camera-probe', '--live-ipc'] + ([] if own_saves else ['--private-saves']), cwd=ROOT, stdout=log, stderr=log)
            if not started.returncode: break
            log.write(f'start {attempt} failed with {started.returncode}' + chr(10)); log.flush()
            if attempt == 2: started.check_returncode()
            time.sleep(5)
    owned = json.loads((LAB / 'process.json').read_text())
    (LAB / 'observer.txt').write_text('1')
    time.sleep(8)
    # Drawn at the game's own 1280x720 and stretched into a maximized window the menu text blurs
    # past recognition (route-83: 0.49 where 0.90 is asked). The menus are then read in the window
    # the references were taken in, and the window is maximized once the save is loading.
    native = json.loads((LAB / 'process.json').read_text()).get('graphics_pack', {}).get('preset') == 'native'
    def size(action):
        with (out / 'resize.log').open('a') as log:
            launch.run([sys.executable, '-B', str(ROOT / 'rig.py'), action], cwd=ROOT, stdout=log, stderr=log, check=True)
    size('resize' if native and not own_saves else 'maximize')
    if own_saves: return owned
    if native: time.sleep(1)
    # Cemu first loads its cached shaders and pipelines; with a large cache that takes minutes, not seconds.
    live.wait_visual(out, 'title', 300)
    if title_only:   # BRIDGE_START=title: the plan's own words go on from the title (New Game)
        if native: time.sleep(1); size('maximize')
        return owned
    rig.pulse(LAB, 'interact'); time.sleep(2)
    if checkpoint_name():
        # Another save's list has other pictures and names than the references: the same three presses by the clock,
        # and then a body that stands still for three seconds, wherever this save is.
        time.sleep(2); rig.capture(LAB, out / 'list.png'); rig.pulse(LAB, 'interact'); time.sleep(3)
        rig.capture(LAB, out / 'confirmation.png'); rig.pulse(LAB, 'interact')
        if native: time.sleep(1); size('maximize')
        until = time.monotonic() + 90; place = None; since = 0
        while time.monotonic() < until:
            rig.window(LAB)
            try:
                row = combined.latest(); now = [float(row['body_' + a]) for a in 'xyz']
                if place is None or max(abs(a - b) for a, b in zip(now, place)) > .03 or not any(now): place = now; since = time.monotonic()
                elif time.monotonic() - since > 3: break
            except (OSError, StopIteration, ValueError, KeyError): place = None
            time.sleep(.5)
        else: raise RuntimeError('The kept save did not load to a body that stands still')
        live.save(out / 'loaded-state.json', row)
        return owned
    live.wait_visual(out, 'list'); rig.pulse(LAB, 'interact'); time.sleep(2)
    live.wait_visual(out, 'confirmation'); rig.pulse(LAB, 'interact')
    if native: time.sleep(1); size('maximize')
    until = time.monotonic() + 70
    while time.monotonic() < until:
        rig.window(LAB)
        try:
            row = combined.latest()
            if max(abs(float(row['body_' + a]) - v) for a, v in zip('xyz', ANCHOR)) < .03: break
        except (OSError, StopIteration, ValueError, KeyError): pass
        time.sleep(.5)
    else: raise RuntimeError('Loaded checkpoint did not reach the expected feet position')
    live.save(out / 'loaded-state.json', row)
    return owned


def start_client(out, keeper, since, in_world=True):
    import launch, common_probe as c
    with (out / 'client-start.log').open('w') as log:
        launch.run([sys.executable, '-B', str(ROOT / 'client.py'), 'start'], cwd=ROOT, stdout=log, stderr=log, check=True)
    client = json.loads((LAB / 'client-process.json').read_text())
    wait_event('ipc_connected', since, 180, keeper)
    until = time.monotonic() + 240
    while time.monotonic() < until:
        keeper.poll()
        try:
            state = c.latest('ipc_mc')
            if (state['flags'] & 1 or not in_world) and state['camera_mode'] == 0: return client, state   # at the game's title (BRIDGE_START=title) Minecraft is linked but held
        except (RuntimeError, AssertionError, OSError): pass
        time.sleep(1)
    raise RuntimeError('Minecraft did not enter its world')


LAST_READY = {'epoch': -1}


def claim(out, keeper, label, since=None, picture=True):
    """Fresh native session readiness, then the production F8 physical claim."""
    import rig, combined, common_probe as c, os_input as i
    since = offset() if since is None else since
    (LAB / 'live-render.txt').write_text('1'); rig.configure(LAB, False, False, [0, 0, 0])
    # `serial` counts every readiness (a control-only release keeps the collision epoch).
    ready = wait_event('native_ready', since, 180, keeper, lambda e: e.get('serial', e['epoch']) > LAST_READY['epoch'])
    LAST_READY['epoch'] = ready.get('serial', ready['epoch'])
    time.sleep(1.5)  # Java holds until known ground is below; let it settle.
    frame = dict(native_anchor=list(ANCHOR), scale=SCALE, mc_origin=[0, 100, 0], body_authority=True,
                 calibration_scope='native stream frame (anchor/scale shared with native_regions.h)')
    if combined.latest()['mode'] == '5':
        # Claimed before and not released with F8: the bridge has claimed again by itself.
        plan = dict(frame, automatic=True)
    else:
        plan = c.begin(); plan.update(frame)
        c.renew(plan); time.sleep(.15); i.focus(); i.tap(F8)
    until = time.monotonic() + 4
    while time.monotonic() < until:
        row = combined.latest()
        if row['mode'] == '5' and row['write'] == '1': break
        time.sleep(.1)
    else: raise RuntimeError('F8 claim did not reach native mode 5')
    live.save(out / f'claim-{label}.json', {'ready': ready, 'plan': plan, 'body': row, 'qpc': rig.qpc()})
    if picture: shot(out / f'claim-{label}.png')
    return plan


class Actions:
    def __init__(self, out, keeper):
        self.out, self.keeper, self.items = out, keeper, []
    def run(self, label, fn, shot_after=True):
        import rig, combined, common_probe as c
        def body():   # no body row yet at the game's title (BRIDGE_START=title)
            try: return combined.latest()
            except (StopIteration, OSError, ValueError, KeyError): return None
        item = {'label': label, 'qpc_start': rig.qpc(), 'frames_offset': offset(), 'before': c.latest('ipc_mc'), 'body_before': body()}
        self.items.append(item)
        try: fn()
        finally:
            time.sleep(.4); self.keeper.poll()
            item.update(qpc_end=rig.qpc(), after=c.latest('ipc_mc'), body_after=body())
            live.save(self.out / 'actions.json', self.items)
        if shot_after: shot(self.out / f'{label}.png')


def shot(path):
    """Screenshot without a helper window: route-13 showed the winapp capture
    helper can take the foreground, which correctly releases authority."""
    import ctypes, winsound, rig
    from PIL import ImageGrab
    hwnd, identity = rig.window(LAB); rect = (ctypes.c_long * 4)()
    ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
    # A maximized window's rectangle reaches a few pixels past the screen: keep to the desktop.
    x0, y0, w, h = [ctypes.windll.user32.GetSystemMetrics(i) for i in (76, 77, 78, 79)]
    rect = (ctypes.c_long * 4)(max(rect[0], x0), max(rect[1], y0), min(rect[2], x0 + w), min(rect[3], y0 + h))
    before = {'qpc': rig.qpc(), 'unix': time.time()}
    ImageGrab.grab(bbox=tuple(rect), all_screens=True).save(path)
    rig.shutter()
    path.with_suffix('.json').write_text(json.dumps({'before': before, 'after': {'qpc': rig.qpc(), 'unix': time.time()}, 'rect': list(rect), 'owned_cemu': identity, 'method': 'PIL ImageGrab of the owned window rectangle'}, indent=2))


def rig_background():
    import rig
    return rig.background(LAB)


def release_inputs():
    import os_input as i
    try:
        if i.ownerHWND and (rig_background() or i.u.GetForegroundWindow() == i.ownerHWND):
            for vk in (W, 83, 65, 68, SPACE, SHIFT, 0xA2): i.key(vk, False)
            for b in (1, 3): i.button(b, False)
    except Exception: pass


def coverage_paused():
    import common_probe as c
    try: return bool(c.latest('native_region_cache').get('paused'))
    except (RuntimeError, AssertionError): return False


def navigate(target, keeper, seconds=240, log=None):
    """Ordinary keyboard steering toward an MC (x, z): MC simulates every step.

    Obstacle following: when the direct heading is blocked, keep to one side
    (80 degrees off) for several steps, retrying the direct heading between
    them; a blocked side tries a jump, then the other side. Coverage pauses
    park MC, and the walker waits for resume instead of pressing on.
    """
    import combined, walking_play as play
    until = time.monotonic() + seconds; side = 1; detour = 0; blocked = 0
    def step(heading, keys, hold, progress=False):
        # Direct steps count only real progress toward the target; sliding
        # along an obstacle would otherwise oscillate at its face.
        before = play.state()['pos']; play.aim(heading, 0); play.hold(keys, hold)
        after = play.state()['pos']; moved = math.hypot(after[0] - before[0], after[2] - before[2])
        if progress: moved = math.hypot(target[0] - before[0], target[1] - before[2]) - math.hypot(target[0] - after[0], target[1] - after[2])
        if log is not None: log.append({'from': before, 'to': after, 'heading': heading, 'keys': keys, 'detour': detour, 'side': side, 'gain': moved})
        return moved
    while time.monotonic() < until:
        keeper.poll()
        assert combined.latest()['mode'] == '5', 'Authority released; stop test input'
        s = play.state(); dx = target[0] - s['pos'][0]; dz = target[1] - s['pos'][2]
        distance = math.hypot(dx, dz)
        if distance < .5: return s
        if coverage_paused():
            if log is not None: log.append({'pause_wait_at': s['pos'], 'qpc': s['qpc']})
            time.sleep(.5); continue
        direct = math.degrees(math.atan2(-dx, dz))
        if step(direct, [W], min(.4, max(.1, distance / 4.3)), True) >= .15:
            detour = 0; blocked = 0; continue
        if step(direct, [W, SPACE], .3, True) >= .15: continue  # one-block step or ledge
        if detour <= 0: detour = 6
        while detour > 0:
            detour -= 1
            if step(direct + side * 80, [W], .5) < .15:
                if step(direct + side * 80, [W, SPACE], .3) < .15:
                    side = -side; blocked += 1; detour = 6 if blocked < 4 else 0; break
            elif step(direct, [W], .3, True) >= .15: detour = 0; break
        if detour <= 0 and blocked < 4: detour = 6
        if blocked >= 4: raise RuntimeError(f'Boxed in near {s["pos"]}')
    raise RuntimeError(f'Route did not reach {target}: {play.state()}')


def probe(out, keeper):
    """Support/landing/release batch near the clearing (inside native coverage)."""
    import os_input as i, walking_play as play, combined
    acts = Actions(out, keeper)
    start = play.state()['pos']
    acts.run('idle', lambda: time.sleep(2))
    for n in range(3):
        acts.run(f'jump-land-{n}', lambda: (play.hold([SPACE], .15), time.sleep(1.2)))
    for label, dx, dz in (('slope-east', 3, 0), ('slope-north', 0, -3), ('slope-west', -3, 0), ('slope-south', 0, 3), ('return', 0, 0)):
        acts.run(label, lambda dx=dx, dz=dz: play.walk_to(start[0] + dx, start[2] + dz))
        acts.run(label + '-jump', lambda: (play.hold([SPACE], .15), time.sleep(1.2)), shot_after=False)
    acts.run('crouch', lambda: play.hold([binding('sneak', SHIFT)], 1))
    acts.run('jump-before-release', lambda: (play.hold([SPACE], .15), time.sleep(1.5)))
    # Release after landing: post-update rows show the native settle (G's 0.031204 case).
    since = offset(); t0 = time.monotonic()
    acts.run('release', lambda: (i.tap(F8), time.sleep(3)))
    assert combined.latest()['mode'] == '0'
    plan = claim(out, keeper, 'after-release', since)
    live.save(out / 'reclaim.json', {'seconds_to_reclaim': time.monotonic() - t0})
    acts.run('walk-after-reclaim', lambda: play.walk_to(start[0] + 2, start[2] + 2))
    acts.run('final-release', lambda: (i.tap(F8), time.sleep(3)))


HALL = ((-1100.73901 - ANCHOR[0]) / SCALE, (1877.45801 - ANCHOR[2]) / SCALE)  # G's surveyed hall (native)


def recover(out, keeper, label, recoveries):
    """Authority was released (focus loss, native screen): refocus, fresh claim."""
    import os_input as i, combined, ctypes
    # Which window took the foreground (diagnostic; nothing is closed).
    u = ctypes.windll.user32; hwnd = u.GetForegroundWindow(); pid = ctypes.c_uint(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    title = ctypes.create_unicode_buffer(256); u.GetWindowTextW(hwnd, title, 256)
    thief = {'hwnd': hwnd, 'pid': pid.value, 'title': title.value}
    try:
        import subprocess
        thief['exe'] = subprocess.run(['powershell', '-NoProfile', '-Command', f'(Get-Process -Id {pid.value}).Path'], capture_output=True, text=True, timeout=10).stdout.strip()
    except Exception as error: thief['exe_error'] = repr(error)
    release_inputs(); time.sleep(1)
    row = combined.latest(); t0 = time.monotonic()
    i.focus()
    if row['mode'] == '5': i.tap(F8); time.sleep(1)
    claim(out, keeper, label, 0)
    recoveries.append({'label': label, 'seconds': time.monotonic() - t0, 'released_row': row, 'foreground': thief})
    live.save(out / 'recoveries.json', recoveries)


def dump(out, keeper):
    """Ask the bridge for its cached native triangles; keep the file as evidence."""
    token = time.time_ns(); since = offset()
    (LAB / 'cache-dump.txt').write_text(str(token))
    wait_event('cache_dump', since, 20, keeper, lambda e: e['token'] == token)
    target = out / 'dumps' / f'cache-dump-{token}.bin'; target.parent.mkdir(exist_ok=True)
    shutil.copy2(LAB / f'cache-dump-{token}.bin', target)
    return target


PLANNER_CACHE = LAB / 'planner-cache'
# Bumped whenever exported collision semantics change; older dumps are not
# reused as route knowledge (2: ground-hit masks honoured, 3601c58; 3: each child's own filter, convex pieces
# instead of boxes, route-201).
COLLISION_REVISION = 3
EXHAUSTED = LAB / 'route-exhausted.json'


def planned_walk(out, keeper, target, seconds, label, arrive=1.0):
    """Repeated dump -> A* -> follow a few waypoints, until near `target`."""
    import planner, rig, walking_play as play
    until = time.monotonic() + seconds; dumps = []; plans = []; recoveries = []; log = []; grid = planner.Grid()
    # Static world knowledge from earlier runs' dumps (evidence, read only).
    def current(run):
        try: return json.loads((run / 'run.json').read_text()).get('collision_revision') == COLLISION_REVISION
        except (OSError, ValueError): return False
    prior = sorted(p for run in EVIDENCE.glob('route-*') if current(run) for p in run.glob('dumps/*.bin'))
    grid.update(planner.regions(prior), PLANNER_CACHE)  # one merge; newest dump of a region wins
    exhausted = [tuple(e) for e in json.loads(EXHAUSTED.read_text())] if EXHAUSTED.exists() else []
    live.save(out / f'{label}-prior.json', {'dumps': len(prior), 'regions': len(grid.known), 'exhausted': exhausted})
    while time.monotonic() < until:
        s = play.state()['pos']
        if math.hypot(target[0] - s[0], target[1] - s[2]) < arrive: break
        try:
            import combined
            if combined.latest()['mode'] != '5': raise RuntimeError('Authority released before dump')
            dumps.append(dump(out, keeper))
        except (RuntimeError, AssertionError) as error:
            recover(out, keeper, f'{label}-recover-{len(recoveries)}', recoveries); continue
        t0 = time.monotonic(); grid.update(planner.regions(dumps[-1:]), PLANNER_CACHE); path = grid.plan(tuple(s), target, avoid=exhausted)
        points = planner.waypoints(path or [], 1.5)
        plans.append({'from': s, 'cells': len(path or []), 'end': path[-1] if path else None, 'waypoints': points[:8], 'seconds': time.monotonic() - t0})
        live.save(out / f'{label}-plans.json', plans)
        recent = [q['from'] for q in plans[-4:]]
        stalled = len(recent) == 4 and max(math.hypot(a[0] - recent[0][0], a[2] - recent[0][2]) for a in recent) < 1
        if stalled or not points or math.hypot(points[-1][0] - s[0], points[-1][2] - s[2]) < .5:
            # Dead end reached: remember it so the next plan picks another frontier.
            exhausted.append(tuple(s)); live.save(out / f'{label}-exhausted.json', exhausted); live.save(EXHAUSTED, exhausted)
            if len(exhausted) > 12: raise RuntimeError(f'No progress possible toward {target} from {s}')
            continue
        for w in points[:6]:
            try: navigate((w[0], w[2]), keeper, 20, log)
            except (RuntimeError, AssertionError) as error:
                if 'Authority released' in str(error) or 'not foreground' in str(error):
                    recover(out, keeper, f'{label}-recover-{len(recoveries)}', recoveries)
                break
        shot(out / f'{label}-{len(plans):02}.png')
    live.save(out / f'{label}-navigation.json', log)
    return play.state()['pos']


def explore(out, keeper):
    """Walk from the clearing toward the Temple of Time hall in 8-block legs."""
    import rig, walking_play as play
    log = []; legs = []; recoveries = []
    start = play.state()['pos']; total = math.hypot(HALL[0] - start[0], HALL[1] - start[2])
    try:
        for n in range(1, int(total // 8) + 2):
            f = min(1, n * 8 / total)
            target = (start[0] + f * (HALL[0] - start[0]), start[2] + f * (HALL[1] - start[2]))
            t0 = time.monotonic()
            # A leg point may lie inside an obstacle: record it and aim further on.
            for attempt in range(3):
                try: reached = navigate(target, keeper, 60, log)['pos']; note = None; break
                except (RuntimeError, AssertionError) as error:
                    import walking_play as wp
                    if 'Authority released' in str(error) or 'not foreground' in str(error):
                        recover(out, keeper, f'recover-{n}-{attempt}', recoveries); continue
                    reached = wp.state()['pos']; note = str(error)[:120]; break
            legs.append({'leg': n, 'target': target, 'reached': reached, 'seconds': time.monotonic() - t0, 'note': note})
            live.save(out / 'legs.json', legs); shot(out / f'leg-{n:02}.png')
    finally:
        live.save(out / 'navigation.json', log); live.save(out / 'legs.json', legs)


MANUAL_STOP = LAB / 'manual-stop.txt'
MANUAL_SECONDS = 900


def manual(out, keeper):
    """A human plays in the owned Cemu window; this runner only records.

    It never sends movement input. After a release (focus loss, native menu)
    the bridge claims again by itself once Cemu is in front; after F8 the
    human presses F8 again. Stop: write 1 to lab/manual-stop.txt, or MANUAL_SECONDS.
    """
    import combined, launch, os_input as i
    MANUAL_STOP.write_text('0')
    recorder, log = combined.record(out, MANUAL_SECONDS + 30, fragmented=True)
    started = time.monotonic(); events = []; next_shot = started + 30; was_held = True
    live.save(out / 'manual.json', {'started_qpc': __import__('rig').qpc(), 'limit_seconds': MANUAL_SECONDS, 'events': events})
    try:
        while time.monotonic() - started < MANUAL_SECONDS:
            keeper.poll()
            if MANUAL_STOP.read_text().strip() == '1': events.append({'stop': 'requested', 't': time.monotonic() - started}); break
            # The bridge reclaims by itself (physical.h `wanted`); only the transitions are noted here.
            held = combined.latest()['mode'] == '5'
            if held != was_held:
                events.append({'control': held, 't': time.monotonic() - started}); was_held = held
                live.save(out / 'manual.json', {'limit_seconds': MANUAL_SECONDS, 'events': events})
            if time.monotonic() >= next_shot:
                shot(out / f'manual-{int(time.monotonic() - started):04}.png'); next_shot += 60
            time.sleep(.5)
        else: events.append({'stop': 'time limit', 't': time.monotonic() - started})
    finally:
        if recorder.poll() is None:
            try: recorder.communicate(b'q', timeout=15)
            except Exception: recorder.kill()
        launch.finish(recorder); log.close()
        live.save(out / 'manual.json', {'limit_seconds': MANUAL_SECONDS, 'events': events, 'seconds': time.monotonic() - started, 'recorder_exit': recorder.returncode})


def restart(out, keeper):
    """Restart recovery: a real Minecraft peer restart, then a native save reload.

    Each must withdraw native control, begin a new native session (one
    collision clear, MC staged onto Link, readiness from fresh coverage) and
    accept only a fresh claim; a short walk then has to be driven again.
    """
    import launch, rig, combined, walking_play as play
    acts = Actions(out, keeper); report = {}

    def walk(label, dx, dz):
        s = play.state()['pos']
        acts.run(label, lambda: play.walk_to(s[0] + dx, s[2] + dz))
        row = combined.latest(); e = play.state()['pos']
        report[label] = {'from': s, 'to': e, 'mode': row['mode'], 'write': row['write']}
        live.save(out / 'restart.json', report)
        assert row['mode'] == '5' and row['write'] == '1', (label, row)
        assert math.hypot(e[0] - s[0], e[2] - s[2]) > 1, (label, s, e)

    walk('walk-before', 2, 1)
    # 1. Peer restart: the owned Minecraft client is stopped and a new one started.
    since = offset(); t0 = time.monotonic()
    shutil.copy2(out / 'client-start.log', out / 'client-start-initial.log')
    with (out / 'peer-stop.log').open('w') as log:
        launch.run([sys.executable, '-B', str(ROOT / 'client.py'), 'stop'], cwd=ROOT, stdout=log, stderr=log, check=True)
    shutil.copy2(LAB / 'client-cleanup.json', out / 'peer-cleanup.json')
    time.sleep(3); row = combined.latest(); shot(out / 'peer-dead.png')
    report['peer_dead'] = {'mode': row['mode'], 'write': row['write']}
    assert row['mode'] == '0', ('native control survived peer death', row)
    (LAB / 'live-ipc.txt').write_text('0'); time.sleep(.5); (LAB / 'live-ipc.txt').write_text('1'); time.sleep(.5)
    client, state = start_client(out, keeper, since)
    live.save(out / 'peer-restarted.json', {'client': {k: client[k] for k in ('pid', 'creation_filetime')}, 'state': state})
    claim(out, keeper, 'after-peer-restart', since)
    report['peer_restart_seconds'] = time.monotonic() - t0
    walk('walk-after-peer-restart', -2, 1)
    # 2. Native save reload: System > Load, first entry, confirm (default is Cancel).
    since = offset(); t0 = time.monotonic(); before = combined.latest()
    rig.pulse(LAB, 'plus'); time.sleep(1.5); rig.pulse(LAB, 'tab-right'); time.sleep(1); rig.pulse(LAB, 'down'); time.sleep(.5)
    shot(out / 'reload-system.png')
    rig.pulse(LAB, 'interact'); time.sleep(2.5); shot(out / 'reload-list.png')
    rig.pulse(LAB, 'interact'); time.sleep(2.5); shot(out / 'reload-confirmation.png')
    row = combined.latest(); report['reload_menu'] = {'mode': row['mode'], 'write': row['write']}
    assert row['mode'] == '0', ('native control survived the native menu', row)
    rig.pulse(LAB, 'up'); time.sleep(.5); rig.pulse(LAB, 'interact')
    claim(out, keeper, 'after-reload', since)
    after = combined.latest()
    report['reload_seconds'] = time.monotonic() - t0
    report['reload_feet'] = {'before': [float(before['body_' + a]) for a in 'xyz'], 'after': [float(after['body_' + a]) for a in 'xyz']}
    walk('walk-after-reload', 2, -1)
    resets = [e for e in events_since(0, ['native_region_reset', 'native_ready', 'native_stage'])]
    report['sessions'] = [{k: e[k] for k in e if k in ('event', 'reason', 'epoch', 'teleport', 'body_epoch', 'peer')} for e in resets]
    live.save(out / 'restart.json', report)


PLAY_SECONDS = 6 * 3600


def play(out, keeper):
    """A human plays with nothing recorded: no video, no screenshots, quiet traces.

    For frame rate and for fun. The bridge keeps only lifecycle events
    (observer.txt 2). The bridge itself claims again after a release once Cemu
    is in front (F8 toggles it off and on). Ends when lab/manual-stop.txt holds 1, when Cemu is closed, or
    after PLAY_SECONDS; the owned processes are then stopped as usual.
    """
    import os_input as i, rig, common_probe as c
    MANUAL_STOP.write_text('0'); (LAB / 'observer.txt').write_text('2')
    started = time.monotonic(); events = []; was_held = True
    try:
        while time.monotonic() - started < PLAY_SECONDS:
            if MANUAL_STOP.read_text().strip() == '1': events.append({'stop': 'requested'}); break
            try: hwnd, _ = rig.window(LAB)
            except (RuntimeError, OSError, AssertionError):
                events.append({'stop': 'Cemu closed'}); break
            # Pose rows are not written in quiet mode; the once-a-second cache
            # event still says whether the movement lease is held.
            try: held = c.latest('native_region_cache').get('lease') == 1
            except (RuntimeError, AssertionError, OSError): held = True
            # The bridge reclaims by itself (physical.h `wanted`); only the transitions are noted here.
            if held != was_held: events.append({'control': held, 't': time.monotonic() - started}); was_held = held
            time.sleep(.5)
        else: events.append({'stop': 'time limit'})
    finally:
        (LAB / 'observer.txt').write_text('1')
        live.save(out / 'play.json', {'seconds': time.monotonic() - started, 'events': events})


PERF = {}


def set_affinity(pid, mask):
    """Processor affinity of a process this runner launched; returns the mask before and after."""
    import ctypes
    k = ctypes.WinDLL('kernel32', use_last_error=True); k.OpenProcess.restype = ctypes.c_void_p
    k.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    k.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]; k.CloseHandle.argtypes = [ctypes.c_void_p]
    h = k.OpenProcess(0x0600, False, pid)  # PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION
    if not h: return {'error': ctypes.get_last_error()}
    try:
        before, system = ctypes.c_size_t(), ctypes.c_size_t(); k.GetProcessAffinityMask(h, ctypes.byref(before), ctypes.byref(system))
        ok = bool(k.SetProcessAffinityMask(h, mask & system.value))
        return {'before': before.value, 'set': mask & system.value, 'ok': ok}
    finally: k.CloseHandle(h)


def perf_phase(out, label, client=None, seconds=12, observer='2', during=None):
    """One load sample (perf_sample.py) while `during` runs; quiet traces unless asked."""
    import perf_sample, threading
    (LAB / 'observer.txt').write_text(observer); (LAB / 'frame-timeline.txt').write_text('1'); time.sleep(2.5)
    pids = {'cemu': json.loads((LAB / 'process.json').read_text())['pid'], 'minecraft': client['pid'] if client else None}
    box = {}
    def work():
        try: box.update(perf_sample.sample(LAB, seconds, pids))
        except Exception as error: box['error'] = repr(error)
    worker = threading.Thread(target=work); worker.start()
    try:
        if during: during(seconds)
    finally:
        worker.join(); (LAB / 'observer.txt').write_text('1'); (LAB / 'frame-timeline.txt').write_text('0')
    PERF[label] = box; live.save(out / 'perf.json', PERF)


PERF_CONTROL = False  # the release/return timings (route-40) are a separate question
PACE_BUDGETS = (0, 25, 30, 0, 25, 30)  # ms of work per frame; 0 is BOTW's own order
PACE_FILE = LAB / 'frame-pace.txt'; PACE_DEFAULT = 25  # the bridge's own default (framePace::kDefaultBudgetMs)


def perf(out, keeper):
    """Where the frame time goes: the same spot sampled with the pieces switched on one by one."""
    import walking_play as play, combined
    client = json.loads((LAB / 'client-process.json').read_text())
    perf_phase(out, 'claimed-idle', client)
    def walk(seconds):
        end = time.monotonic() + seconds - 1.5; turn = 0
        while time.monotonic() < end:
            play.aim(90 * turn); play.hold([W], .8); turn += 1
    def run(seconds):
        end = time.monotonic() + seconds - 1.5; turn = 0
        while time.monotonic() < end:
            play.aim(90 * turn); play.hold([binding('sprint', 0xA2), W], 1.2); turn += 1
    # Frame pacing (lab/frame-pace.txt): the native order against a work budget, alternated
    # because the load of a spot drifts over a session.
    try:
        for index, budget in enumerate(PACE_BUDGETS):
            (LAB / 'frame-pace.txt').write_text(str(budget)); time.sleep(1.5)
            perf_phase(out, f'walking-{index}-budget-{budget}', client, 20, during=walk)
            perf_phase(out, f'sprinting-{index}-budget-{budget}', client, 20, during=run)
    finally:
        PACE_FILE.write_text(PERF.get('frame_pace_configured', str(PACE_DEFAULT)))
        time.sleep(1.5)
    if os.environ.get('PERF_SHORT') == '1': return
    (LAB / 'live-render.txt').write_text('0'); time.sleep(1)
    perf_phase(out, 'claimed-idle-minecraft-not-drawn', client)
    perf_phase(out, 'claimed-walking-minecraft-not-drawn', client, 16, during=walk)
    PERF['mode_without_draw'] = combined.latest()['mode']
    (LAB / 'live-render.txt').write_text('1'); time.sleep(1)
    perf_phase(out, 'claimed-idle-full-trace', client, observer='1')
    if PERF_CONTROL: control_checks(out, keeper)


def control_checks(out, keeper):
    """Release and return timings: F8 off and on, Alt-Tab away and back, the native menu.

    After each return a short walk has to move Minecraft again. Nothing here
    supplies a claim plan: the bridge claims on its own frame.
    """
    import ctypes, os_input as i, combined, rig, walking_play as play
    report = {}; (LAB / 'observer.txt').write_text('1'); time.sleep(1)

    def until_mode(mode, seconds):
        t0 = time.monotonic()
        while time.monotonic() - t0 < seconds:
            row = combined.latest()
            if row['mode'] == mode and (mode != '5' or row['write'] == '1'): return round(time.monotonic() - t0, 3)
            time.sleep(.02)
        return None

    def moved(label):
        if combined.latest()['mode'] != '5': report[label + '_walk'] = None; return
        a = play.state()['pos']; play.hold([W], .5); b = play.state()['pos']
        report[label + '_walk'] = round(math.hypot(b[0] - a[0], b[2] - a[2]), 3)

    def sessions(since):
        return [{k: e[k] for k in e if k in ('event', 'reason', 'control_only', 'restage', 'kept_mc', 'automatic', 'serial', 'entries')}
                for e in events_since(since, ['authority_release', 'native_region_release', 'native_region_reset', 'native_stage', 'native_ready', 'physical_claim', 'physical_claim_reject'])]

    try:
        # 1. F8 off, F8 on.
        since = offset(); i.focus(); i.tap(F8); report['f8_release_s'] = until_mode('0', 3); time.sleep(1.5)
        i.tap(F8); report['f8_return_s'] = until_mode('5', 20); report['f8_events'] = sessions(since); moved('f8')
        live.save(out / 'control.json', report)
        # 2. Alt-Tab away (a real focus change), then Cemu in front again.
        since = offset(); pack = (i.Input * 4)(*[i.Input(1, i.Union(key=i.Keyboard(vk, 0, flags, 0, 0))) for vk, flags in ((0x12, 0), (0x09, 0), (0x09, 2), (0x12, 2))])
        i.u.SendInput(4, pack, ctypes.sizeof(i.Input)); report['focus_release_s'] = until_mode('0', 3); time.sleep(2)
        hwnd, _ = rig.window(LAB); report['focus_was_lost'] = i.u.GetForegroundWindow() != hwnd
        i.focus(); report['focus_return_s'] = until_mode('5', 20); report['focus_events'] = sessions(since); moved('focus')
        live.save(out / 'control.json', report)
        # 3. Native menu (F7 opens it and releases), closed again.
        since = offset(); i.focus(); i.tap(0x76); report['menu_release_s'] = until_mode('0', 4); time.sleep(2.5)
        rig.pulse(LAB, 'plus'); report['menu_return_s'] = until_mode('5', 40); report['menu_events'] = sessions(since); moved('menu')
    finally:
        live.save(out / 'control.json', report)


def terrain(out, keeper):
    """Crouching at a native edge and walking steep ground, at spots found in the cache dumps.

    1. Crouch-walking on open ground still moves (why SkyCraft had switched edge detection off).
    2. At a walkable ledge with a 1.2-3.4 block drop: crouch-walking toward it stops on the
       ledge; walking on without crouching then drops.
    3. On a face steeper than SkyCraft's 45 degrees but inside the current limit: walking
       uphill gains height.
    """
    import terrain_probe, walking_play as play, combined
    try: degrees = float((LAB / 'walkable-degrees.txt').read_text().split()[0])
    except (OSError, ValueError, IndexError): degrees = 60.0
    limit = .7 if degrees == 45 else math.cos(math.radians(degrees)); report = {'walkable_degrees': degrees}
    acts = Actions(out, keeper)

    def position(): return play.state()['pos']

    def current(run):
        try: return json.loads((run / 'run.json').read_text()).get('collision_revision') == COLLISION_REVISION
        except (OSError, ValueError): return False
    prior = sorted(q for run in EVIDENCE.glob('route-*') if current(run) and run != out for q in run.glob('dumps/*.bin'))
    import planner
    grid = planner.Grid(); grid.update(planner.regions(prior), PLANNER_CACHE)

    def known():
        """Triangles around the player (older dumps of the same static world, then a fresh one)."""
        fresh = dump(out, keeper); grid.update(planner.regions([fresh]), PLANNER_CACHE)
        return terrain_probe.Ground(terrain_probe.load(prior + [fresh]))

    def reachable(candidates):
        """First spot the planner can route to through ground it knows (no exploring)."""
        here = tuple(position())
        for spot in candidates[:40]:
            path = grid.plan(here, tuple(spot['approach']))
            if path and math.hypot(path[-1][0] - spot['approach'][0], path[-1][2] - spot['approach'][1]) < 1 and len(path) < 240: return spot
        return None

    a = position(); acts.run('crouch-walk-open', lambda: play.hold([binding('sneak', SHIFT), W], 1.0)); b = position()
    report['crouch_walk_open_ground'] = round(math.hypot(b[0] - a[0], b[2] - a[2]), 3)
    live.save(out / 'terrain.json', report)

    edges = terrain_probe.find_edges(known(), position(), limit); edge = reachable(edges); report['edge_candidates'] = len(edges); report['edge'] = edge
    if edge:
        planned_walk(out, keeper, tuple(edge['approach']), 240, 'edge', arrive=.6)
        # Fresh data at the spot; the ledge must still be there before it is used as a test.
        edge = terrain_probe.find_edge(known(), position(), limit, reach=6.0); report['edge_at_spot'] = edge
    if edge:
        navigate(tuple(edge['approach']), keeper, 30)
        dx, dz = edge['direction']; yaw = math.degrees(math.atan2(-dx, dz)); start = position()
        along = lambda q: (q[0] - edge['last_ground'][0]) * dx + (q[2] - edge['last_ground'][1]) * dz
        acts.run('crouch-to-edge', lambda: (play.aim(yaw), play.hold([binding('sneak', SHIFT), W], 5.0)))
        stopped = position()
        acts.run('walk-off-edge', lambda: (play.aim(yaw), play.hold([W], .7), time.sleep(1)))
        after = position()
        report['edge_result'] = {'start': start, 'stopped': stopped, 'stopped_past_last_ground': round(along(stopped), 3), 'stopped_height_change': round(stopped[1] - edge['height'], 3),
                                 'after_walking_on': after, 'dropped': round(stopped[1] - after[1], 3),
                                 'crouch_stopped_on_ledge': abs(stopped[1] - edge['height']) < .5 and -1.2 < along(stopped) < .75,
                                 'edge_is_real': stopped[1] - after[1] > 1}
    live.save(out / 'terrain.json', report)

    slopes = terrain_probe.find_slopes(known(), position(), limit); slope = reachable(slopes); report['slope_candidates'] = len(slopes); report['slope'] = slope
    if slope:
        planned_walk(out, keeper, tuple(slope['approach']), 240, 'slope', arrive=.6)
        slope = terrain_probe.find_slope(known(), position(), limit, reach=6.0); report['slope_at_spot'] = slope
    if slope:
        navigate(tuple(slope['approach']), keeper, 30)
        dx, dz = slope['direction']; yaw = math.degrees(math.atan2(-dx, dz)); start = position()
        acts.run('walk-up-steep', lambda: (play.aim(yaw), play.hold([W], 1.6)))
        top = position()
        report['slope_result'] = {'start': start, 'end': top, 'rise': round(top[1] - start[1], 3), 'degrees': round(math.degrees(math.acos(slope['ny'])), 1),
                                  'climbed': top[1] - start[1] > .8}
    live.save(out / 'terrain.json', report)


def blocks(out, keeper):
    """What the native side does while Minecraft stands on blocks it built (measurement only).

    A 3x3 stone platform one block high is placed with a command in the
    private world, stood on for ten seconds, left again and removed. The
    post-update rows give Link's native ground bits, his vertical velocity and
    where his body is; lifecycle events show whether control was ever lost.
    """
    import os_input as i, walking_play as play, combined
    acts = Actions(out, keeper); report = {}
    start = play.state()['pos']; bx, by, bz = math.floor(start[0]), math.floor(start[1] + .01), math.floor(start[2])
    box = f'{bx - 1} {by} {bz + 2} {bx + 1} {by} {bz + 4}'
    since = offset(); q0 = __import__('rig').qpc()
    acts.run('place-platform', lambda: i.command(f'/fill {box} minecraft:stone'))
    marks = {'placed': __import__('rig').qpc()}
    try:
        acts.run('walk-onto-platform', lambda: navigate((bx + .5, bz + 3.5), keeper, 40))
        on = play.state(); marks['on_platform'] = __import__('rig').qpc(); report['on_platform'] = {'pos': on['pos'], 'mc_flags': on['flags'], 'rose': round(on['pos'][1] - start[1], 3)}
        acts.run('stand-on-platform', lambda: time.sleep(10)); marks['stood'] = __import__('rig').qpc()
        acts.run('jump-on-platform', lambda: (play.hold([SPACE], .15), time.sleep(1.2))); marks['jumped'] = __import__('rig').qpc()
        acts.run('walk-off-platform', lambda: navigate((start[0], start[2]), keeper, 40))
        off = play.state(); marks['off_platform'] = __import__('rig').qpc(); report['off_platform'] = {'pos': off['pos'], 'mc_flags': off['flags']}
        acts.run('stand-on-native', lambda: time.sleep(3)); marks['end'] = __import__('rig').qpc()
    finally:
        try: i.focus(); i.command(f'/fill {box} minecraft:air')
        except Exception as error: report['cleanup_error'] = repr(error)
        report['marks'] = marks; report['mode_at_end'] = combined.latest()['mode']
        report['lifecycle'] = [{k: e[k] for k in e if k in ('event', 'reason', 'control_only')} for e in events_since(since, ['authority_release', 'native_region_reset', 'native_region_release', 'physical_claim'])]
        live.save(out / 'blocks.json', report)


def render(out, keeper):
    """Everything Minecraft draws, in front of a moving camera (rendering stage 1).

    A small gallery is built with commands in the private world, a few blocks
    ahead of the player: plain and stained glass, ice, slime, a pool of water,
    leaves, a chest, a banner, a campfire and a torch; pigs, a cow, a sheep, a
    villager, an armour stand, a minecart, primed TNT, dropped items and an
    arrow. The camera pans across it from two sides, a block is looked at (the
    outline) and dug in survival mode (the cracks), and F5 goes through both
    third person views while the player walks. Everything is removed again.
    `render.json` holds the commands, the camera mode after each F5 and what
    the bridge reports having drawn (`live_draw`).
    """
    import os_input as i, walking_play as play, common_probe as c, combined
    acts = Actions(out, keeper); report = {'commands': [], 'f5': []}
    start = play.state()['pos']; bx, by, bz = math.floor(start[0]), math.floor(start[1] + .01), math.floor(start[2])
    report['start'] = start
    def at(dx, dy, dz): return f'{bx + dx} {by + dy} {bz + dz}'
    def mid(dx, dy, dz): return f'{bx + dx + .5} {by + dy} {bz + dz + .5}'
    def say(command): report['commands'].append(command); i.command(command); time.sleep(.3)
    def save(): live.save(out / 'render.json', report)
    def drawn(since):
        rows = events_since(since, ['live_draw'])
        return {'frames': len(rows), **{k: max((r.get(k, 0) for r in rows), default=0) for k in ('opaque', 'translucent', 'blended', 'overlaid', 'textures', 'view')}}
    def look(label, yaw, pitch=0):
        play.aim(yaw, pitch); time.sleep(.25); shot(out / f'{label}.png')
    def pan(label, yaws, pitch=4):
        for n, yaw in enumerate(yaws): look(f'{label}-{n:02}', yaw, pitch)
    still = '{NoAI:1b,Silent:1b,PersistenceRequired:1b}'
    setup = [
        f'/fill {at(-8, -1, 2)} {at(9, -1, 10)} minecraft:stone',                       # a floor under everything (below the native ground)
        f'/fill {at(-7, 0, 6)} {at(-7, 1, 6)} minecraft:oak_leaves[persistent=true]',  # tinted cutout
        f'/fill {at(-6, 0, 6)} {at(-6, 1, 6)} minecraft:glass',                        # cutout
        f'/fill {at(-5, 0, 6)} {at(-5, 1, 6)} minecraft:red_stained_glass',            # translucent
        f'/setblock {at(-5, 0, 8)} minecraft:blue_stained_glass',                      # translucent behind translucent
        f'/fill {at(-4, 0, 6)} {at(-4, 1, 6)} minecraft:ice',
        f'/setblock {at(-3, 0, 6)} minecraft:slime_block', f'/setblock {at(-3, 1, 6)} minecraft:honey_block',
        f'/fill {at(-1, 0, 5)} {at(2, 0, 8)} minecraft:smooth_stone', f'/fill {at(0, 0, 6)} {at(1, 0, 7)} minecraft:water',
        f'/setblock {at(4, 0, 6)} minecraft:chest[facing=north]', f'/setblock {at(5, 0, 6)} minecraft:white_banner[rotation=8]',
        f'/setblock {at(6, 0, 6)} minecraft:campfire', f'/setblock {at(7, 0, 6)} minecraft:obsidian', f'/setblock {at(7, 1, 6)} minecraft:torch',
        f'/summon minecraft:pig {mid(-6, 0, 4)} {still}', f'/summon minecraft:cow {mid(-4, 0, 4)} {still}',
        f'/summon minecraft:sheep {mid(-2, 0, 4)} {still}', f'/summon minecraft:villager {mid(3, 0, 4)} {still}',
        f'/summon minecraft:armor_stand {mid(5, 0, 4)} {{ShowArms:1b}}', f'/summon minecraft:minecart {mid(7, 0, 4)}',
        f'/summon minecraft:tnt {mid(0, 1, 3)} {{fuse:6000}}',
        f'/summon minecraft:item {mid(-1, 1, 3)} {{Item:{{id:"minecraft:diamond_sword",count:1}},PickupDelay:32767}}',
        f'/summon minecraft:item {mid(2, 1, 3)} {{Item:{{id:"minecraft:grass_block",count:1}},PickupDelay:32767}}',
        f'/summon minecraft:arrow {mid(1, 2, 3)} {{NoGravity:1b}}',
    ]
    since = offset()
    try:
        acts.run('gallery-built', lambda: [say(command) for command in setup])
        time.sleep(2); save()
        # 1. From where the player stands: a pan across the whole row, then up and down.
        mark = offset(); pan('front', range(-60, 61, 15)); look('front-down', 0, 35); look('front-up', 0, -25)
        say(f'/particle minecraft:flame {mid(0, 2, 4)} 0.6 0.6 0.6 0.01 60 force'); look('particles', 0, 0)
        report['drawn_front'] = drawn(mark); save()
        # 2. From the east end, along the row: glass in front of ice in front of water.
        acts.run('walk-to-side', lambda: navigate((bx - 9.5, bz + 6.5), keeper, 60))
        mark = offset(); pan('side', range(-120, -59, 15)); report['drawn_side'] = drawn(mark); save()
        # 3. Behind the row, looking back through it.
        acts.run('walk-behind', lambda: navigate((bx - 2.5, bz + 12.5), keeper, 60))
        mark = offset(); pan('behind', range(135, 226, 15)); report['drawn_behind'] = drawn(mark); save()
        # 4. The outline of the block looked at, and the cracks of one being dug (survival).
        acts.run('walk-to-pool', lambda: navigate((bx + .5, bz + 3.2), keeper, 60))
        mark = offset(); look('outline', 0, 38)
        say('/gamemode survival'); play.aim(0, 38); i.button(1, True)
        try: time.sleep(1.1); shot(out / 'cracks.png')
        finally: i.button(1, False)
        say('/gamemode creative'); report['drawn_outline_cracks'] = drawn(mark); save()
        # 5. F5: behind the player, then in front, then first person again.
        acts.run('walk-back', lambda: navigate((start[0], start[2] - 3), keeper, 60))
        def f5(label):
            before = play.state()['camera_mode']; i.tap(0x74); time.sleep(1.0); state = play.state()
            report['f5'].append({'label': label, 'camera_mode_before': before, 'camera_mode_after': state['camera_mode'], 'mode': combined.latest()['mode'],
                                 'control_held': c.latest('native_region_cache').get('lease') == 1}); save()
        mark = offset(); f5('behind'); pan('third-behind', (0, 40, 80, 120), 10)
        play.aim(0, 10); i.key(W, True)
        try:
            for n in range(3): time.sleep(.35); shot(out / f'third-behind-walking-{n}.png')
        finally: i.key(W, False)
        report['walked_in_third_person'] = play.state()['pos']
        f5('front'); pan('third-front', (0, 60, 180), 5)
        report['drawn_third_person'] = drawn(mark)
        f5('first'); look('first-again', 0, 5); save()
    finally:
        try:
            i.focus(); i.button(1, False)
            if play.state()['camera_mode'] != 0:
                for _ in range(3 - play.state()['camera_mode']): i.tap(0x74); time.sleep(.5)
            for command in ('/gamemode creative', '/kill @e[type=!minecraft:player,distance=..48]', f'/fill {at(-8, -1, 2)} {at(9, 3, 10)} minecraft:air', '/kill @e[type=minecraft:item,distance=..48]'):
                i.command(command); time.sleep(.3)
        except Exception as error: report['cleanup_error'] = repr(error)
        report['camera_mode_at_end'] = play.state()['camera_mode']; report['mode_at_end'] = combined.latest()['mode']
        report['lifecycle'] = [{k: e[k] for k in e if k in ('event', 'reason', 'control_only', 'message')} for e in events_since(since, ['authority_release', 'native_region_reset', 'native_region_release', 'physical_claim', 'render_error', 'ipc_fault', 'live_gpu_overload'])]
        report['uploads'] = {name: len(events_since(since, [name])) for name in ('live_texture_upload', 'live_section_upload', 'live_atlas_upload', 'live_gpu_generation')}
        save()


def survey_capture(out, keeper, label, report):
    """One frame written down by the layer (frame_survey.h), moved into the run's folder and decoded."""
    import frame_survey
    token = f'{label}-{int(time.time())}'; folder = LAB / 'frame-survey' / token
    (LAB / 'frame-survey.txt').write_text(token); until = time.monotonic() + 45
    while time.monotonic() < until and not (folder / 'done.txt').exists(): time.sleep(.25); keeper.poll()
    if not (folder / 'done.txt').exists(): report[label] = {'error': 'the layer wrote no capture', 'token': token}
    else:
        target = out / f'survey-{label}'; shutil.move(str(folder), str(target))
        try: report[label] = frame_survey.decode(target)
        except Exception as error:
            import traceback
            report[label] = {'error': repr(error), 'traceback': traceback.format_exc()}
    live.save(out / 'survey.json', report)


def survey(out, keeper):
    """How BOTW builds its frame, measured (rendering stage 2): the layer writes one whole frame down.

    Three frames from the same spot: Minecraft's view with a few blocks ahead
    (where and into what the bridge draws today), the same view looking at the
    sky and the sun's side, and BOTW's own camera with Link after an F8
    release. Each capture holds every render pass with its targets, draws,
    pipelines, sampled images, uniform buffers and a reduced picture of every
    target after every pass; `frame_survey.py` makes passes.md, targets.md
    and contact sheets from it.
    """
    import os_input as i, walking_play as play, combined
    report = {}; start = play.state()['pos']; bx, by, bz = math.floor(start[0]), math.floor(start[1] + .01), math.floor(start[2])
    box = f'{bx - 2} {by} {bz + 4} {bx + 2} {by + 1} {bz + 5}'
    def until_mode(mode, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            row = combined.latest()
            if row['mode'] == mode and (mode != '5' or row['write'] == '1'): return True
            time.sleep(.05)
        return False
    try:
        i.command(f'/fill {box} minecraft:stone'); i.command(f'/setblock {bx} {by + 2} {bz + 4} minecraft:red_stained_glass'); time.sleep(1.5)
        play.aim(0, 8); time.sleep(.5); shot(out / 'view-claimed.png'); survey_capture(out, keeper, 'claimed', report)
        play.aim(150, -20); time.sleep(.5); shot(out / 'view-claimed-turned.png'); survey_capture(out, keeper, 'claimed-turned', report)
        play.aim(0, 8); time.sleep(.3)
        i.focus(); i.tap(F8); report['released'] = until_mode('0', 4); time.sleep(2.5)
        shot(out / 'view-native.png'); survey_capture(out, keeper, 'native', report)
        i.focus(); i.tap(F8); report['claimed_again'] = until_mode('5', 25)
    finally:
        try: i.focus(); i.command(f'/fill {bx - 2} {by} {bz + 4} {bx + 2} {by + 2} {bz + 5} minecraft:air')
        except Exception as error: report['cleanup_error'] = repr(error)
        (LAB / 'frame-survey.txt').write_text(''); live.save(out / 'survey.json', report)


def contact_sheet(path, pictures, columns=4, width=640):
    """One JPEG of labelled screenshots (label, file) in rows of `columns`."""
    from PIL import Image, ImageDraw
    cells = []
    for label, file in pictures:
        if not pathlib.Path(file).exists(): continue
        im = Image.open(file).convert('RGB'); im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
        draw = ImageDraw.Draw(im); draw.rectangle((0, 0, 9 + 7 * len(label), 16), fill=(0, 0, 0)); draw.text((4, 2), label, fill=(255, 255, 255)); cells.append(im)
    if not cells: return
    h = max(c.height for c in cells); rows = (len(cells) + columns - 1) // columns
    sheet = Image.new('RGB', (width * min(columns, len(cells)), h * rows), (20, 20, 20))
    for n, cell in enumerate(cells): sheet.paste(cell, (width * (n % columns), h * (n // columns)))
    path.parent.mkdir(exist_ok=True); sheet.save(path, quality=88)


def integrate_summary(since):
    """What integrate.h did in the frames since `since`, and how the frames were paced meanwhile."""
    import statistics
    rows = events_since(since, ['integrate_frame']); pace = events_since(since, ['frame_pacing']); gpu = events_since(since, ['gpu_timing'])
    why = {}
    for r in rows:
        key = ' / '.join(r['why']); why[key] = why.get(key, 0) + 1
    kinds = {}
    for g in gpu: kinds.setdefault(str(g.get('kind', 0)), []).append(g['ns'] / 1e6)
    return {'frames': len(rows), 'opaque': sum(r['opaque'] for r in rows), 'blended': sum(r['blended'] for r in rows), 'cast_all_three': sum(r['cast'] == 7 for r in rows),
            'left_for_marker': sorted({r['left'] for r in rows}), 'why': why, 'max_view_error': max((r['view_error'] for r in rows), default=None), 'max_device_error': max((r['device_error'] for r in rows), default=None),
            'cpu_us_median': statistics.median([r['cpu_us'] for r in rows]) if rows else None, 'cpu_us_max': max((r['cpu_us'] for r in rows), default=None),
            'vertices_max': [max((r['vertices'][k] for r in rows), default=0) for k in range(3)], 'watched_max': max((r['watched'] for r in rows), default=0), 'unread_max': max((r['unread'] for r in rows), default=0), 'sun': rows[-1]['sun'] if rows else None, 'light': rows[-1]['light'] if rows else None,
            'sun_found': sum(any(r['sun']) for r in rows), 'sun_looked_for': sum(r.get('sun_draws', 0) > 0 for r in rows), 'sun_near_when_missing': next((r.get('sun_near') for r in reversed(rows) if r.get('sun_draws') and not any(r['sun'])), None),
            'gpu_ms_median_by_kind': {k: round(statistics.median(v), 3) for k, v in kinds.items()},
            'paced_frames': sum(p['frames'] for p in pace), 'late_frames': sum(p['late'] for p in pace), 'work_mean_ms': round(statistics.mean([p['work_mean_ms'] for p in pace]), 2) if pace else None}


def integrate(out, keeper, only=None):
    """Minecraft inside BOTW's own frame (rendering stages 2 and 3), compared part by part.

    A small scene is built a few blocks ahead: a stone-brick wall, a log with
    a canopy of leaves, a roofed hut with a torch inside, glass, stained
    glass, a pool, a pig and a cow. Each view is photographed four times, with
    `lab/integrate.txt` naming what integrate.h does: 0 the stage 1 path
    (drawn over the finished picture), 1 the G-buffer, 5 G-buffer and shadow
    casters, 7 those and the blended geometry in HDR. Then the camera pans
    and walks with everything on and third person is entered once.
    A slab of stone hangs over the native grass west of the start: its shadow
    is the one that falls on BOTW's own ground. `only='entities'` (the plan
    `speckle`) stops after the two views that hold the pig at the edge of
    the screen, where its side is seen edge-on from the camera's axis.
    `only='materials'` (the plan `materials`) photographs two views once per
    value of the G-buffer's material class, flags and normal alpha instead.
    `integrate.json` holds
    what the layer reports per setting (`integrate_frame`) and the pacing.
    """
    import os_input as i, walking_play as play, combined
    report = {'commands': [], 'settings': {}, 'views': []}; file = LAB / 'integrate.txt'
    start = play.state()['pos']; bx, by, bz = math.floor(start[0]), math.floor(start[1] + .01), math.floor(start[2]); report['start'] = start
    def at(dx, dy, dz): return f'{bx + dx} {by + dy} {bz + dz}'
    def mid(dx, dy, dz): return f'{bx + dx + .5} {by + dy} {bz + dz + .5}'
    def say(command): report['commands'].append(command); i.command(command); time.sleep(.3)
    def save(): live.save(out / 'integrate.json', report)
    def setting(value): file.write_text(str(value)); time.sleep(1.2)
    still = '{NoAI:1b,Silent:1b,PersistenceRequired:1b}'
    setup = [
        f'/fill {at(-2, -1, 2)} {at(8, -1, 13)} minecraft:stone',                       # a floor under the pool, the log and the hut only:
        f'/fill {at(-6, 0, 9)} {at(-3, 3, 9)} minecraft:stone_bricks',                 # the wall stands on the native ground and shades it
        f'/fill {at(-8, 4, -2)} {at(-4, 4, 2)} minecraft:stone',                       # a slab in the air over the native grass
        f'/fill {at(0, 0, 8)} {at(0, 4, 8)} minecraft:oak_log', f'/fill {at(-2, 4, 6)} {at(2, 4, 10)} minecraft:oak_leaves[persistent=true]', f'/fill {at(-1, 5, 7)} {at(1, 5, 9)} minecraft:oak_leaves[persistent=true]',
        f'/setblock {at(0, 4, 8)} minecraft:oak_log',
        f'/fill {at(3, 0, 8)} {at(7, 3, 12)} minecraft:oak_planks hollow', f'/fill {at(4, 0, 8)} {at(5, 1, 8)} minecraft:air', f'/setblock {at(6, 0, 11)} minecraft:torch',
        f'/fill {at(-6, 0, 5)} {at(-6, 1, 5)} minecraft:glass', f'/fill {at(-5, 0, 5)} {at(-5, 1, 5)} minecraft:red_stained_glass', f'/fill {at(-4, 0, 5)} {at(-4, 1, 5)} minecraft:ice',
        f'/fill {at(-2, 0, 3)} {at(1, 0, 6)} minecraft:smooth_stone', f'/fill {at(-1, 0, 4)} {at(0, 0, 5)} minecraft:water',
        f'/summon minecraft:pig {mid(3, 0, 3)} {still}', f'/summon minecraft:cow {mid(6, 0, 3)} {still}',
    ]
    since = offset(); pictures = {}
    def view(label, yaw, pitch):
        row = {'label': label, 'yaw': yaw, 'pitch': pitch, 'pos': play.state()['pos']}
        for value in (0, 1, 5, 7):
            setting(value); play.aim(yaw, pitch); time.sleep(.3); mark = offset(); time.sleep(.5); name = f'{label}-parts{value}.png'; shot(out / name)
            row[str(value)] = integrate_summary(mark); pictures.setdefault(label, []).append((f'{label} parts {value}', out / name))
        report['views'].append(row); save()
        contact_sheet(out / 'sheets' / f'{label}.jpg', pictures[label], 2, 960)
    acts = Actions(out, keeper)
    try:
        acts.run('scene-built', lambda: [say(command) for command in setup]); time.sleep(2.5); save()
        if only == 'materials':
            # What BOTW's lighting makes of the values the G-buffer carries beside colour and normal (report 44 left them open).
            tiles = {'slab': [], 'front': []}; report['materials'] = []
            for cls, flags, alpha in ((4, 0, 47), (4, 0, 0), (4, 0, 128), (4, 0, 255), (4, 64, 47), (4, 96, 47), (4, 112, 47), (7, 0, 0), (7, 0, 47), (8, 0, 47), (22, 0, 47), (4, 0, 47)):
                file.write_text(f'7 {cls} {flags} {alpha}'); time.sleep(1.2); name = f'class{cls}-flags{flags}-alpha{alpha}'; row = {'class': cls, 'flags': flags, 'normal_alpha': alpha}
                for label, yaw, pitch in (('slab', 90, -4), ('front', 0, 6)):
                    play.aim(yaw, pitch); time.sleep(.5); path = out / f'{label}-{name}-{len(tiles[label])}.png'; shot(path); tiles[label].append((f'class {cls} flags {flags} alpha {alpha}', path))
                report['materials'].append(row)
            for label in tiles: contact_sheet(out / 'sheets' / f'materials-{label}.jpg', tiles[label], 4, 640)
            save(); return
        # The pig at the left edge of the picture: its west side has a view-space normal across the camera's axis.
        view('pig-edge', 0, 20); view('pig-edge-right', 6, 20)
        if only == 'entities': return
        # West of the start the slab's shadow lies on BOTW's own grass; the second view holds the slab as well.
        view('ground-shadow', 90, 14); view('ground-caster', 90, -4)
        view('front', 0, 6); view('front-left', -35, 2); view('front-right', 35, 2)
        # From the west, where the morning sun throws the shadows towards the viewer and onto the native ground.
        acts.run('walk-west', lambda: navigate((bx - 9.5, bz + 6.5), keeper, 60)); view('west', -90, 8)
        acts.run('walk-behind', lambda: navigate((bx - 9.5, bz + 12.5), keeper, 60)); view('behind', 245, 10)
        # The floor behind the log, where the canopy's shadow falls, seen from above the grass.
        view('floor-shadow', 262, 22)
        def walk(label, *points):
            for n, point in enumerate(points): acts.run(f'{label}-{n}', lambda point=point: navigate(point, keeper, 60), shot_after=False)
        walk('walk-hut', (bx - 9.5, bz + 0.5), (bx + 5.0, bz + 0.5), (bx + 5.0, bz + 5.5)); view('hut-door', 0, 5)
        # Everything on, the camera moving: a pan, then walking past the scene.
        setting(7); walk('walk-back', (bx + 5.0, bz + 0.5), (start[0], start[2])); mark = offset(); moving = []
        for n, yaw in enumerate(range(-70, 71, 20)): play.aim(yaw, 4); time.sleep(.25); shot(out / f'pan-{n:02}.png'); moving.append((f'pan yaw {yaw}', out / f'pan-{n:02}.png'))
        play.aim(-20, 6); i.key(W, True)
        try:
            for n in range(4): time.sleep(.4); shot(out / f'walk-{n}.png'); moving.append((f'walking {n}', out / f'walk-{n}.png'))
        finally: i.key(W, False)
        report['moving'] = integrate_summary(mark); save(); contact_sheet(out / 'sheets' / 'moving.jpg', moving, 4, 640)
        # Third person: the player's own model is in the G-buffer and casts as well.
        walk('walk-open', (start[0], start[2]), (bx - 9.5, bz + 0.5), (bx - 9.5, bz + 12.5)); mark = offset(); third = []
        i.tap(0x74); time.sleep(1.0); report['camera_mode_third'] = play.state()['camera_mode']
        for n, yaw in enumerate((262, 200, 320)): play.aim(yaw, 20); time.sleep(.4); shot(out / f'third-{n}.png'); third.append((f'third person yaw {yaw}', out / f'third-{n}.png'))
        report['third'] = integrate_summary(mark); contact_sheet(out / 'sheets' / 'third.jpg', third, 3, 640)
        for _ in range(2): i.tap(0x74); time.sleep(.6)
        report['camera_mode_after'] = play.state()['camera_mode']; save()
        # No frame survey here: route-74 ended in a machine check (bugcheck 0x124) seconds after one was asked for.
        # A long steady stretch for the pacing: stage 1 path, then everything.
        walk('walk-front', (bx - 9.5, bz + 0.5), (start[0], start[2])); play.aim(0, 6)
        for value in (0, 7): setting(value); mark = offset(); time.sleep(12); report['settings'][f'steady-{value}'] = integrate_summary(mark)
        save()
    finally:
        try:
            file.write_text('7'); i.focus()
            if play.state()['camera_mode'] != 0:
                for _ in range(3 - play.state()['camera_mode']): i.tap(0x74); time.sleep(.5)
            for command in ('/kill @e[type=!minecraft:player,distance=..48]', f'/fill {at(-7, -1, 2)} {at(8, 5, 13)} minecraft:air', f'/fill {at(-8, 4, -2)} {at(-4, 4, 2)} minecraft:air', '/kill @e[type=minecraft:item,distance=..48]'):
                i.command(command); time.sleep(.3)
        except Exception as error: report['cleanup_error'] = repr(error)
        report['lifecycle'] = [{k: e[k] for k in e if k in ('event', 'reason', 'where', 'message')} for e in events_since(since, ['authority_release', 'render_error', 'integrate_error', 'ipc_fault', 'live_gpu_overload'])]
        (LAB / 'frame-survey.txt').write_text(''); save()


def camera_summary(since):
    """What game_camera.h saw and did since `since`, and whether the camera the game draws with followed."""
    import statistics
    rows = events_since(since, ['game_camera']); probes = [r for r in events_since(since, ['view_probe_camera']) if 'native_eye' in r]
    apart = [r['apart'] for r in rows if r['own']]
    drawn = [math.dist(r['native_eye'], r['eye']) for r in probes]
    return {'records': len(rows), 'own': sum(r['own'] for r in rows), 'written': sum(r['written'] for r in rows), 'branches': sorted({r['branch'] for r in rows}), 'views': sorted({r['view'] for r in rows}),
            # How far the game's own camera stood from Minecraft's before the write (native units = metres).
            'apart_median': round(statistics.median(apart), 3) if apart else None, 'apart_max': round(max(apart), 3) if apart else None,
            # How far the camera object the game would have drawn with is from Minecraft's, at the render selector.
            'selector_records': len(probes), 'drawn_apart_median': round(statistics.median(drawn), 3) if drawn else None, 'drawn_apart_max': round(max(drawn), 3) if drawn else None,
            'last': {k: rows[-1][k] for k in ('native_eye', 'native_at', 'eye', 'forward', 'view', 'branch')} if rows else None}


def game_camera(out, keeper):
    """BOTW's own camera given Minecraft's (game_camera.h): each view with the write off and on.

    `lab/game-camera.txt` 0 leaves the game's camera where its chase camera put
    it (the state before this change), 1 writes Minecraft's eye and direction
    into it at the end of the camera actor's update. First person looks level,
    steeply up (where report 29 saw the wedge in the sky), sideways and down;
    then both of Minecraft's third person views. `camera.json` holds, per
    picture, how far apart the two cameras were, what integrate.h did and the
    pacing; a pan and a walk with the write on follow.
    """
    import os_input as i, walking_play as play
    report = {'views': [], 'settings': {}}; file = LAB / 'game-camera.txt'; since = offset(); pictures = {}
    def save(): live.save(out / 'camera.json', report)
    def setting(value): file.write_text(str(value)); time.sleep(1.2)
    def view(label, yaw, pitch):
        row = {'label': label, 'yaw': yaw, 'pitch': pitch, 'camera_mode': play.state()['camera_mode']}
        for value in (0, 1):
            setting(value); play.aim(yaw, pitch); time.sleep(.4); mark = offset(); time.sleep(.6); name = f'{label}-write{value}.png'; shot(out / name)
            row[str(value)] = {'camera': camera_summary(mark), 'integrate': integrate_summary(mark)}
            pictures.setdefault(label[:5], []).append((f'{label} write {value}', out / name))
        report['views'].append(row); save()
    try:
        for label, yaw, pitch in (('first-level', 0, 6), ('first-up', 0, -70), ('first-side-up', 90, -40), ('first-back', 180, 10), ('first-down', 0, 60)): view(label, yaw, pitch)
        contact_sheet(out / 'sheets' / 'first-person.jpg', pictures['first'], 2, 960)
        i.tap(0x74); time.sleep(1.0)
        for label, yaw, pitch in (('back-level', 0, 10), ('back-up', 120, -30)): view(label, yaw, pitch)
        contact_sheet(out / 'sheets' / 'third-back.jpg', pictures['back-'], 2, 960)
        i.tap(0x74); time.sleep(1.0)
        for label, yaw, pitch in (('front-level', 0, 10), ('front-up', 120, -30)): view(label, yaw, pitch)
        contact_sheet(out / 'sheets' / 'third-front.jpg', pictures['front'], 2, 960)
        i.tap(0x74); time.sleep(1.0); report['camera_mode_after'] = play.state()['camera_mode']
        # The write on, the camera moving: a full turn, then a walk.
        setting(1); mark = offset(); moving = []
        for n, yaw in enumerate(range(0, 360, 45)): play.aim(yaw, -10 if n % 2 else 15); time.sleep(.3); shot(out / f'turn-{n}.png'); moving.append((f'turn yaw {yaw}', out / f'turn-{n}.png'))
        play.aim(0, 6); i.key(W, True)
        try:
            for n in range(4): time.sleep(.5); shot(out / f'walk-{n}.png'); moving.append((f'walking {n}', out / f'walk-{n}.png'))
        finally: i.key(W, False)
        report['moving'] = {'camera': camera_summary(mark), 'integrate': integrate_summary(mark)}; contact_sheet(out / 'sheets' / 'moving.jpg', moving, 4, 640); save()
        play.aim(0, 6)
        for value in (0, 1): setting(value); mark = offset(); time.sleep(10); report['settings'][f'steady-{value}'] = {'camera': camera_summary(mark), 'integrate': integrate_summary(mark)}
        save()
    finally:
        try:
            file.write_text('1'); i.focus()
            if play.state()['camera_mode'] != 0:
                for _ in range(3 - play.state()['camera_mode']): i.tap(0x74); time.sleep(.5)
        except Exception as error: report['cleanup_error'] = repr(error)
        report['lifecycle'] = [{k: e[k] for k in e if k in ('event', 'reason', 'where', 'message')} for e in events_since(since, ['authority_release', 'render_error', 'integrate_error', 'ipc_fault', 'live_gpu_overload', 'read_failed'])]
        save()


def motion_quality(since):
    """Smoothness of the driven body from live_body_write rows after a trace offset.

    While Minecraft moves steadily each update should advance by speed x frame
    time. `held` are updates that barely moved (the 20 Hz steps of the old
    single-tick interpolation), `jumped` those that moved 1.6 times too far.
    """
    rows = events_since(since, ['live_body_write']); ratios = []; delays = []; late = 0
    for a, b in zip(rows, rows[1:]):
        dt = (b['qpc'] - a['qpc']) / 1e7
        if not 0 < dt <= .06 or b['tick_ms'] <= 0: continue
        speed = math.hypot(b['cur'][0] - b['prev'][0], b['cur'][2] - b['prev'][2]) / (b['tick_ms'] / 1000)
        if speed < 3: continue
        ratios.append(math.hypot(b['target'][0] - a['target'][0], b['target'][2] - a['target'][2]) / SCALE / (speed * dt))
        delays.append(b.get('delay_ms', 0)); late += b.get('ticks', 0) > 1
    if not ratios: return {'moving_updates': 0}
    ratios.sort(); n = len(ratios)
    return {'moving_updates': n, 'step_ratio_p5_median_p95': [round(ratios[int(n * .05)], 2), round(ratios[n // 2], 2), round(ratios[int(n * .95)], 2)],
            'held_share': round(sum(r < .2 for r in ratios) / n, 3), 'jumped_share': round(sum(r > 1.6 for r in ratios) / n, 3),
            'delay_ms_median': round(sorted(delays)[n // 2], 1), 'updates_past_tick_end': late}


def fall(out, keeper):
    """Motion smoothness, then a high fall: only Steve may take damage.

    Minecraft is lifted about 13 blocks with a levitation effect and dropped.
    Recorded: whether Link's health was written (and blocked), whether the
    native player entered a state the controller does not drive (ragdoll),
    whether control was released, and the fall guard's own events.
    """
    import os_input as i, walking_play as play, combined
    acts = Actions(out, keeper); report = {}; start = play.state()['pos']
    since = offset()
    acts.run('walk-east', lambda: (play.aim(-90), play.hold([W], 2.0)))
    acts.run('walk-back', lambda: (play.aim(90), play.hold([binding('sprint', 0xA2), W], 2.0)))
    report['motion'] = motion_quality(since); live.save(out / 'fall.json', report)
    # About 22 blocks (28 m): past the 20 m at which Link's own fall damage starts. Steve is
    # given full damage resistance for the test so that the run can continue either way.
    def drop(label, guard):
        (LAB / 'damage-guard.txt').write_text(str(guard)); time.sleep(1.5)
        since = offset(); before = play.state()['pos']
        acts.run(label, lambda: (i.command('/effect give @s minecraft:resistance 30 4'), i.command('/effect give @s minecraft:levitation 3 7'), time.sleep(11)))
        after = play.state()['pos']; row = combined.latest()
        peak = max((e['cur'][1] for e in events_since(since, ['live_body_write'])), default=None)
        refused = events_since(since, ['live_target_reject'])
        report[label] = {'guard': guard, 'from': before, 'to': after, 'peak_height_above_start': None if peak is None else round(peak - before[1], 2), 'mode_after': row['mode'],
                         'native_health_writes': [{k: e[k] for k in ('old', 'new', 'blocked')} for e in events_since(since, ['native_health'])],
                         'releases': [e['reason'] for e in events_since(since, ['authority_release'])],
                         'updates_refused_for_native_state': len(refused), 'native_flags_seen': sorted({e['flags'] for e in refused})[:6]}
        live.save(out / 'fall.json', report)
        return after
    try:
        after = drop('fall-guarded', 1)
        report['fall_guard'] = [{k: e[k] for k in e if k not in ('event',)} for e in events_since(0, ['fall_guard'])]
        acts.run('walk-after-guarded-fall', lambda: (play.aim(0), play.hold([W], 1.0)))
        end = play.state()['pos']; report['walk_after_guarded_fall'] = round(math.hypot(end[0] - after[0], end[2] - after[2]), 2)
        live.save(out / 'fall.json', report)
        drop('fall-health-guard-only', 2)  # the native fall happens; only the health store is refused
        drop('fall-unguarded', 0)          # last: Link may not survive this one
    finally:
        (LAB / 'damage-guard.txt').write_text('1')
        live.save(out / 'fall.json', report)


def robust(out, keeper):
    """Control has to outlast a native state of the player (report 49).

    A fall with only the health store guarded puts Link into a native hit state (route-56).
    The same drop is made for each setting of lab/follow-states.txt, and W is held for three
    seconds from the landing on. Recorded per setting: releases and claims of control, the
    body_follow events (write left out / taken up again), how far Minecraft walked with W
    held, and how far Link's body was from the place written to it one update earlier.
    """
    import os_input as i, walking_play as play
    acts = Actions(out, keeper); report = {}; states = LAB / 'follow-states.txt'
    def save(): live.save(out / 'robust.json', report)
    def gap(since_qpc):
        worst = 0.0; rows = 0
        for r in csv_rows():
            if int(r['qpc']) < since_qpc or r['phase'] != '0' or r['previous_request'] != '1': continue
            rows += 1
            worst = max(worst, math.dist([float(r[f'requested_{a}']) for a in 'xyz'], [float(r[f'body_{a}']) for a in 'xyz']))
        return {'updates_after_a_write': rows, 'largest_distance_from_written_place': round(worst, 4)}
    def csv_rows():
        import csv
        for name in ('poses.csv.1', 'poses.csv'):
            if (LAB / name).exists():
                with (LAB / name).open(newline='', encoding='utf-8', errors='replace') as f:
                    for r in csv.DictReader(f):
                        if None not in r.values(): yield r
    def drop(label, setting, yaw):
        import rig
        waited = time.monotonic()
        while play_mode() != '5' and time.monotonic() - waited < 20: time.sleep(.2)
        waited = round(time.monotonic() - waited, 1); play.aim(yaw)
        states.write_text(setting); (LAB / 'damage-guard.txt').write_text('2'); time.sleep(1.5)
        since = offset(); begun = rig.qpc(); before = play.state()['pos']; walked = {}
        def act():
            i.command('/effect give @s minecraft:resistance 30 4'); i.command('/effect give @s minecraft:levitation 3 7')
            deadline = time.monotonic() + 12; top = before[1]
            while time.monotonic() < deadline:  # up, then down to within a block of the start height
                y = play.state()['pos'][1]; top = max(top, y)
                if top - before[1] > 5 and y - before[1] < 1.0: break
                time.sleep(.03)
            landed = play.state()['pos']; play.hold([W], 3.0); end = play.state()['pos']
            walked.update(peak=round(top - before[1], 2), landed=landed, end=end, distance=round(math.hypot(end[0] - landed[0], end[2] - landed[2]), 2))
            time.sleep(3)
        acts.run(label, act)
        follow = events_since(since, ['body_follow'])
        report[label] = {'follow_states': setting, 'waited_for_control_before_s': waited, 'peak_height_above_start': walked.get('peak'), 'walked_with_W_held_3s': walked.get('distance'),
                         'releases': [{k: e[k] for k in ('reason', 'control_only')} for e in events_since(since, ['authority_release'])],
                         'input_releases': [{k: e.get(k) for k in ('reason', 'why')} for e in events_since(since, ['physical_release'])],
                         'claims': len(events_since(since, ['physical_claim'])),
                         'body_follow': [{k: e.get(k) for k in ('state', 'flags', 'controller_flags', 'ms', 'leash_writes')} for e in follow],
                         'native_health_writes': [{k: e[k] for k in ('old', 'new', 'blocked')} for e in events_since(since, ['native_health'])],
                         'refused_updates': len(events_since(since, ['live_target_reject'])), 'mode_after': play_mode(), **gap(begun)}
        save()
    def play_mode():
        import combined
        return combined.latest()['mode']
    try:
        # The lab profile starts in creative flight, where levitation lifts nothing (route-89).
        i.command('/gamemode survival'); time.sleep(1.5); report['flags_in_survival'] = play.state()['flags']
        drop('state-releases-control', '1 80188490 0', -90)   # as up to route-88
        drop('write-left-out', '0 80188490 0', 90)                # control kept, the write waits for the state to end
        drop('write-continues', '0 0 1', -90)             # control kept, the body is written in every state
        drop('write-left-out-leash-3', '0 80188490 0 3', 90)  # as the second, but never more than 3 units behind
    finally:
        (LAB / 'damage-guard.txt').write_text('1'); states.write_text(FOLLOW_DEFAULT)
        # What was refused on Link and sent to Minecraft's player, and what Minecraft's own log says it did with it.
        sent = [json.loads(line) for line in (LAB / 'control.jsonl').read_text(encoding='utf-8').splitlines() if 'native_damage_to_minecraft' in line] if (LAB / 'control.jsonl').exists() else []
        report['damage_to_minecraft'] = [{k: e[k] for k in ('quarter_hearts', 'minecraft_damage', 'sent')} for e in sent]
        log = LAB / 'minecraft/logs/latest.log'
        report['minecraft_log'] = [line.split('BotwCraft: ', 1)[1].strip() for line in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'hit the player' in line] if log.exists() else None
        save()
        try: i.command('/gamemode creative')   # as the lab world was before this plan (route-92 started in survival)
        except Exception: pass


# Where enemies hit Link in route-101, a play session (body_follow, hit states), in Minecraft's
# coordinates: (BOTW - anchor) / 1.3 + (0, 100, 0). Bokoblins of the Great Plateau.
CAMPS = [{'x': 168.0, 'z': 110.0}, {'x': 293.4, 'z': 66.0}]


def hits(out, keeper, camps=None):
    """Enemies hit Link while Minecraft drives (report 51).

    Steve flies to a place where enemies hit Link in a play session, lands and waits. First the hits
    are only recorded (lab/damage-event.txt 0: the amounts as the game passes them to the
    player's damage manager, the guest stack, and what the health store then did), then they
    are refused there (1). Recorded for each part: the damage events, the health writes, what
    was sent to Minecraft, Link's native states, and Minecraft's own log lines.
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'camps': camps or CAMPS}; mode = LAB / 'damage-event.txt'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'hits.json', report)
    def fly_to(x, z, seconds=240, near=2.5):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            s = play.state(); dx, dz = x - s['pos'][0], z - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < near: return True
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        return False
    def land():
        i.key(SNEAK, True)
        try:
            until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
            while time.monotonic() < until:
                s = play.state(); y = s['pos'][1]
                if y < low - .05: low = y; still = time.monotonic()
                if not s['flags'] & 128 or time.monotonic() - still > 2.5: break   # flight ends on the ground
                time.sleep(.05)
        finally: i.key(SNEAK, False)
        time.sleep(1.5)
    # control.jsonl, by time: frames.jsonl rotates within a phase of a minute (route-105: the refused phase read as empty).
    def logged(since_ms, names):
        rows = [json.loads(line) for line in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if line.startswith('{')]
        return [e for e in rows if e['unix_ms'] >= since_ms and e['event'] in names]
    def struck(since_ms): return sum(1 for e in logged(since_ms, ['native_add_damage']) if e['damage'] > 0)
    def wait_for_hits(label, setting, wanted, seconds):
        """Stand, then walk a small square, until `wanted` damage events have come or the time is over."""
        mode.write_text(setting); time.sleep(1.2)
        since = int(time.time() * 1000); begun = time.monotonic(); before = play.state()['pos']; turn = 0
        def action():
            nonlocal turn
            while time.monotonic() - begun < seconds and struck(since) < wanted:
                if time.monotonic() - begun > 20 + 12 * turn:   # nobody came: move a little, enemies notice movement
                    play.aim(90 * turn); play.hold([W], 1.0); turn += 1
                time.sleep(.5)
            time.sleep(4)   # the states that follow the last hit
        acts.run(label, action)
        after = play.state()
        adds = [e for e in logged(since, ['native_add_damage']) if e['damage'] > 0 or e['r6'] > 0 or e['r7'] > 0]
        receives = [e for e in logged(since, ['native_damage_receive']) if e['r7'] > 0 or e['r8'] > 0 or e['r9'] > 0]
        report[label] = {'damage_event': setting, 'seconds': round(time.monotonic() - begun, 1), 'from': before, 'to': after['pos'], 'minecraft_flags_after': after['flags'],
                         'add_damage': [{k: e[k] for k in ('count', 'refused', 'r4', 'damage', 'r6', 'r7', 'r8', 'r9', 'r10', 'flags', 'stack')} for e in adds][:40],
                         'add_damage_events': len(adds),
                         'damage_receive': [{k: e[k] for k in ('count', 'r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 's0', 's1', 'stack')} for e in receives][:40],
                         'attacks': [{k: e[k] for k in ('damage', 'defence', 'rate', 'bare', 'second', 'attack_damage', 'least', 'kind', 'attacker', 'attacker_id', 'name', 'at')} for e in logged(since, ['native_attack'])][:40],
                         'native_health_writes': [{k: e[k] for k in ('old', 'new', 'blocked')} for e in logged(since, ['native_health'])],
                         'to_minecraft': [{k: e.get(k) for k in ('quarter_hearts', 'minecraft_damage', 'sent', 'attacker')} for e in logged(since, ['native_damage_to_minecraft'])],
                         'body_follow': [{k: e[k] for k in ('state', 'flags', 'ms')} for e in logged(since, ['body_follow'])][:60],
                         'releases': [{k: e[k] for k in ('reason', 'control_only')} for e in logged(since, ['authority_release'])],
                         'input_releases': [{k: e.get(k) for k in ('reason', 'why')} for e in logged(since, ['physical_release'])],
                         'native_screens': [{k: e[k] for k in ('ready', 'screen')} for e in logged(since, ['lifecycle'])]}
        save(); return len(adds)
    try:
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)
        for n, camp in enumerate(report['camps']):
            name = f'camp-{n}'
            i.command('/gamemode creative'); time.sleep(1.5)
            if not play.state()['flags'] & 128:
                i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
                i.key(JUMP, True); time.sleep(1.5); i.key(JUMP, False); time.sleep(.4)
            report[name + '-reached'] = fly_to(camp['x'], camp['z']); report[name + '-above'] = play.state()['pos']; save()
            if not report[name + '-reached']: continue
            acts.run(name + '-land', land); report[name + '-landed'] = play.state()['pos']
            # Survival, so that Minecraft's player takes what is sent; he may die and comes back in place (route-101).
            i.command('/gamemode survival'); time.sleep(1.5); save()
            seen = wait_for_hits(name + '-recorded-only', '0', 3, 100)
            if not seen: continue   # nobody here
            wait_for_hits(name + '-refused', '1', 8, 80)
            wait_for_hits(name + '-recorded-only-again', '0', 2, 40)
            break
    finally:
        mode.write_text(DAMAGE_EVENT_DEFAULT)
        log = LAB / 'minecraft/logs/latest.log'
        report['minecraft_log'] = [line.split('BotwCraft: ', 1)[1].strip() for line in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'hit the player' in line or 'died' in line][-80:] if log.exists() else None
        save()
        try: i.command('/gamemode creative')
        except Exception: pass


def actor_table(out, keeper):
    """The actor table (report 52).

    Steve flies to the Bokoblins of route-101, lands and stands. Recorded: what the bridge
    lists (lab/actors.jsonl, twice a second in this plan), what Minecraft mirrors (its log), and
    whether blows at the nearest enemy's stand-in land (Minecraft's log line for a hit).
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'camp': CAMPS[0]}; census = LAB / 'actors.jsonl'; log = LAB / 'minecraft/logs/latest.log'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'actors.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        return json.loads(lines[-1]) if lines else None
    def lines(text): return [l.split('BotwCraft: ', 1)[1].strip() for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if text in l and 'BotwCraft: ' in l] if log.exists() else []
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def fly_to(x, z, seconds=240, near=2.5):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            s = play.state(); dx, dz = x - s['pos'][0], z - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < near: return True
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        return False
    def land():
        i.key(SNEAK, True)
        try:
            until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
            while time.monotonic() < until:
                s = play.state(); y = s['pos'][1]
                if y < low - .05: low = y; still = time.monotonic()
                if not s['flags'] & 128 or time.monotonic() - still > 2.5: break
                time.sleep(.05)
        finally: i.key(SNEAK, False)
        time.sleep(1.5)
    try:
        (LAB / 'actors.txt').write_text('1 500')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)
        report['at_start'] = last(); save()
        report['reached'] = fly_to(CAMPS[0]['x'], CAMPS[0]['z']); acts.run('land', land); report['landed'] = play.state()['pos']
        acts.run('stand-15s', lambda: time.sleep(15))
        report['at_camp'] = last(); report['mirrored'] = lines('mirrored'); save()
        # Step 3: Steve walks at the nearest enemy for six seconds, first with the stand-ins not solid, then solid.
        def walk_into(label, hard):
            (LAB / 'actors.txt').write_text(f'1 100 {hard}'); time.sleep(1.5); samples = []
            def action():
                end = time.monotonic() + 6; i.key(W, True)
                try:
                    while time.monotonic() < end:
                        table = last(); s = play.state()
                        foes = [a for a in (table or {}).get('actors', []) if a['kind'] == 1 and not a['dead']]
                        if not foes: time.sleep(.1); continue
                        target = min(foes, key=lambda a: a['distance']); x, y, z = mc(target['at'])
                        dx, dz = x - s['pos'][0], z - s['pos'][2]; flat = math.hypot(dx, dz)
                        samples.append({'t': round(6 - (end - time.monotonic()), 2), 'flat': round(flat, 3), 'above': round(s['pos'][1] - y, 2), 'width_blocks': round(target['width'] / 1.3, 3)})
                        if flat > .15: play.aim(math.degrees(math.atan2(-dx, dz)), 0)
                finally: i.key(W, False)
            acts.run(label, action)
            report[label] = {'solid': hard, 'closest': min((x['flat'] for x in samples), default=None), 'samples': samples}; save()
            play.aim(play.state()['yaw'] + 180); play.hold([W], 1.2)   # step back for the next part
        walk_into('walk-into-enemy-not-solid', 0); walk_into('walk-into-enemy-solid', 1)
        (LAB / 'actors.txt').write_text('1 500 1')
        struck = []
        def strike():
            end = time.monotonic() + 90; first = None
            while time.monotonic() < end and len(struck) < 24:
                table = last(); s = play.state()
                foes = [a for a in (table or {}).get('actors', []) if a['kind'] == 1 and not a['dead']]
                if first is not None and first not in [a['id'] for a in foes]: break   # the one that was struck is dead or gone
                if not foes: time.sleep(.5); continue
                target = min(foes, key=lambda a: a['distance']); x, y, z = mc(target['at'])
                dx, dy, dz = x - s['pos'][0], y + .7 - (s['pos'][1] + 1.62), z - s['pos'][2]; flat = math.hypot(dx, dz)
                play.aim(math.degrees(math.atan2(-dx, dz)), -math.degrees(math.atan2(dy, max(flat, .01))))
                if flat > 2.6: play.hold([W], min(.5, flat / 6)); continue
                i.click(1); time.sleep(1.1); first = target['id']
                now = [a for a in (last() or {}).get('actors', []) if a['id'] == target['id']]
                struck.append({'target': target['name'], 'id': target['id'], 'flat': round(flat, 2), 'life_before': target['life'], 'most_life': target['most_life'],
                               'life_after': now[0]['life'] if now else None, 'dead_after': now[0]['dead'] if now else None, 'listed_after': bool(now)})
        acts.run('strike-the-nearest-enemy', strike)
        time.sleep(6)
        report['blows'] = struck; report['hits_in_minecraft_log'] = lines('BotwCraft: hit '); report['at_end'] = last()
        report['actor_hit_events'] = [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"actor_hit"' in l]
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1')
        report['mirrored'] = lines('mirrored'); save()


def weapons(out, keeper):
    """Different weapons of Steve on one enemy (report 53).

    At the Bokoblin of route-101, with one point of Minecraft damage worth half a point of BOTW
    life (so that one enemy outlasts several weapons): an arrow from the bow, the diamond sword,
    the fist. Recorded for each: Minecraft's own log line for the hit and the enemy's life in
    BOTW before and after. The hotbar is that of the lab profile: bow in slot 1, diamond sword in 2, bread in 3 (route-111).
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'camp': CAMPS[0], 'botw_life_per_minecraft_damage': 0.5}; census = LAB / 'actors.jsonl'; log = LAB / 'minecraft/logs/latest.log'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'weapons.json', report)
    def last():
        rows = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        return json.loads(rows[-1]) if rows else None
    def lines(text): return [l.split('BotwCraft: ', 1)[1].strip() for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if text in l and 'BotwCraft: ' in l] if log.exists() else []
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def foe():
        foes = [a for a in (last() or {}).get('actors', []) if a['kind'] == 1 and not a['dead']]
        return min(foes, key=lambda a: a['distance']) if foes else None
    def face(target, reach):
        """Aim at the middle of the target; step towards it while it is out of reach. True when it is within reach."""
        s = play.state(); x, y, z = mc(target['at'])
        dx, dy, dz = x - s['pos'][0], y + .7 - (s['pos'][1] + 1.62), z - s['pos'][2]; flat = math.hypot(dx, dz)
        play.aim(math.degrees(math.atan2(-dx, dz)), -math.degrees(math.atan2(dy, max(flat, .01))))
        if flat > reach: play.hold([W], min(.5, flat / 6)); return False
        return True
    def use(label, slot, act, tries=6):
        rows = []
        def action():
            i.tap(0x30 + slot); time.sleep(.4)
            for _ in range(tries):
                target = foe()
                if not target: break
                if not face(target, 2.6): continue
                before = len(lines('BotwCraft: hit ')); act(); time.sleep(1.2)
                now = [a for a in (last() or {}).get('actors', []) if a['id'] == target['id']]; said = lines('BotwCraft: hit ')[before:]
                rows.append({'life_before': round(target['life'] * target['most_life'], 2), 'most_life': target['most_life'],
                             'life_after': round(now[0]['life'] * now[0]['most_life'], 2) if now else None, 'dead_after': now[0]['dead'] if now else None, 'minecraft': said})
                if said: break
        acts.run(label, action); report[label] = rows; save()
    try:
        (LAB / 'actors.txt').write_text('1 250 1'); (LAB / 'damage-to-botw.txt').write_text('0.5')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)
        end = time.monotonic() + 240
        while time.monotonic() < end:
            s = play.state(); dx, dz = CAMPS[0]['x'] - s['pos'][0], CAMPS[0]['z'] - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < 2.5: break
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        def land():
            i.key(SNEAK, True)
            try:
                until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
                while time.monotonic() < until:
                    s = play.state(); y = s['pos'][1]
                    if y < low - .05: low = y; still = time.monotonic()
                    if not s['flags'] & 128 or time.monotonic() - still > 2.5: break
                    time.sleep(.05)
            finally: i.key(SNEAK, False)
            time.sleep(4)
        acts.run('land', land); report['landed'] = play.state()['pos']; report['enemy'] = foe(); save()
        def shoot(): i.button(3, True); time.sleep(1.3); i.button(3, False)   # a full draw
        use('bow', 1, shoot); use('diamond-sword', 2, lambda: i.click(1)); use('bread-in-hand', 3, lambda: i.click(1)); use('fist', 7, lambda: i.click(1)); use('diamond-sword-again', 2, lambda: i.click(1))
        report['enemy_at_end'] = foe()
        report['actor_hit_events'] = [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"actor_hit"' in l]
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1'); (LAB / 'damage-to-botw.txt').write_text('3')
        report['hits_in_minecraft_log'] = lines('BotwCraft: hit '); save()
        try: i.tap(0x37)
        except Exception: pass


def water_plan(out, keeper):
    """BOTW's water as Minecraft's water (report 54).

    Steve hovers over the pond of route-91, is switched to survival and drops in; once with the
    water grid off and once with it on. Recorded each time: the grid the bridge published
    (lab/water.jsonl), his height while he sinks, at rest and with the jump key held, Minecraft's
    own Air and Health values for him, how far two seconds of walking take him, and whether
    Minecraft's swimming pose is reached with sprint held.
    """
    import os_input as i, walking_play as play, rig, re
    acts = Actions(out, keeper); report = {'place': POND}; census = LAB / 'water.jsonl'; log = LAB / 'minecraft/logs/latest.log'; switch = LAB / 'water.txt'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'water.json', report)
    def grid():
        """The pass published last, in Minecraft's heights."""
        rows = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        if not rows: return None
        row = json.loads(rows[-1]); s = play.state(); codes = row['code']
        heights = [round((h - 251.188492) / 1.3 + 100, 3) if c not in (0, -1) else None for c, h in zip(codes, row['botw_height'])]
        raw = [round((h - 251.188492) / 1.3 + 100, 3) if abs(h) < 1e6 else None for h in row['botw_height']]
        cx, cz = math.floor(s['pos'][0]) - row['origin'][0], math.floor(s['pos'][2]) - row['origin'][1]; inside = 0 <= cx < 16 and 0 <= cz < 16; wet = [h for h in heights if h is not None]
        return {'age_ms': int(time.time() * 1000) - row['unix_ms'], 'passes': row['passes'], 'frames_a_pass': row['frames'], 'origin': row['origin'], 'player': s['pos'],
                'answers': {str(c): codes.count(c) for c in sorted(set(codes))}, 'wet_columns': len(wet), 'surface_lowest': min(wet) if wet else None, 'surface_highest': max(wet) if wet else None,
                'surface_under_player': heights[cz * 16 + cx] if inside else None, 'height_given_under_player': raw[cz * 16 + cx] if inside else None, 'answer_under_player': codes[cz * 16 + cx] if inside else None,
                'surface_rows': [heights[z * 16:z * 16 + 16] for z in range(16)]}
    def data(name):
        """Minecraft's own value of the player (its /data command, read back from its log)."""
        said = lambda: [l for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'has the following entity data: ' in l] if log.exists() else []
        before = len(said()); i.command(f'/data get entity @s {name}'); time.sleep(.7); now = said()
        found = re.search(r'entity data: (-?[0-9.]+)', now[-1]) if len(now) > before else None
        return float(found.group(1)) if found else None
    def watch(seconds):
        rows = []; begun = time.monotonic()
        while time.monotonic() - begun < seconds:
            s = play.state(); rows.append([round(time.monotonic() - begun, 2), round(s['pos'][1], 3), s['flags']]); time.sleep(.1)
        return rows
    def summary(rows): return {'lowest': min(r[1] for r in rows), 'highest': max(r[1] for r in rows), 'last': rows[-1][1], 'on_ground_samples': sum(1 for r in rows if r[2] & 4), 'swimming_samples': sum(1 for r in rows if r[2] & 64), 'samples': len(rows)}
    def over():
        """Creative, flying, a little above the pond's surface and over its middle."""
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.2)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        end = time.monotonic() + 8; i.key(JUMP, True)
        try:
            while time.monotonic() < end and play.state()['pos'][1] < POND['float'] + 2.5: time.sleep(.03)
        finally: i.key(JUMP, False)
        time.sleep(.6); end = time.monotonic() + 240
        while time.monotonic() < end:
            s = play.state(); dx, dz = POND['x'] - s['pos'][0], POND['z'] - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < 1.0: break
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        end = time.monotonic() + 30; i.key(SNEAK, True)   # the run starts far above the pond's height (route-113)
        try:
            while time.monotonic() < end and play.state()['pos'][1] > POND['float'] + 3.5: time.sleep(.03)
        finally: i.key(SNEAK, False)
        time.sleep(.8)
    def trial(label, on):
        result = {'water_grid': on}
        def action():
            switch.write_text(f'{1 if on else 0} 256 250'); over(); time.sleep(1.5)
            result['grid_over_the_pond'] = grid() if on else None; result['dropped_from'] = play.state()['pos']
            i.command('/gamemode survival'); rows = watch(6); result['sinking'] = summary(rows); result['sinking_heights'] = [r[1] for r in rows[:40]]
            result['health_after_the_drop'] = data('Health'); time.sleep(6); result['at_rest'] = play.state()['pos']; result['air_at_rest'] = data('Air')
            i.key(JUMP, True)
            try: rows = watch(6)
            finally: i.key(JUMP, False)
            result['jump_held'] = summary(rows); result['jump_held_heights'] = [r[1] for r in rows[:60]]; result['air_after_jump_held'] = data('Air')
            i.key(SNEAK, True)   # in water: down, and held under
            try: rows = watch(8)
            finally: i.key(SNEAK, False)
            result['sneak_held'] = summary(rows); result['air_after_sneak_held_8s'] = data('Air'); result['eye_after_sneak_held'] = play.state()['eye']
            time.sleep(3); play.aim(90, 0); before = play.state()['pos']; play.hold([W], 2.0); after = play.state()['pos']
            result['walked_in_2s'] = round(math.hypot(after[0] - before[0], after[2] - before[2]), 3)
            play.aim(-90, 25); i.key(SPRINT, True); i.key(W, True)
            try: rows = watch(3)
            finally: i.key(W, False); i.key(SPRINT, False)
            result['sprint_forward'] = summary(rows); result['grid_at_the_end'] = grid() if on else None
            result['releases'] = len([l for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"authority_release"' in l or '"physical_release"' in l])
        acts.run(label, action); report[label] = result; save()
    try:
        trial('grid-off', False); trial('grid-on', True)
        report['water_grid_events'] = [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"water_grid"' in l][-12:]
    finally:
        switch.write_text('1 256 5000'); save()
        try: i.command('/gamemode creative')
        except Exception: pass


def disturbed():
    """How often this session lost control for lateness (a machine at full load): Minecraft's state late, the game's update late."""
    try: rows = [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"authority_release"' in l]
    except OSError: rows = []
    late = [r.get('reason') for r in rows if r.get('reason') in ('unsupported-body-or-target', 'update-heartbeat-expired', 'invalid-player')]
    try: behind = len([l for l in (LAB / 'minecraft/logs/latest.log').read_text(encoding='utf-8', errors='replace').splitlines() if "Can't keep up" in l])
    except OSError: behind = 0
    return {'releases_for_lateness': len(late), 'minecraft_server_behind': behind}


def airwall(out, keeper):
    """Step 5 study: the game's own invisible box asked for where Steve is about to walk (spawn.h, report 56).

    Two AirWall objects are asked for as the map asks for its own (a record and a map entry with
    `Shape: Box` under !Parameters): one eight units ahead of the start along z whose entry holds
    its Scale and Translate (half extents 6, 4, 1 if the scale is taken as the map's own walls
    take it), and one thirty units off to the side whose place and scale are in a parameter pack
    instead. The proc dump is read for both. Steve then walks at the first for three seconds: the
    collision harvest takes the AirWall layer, so a box there stops him at its near face. What
    the harvest saw of the bodies (shape type, bounds) is in the run's native-region-slot files
    and is read when the run is summed up. (Routes 137 and 139: asked for by the ordinary request,
    with or without Shape as an AI value, an AirWall is a sphere of the largest scale's radius.)
    """
    import os_input as i, walking_play as play, rig, combined, struct
    report = {}
    def save(): live.save(out / 'airwall.json', report)
    def link(): r = combined.latest(); return [float(r['body_' + a]) for a in 'xyz']
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def walls():
        """The AirWall objects the game runs now, by id: place, distance and scale (actors.h, lab/procs.txt)."""
        (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
        rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
        found = {}
        for p in (json.loads(rows[-1])['procs'] if rows else []):
            if p.get('name') != 'AirWall': continue
            e = found.setdefault(str(p['id']), {})
            if 'at' in p: e.update(at=p['at'], distance=p['distance'])
            if 'from_1f8' in p:
                raw = bytes.fromhex(p['from_1f8'][:len(p['from_1f8']) // 2 * 2])
                if len(raw) >= 0x88: e['scale'] = [round(v, 3) for v in struct.unpack('>3f', raw[0x7C:0x88])]
                if len(raw) >= 0x16C: e['box_at_34c'] = [round(v, 3) for v in struct.unpack('>6f', raw[0x154:0x16C])]
        if rows: (out / 'procs.jsonl').write_text(rows[-1] + chr(10), encoding='utf-8')
        return found
    acts = Actions(out, keeper)
    try:
        report['walls_before'] = walls(); a = link(); report['start'] = a; save()
        box = {'centre': [a[0], a[1] + 1.0, a[2] + 8.0], 'scale': [6.0, 4.0, 1.0], 'extra': 'Shape=Box'}
        plain = {'centre': [a[0] - 30.0, a[1] + 1.0, a[2] - 30.0], 'scale': [6.0, 4.0, 1.0], 'extra': 'Shape=Box pack=1'}
        report['asked_for'] = {'entry_has_place_and_scale': box, 'pack_has_place_and_scale': plain, 'near_face_if_a_box': round(box['centre'][2] - 1.0, 3)}
        (LAB / 'spawn.txt').write_text(''.join('map AirWall ' + ' '.join(f'{v:.4f}' for v in w['centre'] + w['scale']) + ' ' + w['extra'] + chr(10) for w in (box, plain))); time.sleep(5.0)
        report['answers'] = control('actor_spawn'); after = walls()
        report['walls_new'] = {k: v for k, v in after.items() if k not in report['walls_before']}; save()
        def steve():
            c = link(); play.aim(0, 0); play.hold([W], 3.0); time.sleep(.6); d = link()
            report['steve_walks_at_the_box'] = {'from': c, 'to': d, 'along_z': round(d[2] - c[2], 3), 'before_the_near_face': round(report['asked_for']['near_face_if_a_box'] - d[2], 3),
                                                'minecraft': play.state()['pos'], 'unblocked_would_be_about': round(4.317 * 3 * SCALE, 2)}
        acts.run('steve-walks-at-the-box', steve); save()
        if (LAB / 'creates.jsonl').exists(): shutil.copyfile(LAB / 'creates.jsonl', out / 'creates.jsonl')
    finally:
        report['disturbed'] = disturbed(); save()


def pen(out, keeper):
    """Step 5 study: do the game's invisible boxes hold an enemy? (spawn.h, report 56)

    At the Bokoblin of route-101. Steve lands by it and walks up to it. Four box AirWalls are then
    asked for around the two of them (inside 10 units square, 8 units tall), Steve flies out over
    them and lands some 13 blocks from the middle, and the enemy's place is read for 30 seconds:
    held if it stays inside, not held if it comes to Steve. The four are then deleted
    (BaseProc::deleteLater) and the enemy is read for another 30 seconds; with the walls gone it
    is free to come.
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'camp': CAMPS[0]}; census = LAB / 'actors.jsonl'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'pen.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        try: return json.loads(lines[-1]) if lines else None
        except ValueError: return json.loads(lines[-2]) if len(lines) > 1 else None
    def spawned(): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"actor_spawn"' in l]
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def flat(a, b): return math.hypot(a[0] - b[0], a[2] - b[2])
    def fly():
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
    def fly_to(x, z, seconds=240, near=2.5):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            s = play.state(); dx, dz = x - s['pos'][0], z - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < near: return True
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        return False
    def land():
        i.key(SNEAK, True)
        try:
            until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
            while time.monotonic() < until:
                s = play.state(); y = s['pos'][1]
                if y < low - .05: low = y; still = time.monotonic()
                if not s['flags'] & 128 or time.monotonic() - still > 2.5: break
                time.sleep(.05)
        finally: i.key(SNEAK, False)
        time.sleep(1.5)
    def foe(ident=None):
        foes = [a for a in (last() or {}).get('actors', []) if a['kind'] == 1 and not a['dead'] and ident in (None, a['id'])]
        return min(foes, key=lambda a: a['distance']) if foes else None
    def slots():
        (LAB / 'spawn.txt').write_text('slots\n'); time.sleep(1.5); rows = [r for r in spawned() if r.get('state') == 'slots']
        return rows[-1] if rows else None
    def watch(label, seconds, centre, ident):
        rows = []
        def action():
            start = time.monotonic()
            while time.monotonic() - start < seconds:
                a = foe(ident); s = play.state()
                if a: rows.append({'t': round(time.monotonic() - start, 1), 'at': a['at'], 'from_the_middle': round(flat(a['at'], centre), 2), 'blocks_to_steve': round(flat(mc(a['at']), s['pos']), 2), 'life': a['life']})
                time.sleep(.5)
        acts.run(label, action)
        report[label] = {'samples': len(rows), 'farthest_from_the_middle': max((r['from_the_middle'] for r in rows), default=None),
                         'nearest_to_steve_blocks': min((r['blocks_to_steve'] for r in rows), default=None), 'every_fourth': rows[::4]}; save()
    try:
        (LAB / 'actors.txt').write_text('1 500')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5); fly()
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)
        report['reached'] = fly_to(CAMPS[0]['x'], CAMPS[0]['z'], near=6.0); acts.run('land', land); report['landed'] = play.state()['pos']
        def meet():
            end = time.monotonic() + 45
            while time.monotonic() < end:
                a = foe(); s = play.state()
                if not a: time.sleep(.5); continue
                x, y, z = mc(a['at']); dx, dz = x - s['pos'][0], z - s['pos'][2]; apart = math.hypot(dx, dz)
                if apart < 2.4: break
                play.aim(math.degrees(math.atan2(-dx, dz)), 0); play.hold([W], min(.5, apart / 6))
            time.sleep(1.0)
        acts.run('steve-and-the-enemy-meet', meet)
        a = foe(); assert a, 'no enemy listed at the camp'
        s = play.state(); centre = a['at']; ident = a['id']
        report['enemy'] = {'name': a['name'], 'id': ident, 'at': centre, 'life': a['life'], 'blocks_to_steve': round(flat(mc(centre), s['pos']), 2)}
        half, tall, thick = 5.0, 4.0, .5; y = centre[1] + 2.0; reach = half + thick
        walls = [([centre[0], y, centre[2] + reach], [reach + thick, tall, thick]), ([centre[0], y, centre[2] - reach], [reach + thick, tall, thick]),
                 ([centre[0] + reach, y, centre[2]], [thick, tall, reach + thick]), ([centre[0] - reach, y, centre[2]], [thick, tall, reach + thick])]
        report['asked_for'] = {'inside_half_size': half, 'top_over_the_enemy_feet': 2.0 + tall, 'walls': walls}
        (LAB / 'spawn.txt').write_text(''.join('map AirWall ' + ' '.join(f'{v:.4f}' for v in place + scale) + ' Shape=Box\n' for place, scale in walls)); time.sleep(4.0)
        report['answers'] = [r for r in spawned() if r.get('state') != 'slots']; report['slots_after_asking'] = slots(); save()
        def out_of_it():
            s = play.state(); e = mc(centre); dx, dz = s['pos'][0] - e[0], s['pos'][2] - e[2]
            if math.hypot(dx, dz) < .2: dx, dz = 1.0, 0.0
            fly(); floor = s['pos'][1]; end = time.monotonic() + 6; i.key(JUMP, True)
            try:
                while time.monotonic() < end and play.state()['pos'][1] < floor + 7.5: time.sleep(.05)   # the walls' top is 4.6 blocks over the enemy's feet
            finally: i.key(JUMP, False)
            report['rose_to'] = round(play.state()['pos'][1] - floor, 2); play.aim(math.degrees(math.atan2(-dx, dz)), 0); end = time.monotonic() + 15
            while time.monotonic() < end and flat(play.state()['pos'], e) < 13: play.hold([W], .2)
            land(); play.aim(math.degrees(math.atan2(dx, -dz)), 0)   # down, and turned to face the pen
            report['steve_outside'] = {'at': play.state()['pos'], 'blocks_from_the_middle': round(flat(play.state()['pos'], e), 2), 'flying': bool(play.state()['flags'] & 128)}
        acts.run('steve-flies-out', out_of_it); save()
        watch('with-the-pen', 30, centre, ident)
        (LAB / 'spawn.txt').write_text(''.join(f'delete {n}\n' for n in range(4))); time.sleep(3.0)
        report['delete_answers'] = [r for r in spawned() if 'delete' in str(r.get('state'))]; report['slots_after_deleting'] = slots(); save()
        watch('pen-removed', 30, centre, ident)
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1')
        report['disturbed'] = disturbed(); save()


def things(out, keeper):
    """Step 4, the things Link's weapons fell or break: a pot, a crate, a barrel and a tree struck by Steve (report 56).

    A pot, a crate and a barrel are asked for five units ahead of the start and a tree eleven
    units ahead (spawn.h, the game's ordinary creation request). The actor table lists such things
    as its fourth kind (actors.h). Steve walks to each in turn, the asked-for ones first and then
    the trees the map itself has near, and strikes it up to eight times with a diamond axe.
    Recorded: what the table listed, each thing's life before and after every blow, whether it is
    still listed, the bridge's record of each blow (queued, given to the game or lost) and
    Minecraft's own line for each hit.
    """
    import os_input as i, walking_play as play, rig, combined
    acts = Actions(out, keeper); report = {}; census = LAB / 'actors.jsonl'; log = LAB / 'minecraft/logs/latest.log'
    def save(): live.save(out / 'things.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        try: return json.loads(lines[-1]) if lines else None
        except ValueError: return json.loads(lines[-2]) if len(lines) > 1 else None
    def lines(text): return [l.split('BotwCraft: ', 1)[1].strip() for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if text in l and 'BotwCraft: ' in l] if log.exists() else []
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def link(): r = combined.latest(); return [float(r['body_' + a]) for a in 'xyz']
    def listed(ident=None): return [a for a in (last() or {}).get('actors', []) if a['kind'] == 4 and ident in (None, a['id'])]
    def brief(a): return {k: a[k] for k in ('id', 'name', 'at', 'distance', 'life_offset', 'most_life', 'life', 'width', 'height', 'box')}
    try:
        (LAB / 'actors.txt').write_text('1 500')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        a = link(); report['start'] = a; time.sleep(1.0); report['listed_before'] = [brief(x) for x in listed()]
        asked = [('Pot', -3.0, 5.0), ('Kibako_Contain_01', 0.0, 5.0), ('Barrel', 3.0, 5.0), ('Obj_TreeConiferous_A_02', 0.0, 11.0)]
        report['asked_for'] = [{'name': n, 'at': [a[0] + dx, a[1] + .3, a[2] + dz]} for n, dx, dz in asked]
        (LAB / 'spawn.txt').write_text(''.join(f"{w['name']} " + ' '.join(f'{v:.4f}' for v in w['at']) + '\n' for w in report['asked_for'])); time.sleep(8.0)
        report['answers'] = control('actor_spawn'); before = {x['id'] for x in report['listed_before']}
        report['listed_after_asking'] = [brief(x) for x in listed()]; save()
        i.command('/item replace entity @s weapon.mainhand with diamond_axe'); time.sleep(.5)
        new = sorted([x for x in listed() if x['id'] not in before], key=lambda x: x['distance']); old = sorted([x for x in listed() if x['id'] in before and x['name'].startswith('Obj_Tree')], key=lambda x: x['distance'])
        report['struck'] = []
        def strike(thing):
            blows = []; ident = thing['id']; end = time.monotonic() + 60; entry = {'name': thing['name'], 'id': ident, 'asked_for': ident not in before, 'most_life': thing['most_life'], 'blows': blows}; report['struck'].append(entry)
            while time.monotonic() < end and len(blows) < 8:
                now = listed(ident); s = play.state()
                if not now: entry['gone_after_blows'] = len(blows); break
                target = now[0]; x, y, z = mc(target['at'])
                dx, dy, dz = x - s['pos'][0], y + .4 - (s['pos'][1] + 1.62), z - s['pos'][2]; flat = math.hypot(dx, dz)
                play.aim(math.degrees(math.atan2(-dx, dz)), -math.degrees(math.atan2(dy, max(flat, .01))))
                if flat > 2.2 and not entry.get('stopped_at'):
                    c = s['pos']; play.hold([W], min(.5, flat / 6))
                    if math.hypot(play.state()['pos'][0] - c[0], play.state()['pos'][2] - c[2]) < .02 and flat < 3.4: entry['stopped_at'] = round(flat, 2)   # the thing's own collision holds him off
                    continue
                i.click(1); time.sleep(1.1); after = listed(ident)
                blows.append({'flat': round(flat, 2), 'life_before': target['life'], 'life_after': after[0]['life'] if after else None, 'listed_after': bool(after), 'at_after': after[0]['at'] if after else None})
            entry['still_listed'] = bool(listed(ident)); save()
        for n, thing in enumerate(new + old[:2]): acts.run(f"strike-{n}-{thing['name']}", lambda thing=thing: strike(thing))
        time.sleep(3)
        report['hits_in_minecraft_log'] = lines('BotwCraft: hit '); report['mirrored'] = lines('mirrored')[-12:]
        report['actor_hit_events'] = control('actor_hit'); report['listed_at_end'] = [brief(x) for x in listed()]
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1')
        report['disturbed'] = disturbed(); save()


def swing_sensor(out, keeper):
    """Report 57, trials 1 and 2: the sword on Link's back put into a tree and a pot with its attack sensor live (swing.h).

    A tree is asked for three units ahead of Link along z and a pot two units to its side. Five
    seconds with nothing done. Then the sword's sensor is made live (taken anew every 700 ms) while
    the bridge binds the sword to a place one unit up the tree's trunk for six seconds, and after
    that to the pot's place for four. Recorded: each thing's life twice a second, every weapon whose
    update came by and what was written to it, where the sword was, and every attack the game
    worked out for any actor (who was hit, the amounts, the attack's record, whom it names).
    Route-149 (sensor live, sword left on the back, a tree 1.1 units from it): nothing hit.
    """
    import os_input as i, walking_play as play, rig, combined
    acts = Actions(out, keeper); report = {}; census = LAB / 'actors.jsonl'
    def save(): live.save(out / 'swing.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        try: return json.loads(lines[-1]) if lines else None
        except ValueError: return json.loads(lines[-2]) if len(lines) > 1 else None
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def link(): r = combined.latest(); return [float(r['body_' + a]) for a in 'xyz']
    def carried():
        """Link's weapons (actors of their own) within two units of him: name, id, place."""
        (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
        rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
        return [{'name': q['name'], 'id': q['id'], 'at': q['at'], 'distance': q['distance']} for q in (json.loads(rows[-1])['procs'] if rows else []) if 'at' in q and q['name'].startswith('Weapon')]
    def things(): return [a for a in (last() or {}).get('actors', []) if a['kind'] == 4]
    def watch(label, seconds, during=None):
        rows = []
        def action():
            start = time.monotonic()
            if during: during()
            while time.monotonic() - start < seconds:
                rows.append({'t': round(time.monotonic() - start, 1), 'things': [{'id': a['id'], 'name': a['name'], 'life': round(a['life'] * a['most_life'], 2), 'at': a['at']} for a in things()]}); time.sleep(.5)
        acts.run(label, action); report[label] = rows; save()
    try:
        (LAB / 'actors.txt').write_text('1 500'); (LAB / 'swing.txt').write_text('trace 1\n')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        a = link(); report['start'] = a; report['weapons_before'] = [w for w in carried() if w['distance'] < 2]
        tree = [a[0], a[1], a[2] + 3.0]; pot = [a[0] - 2.0, a[1] + .2, a[2] + 3.0]; report['asked_for'] = {'tree': tree, 'pot': pot}
        (LAB / 'spawn.txt').write_text('Obj_TreeConiferous_A_02 ' + ' '.join(f'{v:.4f}' for v in tree) + '\nPot ' + ' '.join(f'{v:.4f}' for v in pot) + '\n'); time.sleep(6.0)
        report['answers'] = control('actor_spawn'); report['listed'] = [{k: x[k] for k in ('id', 'name', 'at', 'distance', 'most_life', 'life')} for x in things()]; save()
        watch('nothing-done-5s', 5)
        def at_the_tree():
            (LAB / 'swing.txt').write_text(f'put {tree[0]:.4f} {tree[1] + 1.0:.4f} {tree[2]:.4f} 6000\narm 6000 700 1 2\n')
        watch('sword-in-the-tree-6s', 7, at_the_tree)
        report['weapons_while_put'] = None
        now_pot = next((x['at'] for x in things() if x['name'] == 'Pot'), pot)   # it may have rolled
        def at_the_pot():
            (LAB / 'swing.txt').write_text(f'put {now_pot[0]:.4f} {now_pot[1] + .3:.4f} {now_pot[2]:.4f} 4000\narm 4000 700 1 2\n'); time.sleep(1.5)
            report['weapons_while_put'] = [w for w in carried() if w['name'].startswith(('Weapon_Sword', 'Weapon_Lsword'))]
        watch('sword-in-the-pot-4s', 6, at_the_pot)
        time.sleep(1.0); report['weapons_after'] = [w for w in carried() if w['distance'] < 2]
    finally:
        (LAB / 'swing.txt').write_text('trace 0\n'); (LAB / 'actors.txt').write_text('1 5000 1')
        report['weapon_sensor'] = control('weapon_sensor'); report['attacks'] = control('native_attack_any'); report['disturbed'] = disturbed(); save()


def swing_axe(out, keeper):
    """Report 57, trial 3: a woodcutter's axe asked for in Link's hands, swung by the bridge at a tree (spawn.h, swing.h).

    A woodcutter's axe is asked for two units from Link and then handed to him by the function
    every actor takes a weapon with (`hold`, spawn.h; route-151: asked for from the game's manager
    of the player's equipment, the axe is created and nobody takes it). A tree is asked for three
    units ahead. Then four swings, 1.5 s apart: for half a second the axe is
    bound to a place one unit up the trunk with its sensor live, and for the rest it is where the
    game binds it. The game's data gives the axe a power of 3 and seven times that on a tree, whose
    life is 40: 40, 19, felled. Recorded: what Link carries before and after the request, the
    tree's life and what is listed after each swing, the weapons met, and every attack the game
    worked out.
    """
    import os_input as i, walking_play as play, rig, combined
    acts = Actions(out, keeper); report = {}; census = LAB / 'actors.jsonl'
    def save(): live.save(out / 'axe.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        try: return json.loads(lines[-1]) if lines else None
        except ValueError: return json.loads(lines[-2]) if len(lines) > 1 else None
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def link(): r = combined.latest(); return [float(r['body_' + a]) for a in 'xyz']
    def carried():
        (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
        rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
        return [{'name': q['name'], 'id': q['id'], 'at': q['at'], 'distance': q['distance']} for q in (json.loads(rows[-1])['procs'] if rows else []) if 'at' in q and q['name'].startswith('Weapon') and q['distance'] < 4]
    def things(): return [{'id': a['id'], 'name': a['name'], 'life': round(a['life'] * a['most_life'], 2), 'at': a['at']} for a in (last() or {}).get('actors', []) if a['kind'] == 4]
    try:
        (LAB / 'actors.txt').write_text('1 500'); (LAB / 'swing.txt').write_text('trace 1\n')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        a = link(); report['start'] = a; report['carried_before'] = carried()
        (LAB / 'spawn.txt').write_text(f'Weapon_Lsword_032 {a[0] + 2.0:.4f} {a[1] + .5:.4f} {a[2]:.4f}\n'); time.sleep(4.0)
        report['near_after_the_axe_was_asked_for'] = carried()
        slot = int((LAB / 'hold-slot.txt').read_text().strip()) if (LAB / 'hold-slot.txt').exists() else 0; report['slot'] = slot   # which of the player's six weapon slots (0 his sword's, 2 his bow's)
        (LAB / 'spawn.txt').write_text(f'hold Weapon_Lsword_032 {slot}\n'); time.sleep(4.0)
        report['carried_after_the_request'] = carried(); report['answers'] = control('actor_spawn'); save()
        tree = [a[0], a[1], a[2] + 3.0]; report['tree_asked_at'] = tree
        (LAB / 'spawn.txt').write_text('Obj_TreeConiferous_A_02 ' + ' '.join(f'{v:.4f}' for v in tree) + '\n'); time.sleep(6.0)
        report['listed'] = things(); report['swings'] = []; save()
        def swings():
            for n in range(4):
                (LAB / 'swing.txt').write_text(f'put {tree[0]:.4f} {tree[1] + 1.0:.4f} {tree[2]:.4f} 500\narm 500 0 1 2\n'); time.sleep(1.5)
                report['swings'].append({'swing': n + 1, 'things': things(), 'attacks_so_far': len(control('native_attack_any'))}); save()
        acts.run('four-swings-at-the-tree', swings)
        time.sleep(3.0); report['listed_at_end'] = things(); report['carried_at_end'] = carried()
    finally:
        (LAB / 'swing.txt').write_text('trace 0\n'); (LAB / 'actors.txt').write_text('1 5000 1')
        report['weapon_sensor'] = [e for e in control('weapon_sensor') if e.get('state') != 'armed' or e.get('take', 0) < 8]; report['attacks'] = control('native_attack_any'); report['disturbed'] = disturbed(); save()


def steve_swing(out, keeper):
    """Report 58: Steve's own clicks swung as a BOTW weapon (the mod's kEvSwing, swing.h).

    lab/steve-swing.txt chooses the part (default `things`).
    `things`: a tree is asked for 3.5 units ahead of the start and a pot two units to its side.
    Steve takes a wooden axe and clicks at the tree three times, takes nothing in his hand and
    clicks at the pot twice, takes an iron sword and clicks at the ground ahead twice; the weapon
    is then held at the end of its swing for three seconds, for the picture.
    `enemy`: a Bokoblin is asked for four units ahead; Steve, in survival, takes an iron sword
    and clicks at it up to six times.
    `wear`: a tree 3.5 units ahead, struck fourteen times with the bare hand (the torch, whose
    life is 8: route-160).
    Recorded: the bridge's lines for each swing (the weapon asked for and ready, begun, struck),
    every attack the game worked out, the listed things' or the enemy's life after each click,
    the blows of Minecraft's (queued, given, held for the swing), and what Link carries at the
    start and the end.
    """
    import os_input as i, walking_play as play, rig, combined
    acts = Actions(out, keeper); report = {}; census = LAB / 'actors.jsonl'
    part = (LAB / 'steve-swing.txt').read_text().strip() if (LAB / 'steve-swing.txt').exists() else 'things'; report['part'] = part
    def save(): live.save(out / 'steve.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        try: return json.loads(lines[-1]) if lines else None
        except ValueError: return json.loads(lines[-2]) if len(lines) > 1 else None
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def link(): r = combined.latest(); return [float(r['body_' + a]) for a in 'xyz']
    def carried():
        (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
        rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
        return [{'name': q['name'], 'id': q['id'], 'at': q['at'], 'distance': q['distance']} for q in (json.loads(rows[-1])['procs'] if rows else []) if 'at' in q and q['name'].startswith('Weapon') and q['distance'] < 4]
    def listed(kinds=(4,)): return [{'id': a['id'], 'name': a['name'], 'kind': a['kind'], 'life': round(a['life'] * a['most_life'], 2), 'at': a['at'], 'distance': a['distance']} for a in (last() or {}).get('actors', []) if a['kind'] in kinds]
    def take(item, seconds=8.0):
        """Steve's main hand, and the wait for the bridge's weapon for it."""
        before = len([e for e in control('swing') if e.get('state') == 'weapon-ready'])
        i.command(f'/item replace entity @s weapon.mainhand with {item}'); end = time.monotonic() + seconds
        while time.monotonic() < end and len([e for e in control('swing') if e.get('state') == 'weapon-ready']) == before: time.sleep(.25)
        time.sleep(.5)
    def aim_at(at, up=0.0):
        s = play.state(); x, y, z = mc(at); dx, dy, dz = x - s['pos'][0], y + up - (s['pos'][1] + 1.62), z - s['pos'][2]; flat = math.hypot(dx, dz)
        play.aim(math.degrees(math.atan2(-dx, dz)), -math.degrees(math.atan2(dy, max(flat, .01)))); return round(flat, 2)
    def clicks(label, count, where, kinds=(4,), pause=1.2):
        rows = []
        def action():
            for n in range(count):
                target = where()
                if target is None: rows.append({'click': n + 1, 'gone': True}); break
                flat = aim_at(*target); walked = 0
                while flat > 2.2 and walked < 12: play.hold([W], min(.5, flat / 6)); walked += 1; target = where() or target; flat = aim_at(*target)   # within Minecraft's reach
                i.click(1); time.sleep(pause)
                rows.append({'click': n + 1, 'flat_blocks': flat, 'listed': listed(kinds), 'swings_begun': len([e for e in control('swing') if e.get('state') == 'begun']), 'struck': len([e for e in control('swing') if e.get('state') == 'struck'])})
        acts.run(label, action); report[label] = rows; save()
    try:
        (LAB / 'actors.txt').write_text('1 500'); (LAB / 'swing.txt').write_text('trace 1\n')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        a = link(); report['start'] = a; report['carried_before'] = carried()
        if part == 'enemy':
            foe = [a[0], a[1] + .3, a[2] + 4.0]; report['asked_for'] = {'Enemy_Bokoblin_Junior': foe}
            (LAB / 'spawn.txt').write_text('Enemy_Bokoblin_Junior ' + ' '.join(f'{v:.4f}' for v in foe) + '\n'); time.sleep(6.0)
            report['listed'] = listed((1,)); take('iron_sword'); i.command('/gamemode survival'); time.sleep(1.0); save()
            def nearest():
                foes = sorted([x for x in listed((1,)) if x['life'] > 0], key=lambda x: x['distance'])
                return (foes[0]['at'], 1.0) if foes and foes[0]['distance'] < 5.5 else None
            clicks('six-clicks-at-the-enemy', 6, nearest, kinds=(1,), pause=1.0)
            i.command('/gamemode creative'); time.sleep(.8)
        elif part == 'wear':
            tree = [a[0], a[1], a[2] + 3.5]; report['asked_for'] = {'tree': tree}
            (LAB / 'spawn.txt').write_text('Obj_TreeConiferous_A_02 ' + ' '.join(f'{v:.4f}' for v in tree) + '\n'); time.sleep(6.0)
            report['listed'] = listed(); take('air'); save()
            clicks('fourteen-clicks-at-the-tree-bare-hand', 14, lambda: (tree, 1.2), pause=.8)
        else:
            tree = [a[0], a[1], a[2] + 3.5]; pot = [a[0] - 2.0, a[1] + .2, a[2] + 3.0]; report['asked_for'] = {'tree': tree, 'pot': pot}
            (LAB / 'spawn.txt').write_text('Obj_TreeConiferous_A_02 ' + ' '.join(f'{v:.4f}' for v in tree) + '\nPot ' + ' '.join(f'{v:.4f}' for v in pot) + '\n'); time.sleep(6.0)
            report['listed'] = listed(); save()
            take('wooden_axe'); clicks('three-clicks-at-the-tree-wooden-axe', 3, lambda: (tree, 1.2))
            take('air')
            def the_pot():
                pots = [x for x in listed() if x['name'] == 'Pot']
                return (pots[0]['at'], .3) if pots else None
            clicks('two-clicks-at-the-pot-bare-hand', 2, the_pot)
            take('iron_sword'); ground = [a[0] + 1.5, a[1], a[2] + 1.5]
            clicks('two-clicks-at-the-ground-iron-sword', 2, lambda: (ground, 0.0))
            def held():
                (LAB / 'swing.txt').write_text('stay 3000\n'); time.sleep(.3); aim_at(ground, 0.0); i.click(1); time.sleep(1.2)
            acts.run('the-weapon-held-at-the-end-of-its-swing', held)
            def in_the_air():
                # the sword swung by the lab's line to a place two units ahead of the eye, and held there: is it drawn?
                s = play.state(); yaw, pitch = math.radians(s['yaw']), math.radians(s['pitch']); here = link()
                d = [-math.sin(yaw) * math.cos(pitch), -math.sin(pitch), math.cos(yaw) * math.cos(pitch)]
                at = [here[0] + d[0] * 2.0, here[1] + 1.62 * 1.3 + d[1] * 2.0, here[2] + d[2] * 2.0]; report['held_in_the_air_at'] = at
                (LAB / 'swing.txt').write_text('stay 4000\nsteve 1 3 ' + ' '.join(f'{v:.4f}' for v in at) + '\n'); time.sleep(1.5)
            play.aim(play.state()['yaw'], 0); acts.run('the-weapon-held-in-the-air', in_the_air)
        time.sleep(2.0); report['listed_at_end'] = listed((1, 4)); report['carried_at_end'] = carried()
    finally:
        (LAB / 'swing.txt').write_text('trace 0\n'); (LAB / 'actors.txt').write_text('1 5000 1')
        report['swing'] = control('swing'); report['attacks'] = control('native_attack_any'); report['actor_hit'] = control('actor_hit')
        report['spawn'] = [e for e in control('actor_spawn') if e.get('state') != 'slots']; report['disturbed'] = disturbed(); save()


def pad_keys(out, keeper):
    """The game's own buttons from the keyboard (physical.h padButton, report 61): G is the Wii U's A, Backspace its B.

    An apple (Item_Fruit_A) is asked for one unit ahead of Link, a little over the ground. It rolls
    (route-173: twelve units down the slope before the key), so Steve waits until it lies still, walks
    to within reach of it and looks at it; G is tapped: the game's own "Take". G twice more (a notice
    of the game's wants A to go on, if one comes), then Backspace once and G a last time.
    Recorded: the game's things named Item* within three units before and after each key (lab/procs),
    the bridge's `pad` lines (the bits it wrote into the pad), the releases of control, and a picture
    before the first key and after each.
    """
    import os_input as i, walking_play as play, rig, combined
    acts = Actions(out, keeper); report = {}
    def save(): live.save(out / 'pad.json', report)
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def link(): r = combined.latest(); return [float(r['body_' + a]) for a in 'xyz']
    def items(within=3.0):
        (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
        rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
        return [{'name': q['name'], 'id': q['id'], 'at': q['at'], 'distance': q['distance']} for q in (json.loads(rows[-1])['procs'] if rows else []) if 'at' in q and q['name'].startswith('Item') and q['distance'] < within]
    def apples(): return sorted([q for q in items(40.0) if q['name'] == 'Item_Fruit_A'], key=lambda q: q['distance'])
    def aim_at(at, up=0.0):
        s = play.state(); x, y, z = mc(at); dx, dy, dz = x - s['pos'][0], y + up - (s['pos'][1] + 1.62), z - s['pos'][2]; flat = math.hypot(dx, dz)
        play.aim(math.degrees(math.atan2(-dx, dz)), -math.degrees(math.atan2(dy, max(flat, .01)))); return round(flat, 2)
    try:
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        a = link(); report['start'] = a; report['items_at_the_start'] = items()
        apple = [a[0], a[1] + .4, a[2] + 1.0]; report['asked_for'] = {'Item_Fruit_A': apple}
        (LAB / 'spawn.txt').write_text('Item_Fruit_A ' + ' '.join(f'{v:.4f}' for v in apple) + '\n'); time.sleep(5.0)
        seen = apples(); report['apple_seen'] = [seen[:1]]
        for _ in range(8):   # until it lies still
            again = apples(); report['apple_seen'].append(again[:1])
            if seen and again and max(abs(u - v) for u, v in zip(seen[0]['at'], again[0]['at'])) < .05: break
            seen = again
        def to_the_apple():
            for _ in range(40):
                flat = aim_at(seen[0]['at'])
                if flat <= .75: break
                play.hold([W], min(.35, flat / 6))
            time.sleep(1.0)
        if seen: acts.run('steve-walks-to-the-apple', to_the_apple, shot_after=False)
        there = apples(); report['apple_from_link_after_the_walk'] = there[:1]
        if there: report['flat_blocks_to_the_apple'] = aim_at(there[0]['at'])
        report['items_before_the_key'] = items(); save()
        acts.run('before-the-key', lambda: time.sleep(.5))
        def g(): i.tap(0x47, .15); time.sleep(2.5)
        def backspace(): i.tap(0x08, .15); time.sleep(1.5)
        for label, key in (('g-is-a', g), ('g-again', g), ('backspace-is-b', backspace), ('g-a-last-time', g)):
            acts.run(label, key); report['items_after_' + label] = items(); save()
        report['pad'] = control('pad'); report['spawn'] = control('actor_spawn')
        report['releases'] = [e for e in control('authority_release') + control('physical_release')]
    finally:
        save()


def keys_plan(out, keeper):
    """Keys from the environment, one after the other: for looking at a screen of the game's with the keyboard.

    BRIDGE_KEYS holds words: F7 (the game's menu), G (A), BACK (B), UP DOWN LEFT RIGHT, the menu keys Q E Z C X Y
    TAB M, ESC, any of them
    with *n for n taps; `shot` (a picture), `waitN` (N seconds), `health` (Steve's, from Minecraft's /data, and
    the health writes of the game's since the last one), `survival`, `creative`, `hurtN` (Minecraft's /damage),
    `effect:ID_AMOUNT_SECONDS` (lab/effect.txt: an effect of the game's sent to Minecraft without the food);
    `set:NAME=VALUE` (lab/NAME.txt, emptied at the end), `cmd:TEXT` (a command of Minecraft's, _ for a space),
    `hold:KEY:SECONDS` (W A S D SPACE SHIFT CTRL too), `down:KEY` and `up:KEY` (held between them), `look:DX:DY` (the mouse), `aim:YAW:PITCH`, `where` (Steve's place),
    `seek:NAME:UP` (the nearest four of the game's objects with NAME in their name, in Minecraft's numbers; Steve
    looks at the nearest, UP blocks over its place; `find:NAME` is the same without the look), `spawn:NAME:AHEAD:UP` (lab/spawn.txt: an object of the
    game's asked for AHEAD blocks before Steve and UP blocks over his feet), `mouse:right:SECONDS`, `mouse:left:SECONDS`,
    `wheel:NOTCHES`, the hotbar's 1 to 9, `frames`
    (how many frames the lit path drew, how many of them it could not match to their frame, how many the marker
    path drew again, since the last `frames`).
    Recorded in keys.json with the bridge's pad, event and health lines.
    """
    import os, re, os_input as i
    acts = Actions(out, keeper); report = {'words': os.environ.get('BRIDGE_KEYS', '').split(), 'steps': []}; log = LAB / 'minecraft/logs/latest.log'
    codes = {'F7': 0x76, 'G': 0x47, 'BACK': 0x08, 'UP': 0x26, 'DOWN': 0x28, 'LEFT': 0x25, 'RIGHT': 0x27, 'E': 0x45, 'ESC': 0x1B,
             'Q': 0x51, 'Z': 0x5A, 'C': 0x43, 'X': 0x58, 'Y': 0x59, 'TAB': 0x09, 'M': 0x4D, 'H': 0x48,
             'W': 0x57, 'A': 0x41, 'S': 0x53, 'D': 0x44, 'SPACE': 0x20, 'SHIFT': 0xA0, 'CTRL': 0xA2, **{str(n): 0x30 + n for n in range(1, 10)}}
    switches = set(); marks = {'frames': None}
    def frames():
        # What the lit path and the marker path did with each frame since the last call (frames.jsonl turns over
        # within a minute: ask at least that often).
        rows = events_since(marks['frames'], ['integrate_frame', 'live_draw', 'frame_start']) if marks['frames'] is not None else []
        marks['frames'] = offset()
        lit = [e for e in rows if e['event'] == 'integrate_frame' and e['vertices'][0]]; again = [e for e in rows if e['event'] == 'live_draw' and e['opaque']]
        unlit = [e['frame'] for e in lit if not e['opaque']]
        return {'frames_with_blocks': len(lit), 'first': lit[0]['frame'] if lit else None, 'last': lit[-1]['frame'] if lit else None,
                'not_matched_to_their_frame': len(unlit), 'their_numbers_mod_64': sorted({n % 64 for n in unlit})[:16],
                'drawn_again_by_the_marker_path': len(again), 'frame_start': [e for e in rows if e['event'] == 'frame_start']}
    def save(): live.save(out / 'keys.json', report)
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def data(name):
        said = lambda: [l for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'has the following entity data: ' in l] if log.exists() else []
        before = len(said()); i.command(f'/data get entity @s {name}'); time.sleep(.7); now = said()
        found = re.search(r'entity data: (-?[0-9.]+)', now[-1]) if len(now) > before else None
        return float(found.group(1)) if found else None
    recording = []
    since = offset(); (LAB / 'effect.txt').write_text('')
    try:
        for n, word in enumerate(report['words']):
            step = {'word': word}; report['steps'].append(step)
            if word == 'shot': acts.run(f'{n:02d}-shot', lambda: None)
            elif word.startswith('wait'): time.sleep(float(word[4:]))
            elif word == 'health':
                step['steve'] = data('Health'); step['game_health_writes'] = [{k: e.get(k) for k in ('old', 'new', 'blocked', 'lr', 'stack')} for e in events_since(since, ['native_health'])]; since = offset()
            elif word in ('survival', 'creative'): i.command('/gamemode ' + word); time.sleep(1.0)
            elif word.startswith('hurt'): i.command('/damage @s ' + word[4:]); time.sleep(1.0)
            elif word.startswith('effect:'): (LAB / 'effect.txt').write_text(word[7:].replace('_', ' ')); time.sleep(1.5)
            elif word.startswith('set:'):
                name, _, value = word[4:].partition('='); switches.add(name); (LAB / f'{name}.txt').write_text(value.replace('__', chr(1)).replace('_', ' ').replace(chr(1), '_')); time.sleep(1.5)   # _ is a space, __ is _
            elif word.startswith('cmd:'): i.command(word[4:].replace('__', chr(1)).replace('_', ' ').replace(chr(1), '_')); time.sleep(1.0)   # _ is a space, __ is _
            elif word in ('rec', 'recstop'):   # rec: a video of the game's window from here (ffmpeg, the screen's own pixels) until recstop or the plan's end
                if recording: recording[0].communicate(b'q', timeout=20); step['stopped'] = recording[1]; recording.clear()
                if word == 'rec':
                    import ctypes, subprocess, rig; hwnd, _ = rig.window(LAB); r = (ctypes.c_long * 4)(); ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(r))
                    x0, y0, w, h = [ctypes.windll.user32.GetSystemMetrics(k) for k in (76, 77, 78, 79)]; box = [max(r[0], x0), max(r[1], y0), min(r[2], x0 + w), min(r[3], y0 + h)]
                    size = [(box[2] - box[0]) // 2 * 2, (box[3] - box[1]) // 2 * 2]; name = f'{n:02d}-rec.mp4'
                    recording[:] = [subprocess.Popen([local.needed('ffmpeg', 'ffmpeg'), '-hide_banner', '-loglevel', 'error', '-y', '-f', 'gdigrab', '-framerate', '15', '-draw_mouse', '0', '-offset_x', str(box[0]), '-offset_y', str(box[1]), '-video_size', f'{size[0]}x{size[1]}', '-i', 'desktop',
                                                      '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '30', '-pix_fmt', 'yuv420p', str(out / name)], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL), name]
                    step['video'] = name; step['unix'] = time.time(); step['rect'] = box; time.sleep(.5)
            elif word.startswith('freeze:'):   # freeze:MS: Minecraft's process stands still for that long (a hitch of its client)
                import ctypes; ctypes.windll.kernel32.OpenProcess.restype = ctypes.c_void_p; pids = [int(json.loads((LAB / 'client-process.json').read_text())['pid'])]   # this run's own client only
                step['pids'] = pids; handles = [ctypes.windll.kernel32.OpenProcess(0x0800, False, pid) for pid in pids]   # PROCESS_SUSPEND_RESUME
                try:
                    for h in handles: ctypes.windll.ntdll.NtSuspendProcess(ctypes.c_void_p(h))
                    time.sleep(float(word.split(':')[1]) / 1000)
                finally:
                    for h in handles: ctypes.windll.ntdll.NtResumeProcess(ctypes.c_void_p(h)); ctypes.windll.kernel32.CloseHandle(ctypes.c_void_p(h))
            elif word.startswith('hold:'):
                _, name, seconds = word.split(':'); i.key(codes[name], True); time.sleep(float(seconds)); i.key(codes[name], False); time.sleep(.3)
            elif word.startswith('down:') or word.startswith('up:'):   # a key held until its up: (W held while SPACE jumps)
                which, name = word.split(':'); i.key(codes[name], which == 'down'); time.sleep(.05)
            elif word.startswith('look:'): _, dx, dy = word.split(':'); i.move_exact(int(dx), int(dy)); time.sleep(.4)
            elif word == 'frames': step['frames'] = frames()
            elif word == 'where': import walking_play as play; step['steve'] = play.state()['pos']
            elif word.startswith('aim:'): import walking_play as play; _, yaw, pitch = word.split(':'); play.aim(float(yaw), float(pitch)); time.sleep(.3)
            elif word.startswith('mouse:'):   # mouse:right:SECONDS or mouse:left:SECONDS, held so long
                _, which, seconds = (word + ':0.15').split(':')[:3]; code = 3 if which == 'right' else 1; i.button(code, True); time.sleep(float(seconds)); i.button(code, False); time.sleep(.4)
            elif word.startswith('wheel:'):   # wheel:NOTCHES (up is more), one notch at a time
                notches = int(word[6:])
                for _ in range(abs(notches)): i.wheel(120 if notches > 0 else -120); time.sleep(.08)
                time.sleep(.3)
            elif word.startswith('spawn:'):
                import walking_play as play; _, name, ahead, up, wait = (word.split(':') + ['5'])[:5]; s = play.state(); yaw = math.radians(s['yaw'])   # spawn:NAME:AHEAD:UP[:WAIT]: 5 s unless given (a ball rolls away in that time)
                at = [s['pos'][0] - math.sin(yaw) * float(ahead), s['pos'][1] + float(up), s['pos'][2] + math.cos(yaw) * float(ahead)]
                name = name.replace('+', ' '); size = ' 1 1 1' if name.startswith('map ') else ''   # spawn:map+AirWall:AHEAD:UP: as the map asks, a box of half a size of 1
                step['minecraft'] = at; (LAB / 'spawn.txt').write_text(f'{name} {at[0] * 1.3 - 1027.16809:.4f} {(at[1] - 100) * 1.3 + 251.188492:.4f} {at[2] * 1.3 + 1812.87195:.4f}{size}\n'); time.sleep(float(wait))
            elif word.startswith('word:'):   # word:HEX: every place in the game's memory that holds the word, with the words about it
                token = int(time.time() * 10) % 1000000000; result = LAB / f'scan-{token}.json'; (LAB / 'scan.txt').write_text(f'{token} {word[5:]}'); switches.add('scan')
                for _ in range(60):
                    time.sleep(.25)
                    if result.exists(): time.sleep(.3); step['found'] = json.loads(result.read_text())['found']; shutil.copy2(result, out / result.name); break
            elif word.startswith('scan:'):
                _, name = word.split(':'); (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
                rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
                every = json.loads(rows[-1])['procs'] if rows else []; near = sorted([q for q in every if 'at' in q and name in q['name']], key=lambda q: q['distance'])[:1]
                step['others'] = [{k: q.get(k) for k in ('name', 'id', 'proc')} for q in every if 'at' in q and ('Magnet' in q['name'] or q['name'] == 'GameROMPlayer')]
                if near:
                    step['proc'] = near[0]['proc']; step['id'] = near[0]['id']; token = int(time.time() * 10) % 1000000000; result = LAB / f'scan-{token}.json'
                    (LAB / 'scan.txt').write_text(f"{token} {near[0]['proc']:x}"); switches.add('scan')
                    for _ in range(40):
                        time.sleep(.25)
                        if result.exists(): time.sleep(.3); step['found'] = json.loads(result.read_text())['found']; shutil.copy2(result, out / result.name); break
                    # The object's link data (BaseProcLinkData: the id one word before the proc), and every link that points at it.
                    data = [r[0] - 0x40 for r in step.get('found', []) if r[2] == near[0]['id']]
                    if data:
                        token += 1; result = LAB / f'scan-{token}.json'; (LAB / 'scan.txt').write_text(f'{token} {data[0]:x}'); step['link_data'] = data[0]
                        for _ in range(40):
                            time.sleep(.25)
                            if result.exists(): time.sleep(.3); step['links'] = json.loads(result.read_text())['found']; break
                        # What in the program's own data points to just before each link that holds the object now.
                        step['statics'] = {}
                        for link in [r[0] for r in step.get('links', []) if r[3] == near[0]['id'] and not 0x45000000 <= r[0] < 0x48000000][:6]:
                            token += 1; result = LAB / f'scan-{token}.json'; (LAB / 'scan.txt').write_text(f'{token} {link - 0x1000:x} {link:x}')
                            for _ in range(40):
                                time.sleep(.25)
                                if result.exists(): time.sleep(.3); step['statics'][f'{link:x}'] = json.loads(result.read_text())['found']; break
            elif word.startswith('vec:'):   # vec:x:y:z:DEGREES: where three floats point that way (lab/vector-scan.txt); the answer comes 10 s later and is fetched at the end
                _, x, y, z, degrees = word.split(':'); token = int(time.time() * 10) % 1000000000; (LAB / 'vector-scan.txt').write_text(f'{token} {x} {y} {z} {degrees}'); switches.add('vector-scan'); step['token'] = token; marks.setdefault('vectors', []).append(token)
            elif word.startswith('poke:'):   # poke:ADDRESS(hex):x:y:z:MS: three floats written there for that long (lab/vector-poke.txt)
                _, at, x, y, z, ms = word.split(':'); (LAB / 'vector-poke.txt').write_text(f'{at} {x} {y} {z} {ms}')
            elif word.startswith('dump:'):   # dump:LABEL[:HEX...]: the tracked object's memory and what it points to (lab/dump.txt), with other roots; the file goes to the evidence as dump-LABEL.bin
                parts = word.split(':'); t = marks.get('tracked'); roots = ([f"{t['proc']:x}"] if t else []) + parts[2:]; token = int(time.time() * 10) % 1000000000; result = LAB / f'dump-{token}.bin'
                (LAB / 'dump.txt').write_text(f"{token} {' '.join(roots)}"); step['roots'] = roots
                for _ in range(40):
                    time.sleep(.1)
                    if (LAB / 'dump.done.txt').exists() and result.exists(): break
                time.sleep(.4)
                if result.exists(): shutil.copy2(result, out / f'dump-{parts[1]}.bin'); step['bytes'] = result.stat().st_size
            elif word.startswith('ladder:'):   # ladder:UP: Steve is put UP blocks over the middle of the last ladder the bridge has read (its native_ladders line)
                rows = [l for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"native_ladders"' in l]
                if rows:
                    r = json.loads(rows[-1]); x, y, z = (r['lo'][0] + r['hi'][0]) / 2, r['hi'][1] + float(word[7:]), (r['lo'][2] + r['hi'][2]) / 2
                    step['ladder'] = {'lo': r['lo'], 'hi': r['hi'], 'triangles': r['triangles'], 'seen': len(rows)}; i.command(f'/tp @s {x:.3f} {y:.3f} {z:.3f}'); time.sleep(1.0)
            elif word.startswith('track:'):   # track:NAME:SECONDS: the nearest object of that name, its place logged for that long (lab/track.txt)
                _, name, seconds = word.split(':'); (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
                rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
                near = sorted([q for q in (json.loads(rows[-1])['procs'] if rows else []) if 'at' in q and name in q['name']], key=lambda q: q['distance'])[:1]
                if near: (LAB / 'track.txt').write_text(f"{near[0]['proc']:x} {near[0]['id']} {int(float(seconds) * 1000)}"); step['tracked'] = marks['tracked'] = near[0]
            elif word.startswith('hit:'):   # hit:KIND[:COUNT[:HALF[:CLASS]]]: lab swings at the tracked object (its place is its bottom middle, HALF its half size; CLASS -1: the weapon kept now)
                import walking_play as play; parts = word.split(':'); _, kind, count, half, cls, turn, rise = (parts + ['1', '1.9', '-1', '0', '0'][len(parts) - 2:])[:7]; half = float(half); t = marks.get('tracked')
                if t:
                    s = play.state(); feet = [s['pos'][0] * 1.3 - 1027.16809, (s['pos'][1] - 100) * 1.3 + 251.188492, s['pos'][2] * 1.3 + 1812.87195]
                    c = [t['at'][0], t['at'][1] + half, t['at'][2]]; ux, uz = c[0] - feet[0], c[2] - feet[2]; flat = math.hypot(ux, uz) or 1; ux /= flat; uz /= flat; rx, rz = -uz, ux
                    if kind == 'side': a = [c[0] - ux * (half - .3) - rx * (half + 1.2), c[1], c[2] - uz * (half - .3) - rz * (half + 1.2)]; b = [c[0] - ux * (half - .3) + rx * (half + 1.2), c[1], c[2] - uz * (half - .3) + rz * (half + 1.2)]
                    elif kind == 'up': a = [c[0] - ux * (half + .9), c[1] - half + .4, c[2] - uz * (half + .9)]; b = [c[0] - ux * (half - .3), c[1] + half + 1, c[2] - uz * (half - .3)]
                    elif kind == 'down': a = [c[0] - ux * (half + .9), c[1] + half + 1, c[2] - uz * (half + .9)]; b = [c[0] - ux * (half - .3), c[1] - half + .4, c[2] - uz * (half - .3)]
                    elif kind == 'line':   # hit:line:COUNT:HALF:CLASS:TURN:RISE: through the object's middle (the last track line), TURN degrees to the right of the way from Steve, RISE degrees up; from 1.2 short of HALF to the middle
                        rows = [l for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines()[-400:] if '"track"' in l and '"middle"' in l]
                        if rows: c = json.loads(rows[-1])['middle']
                        ux, uz = c[0] - feet[0], c[2] - feet[2]; flat = math.hypot(ux, uz) or 1; ux /= flat; uz /= flat; rx, rz = -uz, ux
                        yaw, up = math.radians(float(turn)), math.radians(float(rise)); d = [(ux * math.cos(yaw) + rx * math.sin(yaw)) * math.cos(up), math.sin(up), (uz * math.cos(yaw) + rz * math.sin(yaw)) * math.cos(up)]
                        a = [c[k] - d[k] * (half + 1.2) for k in range(3)]; b = c; step['line'] = d; step['middle'] = c
                    else: a = [feet[0] + ux * .9, feet[1] + 1.5, feet[2] + uz * .9]; b = c
                    step['from'] = a; step['to'] = b
                    for _ in range(int(count)):
                        (LAB / 'swing.txt').write_text(f'steve {int(cls)} 5 ' + ' '.join(f'{v:.4f}' for v in b + a) + chr(10)); time.sleep(.8)
            elif word.startswith('onto:@:'):   # onto:@:UP: at once over the tracked object's place (its last track line), without the search of three seconds
                rows = [l for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines()[-400:] if '"track"' in l and '"at"' in l]
                if rows:
                    at = json.loads(rows[-1])['at']; x, y, z = (at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100 + float(word[7:]), (at[2] - 1812.87195) / 1.3
                    step['onto'] = [round(x, 3), round(y, 3), round(z, 3)]; i.command(f'/tp @s {x:.3f} {y:.3f} {z:.3f}'); time.sleep(.3)
            elif word.startswith('seek:') or word.startswith('find:') or word.startswith('onto:'):   # onto:NAME:UP: Steve is put UP blocks over the nearest such object's place (a /tp)
                import walking_play as play; _, name, up = (word + ':0').split(':')[:3]
                (LAB / 'procs.txt').write_text('1'); time.sleep(3.0)
                rows = (LAB / 'procs.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if (LAB / 'procs.jsonl').exists() else []
                near = sorted([q for q in (json.loads(rows[-1])['procs'] if rows else []) if 'at' in q and name in q['name']], key=lambda q: q['distance'])[:4]
                mc = lambda at: [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
                s = play.state(); step['steve'] = s['pos']; step['found'] = [{'name': q['name'], 'id': q['id'], 'minecraft': [round(v, 2) for v in mc(q['at'])], 'distance': q['distance']} for q in near]
                if near and word.startswith('onto:'):
                    x, y, z = mc(near[0]['at']); step['onto'] = [round(x, 3), round(y + float(up), 3), round(z, 3)]; i.command(f'/tp @s {x:.3f} {y + float(up):.3f} {z:.3f}'); time.sleep(1.0)
                if near and word.startswith('seek:'):
                    x, y, z = mc(near[0]['at']); dx, dy, dz = x - s['pos'][0], y + float(up) - (s['pos'][1] + 1.62), z - s['pos'][2]; flat = math.hypot(dx, dz)
                    step['aim'] = [round(math.degrees(math.atan2(-dx, dz)), 1), round(-math.degrees(math.atan2(dy, max(flat, .01))), 1)]; play.aim(*step['aim']); time.sleep(.3)
            else:
                name, _, times = word.partition('*')
                for _ in range(int(times or 1)): i.tap(codes[name], .12); time.sleep(.45)
                time.sleep(.6)
            save()
    finally:
        if recording:   # a video still running at the plan's end (or at a failure) is closed
            try: recording[0].communicate(b'q', timeout=20)
            except Exception: recording[0].kill()
        for token in marks.get('vectors', []):
            result = LAB / f'vector-scan-{token}.json'
            for _ in range(60):
                if result.exists(): break
                time.sleep(.25)
            if result.exists(): time.sleep(.3); shutil.copy2(result, out / result.name)
        for name in switches: (LAB / f'{name}.txt').write_text('')
        report['pad'] = control('pad'); report['game_event'] = control('game_event'); report['life'] = control('native_life'); report['recover'] = control('native_recover'); report['eaten'] = control('native_eaten'); report['heal'] = control('native_heal_to_minecraft'); report['hearts'] = control('native_hearts'); report['most'] = control('native_hearts_to_minecraft'); report['effects'] = control('native_effect'); report['effects_sent'] = control('native_effect_to_minecraft'); (LAB / 'effect.txt').write_text('')
        report['minecraft_said'] = [l for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'BotwCraft: the game' in l] if log.exists() else []
        report['releases'] = control('authority_release') + control('physical_claim'); report['rune'] = control('rune_state'); save()


def block_walls(out, keeper):
    """Step 5: Minecraft's blocks as walls and ground for BOTW (walls.h, report 59).

    Steve, in creative, fills a hollow box of stone, seven blocks square and five tall, whose
    middle is seven blocks ahead of him, its floor's top one block over his feet. The bridge asks
    the game for an AirWall for every merged box of those blocks. A Bokoblin is then asked for in
    the middle of the box, on its floor. Its place is read for 25 seconds: held if it stays within
    the box and on the floor. The box is then filled with air and the Bokoblin read for another 25
    seconds: free to come to Steve, and standing on BOTW's own ground. Before that Steve clicks
    once at the box's near wall (the block must go: his click ends on Minecraft's block), and at
    the end he stands for ten seconds on a platform of blocks three blocks over the ground.
    Recorded: the bridge's lines for the walls (sections, boxes, asked, standing, sent away), the
    game's answers, and the enemy's place twice a second.
    """
    import os_input as i, walking_play as play, rig, combined
    acts = Actions(out, keeper); report = {}; census = LAB / 'actors.jsonl'
    def save(): live.save(out / 'walls.json', report)
    def last():
        lines = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        try: return json.loads(lines[-1]) if lines else None
        except ValueError: return json.loads(lines[-2]) if len(lines) > 1 else None
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def botw(at): return [at[0] * 1.3 - 1027.16809, (at[1] - 100) * 1.3 + 251.188492, at[2] * 1.3 + 1812.87195]
    def foe():
        foes = [a for a in (last() or {}).get('actors', []) if a['kind'] == 1 and not a['dead'] and a['name'].startswith('Enemy_Bokoblin')]
        return min(foes, key=lambda a: math.dist(mc(a['at']), middle)) if foes else None
    def standing(): rows = control('walls'); return rows[-1] if rows else None
    def watch(label, seconds):
        rows = []
        def action():
            start = time.monotonic()
            while time.monotonic() - start < seconds:
                a = foe(); s = play.state()
                if a:
                    m = mc(a['at']); rows.append({'t': round(time.monotonic() - start, 1), 'at_blocks': [round(v, 2) for v in m], 'flat_from_the_middle': round(math.hypot(m[0] - middle[0], m[2] - middle[2]), 2),
                                                 'over_the_floor': round(m[1] - middle[1], 2), 'blocks_to_steve': round(math.hypot(m[0] - s['pos'][0], m[2] - s['pos'][2]), 2), 'life': a['life']})
                time.sleep(.5)
        acts.run(label, action)
        report[label] = {'samples': len(rows), 'farthest_from_the_middle': max((r['flat_from_the_middle'] for r in rows), default=None), 'nearest_to_steve': min((r['blocks_to_steve'] for r in rows), default=None),
                         'over_the_floor_least_most': [min((r['over_the_floor'] for r in rows), default=None), max((r['over_the_floor'] for r in rows), default=None)], 'every_fourth': rows[::4]}; save()
    try:
        (LAB / 'actors.txt').write_text('1 500')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        s = play.state(); bx, by, bz = math.floor(s['pos'][0]), round(s['pos'][1]), math.floor(s['pos'][2])
        lo, hi = [bx - 3, by, bz + 4], [bx + 3, by + 4, bz + 10]; middle = [bx + .5, by + 1.0, bz + 7.5]
        report['steve'] = s['pos']; report['box_of_blocks'] = {'lo': lo, 'hi': hi, 'middle_on_the_floor': middle}; report['walls_before'] = standing()
        i.command(f'/fill {lo[0]} {lo[1]} {lo[2]} {hi[0]} {hi[1]} {hi[2]} stone hollow'); time.sleep(5.0)
        report['walls_after_the_fill'] = standing(); save()
        place = botw([middle[0], middle[1] + .3, middle[2]]); report['enemy_asked_at'] = place
        (LAB / 'spawn.txt').write_text('Enemy_Bokoblin_Junior ' + ' '.join(f'{v:.4f}' for v in place) + '\n'); time.sleep(6.0)
        a = foe(); report['enemy'] = a and {k: a[k] for k in ('name', 'id', 'at', 'life')}; save()
        play.aim(0, 0); watch('in-the-box-of-blocks', 25)
        # Steve's own click still ends on the block and not on BOTW's copy of it: in creative one click takes the
        # block ahead of his eye out of the box's near wall, and the bridge's boxes change with it.
        def a_click():
            play.aim(0, 0); time.sleep(.3); report['walls_before_the_click'] = standing(); i.click(1); time.sleep(3.0); report['walls_after_the_click'] = standing()
        acts.run('steve-takes-a-block-out', a_click); save()
        i.command(f'/fill {lo[0]} {lo[1]} {lo[2]} {hi[0]} {hi[1]} {hi[2]} air'); time.sleep(4.0)
        report['walls_after_the_blocks_went'] = standing(); save()
        watch('the-blocks-gone', 25)
        # Steve on a platform of blocks three blocks over BOTW's ground: what the game says of Link's footing there
        # (read after the run from the frames of this action: lab/platform-footing in the summary).
        i.command(f'/fill {bx - 1} {by + 3} {bz - 3} {bx + 1} {by + 3} {bz - 1} stone'); time.sleep(3.0)
        i.command(f'/tp @s {bx + .5} {by + 4} {bz - 1.5}'); time.sleep(3.0); report['walls_with_the_platform'] = standing()
        acts.run('steve-on-a-platform', lambda: time.sleep(10.0)); report['steve_on_the_platform'] = play.state()['pos']
        i.command(f'/fill {bx - 1} {by + 3} {bz - 3} {bx + 1} {by + 3} {bz - 1} air'); time.sleep(2.0)
        acts.run('the-platform-gone', lambda: time.sleep(4.0)); report['steve_after_the_platform'] = play.state()['pos']
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1')
        report['walls'] = control('walls'); report['spawn'] = [e for e in control('actor_spawn') if e.get('state') != 'slots'][-40:]; report['disturbed'] = disturbed(); save()


def boats_plan(out, keeper):
    """A Minecraft boat and a fishing bobber on BOTW's water (report 56).

    Over the pond of route-91. A boat is summoned above the water and Minecraft is asked for its
    place every two seconds: once with the water grid off and once with it on. Then, with the grid
    on, Steve hovers low over the water, looks down and uses a boat item (the boats about him are
    counted before and after), and casts a fishing rod (the bobber's place is read).
    """
    import os_input as i, walking_play as play, rig, re
    acts = Actions(out, keeper); report = {'place': POND}; census = LAB / 'water.jsonl'; log = LAB / 'minecraft/logs/latest.log'; switch = LAB / 'water.txt'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'boats.json', report)
    def logged(*marks): return [l for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if any(m in l for m in marks)] if log.exists() else []
    def place_of(selector):
        """Minecraft's own place of an entity (its /data command, read back from its log); None where there is none."""
        marks = ('has the following entity data: ', 'No entity was found'); before = len(logged(*marks)); i.command(f'/data get entity {selector} Pos'); time.sleep(.8); now = logged(*marks)
        found = re.search(r'entity data: \[(-?[0-9.eE]+)d, (-?[0-9.eE]+)d, (-?[0-9.eE]+)d\]', now[-1]) if len(now) > before else None
        return [round(float(v), 3) for v in found.groups()] if found else None
    def count(selector):
        marks = ('Test passed', 'Test failed'); before = len(logged(*marks)); i.command(f'/execute if entity {selector}'); time.sleep(.8); now = logged(*marks)
        if len(now) <= before: return None
        found = re.search(r'Test passed, count: (\d+)', now[-1]); return int(found.group(1)) if found else 0
    def surface_at(x, z):
        """Minecraft height of BOTW's water over a column by the bridge's last census; None where it gave none."""
        rows = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        if not rows: return None
        row = json.loads(rows[-1]); cx, cz = math.floor(x) - row['origin'][0], math.floor(z) - row['origin'][1]
        if not (0 <= cx < 16 and 0 <= cz < 16) or row['code'][cz * 16 + cx] in (0, -1): return None
        return round((row['botw_height'][cz * 16 + cx] - 251.188492) / 1.3 + 100, 3)
    def over(above):
        """Creative, flying, over the pond's middle and `above` blocks over where Steve floats."""
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.2)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        end = time.monotonic() + 8; i.key(JUMP, True)
        try:
            while time.monotonic() < end and play.state()['pos'][1] < POND['float'] + above: time.sleep(.03)
        finally: i.key(JUMP, False)
        time.sleep(.6); end = time.monotonic() + 240
        while time.monotonic() < end:
            s = play.state(); dx, dz = POND['x'] - s['pos'][0], POND['z'] - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < 1.0: break
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        end = time.monotonic() + 30; i.key(SNEAK, True)
        try:
            while time.monotonic() < end and play.state()['pos'][1] > POND['float'] + above: time.sleep(.03)
        finally: i.key(SNEAK, False)
        time.sleep(.8)
    def summoned(label, on):
        row = {'water_grid': on}
        def action():
            switch.write_text(f'{1 if on else 0} 256 250 1'); over(3.5); time.sleep(1.5); s = play.state()['pos']
            x, y, z = s[0] + 1.6, POND['float'] + 3.0, s[2]; row['summoned_at'] = [round(v, 2) for v in (x, y, z)]; row['surface_there'] = surface_at(x, z) if on else None
            i.command('/kill @e[type=oak_boat]'); i.command(f'/summon oak_boat {x:.2f} {y:.2f} {z:.2f} {{Tags:["lab"]}}'); places = []
            for n in range(6): time.sleep(1.2); places.append(place_of('@e[type=oak_boat,tag=lab,limit=1]'))
            row['boat_every_two_seconds'] = places; row['boat_height_last'] = places[-1] and places[-1][1]
        acts.run(label, action); report[label] = row; save()
    try:
        summoned('boat-summoned-grid-off', False); summoned('boat-summoned-grid-on', True)
        i.command('/kill @e[type=oak_boat]')
        placed = {}
        def level(height):
            end = time.monotonic() + 20
            while time.monotonic() < end:
                y = play.state()['pos'][1]
                if abs(y - height) < .2: break
                key = JUMP if y < height else SNEAK; i.key(key, True); time.sleep(min(.3, abs(y - height) / 9 + .03)); i.key(key, False); time.sleep(.45)
        def place():
            # Feet a block and a half over the water and the look 60 degrees down: the boat's place is then 1.8 blocks
            # ahead of Steve and under his feet's height. Minecraft refuses a boat where it would be inside a mob or a
            # player (route-138: looking 80 degrees down from the water's surface, the place was under Steve himself).
            over(3.5); time.sleep(.5); surface = surface_at(play.state()['pos'][0], play.state()['pos'][2]) or 63.78; level(surface + 1.5); time.sleep(.6)
            s = play.state(); placed['from'] = s['pos']; placed['eye'] = s['eye']; placed['surface_below'] = surface
            i.command('/item replace entity @s weapon.mainhand with oak_boat'); time.sleep(.5); play.aim(-90, 60); time.sleep(.3)   # towards +x: clear water (west of here a pillar of blocks stands in the pond, route-140)
            placed['flying'] = bool(play.state()['flags'] & 128); placed['looking'] = [play.state()['yaw'], play.state()['pitch']]
            placed['boat_before'] = place_of('@e[type=oak_boat,limit=1,sort=nearest]'); i.click(3); time.sleep(1.5); places = []
            for n in range(4): time.sleep(1.2); places.append(place_of('@e[type=oak_boat,limit=1,sort=nearest]'))
            placed['boat_every_two_seconds'] = places
            placed['minecraft_said'] = [l.split('BotwCraft: ', 1)[1] for l in logged('BotwCraft: use on the bridged water', 'BotwCraft: click 3')][-3:]
        acts.run('boat-item-used-on-the-water', place); report['boat-item-used-on-the-water'] = placed; save()
        i.command('/kill @e[type=oak_boat]')
        fished = {}
        def fish():
            s = play.state(); i.command('/item replace entity @s weapon.mainhand with fishing_rod'); time.sleep(.5); play.aim(-90, 55); time.sleep(.3)
            i.click(3); places = []
            for n in range(5): time.sleep(1.2); places.append(place_of('@e[type=fishing_bobber,limit=1]'))
            fished['bobber_every_two_seconds'] = places; last = next((p for p in reversed(places) if p), None)
            fished['surface_at_the_bobber'] = last and surface_at(last[0], last[2]); fished['floor'] = POND['floor']
        acts.run('fishing-rod-cast', fish); report['fishing-rod-cast'] = fished; save()
        i.click(3); time.sleep(.5)
    finally:
        switch.write_text('1 256 5000 1'); report['hits_on_fish'] = [l.split('BotwCraft: ', 1)[1] for l in logged('BotwCraft: hit Fish')][:12]; report['disturbed'] = disturbed(); save()
        try: i.command('/kill @e[type=oak_boat]'); i.command('/item replace entity @s weapon.mainhand with air'); i.command('/gamemode creative')
        except Exception: pass


def world_plan(out, keeper):
    """Minecraft's armour, explosions and lava against one of BOTW's enemies (report 55).

    At the Bokoblin of route-101. Armour: Steve stands by it in survival, first bare and then in
    diamond armour; Minecraft's own log gives his health before and after every blow. Then,
    with one point of Minecraft damage worth a fifth of a point of BOTW life: TNT lit four
    blocks from the enemy, and lava poured where it stands. Recorded: Minecraft's line for each
    hit on the stand-in, what the bridge passed on, and the enemy's life in BOTW.
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'camp': CAMPS[0], 'botw_life_per_minecraft_damage': 0.2}; census = LAB / 'actors.jsonl'; log = LAB / 'minecraft/logs/latest.log'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'world.json', report)
    def last():
        rows = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        return json.loads(rows[-1]) if rows else None
    def lines(text): return [l.split('BotwCraft: ', 1)[1].strip() for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if text in l and 'BotwCraft: ' in l] if log.exists() else []
    def control(name): return [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if f'"{name}"' in l]
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def foe():
        foes = [a for a in (last() or {}).get('actors', []) if a['kind'] == 1 and not a['dead']]
        return min(foes, key=lambda a: a['distance']) if foes else None
    def life(a): return round(a['life'] * a['most_life'], 2) if a else None
    def same(target): return next((a for a in (last() or {}).get('actors', []) if a['id'] == target['id']), None)
    def mode(name):
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command(f'/gamemode {name}'); time.sleep(1.0)
    def stand(label, seconds):
        """Survival by the enemy: every blow Minecraft's log reports in that time."""
        rows = {}
        def action():
            before = len(lines('BOTW hit the player')); mode('survival'); time.sleep(seconds); mode('creative'); time.sleep(.5)
            rows['blows'] = lines('BOTW hit the player')[before:]
        acts.run(label, action); report[label] = rows['blows']; save()
    try:
        (LAB / 'actors.txt').write_text('1 250 1'); (LAB / 'damage-to-botw.txt').write_text('0.2')
        mode('creative')
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)
        end = time.monotonic() + 240
        while time.monotonic() < end:
            s = play.state(); dx, dz = CAMPS[0]['x'] - s['pos'][0], CAMPS[0]['z'] - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < 2.5: break
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))
        def land():
            i.key(SNEAK, True)
            try:
                until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
                while time.monotonic() < until:
                    s = play.state(); y = s['pos'][1]
                    if y < low - .05: low = y; still = time.monotonic()
                    if not s['flags'] & 128 or time.monotonic() - still > 2.5: break
                    time.sleep(.05)
            finally: i.key(SNEAK, False)
            time.sleep(3)
        acts.run('land', land); report['landed'] = play.state()['pos']; report['enemy'] = foe(); save()
        def no_lava():
            # The lab's Minecraft world keeps its blocks between runs, and lava runs further over BOTW's ground than the
            # few blocks it does in Minecraft (route-118: Steve died in what route-117 had left). Four fills, 30 x 21 x 30 each.
            x, y, z = (math.floor(v) for v in play.state()['pos'])
            for dx in (-30, 0):
                for dz in (-30, 0): i.command(f'/fill {x + dx} {y - 12} {z + dz} {x + dx + 29} {y + 8} {z + dz + 29} air replace lava')
        no_lava()
        # Everything the game runs near the camp, of whatever kind (actors.h, lab/procs.txt), kept with the run.
        (LAB / 'procs.txt').write_text('1'); time.sleep(1.5)
        if (LAB / 'procs.jsonl').exists(): shutil.copyfile(LAB / 'procs.jsonl', out / 'procs.jsonl')
        stand('bare-20s', 20)
        for slot, item in (('head', 'diamond_helmet'), ('chest', 'diamond_chestplate'), ('legs', 'diamond_leggings'), ('feet', 'diamond_boots')): i.command(f'/item replace entity @s armor.{slot} with {item}')
        i.command('/effect give @s instant_health 1 10'); time.sleep(.5)
        stand('diamond-armour-20s', 20)
        for slot in ('head', 'chest', 'legs', 'feet'): i.command(f'/item replace entity @s armor.{slot} with air')
        i.command('/effect give @s instant_health 1 10')
        def blast(label, command, seconds, after=None):
            row = {}
            def action():
                target = foe(); row['enemy'] = target and target['name']; row['life_before'] = life(target)
                if not target: return
                x, y, z = mc(target['at']); row['at'] = [x, y, z]; hits = len(lines('BotwCraft: hit ')); events = len(control('actor_hit'))
                row['command'] = command(x, y, z); i.command(row['command']); time.sleep(seconds)
                now = same(target); row['life_after'] = life(now); row['dead_after'] = now['dead'] if now else None; row['listed_after'] = now is not None
                row['minecraft'] = lines('BotwCraft: hit ')[hits:]
                row['passed_on'] = [{k: e.get(k) for k in ('state', 'minecraft_damage', 'amount', 'weapon', 'flags', 'age_ms', 'fire') if k in e} for e in control('actor_hit')[events:]]
            acts.run(label, action); report[label] = row; save()
            if after and 'at' in row: i.command(after(*row['at'])); time.sleep(1.0)
        blast('tnt-four-blocks-away', lambda x, y, z: f'/summon tnt {x + 4:.2f} {y + .5:.2f} {z:.2f} {{fuse:30,NoGravity:1b}}', 4.0)
        # Two layers: the enemy's feet are on BOTW's ground, which seldom lies at the bottom of a block (route-116: one layer missed it).
        blast('lava-where-it-stands', lambda x, y, z: f'/fill {math.floor(x) - 1} {math.floor(y)} {math.floor(z) - 1} {math.floor(x) + 1} {math.floor(y) + 1} {math.floor(z) + 1} lava', 1.6,
              lambda x, y, z: f'/fill {math.floor(x) - 6} {math.floor(y) - 12} {math.floor(z) - 6} {math.floor(x) + 6} {math.floor(y) + 2} {math.floor(z) + 6} air replace lava')
        def burning():
            # What Minecraft's fire on the stand-in still does after the lava is gone.
            no_lava(); target = foe(); hits = len(lines('BotwCraft: hit ')); events = len(control('actor_hit')); before = life(target); time.sleep(6)
            report['after-the-lava-6s'] = {'life_before': before, 'life_after': life(same(target)) if target else None, 'minecraft': lines('BotwCraft: hit ')[hits:],
                                           'passed_on': [{k: e.get(k) for k in ('state', 'minecraft_damage', 'amount', 'flags') if k in e} for e in control('actor_hit')[events:]]}
        acts.run('after-the-lava-6s', burning); no_lava(); save()
        def animal():
            # A Minecraft animal on BOTW's ground: other entities get the bridged world as block-sized shapes (BotwCollision).
            import re
            said = lambda: [l for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'has the following entity data: ' in l]
            s = play.state()['pos']; i.command(f'/summon pig {s[0] + 2:.2f} {s[1] + 3:.2f} {s[2]:.2f}'); rows = []   # with its own movement: an animal without it does not fall either (route-117)
            for _ in range(4):
                time.sleep(1.5); before = len(said()); i.command('/data get entity @e[type=pig,limit=1,sort=nearest] Pos'); time.sleep(.7); now = said()
                found = re.findall(r'(-?[0-9.]+)d', now[-1].split('entity data: ', 1)[1]) if len(now) > before else []
                rows.append([float(v) for v in found[:3]])
            report['pig'] = {'summoned_at': [round(s[0] + 2, 2), round(s[1] + 3, 2), round(s[2], 2)], 'player': play.state()['pos'], 'positions': rows}
            i.command('/kill @e[type=pig]')
        acts.run('a-pig-on-botw-ground', animal); save()
        report['enemy_at_end'] = foe()
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1'); (LAB / 'damage-to-botw.txt').write_text('3'); save()


# A camp with a lit fire (the game's FireWood, with meat over it) and three Bokoblins, one of the stronger
# kind; from the list of everything the game ran in route-118 (procs.jsonl). Minecraft coordinates.
FIRE_CAMP = {'x': 54.8, 'y': 89.4, 'z': 104.6}


def campfire(out, keeper):
    """BOTW's fire on Steve (report 55): what the game does to Link in a camp's fire, and what of it reaches Minecraft.

    Steve flies to the fire of FIRE_CAMP, comes down on it and is switched to survival for a while.
    Recorded: every damage event of the game for the player with its codes (control.jsonl), what
    Minecraft's log says reached Steve, Link's native states, and the actors listed there.
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'camp': FIRE_CAMP}; log = LAB / 'minecraft/logs/latest.log'; census = LAB / 'actors.jsonl'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'campfire.json', report)
    def lines(text): return [l.split('BotwCraft: ', 1)[1].strip() for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if text in l and 'BotwCraft: ' in l] if log.exists() else []
    def logged(since_ms, names):
        rows = [json.loads(line) for line in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if line.startswith('{')]
        return [e for e in rows if e['unix_ms'] >= since_ms and e['event'] in names]
    def listed():
        rows = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
        return [{k: a[k] for k in ('name', 'id', 'distance', 'most_life', 'dead')} for a in json.loads(rows[-1])['actors'] if a['kind'] == 1] if rows else []
    def stay(label, seconds, walk=False):
        row = {}
        def action():
            since = int(time.time() * 1000); begun = rig.qpc(); said = len(lines('BOTW hit the player')); before = play.state()['pos']
            if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
            i.command('/gamemode survival'); end = time.monotonic() + seconds
            while time.monotonic() < end:
                if walk: play.aim(90); play.hold([W], .25); play.aim(-90); play.hold([W], .25)
                else: time.sleep(.25)
            i.command('/gamemode creative'); time.sleep(.5)
            hits = [e for e in logged(since, ['native_add_damage']) if e['damage'] > 0 or e['r6'] > 0 or e['r7'] > 0]
            row.update({'from': before, 'to': play.state()['pos'], 'minecraft': lines('BOTW hit the player')[said:],
                        'add_damage': [{k: e[k] for k in ('refused', 'r4', 'damage', 'r6', 'r7', 'r8', 'r9', 'r10', 'state')} for e in hits][:40],
                        'received': [{k: e[k] for k in ('r4', 'r5', 'r6', 'r7', 'r8', 'r9', 'r10', 's0', 's1')} for e in logged(since, ['native_damage_receive']) if e['r7'] > 0 or e['r8'] > 0][:40],
                        'attacks': [{k: e[k] for k in ('damage', 'defence', 'attack_damage', 'least', 'kind', 'name')} for e in logged(since, ['native_attack'])][:40],
                        'sent_to_minecraft': [{k: e.get(k) for k in ('quarter_hearts', 'minecraft_damage', 'attacker', 'fire')} for e in logged(since, ['native_damage_to_minecraft'])][:40],
                        'health_writes': [{k: e[k] for k in ('old', 'new', 'blocked')} for e in logged(since, ['native_health'])][:20],
                        'releases': [{k: e.get(k) for k in ('reason', 'why', 'control_only')} for e in logged(since, ['authority_release', 'physical_release'])], 'enemies_listed': listed(), **follow_summary(begun)})
        acts.run(label, action); report[label] = row; save()
    try:
        (LAB / 'actors.txt').write_text('1 500 1')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)
        end = time.monotonic() + 240
        while time.monotonic() < end:
            s = play.state(); dx, dz = FIRE_CAMP['x'] - s['pos'][0], FIRE_CAMP['z'] - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < .6: break
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.05, distance / 11 - .15)))
        def land():
            i.key(SNEAK, True)
            try:
                until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
                while time.monotonic() < until:
                    s = play.state(); y = s['pos'][1]
                    if y < low - .05: low = y; still = time.monotonic()
                    if not s['flags'] & 128 or time.monotonic() - still > 2.5: break
                    time.sleep(.05)
            finally: i.key(SNEAK, False)
            time.sleep(1)
        acts.run('land-on-the-fire', land); report['landed'] = play.state()['pos']
        (LAB / 'procs.txt').write_text('1'); time.sleep(1.5)
        if (LAB / 'procs.jsonl').exists(): shutil.copyfile(LAB / 'procs.jsonl', out / 'procs.jsonl')
        save()
        stay('in-the-fire-12s', 12)
        # Minecraft's own answer to fire: with its fire resistance the game's fire has to do nothing.
        i.command('/effect give @s fire_resistance 20'); i.command('/effect give @s instant_health 1 10'); time.sleep(.5)
        stay('in-the-fire-with-fire-resistance-10s', 10); i.command('/effect clear @s')
        stay('about-the-fire-12s', 12, walk=True)
    finally:
        (LAB / 'actors.txt').write_text('1 5000 1'); save()


def procs_plan(out, keeper):
    """Everything the game runs, with its place, where a session starts (actors.h, lab/procs.txt). Read only."""
    import walking_play as play
    (LAB / 'procs.txt').write_text('1'); time.sleep(2.5)
    if (LAB / 'procs.jsonl').exists(): shutil.copyfile(LAB / 'procs.jsonl', out / 'procs.jsonl')
    if (LAB / 'creates.jsonl').exists(): shutil.copyfile(LAB / 'creates.jsonl', out / 'creates.jsonl')   # what was written so far; the bridge keeps the file open
    live.save(out / 'procs.json', {'player': play.state()['pos'], 'written': (out / 'procs.jsonl').exists()})


# The nearer of the two boxes of placed water the game runs about the plateau (its BoxWater actors, route-124):
# the middle of its top face in BOTW's coordinates, its own x direction in the world, and its size.
WATER_BOX = {'top': [-1119.85, 306.12, 2170.02], 'x_axis': [-0.9212, -0.3891], 'z_axis': [0.3891, -0.9212], 'size': [34.0, 4.0, 40.0]}


def boxwater(out, keeper):
    """The game's placed water in Minecraft's water grid (report 56).

    Steve flies to WATER_BOX and, a little under its top, along both of the box's own directions
    well past its faces. Recorded along the way: where the game puts Link into his swim state and
    where the grid says water, both in the box's own coordinates. Then the drop in survival, as
    in plan `water`.
    """
    import os_input as i, walking_play as play, rig, re
    acts = Actions(out, keeper); box = WATER_BOX; report = {'box': box}; census = LAB / 'water.jsonl'; log = LAB / 'minecraft/logs/latest.log'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def mc(at): return [(at[0] + 1027.16809) / 1.3, (at[1] - 251.188492) / 1.3 + 100, (at[2] - 1812.87195) / 1.3]
    def botw(pos): return [-1027.16809 + pos[0] * 1.3, 251.188492 + (pos[1] - 100) * 1.3, 1812.87195 + pos[2] * 1.3]
    def local(at): dx, dz = at[0] - box['top'][0], at[2] - box['top'][2]; return [dx * box['x_axis'][0] + dz * box['x_axis'][1], dx * box['z_axis'][0] + dz * box['z_axis'][1]]
    centre = mc(box['top'])
    def save(): live.save(out / 'boxwater.json', report)
    def fly_to(x, z, near=.8, seconds=240):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            s = play.state(); dx, dz = x - s['pos'][0], z - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < near: return True
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))
            else: play.hold([W], min(1.2, max(.05, distance / 11 - .15)))
        return False
    def level(y, seconds=40):
        """Creative flight up or down to a height."""
        end = time.monotonic() + seconds; key = JUMP if play.state()['pos'][1] < y else SNEAK; i.key(key, True)
        try:
            while time.monotonic() < end and (play.state()['pos'][1] < y) == (key == JUMP): time.sleep(.02)
        finally: i.key(key, False)
        time.sleep(.5)
    def data(name):
        said = lambda: [l for l in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'has the following entity data: ' in l] if log.exists() else []
        before = len(said()); i.command(f'/data get entity @s {name}'); time.sleep(.7); now = said()
        found = re.search(r'entity data: (-?[0-9.]+)', now[-1]) if len(now) > before else None
        return float(found.group(1)) if found else None
    try:
        (LAB / 'water.txt').write_text('1 256 100 1')
        if play.state()['flags'] & 2: i.tap(0x1B); time.sleep(.8)
        i.command('/gamemode creative'); time.sleep(1.5)
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        level(centre[1] + 8); report['reached'] = fly_to(centre[0], centre[2]); level(centre[1] + 2.5); fly_to(centre[0], centre[2], near=.5)
        report['over_the_middle'] = play.state()['pos']; save()
        assert report['reached'], 'the box was not reached'
        def sweep(label, axis):
            row = {}
            def action():
                level(centre[1] - 1.6); begun = rig.qpc(); since = int(time.time() * 1000)
                reach = max(box['size'][0], box['size'][2]) / 2 / 1.3 + 9   # blocks, past the further face
                for sign in (1, -1):
                    fly_to(centre[0] + sign * axis[0] * reach, centre[2] + sign * axis[1] * reach, near=1.0, seconds=30)
                fly_to(centre[0], centre[2], near=.6, seconds=30)
                # The game's side: Link's state at each place his body was written to (bit 0x400: he swims, report 49).
                swim = []; dry = []
                for r in pose_rows(begun):
                    at = [float(r[f'body_{a}']) for a in 'xyz']; l = local(at); (swim if int(r['flags']) & 0x400 else dry).append((l, at[1], int(r['flags'])))
                along = 0 if axis is box['x_axis'] else 1
                row['updates_swimming'] = len(swim); row['updates_not_swimming'] = len(dry); row['flags_seen'] = sorted({f'{f:#x}' for _, _, f in swim + dry})
                if swim: row['swimming_from_to_along_this_direction'] = [round(min(l[along] for l, _, _ in swim), 2), round(max(l[along] for l, _, _ in swim), 2)]
                if dry: row['not_swimming_nearest_the_middle'] = round(min(abs(l[along]) for l, _, _ in dry), 2)
                row['height_flown_botw'] = round(botw(play.state()['pos'])[1], 2)
                # The grid's side: the surface under the player as published while he flew (lab/water.jsonl every 100 ms holds only
                # the game's own answer; the boxes are added at publication, so the published grid is read through Minecraft: his swimming).
                row['water_grid_events'] = [{k: e[k] for k in ('boxes', 'columns_in_boxes', 'wet', 'highest') if k in e} for e in
                                            [json.loads(l) for l in (LAB / 'control.jsonl').read_text(encoding='utf-8', errors='replace').splitlines() if '"water_grid"' in l and json.loads(l)['unix_ms'] >= since]][:60:6]
            acts.run(label, action); report[label] = row; save()
        sweep('along-its-x', box['x_axis'])
        def only_the_box():
            """A column near the middle that the game's own answer leaves dry (-1) and the box covers: Minecraft's place for the drop."""
            level(centre[1] + 2.5); fly_to(centre[0], centre[2], near=.5, seconds=20); time.sleep(.5)
            rows = census.read_text(encoding='utf-8', errors='replace').splitlines() if census.exists() else []
            if not rows: return None
            row = json.loads(rows[-1]); best = None
            for n, code in enumerate(row['code']):
                x, z = row['origin'][0] + n % 16 + .5, row['origin'][1] + n // 16 + .5; l = local(botw([x, 0, z]))
                # Route-126 floated without the boxes too, beside a column the game's own answer makes wet:
                # every column within two blocks has to be dry by that answer.
                around = [row['code'][(n // 16 + dz) * 16 + n % 16 + dx] if 0 <= n % 16 + dx < 16 and 0 <= n // 16 + dz < 16 else None for dz in range(-2, 3) for dx in range(-2, 3)]
                if code == -1 and all(c == -1 for c in around) and abs(l[0]) < box['size'][0] / 2 - 3 and abs(l[1]) < box['size'][2] / 2 - 3:
                    d = math.hypot(x - centre[0], z - centre[2])
                    if best is None or d < best[0]: best = (d, x, z, [round(v, 2) for v in l])
            report['answers_about_the_middle'] = {str(c): row['code'].count(c) for c in sorted(set(row['code']))}
            return best
        spot = only_the_box(); report['column_only_the_box_covers'] = spot and {'minecraft': [spot[1], spot[2]], 'in_the_box': spot[3], 'blocks_from_the_middle': round(spot[0], 2)}; save()
        place = (spot[1], spot[2]) if spot else (centre[0], centre[2])
        def drop(label, boxes):
            row = {'boxes_in_the_grid': boxes}
            def action():
                (LAB / 'water.txt').write_text(f'1 256 100 {1 if boxes else 0}'); i.command('/gamemode creative'); time.sleep(1.2)
                if not play.state()['flags'] & 128:
                    i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
                level(centre[1] + 2.5); fly_to(place[0], place[1], near=.4, seconds=20); time.sleep(.6); row['from'] = play.state()['pos']
                i.command('/gamemode survival'); i.key(JUMP, True); ys = []
                try:
                    end = time.monotonic() + 6
                    while time.monotonic() < end: ys.append(round(play.state()['pos'][1], 3)); time.sleep(.1)
                finally: i.key(JUMP, False)
                rows = census.read_text(encoding='utf-8', errors='replace').splitlines()
                if rows:
                    last = json.loads(rows[-1]); ox, oz = last['origin']; px, pz = math.floor(place[0]) - ox, math.floor(place[1]) - oz
                    row['grid_about_the_column'] = {'boxes': last.get('boxes'), 'columns_in_boxes': last.get('columns_in_boxes'), 'codes': [[last['code'][z * 16 + x] if 0 <= x < 16 and 0 <= z < 16 else None for x in range(px - 2, px + 3)] for z in range(pz - 2, pz + 3)]}
                row['jump_held_heights'] = ys[::4]; row['jump_held_lowest_highest_last'] = [min(ys), max(ys), ys[-1]]; row['surface_minecraft'] = round(centre[1], 3)
                if boxes:
                    i.key(SNEAK, True); time.sleep(6); i.key(SNEAK, False); row['after_sneak_6s'] = play.state()['pos']; row['air_after_sneak'] = data('Air')
                i.command('/gamemode creative')
            acts.run(label, action); report[label] = row; save()
        drop('drop-with-the-boxes', True); drop('drop-without-the-boxes', False)
    finally:
        (LAB / 'water.txt').write_text('1 256 5000 1'); save()


def pose_rows(since_qpc):
    """Controller updates (poses.csv, phase 0) of the running session from a moment on."""
    import csv
    for name in ('poses.csv.2', 'poses.csv.1', 'poses.csv'):
        if (LAB / name).exists():
            with (LAB / name).open(newline='', encoding='utf-8', errors='replace') as f:
                for r in csv.DictReader(f):
                    if None not in r.values() and r['phase'] == '0' and int(r['qpc']) >= since_qpc: yield r


def follow_summary(since_qpc):
    """How well the body stayed where it was written, and the native states met, in order."""
    worst = 0.0; written = 0; states = []; frequency = 1e7
    for r in pose_rows(since_qpc):
        if r['previous_request'] == '1':
            written += 1
            worst = max(worst, math.dist([float(r[f'requested_{a}']) for a in 'xyz'], [float(r[f'body_{a}']) for a in 'xyz']))
        flags = int(r['flags'])
        if not states or states[-1]['flags'] != f'{flags:#x}': states.append({'flags': f'{flags:#x}', 'from': int(r['qpc']), 'to': int(r['qpc']), 'written': 0, 'updates': 0})
        states[-1]['to'] = int(r['qpc']); states[-1]['updates'] += 1; states[-1]['written'] += r['write'] == '1'
    for s in states: s['seconds'] = round((s.pop('to') - s.pop('from')) / frequency, 2)
    return {'updates_after_a_write': written, 'largest_distance_from_written_place': round(worst, 4), 'native_states': states[:40]}


# Where Link swam in route-91, a play session (Minecraft coordinates of a run started by this script):
# the floor of the pond east of the start, and the height Link floated at.
POND = {'x': 83.8, 'z': 55.3, 'floor': 60.9, 'float': 62.7}
# Where Link drowned in route-95, a play session: the bed of a river about ten blocks under its surface.
RIVER = {'x': 556.0, 'z': -1862.0, 'floor': -17.0, 'float': -6.9}


def swim(out, keeper, place=POND):
    """Link in water while Minecraft drives (report 49).

    Plan `river` is the same at the river of route-95, where Link drowned: a stay on the bed
    with his stamina guarded, then with the guards off until he drowns, and then the wait for
    control to come back by itself.

    Steve flies to the pond of route-91 and goes down to its floor, which puts Link into
    the native swim state. Then, with control kept in both cases: the write left out while he
    swims (Link floats up and stays behind), and the write continued. Recorded for each: the
    native states in order, whether the body stayed where it was written, releases, native
    screens (a fade would be Link drowning) and health writes. With the write continued Steve
    also stands still for a while and walks across the floor.
    """
    import os_input as i, walking_play as play, rig
    acts = Actions(out, keeper); report = {'place': place}; states = LAB / 'follow-states.txt'
    JUMP, SNEAK, SPRINT = binding('jump', 0x20), binding('sneak', 0xA0), binding('sprint', 0xA2)
    def save(): live.save(out / 'swim.json', report)
    def fly_to(x, z, seconds=300, near=2.5):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            s = play.state(); dx, dz = x - s['pos'][0], z - s['pos'][2]; distance = math.hypot(dx, dz)
            if distance < near: return True
            play.aim(math.degrees(math.atan2(-dx, dz)), 0)
            if distance > 80: play.hold([SPRINT, W], min(4.0, distance / 22 - 1.5))   # sprint flight: about 21 blocks/s
            else: play.hold([W], min(1.2, max(.08, distance / 11 - .15)))            # creative flight: about 11 blocks/s
        return False
    def phase(label, setting, action):
        states.write_text(setting); time.sleep(1.2)
        since = offset(); begun = rig.qpc(); before = play.state()['pos']
        acts.run(label, action)
        after = play.state()
        report[label] = {'follow_states': setting, 'from': before, 'to': after['pos'], 'minecraft_flags_after': after['flags'],
                         'releases': [{k: e[k] for k in ('reason', 'control_only')} for e in events_since(since, ['authority_release'])],
                         'input_releases': [{k: e.get(k) for k in ('reason', 'why')} for e in events_since(since, ['physical_release'])],
                         'native_screens': [{k: e[k] for k in ('ready', 'screen')} for e in events_since(since, ['lifecycle'])],
                         'body_follow': [{k: e[k] for k in ('state', 'flags', 'controller_flags', 'ms', 'body')} for e in events_since(since, ['body_follow'])],
                         'native_health_writes': [{k: e[k] for k in ('old', 'new', 'blocked')} for e in events_since(since, ['native_health'])],
                         'coverage_parks': len(events_since(since, ['native_coverage_park'])), 'stamina': stamina(since), **follow_summary(begun)}
        save()
    def stamina(since):
        rows = events_since(since, ['native_stamina'])
        if not rows: return {'writes_logged': 0}
        return {'writes_logged': len(rows), 'writes': rows[-1]['writes'] - rows[0]['writes'], 'refused': rows[-1]['blocked_writes'] - rows[0]['blocked_writes'],
                'stored_first_last_lowest': [rows[0]['old'], rows[-1]['old'], min(r['old'] for r in rows)], 'asked_lowest': min(r['new'] for r in rows)}
    try:
        report['flags_at_start'] = play.state()['flags']
        if report['flags_at_start'] & 2:   # a Minecraft screen is open (route-97: keys typed by the player opened the inventory)
            i.tap(0x1B); time.sleep(.8); report['flags_after_escape'] = play.state()['flags']
        i.command('/gamemode creative'); time.sleep(1.5)   # the lab world keeps the game mode of the session before
        if not play.state()['flags'] & 128:
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
        assert play.state()['flags'] & 128, 'creative flight did not start'
        i.key(JUMP, True); time.sleep(.9); i.key(JUMP, False); time.sleep(.4)   # clear of the ground
        report['reached'] = fly_to(place['x'], place['z']); report['above'] = play.state()['pos']; save()
        assert report['reached'], 'the water was not reached'
        def descend():
            i.key(SNEAK, True)
            try:
                until = time.monotonic() + 60; low = play.state()['pos'][1]; still = time.monotonic()
                while time.monotonic() < until:
                    y = play.state()['pos'][1]
                    if y < low - .05: low = y; still = time.monotonic()
                    if y <= place['floor'] + .3 or time.monotonic() - still > 2.5: break
                    time.sleep(.05)
            finally: i.key(SNEAK, False)
            time.sleep(2)
        if place is RIVER:
            def walk():
                play.aim(90); play.hold([W], 2.0); play.aim(-90); play.hold([W], 2.0); time.sleep(1)
            phase('descend', FOLLOW_DEFAULT, descend)
            phase('river-bed-guarded-still-40s', FOLLOW_DEFAULT, lambda: time.sleep(40))
            phase('river-bed-guarded-walking', FOLLOW_DEFAULT, lambda: (walk(), walk(), walk()))
            seen = {}
            def drown():
                # Guards off: what the game does here by itself. Until a native screen (the fade of a drowning) or 60 s.
                since = offset(); (LAB / 'damage-guard.txt').write_text('0'); begun = time.monotonic()
                while time.monotonic() - begun < 60 and not any(not e['ready'] for e in events_since(since, ['lifecycle'])): time.sleep(.25)
                seen['seconds_to_native_screen'] = round(time.monotonic() - begun, 1) if time.monotonic() - begun < 60 else None
                (LAB / 'damage-guard.txt').write_text('1'); back = time.monotonic()
                # Control has to come back by itself (route-95: it never did, Link was afloat and drifting).
                while time.monotonic() - back < 90 and not (seen['seconds_to_native_screen'] is None or events_since(since, ['physical_claim'])): time.sleep(.25)
                claims = events_since(since, ['physical_claim'])
                seen['seconds_from_guard_on_to_control'] = round(time.monotonic() - back, 1) if claims else None
                seen['claims'] = [{k: e.get(k) for k in ('automatic',)} for e in claims]
                seen['stages'] = len(events_since(since, ['native_stage'])); seen['readies'] = len(events_since(since, ['native_ready']))
                time.sleep(8)
            phase('river-bed-unguarded-until-drowned', FOLLOW_DEFAULT, drown); report['river-bed-unguarded-until-drowned'].update(seen); save()
            phase('after-control-is-back-walking', FOLLOW_DEFAULT, walk)
            return
        phase('descend-write-left-out', '0 188490 0 100000', descend)
        phase('float-write-left-out', '0 188490 0 100000', lambda: time.sleep(8))
        phase('swim-write-continues-still', '0 188080 0', lambda: time.sleep(45))
        def walk():
            play.aim(90); play.hold([W], 1.5); play.aim(-90); play.hold([W], 1.5); time.sleep(2)
        phase('swim-write-continues-walking', '0 188080 0', walk)
        def unguarded():
            (LAB / 'damage-guard.txt').write_text('0'); time.sleep(15); (LAB / 'damage-guard.txt').write_text('1'); time.sleep(1.5)
        phase('swim-stamina-unguarded-15s', '0 188080 0', unguarded)   # what the game does to his stamina here by itself
        def leave():
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
            i.key(JUMP, True); time.sleep(2.5); i.key(JUMP, False); time.sleep(4)
        phase('leave-the-water-upwards', '0 188080 0', leave)
    finally:
        states.write_text(FOLLOW_DEFAULT); (LAB / 'damage-guard.txt').write_text('1')
        save()


def binding(action, default):
    """Windows key code of a Minecraft key binding, from the lab profile's options.txt.

    The player may have rebound keys (route-60: sprint on left shift, sneak on
    left control), so a test never assumes the defaults.
    """
    names = {'left.shift': 0xA0, 'right.shift': 0xA1, 'left.control': 0xA2, 'right.control': 0xA3, 'left.alt': 0xA4, 'space': 0x20, 'tab': 9, 'caps.lock': 0x14}
    try: lines = (LAB / 'minecraft/options.txt').read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError: return default
    for line in lines:
        if line.startswith(f'key_key.{action}:key.keyboard.'):
            name = line.split('key.keyboard.', 1)[1].strip()
            if name in names: return names[name]
            if len(name) == 1 and name.isalnum(): return ord(name.upper())
            raise RuntimeError(f'Minecraft binding for {action} is {name}; add it to native_route.binding')
    return default


# label, lab/region-path.txt, lab/coverage-hold.txt. `wait-only` keeps the old order so that pauses
# still happen and the wait itself is measured.
FLIGHT_MODES = (('before', 0, 0), ('wait-only', 0, 1), ('after', 1, 1))


def flight_phase(report, label, seconds, during, out=None):
    """One stretch of fast travel: speed, coverage pauses, time Minecraft stood still, reads, late frames.

    `during(seconds, moving)` drives the input and keeps `moving` set while the
    body is expected to be under way; a stall is Minecraft's position standing
    still for 150 ms or more in that time (what the player sees as a freeze).
    """
    import walking_play as play, threading
    since = offset(); samples = []; stop = threading.Event(); moving = threading.Event()
    def watch():
        while not stop.is_set():
            try: s = play.state(); samples.append((s['qpc'], s['pos'], s['flags'], moving.is_set(), s['frame'], s['ack']))
            except (RuntimeError, AssertionError, OSError, KeyError): pass
            time.sleep(.03)
    watcher = threading.Thread(target=watch); watcher.start()
    try: during(seconds, moving)
    finally: moving.clear(); stop.set(); watcher.join()
    time.sleep(.5)
    pairs = [(a, b) for a, b in zip(samples, samples[1:]) if b[0] > a[0]]
    speeds = sorted(math.dist(a[1], b[1]) / ((b[0] - a[0]) / 1e7) for a, b in pairs if a[3] and b[3])
    runs = []; run = None
    for a, b in pairs:
        still = a[3] and b[3] and math.dist(a[1], b[1]) < 1e-3   # held exactly in place; sliding along a wall is not a stall
        if still: run = [run[0] if run else a, b]
        elif run: runs.append(run); run = None
    if run: runs.append(run)
    runs = [r for r in runs if (r[1][0] - r[0][0]) / 1e4 >= 150]
    stalls = [round((r[1][0] - r[0][0]) / 1e4) for r in runs]
    rows = events_since(since, ['native_coverage_pause', 'native_coverage_resume', 'native_coverage_park', 'native_region_cache', 'frame_pacing', 'authority_release'])
    # A stall with Minecraft frames still counting is Minecraft being held; without, Minecraft itself hitched.
    # `pause_before_ms`: how long before the stall began the nearest coverage pause was raised (None: none within 2 s).
    detail = []
    for r in runs:
        near = [(r[0][0] - e['qpc']) / 1e4 for e in rows if e['event'] == 'native_coverage_pause' and 'qpc' in e and -2e7 < r[0][0] - e['qpc'] < 2e7]
        detail.append({'ms': round((r[1][0] - r[0][0]) / 1e4), 'mc_frames': r[1][4] - r[0][4], 'flags': [r[0][2], r[1][2]], 'ack': [r[0][5], r[1][5]], 'pos': [round(v, 1) for v in r[0][1]],
                       'pause_before_ms': round(min(near, key=abs)) if near else None})
    if out: live.save(out / f'flight-{label}.json', {'samples': samples, 'events': [e for e in rows if e['event'].startswith('native_coverage') or e['event'] == 'authority_release']})
    pauses = [e for e in rows if e['event'] == 'native_coverage_pause']; resumes = [e for e in rows if e['event'] == 'native_coverage_resume']
    reasons = {}
    for e in pauses: reasons[e['reason']] = reasons.get(e['reason'], 0) + 1
    lengths = sorted(e['paused_ms'] for e in resumes if 'paused_ms' in e)
    pacing = [e for e in rows if e['event'] == 'frame_pacing']; cache = [e for e in rows if e['event'] == 'native_region_cache' and 'requests' in e]
    report[label] = {
        'seconds': seconds, 'distance': round(sum(math.dist(a[1], b[1]) for a, b in pairs), 1),
        'speed_median_p90_max': [round(speeds[len(speeds) // 2], 1), round(speeds[int(len(speeds) * .9)], 1), round(speeds[-1], 1)] if speeds else None,
        'stalls': len(stalls), 'stalled_ms_total': sum(stalls), 'stall_ms': stalls[:40], 'stall_detail': detail[:40],
        'pauses': len(pauses), 'pause_reasons': reasons, 'pauses_as_wait': sum(e.get('hold', 0) for e in pauses), 'parks': sum(e['event'] == 'native_coverage_park' for e in rows) + sum(not e.get('hold', 0) for e in pauses),
        'paused_ms_total': sum(lengths), 'paused_ms_median_max': [lengths[len(lengths) // 2], lengths[-1]] if lengths else None,
        'pause_speed': [round(math.hypot(*e['velocity']), 1) for e in pauses][:40],
        'reads_per_second': round((cache[-1]['requests'] - cache[0]['requests']) / max(len(cache) - 1, 1), 1) if len(cache) > 1 else None,
        'reads_skipped': cache[-1].get('skipped', 0) - cache[0].get('skipped', 0) if len(cache) > 1 else None, 'read_timeouts': cache[-1]['timeouts'] - cache[0]['timeouts'] if len(cache) > 1 else None,
        'frames': sum(e['frames'] for e in pacing), 'late_frames': sum(e['late'] for e in pacing),
        'releases': [e['reason'] for e in rows if e['event'] == 'authority_release'],
        'flags_seen': sorted({s[2] for s in samples})}


def flight(out, keeper):
    """Fast travel, before and after: sprint flight and an elytra with firework rockets.

    Each stretch is flown twice in one session: with acquisition in the old
    order and SkyCraft's loading park (`before`: lab/region-path.txt 0,
    lab/coverage-hold.txt 0), and with acquisition along the way and the wait
    (`after`: both 1). Reported per stretch (`flight.json`): speed, coverage
    pauses with reason and length, the time Minecraft stood still, harvests
    per second and late frames. Keys come from the profile's own bindings.
    """
    import os_input as i, walking_play as play
    report = {}; JUMP, SPRINT = binding('jump', 0x20), binding('sprint', 0xA2)
    report['bindings'] = {'jump': JUMP, 'sprint': SPRINT, 'sneak': binding('sneak', 0xA0)}
    def save(): live.save(out / 'flight.json', report)
    def mode(path, hold):
        (LAB / 'region-path.txt').write_text(str(path)); (LAB / 'coverage-hold.txt').write_text(str(hold)); time.sleep(1.3)
    def toggle(on):
        """Creative flight on or off: a double tap of jump, each press long enough for a Minecraft tick to see it."""
        for _ in range(4):
            if bool(play.state()['flags'] & 128) == on: return
            i.tap(JUMP, .11); time.sleep(.11); i.tap(JUMP, .11); time.sleep(.6)
    def fly():
        """Creative flight on (a double tap of jump toggles it), then up, clear of the terrain."""
        toggle(True); assert play.state()['flags'] & 128, 'creative flight did not start'
    def climb(height):
        fly(); start = play.state()['pos'][1]; until = time.monotonic() + 20
        i.key(JUMP, True)
        try:
            while play.state()['pos'][1] - start < height and time.monotonic() < until: time.sleep(.1)
        finally: i.key(JUMP, False)
        time.sleep(.4)
    def legs(keys, leg, yaw):
        def run(seconds, moving):
            end = time.monotonic() + seconds; turn = 0
            while time.monotonic() < end:
                play.aim(yaw + 180 * turn, 0); turn += 1
                for k in keys: i.key(k, True)
                try:
                    time.sleep(.8); moving.set()   # up to speed
                    time.sleep(max(.1, min(leg, end - time.monotonic())))
                finally:
                    moving.clear()
                    for k in reversed(keys): i.key(k, False)
                time.sleep(.5)
        return run
    def glide(seconds, moving):
        """Creative flight off, wings open in the fall, a rocket every 1.5 s; one turn back halfway."""
        play.aim(45, 0)
        toggle(False); assert not play.state()['flags'] & 128, 'creative flight did not stop'   # falling
        i.tap(JUMP, .11); time.sleep(.25)                                      # wings open
        end = time.monotonic() + seconds; half = time.monotonic() + seconds / 2; turned = False
        i.click(3); time.sleep(.6); moving.set()
        while time.monotonic() < end:
            if not turned and time.monotonic() >= half:
                moving.clear(); play.aim(225, -5); turned = True; i.click(3); time.sleep(.6); moving.set(); continue
            i.click(3); time.sleep(1.5)
    try:
        climb(40); report['start'] = play.state()['pos']; save()
        i.command('/item replace entity @s armor.chest with minecraft:elytra'); i.command('/item replace entity @s hotbar.0 with minecraft:firework_rocket 64')
        i.tap(ord('1')); time.sleep(.2)
        for label, path, hold in FLIGHT_MODES:
            mode(path, hold); fly()
            flight_phase(report, f'sprint-flight-{label}', 24, legs([SPRINT, W], 5.2, 45), out); save()
        for label, path, hold in FLIGHT_MODES:
            mode(path, hold); climb(45)
            flight_phase(report, f'elytra-rockets-{label}', 20, glide, out); save()
        report['end'] = play.state()['pos']
        # F5 (third person) used to withdraw control for good (route-59); it is followed now
        # (plan `render` shows it). One press here, control has to stay, then back to first person.
        import common_probe as c
        fly(); before = play.state()['camera_mode']; i.tap(0x74); time.sleep(1.2); play.hold([W], 1.0)
        report['f5'] = {'camera_mode_before': before, 'camera_mode_after': play.state()['camera_mode'], 'control_held': c.latest('native_region_cache').get('lease') == 1}
        for _ in range((3 - play.state()['camera_mode']) % 3): i.tap(0x74); time.sleep(.5)
    finally:
        mode(1, 1); save()


def hall(out, keeper):
    """Planned walk from the clearing to G's surveyed Temple of Time hall."""
    reached = planned_walk(out, keeper, HALL, 1200, 'hall')
    live.save(out / 'hall-reached.json', {'reached': reached, 'target': HALL})


def stop_all(out, owned, client):
    import launch
    for name, value in [('native-stream.txt', '0'), ('E-control.txt', 'release-route'), ('command.txt', 'release-route')]:
        (LAB / name).write_text(value)
    if client:
        current = json.loads((LAB / 'client-process.json').read_text())
        if (current['pid'], current['creation_filetime']) == (client['pid'], client['creation_filetime']):
            with (out / 'client-stop.log').open('w') as log:
                launch.run([sys.executable, '-B', str(ROOT / 'client.py'), 'stop'], cwd=ROOT, stdout=log, stderr=log)
            shutil.copy2(LAB / 'client-cleanup.json', out / 'client-cleanup.json')
    time.sleep(1.1)
    current = json.loads((LAB / 'process.json').read_text())
    if (current['pid'], current['creation_filetime']) != (owned['pid'], owned['creation_filetime']):
        raise RuntimeError('Owned identity file changed; cleanup refused')
    with (out / 'stop.log').open('w') as log:
        launch.run([sys.executable, '-B', str(ROOT / 'session.py'), 'stop'], cwd=ROOT, stdout=log, stderr=log, check=True)


# Plans a human plays, or that test what the foreground does: they keep the window in front on the
# main display and the operating system's input. Every other plan runs in the background (rig.background).
FOREGROUND_PLANS = ('play', 'manual', 'robust')


def run(plan_name, evidence=None, foreground=False):
    import msvcrt, launch, modules
    with (LAB / 'live-check.lock').open('a+b') as lock:
        lock.seek(0); lock.write(b'1'); lock.flush(); lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        query = launch.run(['powershell', '-NoProfile', '-Command', "@(Get-CimInstance Win32_Process -Filter \"Name='Cemu.exe'\").Count"], capture_output=True, text=True, check=True)
        if int(query.stdout.strip()):
            raise RuntimeError('Cemu already running; existing session was left untouched')
        live.previous_archived()
        out = numbered(GAMEPLAY_EVIDENCE if plan_name in GAMEPLAY_PLANS or evidence == 'gameplay' else RENDER_EVIDENCE if plan_name in ('render', 'survey', 'integrate', 'speckle', 'materials', 'camera') or evidence == 'render' else EVIDENCE); owned = client = None
        (LAB / 'background.txt').write_text('0' if plan_name in FOREGROUND_PLANS else '2' if foreground else '1')
        (LAB / 'integrate.txt').write_text('7') # every part of integrate.h; the `integrate` plan switches it
        (LAB / 'follow-states.txt').write_text(FOLLOW_DEFAULT); (LAB / 'damage-event.txt').write_text(DAMAGE_EVENT_DEFAULT); (LAB / 'actors.txt').write_text('1 5000 1'); (LAB / 'damage-to-botw.txt').write_text('3'); (LAB / 'water.txt').write_text('1 256 5000 1')
        (LAB / 'game-camera.txt').write_text('1') # Minecraft's camera in the game's own; the `camera` plan switches it
        # The layer keeps every shader's SPIR-V only in a session that starts with a survey token (frame_survey.h).
        (LAB / 'frame-survey.txt').write_text('keep' if plan_name == 'survey' else '')
        import recycle
        for pattern in ('native-region-slot-*.json', 'frames.jsonl.[12]', 'poses.csv.[12]', 'cache-dump-*.bin', 'control.jsonl', 'actors.jsonl', 'water.jsonl', 'procs.jsonl', 'creates.jsonl'):
            recycle.recycle(*LAB.glob(pattern))
        live.save(out / 'run.json', {'plan': plan_name, 'background': rig_background(), 'started_unix': time.time(), 'anchor': ANCHOR, 'scale': SCALE, 'collision_revision': COLLISION_REVISION})
        keeper = Keeper(out)
        try:
            prepare_profile(out)
            # `play` is the player's own session: their Cemu saves, chosen by them in the game's title menu. No save
            # is restored or loaded for them, and nothing is pressed there.
            own_saves = plan_name == 'play'
            if own_saves: backup_saves(out)
            else: restore_save(out, 'before-run')
            # BRIDGE_START=title (keys): the plan's own words go on from the game's title. BRIDGE_START=newgame (keys):
            # the run itself chooses New Game there, so the game's opening scene is the first thing seen.
            new_game = plan_name == 'keys' and os.environ.get('BRIDGE_START') == 'newgame'
            at_title = own_saves or new_game or (plan_name == 'keys' and os.environ.get('BRIDGE_START') == 'title')
            owned = start_cemu(out, at_title, own_saves)
            live.save(out / 'modules.json', modules.audit())
            if plan_name == 'perf':
                # The comparison starts from BOTW's own frame order; the configured pacing is put back at the end.
                PERF['frame_pace_configured'] = PACE_FILE.read_text().strip() if PACE_FILE.exists() else str(PACE_DEFAULT)
                PACE_FILE.write_text('0'); time.sleep(1.5)
                perf_phase(out, 'bridge-only')
                # Link walked by the native stick (no Minecraft): the game's own cost of moving here.
                def native_walk(seconds):
                    import rig
                    end = time.monotonic() + seconds - 1
                    while time.monotonic() < end: rig.pulse(LAB, 'forward', 900)
                perf_phase(out, 'bridge-only-native-walking', None, 7, during=native_walk)
                (LAB / 'frame-pace.txt').write_text('25'); time.sleep(1.5)
                perf_phase(out, 'bridge-only-native-walking-budget-25', None, 7, during=native_walk)
                perf_phase(out, 'bridge-only-budget-25', None, 8)
                (LAB / 'frame-pace.txt').write_text('0'); time.sleep(1.5)
            client, state = start_client(out, keeper, 0, not at_title)
            live.save(out / 'client-in-world.json', state)
            if plan_name == 'perf': time.sleep(10); perf_phase(out, 'minecraft-in-world', client)
            (LAB / 'native-stream.txt').write_text('1')
            if at_title:
                # No save is loaded yet. F8 here is the wish for Minecraft's control: the bridge claims by itself once
                # the game is ready (physical.h), and a scene of the game's before that is shown with Steve.
                import os_input as title_input, rig as title_rig
                (LAB / 'live-render.txt').write_text('1'); title_rig.configure(LAB, False, False, [0, 0, 0]); title_input.focus(); title_input.tap(F8)
                if new_game:
                    # The title's menu: New Game is under Continue; it asks "Overwrite previous save data?" with Cancel chosen.
                    for seconds, key in ((3, 0x28), (1.5, 0x47), (3, 0x26), (1, 0x47)):
                        time.sleep(seconds); title_input.focus(); title_input.tap(key, .12)
            else: claim(out, keeper, 'initial')
            {'probe': probe, 'explore': explore, 'hall': hall, 'manual': manual, 'restart': restart, 'play': play, 'perf': perf, 'terrain': terrain, 'blocks': blocks, 'fall': fall, 'robust': robust, 'swim': swim, 'hits': hits, 'actors': actor_table, 'weapons': weapons, 'water': water_plan, 'world': world_plan, 'campfire': campfire, 'procs': procs_plan, 'boxwater': boxwater, 'boats': boats_plan, 'airwall': airwall, 'pen': pen, 'things': things, 'swing': swing_sensor, 'axe': swing_axe, 'steve': steve_swing, 'mcwalls': block_walls, 'padkeys': pad_keys, 'keys': keys_plan, 'river': lambda out, keeper: swim(out, keeper, RIVER), 'flight': flight, 'render': render, 'survey': survey, 'integrate': integrate, 'speckle': lambda out, keeper: integrate(out, keeper, 'entities'), 'materials': lambda out, keeper: integrate(out, keeper, 'materials'), 'camera': game_camera}[plan_name](out, keeper)
        except BaseException as error:
            import traceback
            live.save(out / 'error.json', {'error': repr(error), 'traceback': traceback.format_exc()})
        finally:
            release_inputs()
            if owned is None and (LAB / 'process.json').exists():
                candidate = json.loads((LAB / 'process.json').read_text())
                if candidate['started_unix'] >= json.loads((out / 'run.json').read_text())['started_unix']: owned = candidate
            if (LAB / 'client-process.json').exists():
                # The plan may have restarted the client; the newest identity launched by this run is the owned one.
                candidate = json.loads((LAB / 'client-process.json').read_text())
                if candidate['started_unix'] >= json.loads((out / 'run.json').read_text())['started_unix']: client = candidate
            if owned:
                try: stop_all(out, owned, client)
                except BaseException as error: live.save(out / 'cleanup-error.json', {'error': repr(error)})
                keeper.poll()
                live.archive(out, owned)
                try: live.save(out / 'save-after-run.json', {'changed': save_differences()})
                except OSError as error: live.save(out / 'save-after-run.json', {'error': repr(error)})
                for name in ('client-process.json', 'minecraft/console.log', 'minecraft/logs/latest.log'):
                    if (LAB / name).exists(): shutil.copy2(LAB / name, out / pathlib.Path(name).name)
            # Cemu's own settings as they were before the session (session.py), should its stop not have done it.
            try:
                import session
                if session.restore_settings(): live.save(out / 'settings-restored.json', {'by': 'the run'})
            except BaseException as error: live.save(out / 'settings-restore-error.json', {'error': repr(error)})
        result = summarize(out)
        live.compress(out)
        return int(bool(result['run_error']))


def read_events(out, names):
    paths = sorted(out.glob('frames.jsonl.seg*')) + [out / n for n in ('frames.jsonl.2', 'frames.jsonl.1', 'frames.jsonl')]
    seen, found = set(), []
    for path in paths:
        if not path.exists() and path.with_name(path.name + '.gz').exists(): path = path.with_name(path.name + '.gz')
        if not path.exists(): continue
        opener = gzip.open if path.suffix == '.gz' else open
        with opener(path, 'rt', encoding='utf-8', errors='replace') as f:
            for line in f:
                if line in seen or not any(f'"event":"{n}"' in line for n in names): continue
                seen.add(line)
                try: found.append(json.loads(line))
                except ValueError: pass
    return found


def read_poses(out):
    import csv, io
    rows, seen = [], set()
    paths = sorted(out.glob('poses.csv.seg*')) + [out / n for n in ('poses.csv.2', 'poses.csv.1', 'poses.csv')]
    for path in paths:
        if not path.exists() and path.with_name(path.name + '.gz').exists(): path = path.with_name(path.name + '.gz')
        if not path.exists(): continue
        opener = gzip.open if path.suffix == '.gz' else open
        with opener(path, 'rt', encoding='utf-8', errors='replace') as f:
            for r in csv.DictReader(f):
                key = (r.get('seq'), r.get('phase'), r.get('qpc'))
                if key in seen or None in r.values(): continue
                seen.add(key); rows.append(r)
    rows.sort(key=lambda r: int(r['qpc']))
    return rows


def summarize(out, write=True):
    """Ground/air agreement after each driven update, support kinds and release settles."""
    writes = read_events(out, ['live_body_write'])
    posts = {int(r['seq']): r for r in read_poses(out) if r['phase'] == '1'}
    pre = [r for r in read_poses(out) if r['phase'] == '0']
    agree = {}
    kinds = {}
    landing_lag = []
    pending = None
    for w in writes:
        p = posts.get(w['seq'])
        kinds[w.get('support')] = kinds.get(w.get('support'), 0) + 1
        if not p: continue
        mc_ground = bool(w['flags'] & 4); native = int(p['flags']) & 3
        key = f"mc_{'ground' if mc_ground else 'air'}_native_{native}"
        agree[key] = agree.get(key, 0) + 1
        if mc_ground and pending is None and native != 1: pending = w['seq']
        if pending is not None and native == 1: landing_lag.append(w['seq'] - pending); pending = None
    # Release settles: last driven body y versus the native rest 10 updates later.
    settles = []
    for idx, r in enumerate(pre):
        if idx and pre[idx - 1]['mode'] == '5' and r['mode'] == '0':
            later = [x for x in pre[idx:idx + 12]]
            if len(later) >= 11:
                settles.append({'seq': int(r['seq']), 'driven_y': float(pre[idx - 1]['requested_y']) if pre[idx - 1]['requested_y'] != 'nan' else None,
                                'rest_y': float(later[10]['body_y']), 'flags_before': int(pre[idx - 1]['flags']) & 3,
                                'drop': (float(pre[idx - 1]['requested_y']) - float(later[10]['body_y'])) if pre[idx - 1]['requested_y'] != 'nan' else None})
    sessions = read_events(out, ['native_stage', 'native_ready', 'native_coverage_pause', 'native_coverage_resume', 'native_region_reset', 'authority_release', 'physical_claim', 'physical_release'])
    gaps = sorted(w['gap'] for w in writes if w.get('support') == 1)
    # Guest-thread cost of collision reading, and how much of it was reuse.
    probes = read_events(out, ['native_shape_probe']); caches = read_events(out, ['native_region_cache'])
    ms = sorted(p['ms'] for p in probes)
    harvest = {
        'harvests': len(ms), 'ms_total': round(sum(ms), 1),
        'ms_median_p95_max': [round(ms[len(ms) // 2], 3), round(ms[len(ms) * 95 // 100], 3), round(ms[-1], 3)] if ms else None,
        'over_budget': sum(p['budget_exceeded'] for p in probes), 'incomplete': sum(not p['complete'] for p in probes),
        'queries': sum(p['queries'] for p in probes), 'saturated': sum(p['saturated'] for p in probes),
        'reused_roots': sum(p.get('reused_roots', 0) for p in probes), 'queried_roots': sum(p.get('queried_roots', 0) for p in probes),
        'continued': sum(p.get('continued', 0) for p in probes),
        'decoded_roots': sum(p.get('decoded_roots', 0) for p in probes), 'dropped_keys': sum(p.get('dropped_keys', 0) for p in probes), 'native_triangles': sum(p['triangles'] for p in probes),
        'cache': {k: max(c.get(k, 0) for c in caches) for k in ('splits', 'part_restarts', 'reuses', 'full_reads', 'static_stale', 'chunks', 'chunk_restarts', 'decode_ready', 'decode_captures', 'decode_failures', 'decode_audits', 'decode_matched', 'decode_mismatches', 'decode_uses', 'decode_triangles', 'pauses', 'resumes', 'unknown', 'timeouts')} if caches else None,
    }
    result = {
        'route': out.name,
        'run_error': json.loads((out / 'error.json').read_text()) if (out / 'error.json').exists() else None,
        'body_writes': len(writes), 'support_kinds': kinds,
        'gap_native_median_max': [gaps[len(gaps) // 2] * SCALE, gaps[-1] * SCALE] if gaps else None,
        'post_update_ground_air': agree, 'false_air_runs_lag_updates': landing_lag,
        'release_settles': settles, 'harvest': harvest,
        'session_events': [{k: e[k] for k in e if k in ('event', 'reason', 'teleport', 'epoch', 'region', 'pauses', 'resumes')} for e in sessions][-40:],
    }
    if write: live.save(out / 'route-summary.json', result)
    print(f"{out.name}: {'RUN FAILED' if result['run_error'] else 'run complete'} | writes {len(writes)} | support kinds {kinds} | gap native median/max {result['gap_native_median_max']}")
    print(f"post-update MC/native ground-air {agree} | air->ground lags {landing_lag[:20]}")
    print(f"release settles {[(s['flags_before'], round(s['drop'], 6) if s['drop'] is not None else None) for s in settles]}")
    print(f"harvest {harvest}")
    if result['run_error']: print(result['run_error']['error'])
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', nargs='?', choices=['probe', 'explore', 'hall', 'manual', 'restart', 'play', 'perf', 'terrain', 'blocks', 'fall', 'robust', 'swim', 'river', 'hits', 'actors', 'weapons', 'water', 'world', 'campfire', 'procs', 'boxwater', 'boats', 'airwall', 'pen', 'things', 'swing', 'axe', 'steve', 'mcwalls', 'padkeys', 'keys', 'flight', 'render', 'survey', 'integrate', 'speckle', 'materials', 'camera'])
    parser.add_argument('--summary', type=pathlib.Path)
    parser.add_argument('--evidence', choices=['render', 'gameplay'], help='keep this run with the rendering evidence')
    parser.add_argument('--foreground', action='store_true', help='an automatic plan with the window in front and real keyboard and mouse input, still on the secondary display (play, manual and robust are in front on the main display anyway)')
    a = parser.parse_args()
    if a.summary: summarize(a.summary.resolve(), write=False)
    elif a.plan:
        try: sys.exit(run(a.plan, a.evidence, a.foreground))
        except Exception as error: print('native_route FAILED:', error); sys.exit(1)
    else: parser.error('choose a plan or --summary')
