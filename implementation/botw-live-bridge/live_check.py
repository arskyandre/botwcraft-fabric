"""One owned Cemu cycle; screenshot checks, short summary, unconditional archive.

python live_check.py --seconds 12
python live_check.py --summary <a run's folder>
Menu references are the tester's own screenshots of the game's screens (lab/references, docs/testing.md).
"""
import argparse
import hashlib
import gzip
import json
import math
import pathlib
import shutil
import sys
import time

import local

ROOT = pathlib.Path(__file__).resolve().parent
LAB = ROOT / 'lab'
EVIDENCE = local.evidence('collision')
# Pictures of the game's own title and of its save list, taken once from the save the automatic runs load
# (lab/references, not in Git: they are the game's pictures). An automatic run finds its way through the menus by them.
REFERENCES = LAB / 'references'
ANCHOR = (-1027.16809, 251.188492, 1812.87195)
TEMPLATES = {
    'title': ('title.png', [(1090, 370, 1215, 399)]),
    'list': ('list.png', [(584, 188, 764, 231)]),
    'confirmation': ('confirmation.png', [(582, 328, 759, 373), (602, 459, 674, 484)]),
}


def save(path, value):
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


def numbered():
    number = max([int(p.name.split('-')[1]) for p in EVIDENCE.glob('session-[0-9]*') if p.name.split('-')[1].isdigit()] or [0]) + 1
    out = EVIDENCE / f'session-{number:02}'
    out.mkdir(parents=True, exist_ok=False)
    return out


def archive(out, identity):
    names = ['process.json', 'cleanup.json', 'native.txt', 'frames.jsonl', 'poses.csv',
             'trace-stats.json', 'process-ledger.jsonl', 'control.jsonl', 'actors.jsonl', 'water.jsonl']
    paths = {LAB / n for n in names}
    try: paths.add(local.cemu_data() / 'log.txt')   # Cemu's own log of the session
    except SystemExit: pass
    for pattern in ('native-region-slot-*.json', 'frames.jsonl.[12]', 'poses.csv.[12]'):
        paths.update(LAB.glob(pattern))
    manifest = []
    for source in sorted(paths):
        if not source.is_file():
            continue
        target = out / source.name
        shutil.copy2(source, target)
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        manifest.append({'file': source.name, 'bytes': target.stat().st_size, 'sha256': digest,
                         'predates_session': source.stat().st_mtime < identity['started_unix']})
    save(out / 'archive.json', {'identity': identity, 'archived_unix': time.time(), 'files': manifest})


def previous_archived():
    """Never overwrite a prior identity's traces, even after an interrupted runner."""
    path = LAB / 'process.json'
    if not path.exists():
        return
    try: identity = json.loads(path.read_text())
    except ValueError:
        # A crash of the machine left the file filled with zeros (2026-10-13), and no run started any more. There is
        # no identity to keep traces for; the file stays under another name.
        import time
        path.rename(path.with_name(f'process-unreadable-{int(time.time())}.json')); return
    for path in EVIDENCE.glob('session-*/archive.json'):
        old = json.loads(path.read_text())['identity']
        if not old: continue
        if (old['pid'], old['creation_filetime']) == (identity['pid'], identity['creation_filetime']):
            return
    out = numbered()
    archive(out, identity)
    save(out / 'recovered.json', {'note': 'Prior process already absent; preserved before next launch.'})


def visual_match(path, phase):
    from PIL import Image, ImageChops, ImageFilter
    reference, boxes = TEMPLATES[phase]
    with Image.open(REFERENCES / reference) as ref, Image.open(path) as actual:
        mapped = None
        if ref.size != actual.size:
            # Another window size (the runs are maximized now): the game picture is fitted 16:9 into
            # the client area, so the capture's picture is scaled onto the reference's.
            try: rects = json.loads(pathlib.Path(path).with_suffix('.json').read_text())['before']['rects']
            except (OSError, ValueError, KeyError): return {'matched': False, 'reason': 'window size differs', 'actual': actual.size}
            wx, wy = rects['window'][:2]; cx0, cy0, cx1, cy1 = rects['client']
            source = picture_box(cx0 - wx, cy0 - wy, cx1 - cx0, cy1 - cy0); target = picture_box(*REFERENCE_CLIENT)
            canvas = Image.new('RGB', ref.size)
            canvas.paste(actual.convert('RGB').crop(source).resize((target[2] - target[0], target[3] - target[1]), Image.LANCZOS), target[:2])
            mapped = {'from': source, 'to': target}; actual = canvas
        scores, covers = [], []
        for box in boxes:
            a = ref.crop(box).convert('L').point(lambda v: 255 if v >= 210 else 0, '1')
            b = actual.crop(box).convert('L').point(lambda v: 255 if v >= 210 else 0, '1')
            intersection = ImageChops.logical_and(a, b).histogram()[255]
            union = ImageChops.logical_or(a, b).histogram()[255]
            scores.append(intersection / union if union >= 30 else 0)
            # The references were taken with the game drawn at 1280x720. Drawn at another size and
            # scaled into the same window, the same text keeps its place but its thin strokes land
            # a pixel off (route-67: 0.62 for the right title screen). Each image's bright pixels
            # must then lie within one pixel of the other's.
            near = lambda image: image.convert('L').filter(ImageFilter.MaxFilter(3)).point(lambda v: 255 if v else 0, '1')
            on = (a.histogram()[255], b.histogram()[255])
            covers.append(min(ImageChops.logical_and(a, near(b)).histogram()[255] / on[0], ImageChops.logical_and(b, near(a)).histogram()[255] / on[1],
                              min(on) / max(on) / .75) if min(on) >= 30 else 0)
    strict = min(scores) >= .80
    return {'matched': strict or min(covers) >= .90, 'strict': strict, 'scores': scores, 'within_one_pixel': covers, 'reference': reference, 'scaled': mapped}


# Client area (x, y, width, height inside the capture) of the 1280x760 window the references show.
REFERENCE_CLIENT = (8, 51, 1264, 701)


def picture_box(x, y, w, h):
    """Where Cemu draws the 16:9 picture inside a client area."""
    pw = min(w, round(h * 16 / 9)); ph = min(h, round(w * 9 / 16))
    return (x + (w - pw) // 2, y + (h - ph) // 2, x + (w - pw) // 2 + pw, y + (h - ph) // 2 + ph)


def wait_visual(out, phase, seconds=45):
    import rig
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        rig.capture(LAB, out / f'{phase}.png')  # Includes the requested click WAV.
        match = visual_match(out / f'{phase}.png', phase)
        save(out / f'{phase}-match.json', match)
        if match['matched']:
            return
        time.sleep(2)
    raise RuntimeError(f'{phase} screen not recognized; see {phase}.png')


def compress(out):
    """Lossless gzip of bulky traces; originals are recycled after a verified roundtrip."""
    import recycle
    manifest = json.loads((out / 'compression.json').read_text())['files'] if (out / 'compression.json').exists() else []
    for path in sorted(out.iterdir()):
        if not path.is_file() or not (path.name.startswith(('frames.jsonl', 'poses.csv')) or path.name == 'loaded.json') or path.suffix == '.gz':
            continue
        original = path.read_bytes();target = path.with_name(path.name + '.gz')
        if target.exists(): raise RuntimeError(f'{target} already exists')
        packed = gzip.compress(original, compresslevel=6, mtime=0);target.write_bytes(packed)
        if hashlib.sha256(gzip.decompress(target.read_bytes())).hexdigest() != hashlib.sha256(original).hexdigest():
            raise RuntimeError(f'gzip roundtrip failed for {path}')
        manifest.append({'file': path.name, 'original_bytes': len(original), 'original_sha256': hashlib.sha256(original).hexdigest(),
                         'gzip_bytes': len(packed), 'gzip_sha256': hashlib.sha256(packed).hexdigest()})
        recycle.recycle(path)
    save(out / 'compression.json', {'codec': 'lossless gzip mtime0', 'files': manifest, 'roundtrip_verified': True})


def summarize(out, write=True):
    stale = {r['file'] for r in json.loads((out / 'archive.json').read_text()).get('files', []) if r.get('predates_session')}
    slots = [json.loads(p.read_text()) for p in sorted(out.glob('native-region-slot-*.json')) if p.name not in stale]
    events = []
    for name in ('frames.jsonl.2', 'frames.jsonl.1', 'frames.jsonl'):
        path = out / name
        opener = open
        if not path.exists() and path.with_name(path.name+'.gz').exists():
            path = path.with_name(path.name+'.gz');opener = gzip.open
        if path.exists():
            with opener(path, 'rt', encoding='utf-8', errors='replace') as file:
                for line in file:
                    if 'native_region_cache' in line or 'native_shape_probe' in line or 'ground_check' in line:
                        try: events.append(json.loads(line))
                        except ValueError: pass  # Incomplete last line on termination.
    harvests = [e for e in events if e.get('event') == 'native_shape_probe']
    grounds = [e for e in events if e.get('event') == 'ground_check']
    # A miss has no exported surface: zero normal (older builds wrote NaN).
    diffs = sorted(abs(e['diff_native']) for e in grounds if isinstance(e.get('diff_native'), (int, float)) and math.isfinite(e['diff_native']) and any(e.get('normal', [0, 0, 0])))
    caches = [e for e in events if e.get('event') == 'native_region_cache']
    trace_path = out / 'trace-stats.json'
    traces = json.loads(trace_path.read_text()) if trace_path.exists() and trace_path.name not in stale else {}
    inventories = [s.get('inventory', {}) for s in slots]
    failures = sorted({(i.get('failure', -1), i.get('failure_address', 0)) for i in inventories if not i.get('complete')})
    result = {
        'session': out.name, 'run_error': json.loads((out / 'error.json').read_text()) if (out / 'error.json').exists() else None,
        'inventory_complete': bool(inventories) and all(i.get('complete', False) for i in inventories),
        'inventory_failures': [{'code': c, 'address': hex(a)} for c, a in failures],
        'bodies_fixed_active_inactive': sorted({tuple(i.get('bodies', [])) for i in inventories}),
        'unsupported_per_region': [i.get('unsupported') for i in inventories],
        'triangles_per_region': [len(s.get('triangles', [])) for s in slots],
        'terrain_available_per_region': [s.get('terrain_grid', {}).get('available', 0) for s in slots],
        'harvests_in_retained_trace': len(harvests),
        'budget_overruns': sum(bool(e.get('budget_exceeded')) for e in harvests),
        'complete_harvests': sum(bool(e.get('complete')) for e in harvests),
        'incomplete_causes': {k: sum(bool(e.get(k)) for e in harvests if not e.get('complete')) for k in
                              ('budget_exceeded', 'unknown_leaves', 'unsupported', 'unboundable', 'key_faults')},
        'ground_checks': {'count': len(grounds), 'no_exported_surface': len(grounds) - len(diffs),
                          'abs_diff_native_median': diffs[len(diffs)//2] if diffs else None, 'abs_diff_native_max': diffs[-1] if diffs else None,
                          'over_0_01': sum(d > .01 for d in diffs), 'non_flat': sum(1 for e in grounds if abs(e.get('normal', [0, 1, 0])[1]) < .995)},
        'fallback_boxes': sum(e.get('fallbacks', 0) + e.get('leaf_boxes', 0) for e in harvests),
        'queries_saturated_total': [sum(e.get('saturated', 0) for e in harvests), sum(e.get('queries', 0) for e in harvests)],
        'max_keys': max((e.get('max_keys', 0) for e in harvests), default=None),
        'key_faults': sum(e.get('key_faults', 0) for e in harvests),
        'harvest_ms_median_max': [sorted(m)[len(m)//2], max(m)] if (m := [e['ms'] for e in harvests if 'ms' in e]) else None,
        'saturated_total_per_region': [[s.get('saturated_queries'), s.get('queries')] for s in slots],
        'cache': caches[-1] if caches else None, 'trace': traces,
        'region_ids': [s['id'] for s in slots],
        'note': 'Per-region arrays follow region_ids; incomplete source is never accepted for gameplay.',
    }
    if write:  # Re-summarizing an archive never rewrites its preserved summary.
        save(out / 'summary.json', result)
    inv = 'complete' if result['inventory_complete'] else 'FAILED ' + str(result['inventory_failures'])
    cache = result['cache'] or {}
    print(f"{out.name}: {'RUN FAILED' if result['run_error'] else 'cycle complete'} | inventory {inv} | bodies F/A/I {result['bodies_fixed_active_inactive']}")
    print(f"unsupported {result['unsupported_per_region']} | triangles {result['triangles_per_region']} | terrain {result['terrain_available_per_region']} | overruns {result['budget_overruns']}/{len(harvests)} | cache {cache.get('entries', '?')} | clear/wipe collision={traces.get('collision_clears', '?')}, render={traces.get('render_clears', '?')}, cache={traces.get('cache_wipes', '?')} | rotations F/P={traces.get('frame_rotations', '?')}/{traces.get('pose_rotations', '?')}, drops={traces.get('write_drops', '?')}")
    print(f"complete harvests {result['complete_harvests']}/{len(harvests)} | incomplete causes {result['incomplete_causes']} | fallback boxes {result['fallback_boxes']}")
    print(f"ground checks {result['ground_checks']}")
    print(f"queries saturated/total {result['queries_saturated_total']} | max keys {result['max_keys']} | key faults {result['key_faults']} | harvest ms median/max {result['harvest_ms_median_max']} | per region {result['saturated_total_per_region']}")
    if result['run_error']:
        print(result['run_error']['error'])
    return result


def run(seconds):
    import launch, session, rig, combined, modules, msvcrt
    # Serialize this runner; do not adopt or terminate any pre-existing Cemu.
    with (LAB / 'live-check.lock').open('a+b') as lock:
        lock.seek(0);lock.write(b'1');lock.flush();lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        query = launch.run(['powershell', '-NoProfile', '-Command', "@(Get-CimInstance Win32_Process -Filter \"Name='Cemu.exe'\").Count"], capture_output=True, text=True, check=True)
        if int(query.stdout.strip()):
            raise RuntimeError('Cemu already running; existing session was left untouched')
        previous_archived()
        out = numbered();owned = None
        # Already archived above. Remove only stale rolling outputs in this lab.
        import recycle  # User rule: recycle bin, never direct deletion.
        for pattern in ('native-region-slot-*.json', 'frames.jsonl.[12]', 'poses.csv.[12]'):
            recycle.recycle(*LAB.glob(pattern))
        save(out / 'run.json', {'seconds': seconds, 'expected_feet': ANCHOR, 'started_unix': time.time()})
        try:
            with (out / 'start.log').open('w') as log:
                launch.run([sys.executable, '-B', str(ROOT / 'session.py'), 'start', '--camera-probe'], cwd=ROOT, stdout=log, stderr=log, check=True)
            owned = json.loads((LAB / 'process.json').read_text())
            (LAB / 'observer.txt').write_text('1')  # Pose evidence; bounded by native rotation.
            time.sleep(8)
            with (out / 'resize.log').open('w') as log:
                launch.run([sys.executable, '-B', str(ROOT / 'rig.py'), 'maximize'], cwd=ROOT, stdout=log, stderr=log, check=True)
            wait_visual(out, 'title')
            rig.pulse(LAB, 'interact');time.sleep(2)
            wait_visual(out, 'list')
            rig.pulse(LAB, 'interact');time.sleep(2)
            wait_visual(out, 'confirmation')
            rig.pulse(LAB, 'interact')
            until = time.monotonic() + 70
            while time.monotonic() < until:
                rig.window(LAB)
                try:
                    row = combined.latest()
                    if max(abs(float(row['body_' + axis]) - v) for axis, v in zip('xyz', ANCHOR)) < .03:
                        break
                except (OSError, StopIteration, ValueError, KeyError): pass
                time.sleep(.5)
            else: raise RuntimeError('Loaded checkpoint did not reach the expected feet position')
            save(out / 'loaded-state.json', row)
            rig.capture(LAB, out / 'loaded.png')
            save(out / 'modules.json', modules.audit())
            (LAB / 'native-stream.txt').write_text('1')
            until = time.monotonic() + seconds
            while time.monotonic() < until:
                rig.window(LAB);time.sleep(min(1, max(0, until-time.monotonic())))
            (LAB / 'native-stream.txt').write_text('0')
            time.sleep(1.1)  # Flush the final counters before owned shutdown.
            if not list(LAB.glob('native-region-slot-*.json')):
                raise RuntimeError('No native acquisition snapshot was produced')
        except BaseException as error:
            save(out / 'error.json', {'error': repr(error)})
        finally:
            # session.py records identity before injection, including startup failure.
            path = LAB / 'process.json'
            if owned is None and path.exists():
                candidate = json.loads(path.read_text())
                if candidate['started_unix'] >= json.loads((out / 'run.json').read_text())['started_unix']:
                    owned = candidate
            if owned:
                for name, value in [('native-stream.txt', '0'), ('E-control.txt', 'release-live-check'), ('command.txt', 'release-live-check')]:
                    (LAB / name).write_text(value)
                with (out / 'stop.log').open('w') as log:
                    try:
                        current = json.loads((LAB / 'process.json').read_text())
                        if (current['pid'],current['creation_filetime']) != (owned['pid'],owned['creation_filetime']):
                            raise RuntimeError('Owned identity file changed; cleanup refused')
                        launch.run([sys.executable, '-B', str(ROOT / 'session.py'), 'stop'], cwd=ROOT, stdout=log, stderr=log, check=True)
                        cleanup = json.loads((LAB / 'cleanup.json').read_text())
                        if not (cleanup.get('already_exited') or (cleanup.get('stopped') and cleanup.get('wait') == 0)):
                            raise RuntimeError('Owned Cemu shutdown did not complete')
                    except BaseException as error: save(out / 'error.json', {'error': 'Cleanup: '+repr(error)})
                    finally: archive(out, owned)
            else:
                save(out / 'archive.json', {'identity': None, 'files': [], 'note': 'No new Cemu identity was created.'})
        result = summarize(out)
        compress(out)
        return int(result['run_error'] is not None)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=12)
    parser.add_argument('--summary', type=pathlib.Path)
    parser.add_argument('--compress', type=pathlib.Path, help='gzip an archived session directory')
    args = parser.parse_args()
    if args.compress:
        compress(args.compress.resolve())
    elif args.summary:
        summarize(args.summary.resolve(), write=False)
    else:
        if not .1 <= args.seconds <= 300: parser.error('--seconds must be between .1 and 300')
        try: sys.exit(run(args.seconds))
        except Exception as error: print('live_check FAILED:', error);sys.exit(1)
