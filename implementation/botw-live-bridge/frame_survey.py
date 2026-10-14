"""Reads one frame survey written by the layer (frame_survey.h) and makes it readable.

python -B frame_survey.py FOLDER      # FOLDER holds frame.jsonl, snaps.bin, uniforms.bin

Writes into FOLDER: passes.md (every render pass in order: targets, draws,
state, what it samples), targets.md (every image and the passes that write and
read it), snaps/NNN-*.png (the reduced picture of each target after each
pass) and sheet-NN.jpg (contact sheets of those pictures in frame order).
Nothing is interpreted here: the tables say what the layer saw.
"""
import json, pathlib, struct, sys
import numpy as np
from PIL import Image, ImageDraw

FORMATS = {0: 'UNDEFINED', 2: 'R4G4B4A4', 4: 'R5G6B5', 6: 'R5G5B5A1', 9: 'R8', 10: 'R8_SNORM', 13: 'R8_UINT', 16: 'R8G8', 17: 'R8G8_SNORM', 20: 'R8G8_UINT', 37: 'R8G8B8A8', 38: 'R8G8B8A8_SNORM',
           41: 'R8G8B8A8_UINT', 43: 'R8G8B8A8_SRGB', 44: 'B8G8R8A8', 50: 'B8G8R8A8_SRGB', 64: 'A2B10G10R10', 68: 'A2B10G10R10_UINT', 70: 'R16', 71: 'R16_SNORM', 74: 'R16_UINT', 76: 'R16F', 77: 'R16G16', 78: 'R16G16_SNORM',
           81: 'R16G16_UINT', 83: 'R16G16F', 91: 'R16G16B16A16', 92: 'R16G16B16A16_SNORM', 95: 'R16G16B16A16_UINT', 97: 'R16G16B16A16F', 98: 'R32_UINT', 100: 'R32F', 101: 'R32G32_UINT', 103: 'R32G32F',
           107: 'R32G32B32A32_UINT', 109: 'R32G32B32A32F', 122: 'B10G11R11F', 123: 'E5B9G9R9', 124: 'D16', 125: 'X8_D24', 126: 'D32F', 127: 'S8', 128: 'D16_S8', 129: 'D24_S8', 130: 'D32F_S8',
           131: 'BC1_RGB', 133: 'BC1_RGBA', 135: 'BC2', 137: 'BC3', 139: 'BC4', 141: 'BC5'}
COMPARE = ['never', 'less', 'equal', 'lequal', 'greater', 'notequal', 'gequal', 'always']
FACTOR = ['0', '1', 'src', '1-src', 'dst', '1-dst', 'srcA', '1-srcA', 'dstA', '1-dstA', 'const', '1-const', 'constA', '1-constA', 'srcAsat']
CULL = ['none', 'front', 'back', 'both']


def fmt(code): return FORMATS.get(code, f'format{code}')


def load(folder):
    rows = [json.loads(line) for line in (folder / 'frame.jsonl').read_text().splitlines() if line.strip()]
    by = {}
    for row in rows: by.setdefault(row['t'], []).append(row)
    return rows, by


def picture(snap, raw):
    """(image, note) for one snap: colours clamped to 0..1 (a brighter target is scaled to its 99.5th percentile), depth stretched over its range."""
    w, h, data = snap['w'], snap['h'], raw[snap['offset']:snap['offset'] + snap['bytes']]
    source = snap['source'][2]
    if snap['format'] == 97:
        a = np.frombuffer(data, dtype=np.float16).astype(np.float32).reshape(h, w, 4)
        rgb = np.nan_to_num(a[..., :3], nan=0, posinf=65504, neginf=0); alpha = np.nan_to_num(a[..., 3])
        top = float(np.percentile(rgb, 99.5)) if rgb.size else 0; low = float(rgb.min()) if rgb.size else 0
        scale = top if top > 1.05 else 1.0
        if low < -.01: shown = np.clip(rgb * .5 + .5, 0, 1); note = f'signed, shown as x/2+1/2 (min {low:.3g}, p99.5 {top:.3g})'
        else: shown = np.clip(rgb / scale, 0, 1); note = f'max {float(rgb.max()):.4g}' + (f', shown divided by {scale:.3g}' if scale != 1 else '')
        note += f'; alpha {float(alpha.min()):.3g}..{float(alpha.max()):.3g}'
        image = Image.fromarray((shown * 255 + .5).astype(np.uint8), 'RGB')
        if float(alpha.max()) - float(alpha.min()) > .02:
            # Alpha often carries a separate quantity (a mask, a material value): shown beside the colour.
            both = Image.new('RGB', (w * 2, h)); both.paste(image, (0, 0))
            both.paste(Image.fromarray((np.clip(alpha, 0, 1) * 255 + .5).astype(np.uint8), 'L').convert('RGB'), (w, 0)); image = both; note += ' (right half: alpha)'
        return image, note
    if source == 124: d = np.frombuffer(data, dtype=np.uint16).astype(np.float32).reshape(h, w) / 65535
    elif source in (125, 129): d = (np.frombuffer(data, dtype=np.uint32).reshape(h, w) & 0xFFFFFF).astype(np.float32) / 0xFFFFFF
    else: d = np.frombuffer(data, dtype=np.float32).reshape(h, w)
    lo, hi = float(d.min()), float(d.max()); drawn = d[d < hi] if hi > lo else d
    near = float(np.percentile(drawn, 1)) if drawn.size else lo
    shown = np.clip((d - near) / max(hi - near, 1e-12), 0, 1) ** 4   # depth crowds near 1: the power spreads it
    image = Image.fromarray((shown * 255 + .5).astype(np.uint8), 'L').convert('RGB')
    if max(w, h) > 480: image.thumbnail((480, 480))
    return image, f'depth {lo:.6g}..{hi:.6g} (shown from {near:.6g}, power 4)'


def decode(folder):
    folder = pathlib.Path(folder); rows, by = load(folder)
    raw = (folder / 'snaps.bin').read_bytes() if (folder / 'snaps.bin').exists() else b''
    names = {}
    def name(image):
        if not image: return '-'
        if image not in names: names[image] = f'T{len(names) + 1:02}'
        return names[image]
    passes = {p['i']: dict(p, draws=[], reads={}, pipes={}) for p in by.get('pass', [])}
    for end in by.get('pass_end', []): passes[end['i']].update(draw_count=end['draws'], vertices=end['vertices'])
    info = {}
    for p in passes.values():
        for a in p['attachments']:
            name(a['image'])
            if 'format' in a: info[a['image']] = a
    pipes = {p['handle']: p for p in by.get('pipeline', [])}
    for d in by.get('draw', []):
        p = passes[d['p']]; p['draws'].append(d); p['pipes'][d['pipe']] = p['pipes'].get(d['pipe'], 0) + 1
        for s in d['sets']:
            for binding, image, mip, layer in s['tex']: p['reads'].setdefault((s['set'], binding, image, mip, layer), 0); p['reads'][(s['set'], binding, image, mip, layer)] += 1
    snaps = {}
    for s in by.get('snap', []): snaps.setdefault(s['pass'], []).append(s)
    out = folder / 'snaps'; out.mkdir(exist_ok=True)
    notes = {}; cells = []
    for s in by.get('snap', []):
        image, note = picture(s, raw); a = passes[s['pass']]['attachments'][s['slot']]
        label = f"{s['id']:03}-p{s['pass']:03}-{name(s['image'])}-{fmt(s['source'][2])}-{s['source'][0]}x{s['source'][1]}" + (f"-m{s['mip']}" if s['mip'] else '') + (f"-l{s['layer']}" if s['layer'] else '')
        image.save(out / f'{label}.png'); notes[s['id']] = note; cells.append((label, image, passes[s['pass']].get('draw_count', 0)))
    # Contact sheets: 5 x 4 cells of 384 x 232 in frame order.
    for n in range(0, len(cells), 20):
        sheet = Image.new('RGB', (5 * 388, 4 * 250), (24, 24, 24)); draw = ImageDraw.Draw(sheet)
        for k, (label, image, draws) in enumerate(cells[n:n + 20]):
            x, y = (k % 5) * 388, (k // 5) * 250; small = image.copy(); small.thumbnail((384, 216)); sheet.paste(small, (x + 2, y + 16))
            draw.text((x + 3, y + 2), f'{label}  d{draws}', fill=(255, 255, 255))
        sheet.save(folder / f'sheet-{n // 20:02}.jpg', quality=85)
    # passes.md
    lines = ['# Render passes in frame order', '',
             'Targets are named in order of first appearance; `cN` is the colour output a target is bound to. A draw state line is',
             '`count x [depth test/write compare | cull | blend per colour target | vs fs]`.', '']
    writers, readers = {}, {}
    events = sorted([(e.get('after_pass', -1), e) for t in ('copy', 'blit', 'upload', 'readback', 'clear', 'bridge_marker', 'submit', 'present') for e in by.get(t, [])], key=lambda x: x[0])
    after = {}
    for at, e in events: after.setdefault(at, []).append(e)
    def note_events(index):
        for e in after.get(index, []):
            if e['t'] in ('copy', 'blit'): lines.append(f"- {e['t']}: {name(e['from'])} (mip {e['from_mip']}, layer {e['from_layer']}) -> {name(e['to'])} (mip {e['to_mip']}, layer {e['to_layer']})"); writers.setdefault(e['to'], []).append(f'{e["t"]} after p{index}'); readers.setdefault(e['from'], set()).add(f'{e["t"]} after p{index}')
            elif e['t'] == 'clear': lines.append(f"- clear ({e['kind']}): {name(e['image'])} colour {e['color']} depth {e['depth']}")
            elif e['t'] == 'bridge_marker': lines.append(f"- **bridge marker** (where Minecraft is drawn today): colour {name(e['color'])}, depth {name(e['depth'])}, result `{e['result']}`, near {e['near']:.4g} far {e['far']:.6g}")
            elif e['t'] == 'present': lines.append('- present (second picture of the same frame)')
        if after.get(index): lines.append('')
    note_events(-1)
    for index in sorted(passes):
        p = passes[index]
        slots = p.get('colour_slots', [])
        def role(k): return 'depth' if p.get('depth_slot') == k else (f'c{slots.index(k)}' if k in slots else 'unused')
        targets = ', '.join(f"{role(k)}={name(a['image'])} {fmt(a.get('format', 0))} {a.get('w', '?')}x{a.get('h', '?')}" + (f" mip {a['mip']}" if a.get('mip') else '') + (f" layer {a['layer']}/{a['layers']}" if a.get('layers', 1) > 1 else '') for k, a in enumerate(p['attachments']))
        lines.append(f"## p{index:03} — {p.get('draw_count', 0)} draws, {p.get('vertices', 0)} vertices"); lines.append(''); lines.append(f'Targets: {targets}'); lines.append('')
        for a in p['attachments']:
            if p.get('draw_count', 0): writers.setdefault(a['image'], []).append(f'p{index:03}')
        states = {}
        for handle, count in p['pipes'].items():
            pipe = pipes.get(handle, {})
            if 'vs' not in pipe: key = 'unknown pipeline'
            else:
                blend = ' '.join(('off' if not b[0] else f'{FACTOR[b[1]]},{FACTOR[b[2]]}/{FACTOR[b[4]]},{FACTOR[b[5]]}') + (f' mask{b[7]:x}' if b[7] != 15 else '') for b in pipe['blend']) or '-'
                key = f"depth {'T' if pipe['depth_test'] else '-'}{'W' if pipe['depth_write'] else '-'} {COMPARE[pipe['compare']]}{' stencil' if pipe['stencil'] else ''}{' bias' if pipe['bias'] else ''} | cull {CULL[pipe['cull']]} | {blend}"
                key = (key, pipe['vs'][:8], pipe['fs'][:8])
            states.setdefault(key if isinstance(key, str) else key[0], []).append((count, key if isinstance(key, str) else f'{key[1]} {key[2]}'))
        for state, users in sorted(states.items(), key=lambda kv: -sum(c for c, _ in kv[1])):
            total = sum(c for c, _ in users); shaders = sorted(users, key=lambda u: -u[0])
            lines.append(f"- {total} x [{state}] shaders (vs fs x draws): " + ', '.join(f'{s} x{c}' for c, s in shaders[:6]) + (f' … {len(shaders) - 6} more' if len(shaders) > 6 else ''))
        reads = {}
        for (set_index, binding, image, mip, layer), count in p['reads'].items():
            if image in info or image in names: reads.setdefault(image, []).append((set_index, binding, mip, layer, count))
        targets_read = [f"{name(image)}" + ''.join(f" (set {s} binding {b}{', mip ' + str(m) if m else ''}{', layer ' + str(l) if l else ''}, {c} draws)" for s, b, m, l, c in uses[:3]) for image, uses in reads.items() if image in info]
        if targets_read: lines.append('- samples render targets: ' + '; '.join(targets_read))
        for image in reads:
            if image in info: readers.setdefault(image, set()).add(f'p{index:03}')
        for s in snaps.get(index, []): lines.append(f"- snap {s['id']:03} {name(s['image'])}: {notes[s['id']]}")
        lines.append(''); note_events(index)
    (folder / 'passes.md').write_text('\n'.join(lines), encoding='utf-8')
    lines = ['# Render targets', '', '| Target | Format | Size | Mips | Layers | Written by | Sampled by |', '|---|---|---|---|---|---|---|']
    def squeeze(items):
        items = list(items); return ', '.join(items[:14]) + (f' … ({len(items)})' if len(items) > 14 else '')
    for image, alias in names.items():
        a = info.get(image, {})
        lines.append(f"| {alias} | {fmt(a.get('format', 0))} | {a.get('w', '?')}x{a.get('h', '?')} | {a.get('mips', '?')} | {a.get('layers', '?')} | {squeeze(writers.get(image, []))} | {squeeze(sorted(readers.get(image, [])))} |")
    (folder / 'targets.md').write_text('\n'.join(lines), encoding='utf-8')
    end = by.get('end', [{}])[0]
    return {'passes': len(passes), 'draws': len(by.get('draw', [])), 'snaps': len(by.get('snap', [])), 'skipped_snaps': len(by.get('snap_skipped', [])), 'targets': len(names),
            'pipelines': len(pipes), 'uniform_blobs': len(by.get('uniform', [])), 'marker': [(m['after_pass'], m['result']) for m in by.get('bridge_marker', [])], 'end': end}


def sequence(folder):
    """sequence.md: the frame as runs of passes with the same targets (GX2 colour slot = format:size[layer])."""
    folder = pathlib.Path(folder); rows, by = load(folder); draws = {}
    for d in by.get('draw', []): draws[d['p']] = draws.get(d['p'], 0) + 1
    def signature(p):
        out = []; att = p['attachments']
        def one(a): return f"{fmt(a.get('format', 0))}:{a.get('w', '?')}x{a.get('h', '?')}" + (f"[{a.get('layer')}]" if a.get('layers', 1) > 1 else '')
        for slot, index in enumerate(p['colour_slots']):
            if 0 <= index < len(att): out.append(f'c{slot}={one(att[index])}')
        if 0 <= p['depth_slot'] < len(att): out.append(f"depth={one(att[p['depth_slot']])}")
        return ' '.join(out) or '(no attachment known)'
    lines = ['# Pass sequence', '', '| Passes | Count | Draws | Targets |', '|---|---|---|---|']; run = None
    for p in by.get('pass', []):
        s = signature(p)
        if run and run[0] == s: run[2] = p['i']; run[3] += draws.get(p['i'], 0); run[4] += 1
        else:
            if run: lines.append(f'| p{run[1]:03d}–p{run[2]:03d} | {run[4]} | {run[3]} | {run[0]} |')
            run = [s, p['i'], p['i'], draws.get(p['i'], 0), 1]
    if run: lines.append(f'| p{run[1]:03d}–p{run[2]:03d} | {run[4]} | {run[3]} | {run[0]} |')
    for m in by.get('bridge_marker', []): lines.append(f"\nBridge marker after p{m['after_pass']:03d}: {m['result']}.")
    (folder / 'sequence.md').write_text('\n'.join(lines), encoding='utf-8'); return len(lines) - 4


def lights(folder):
    """lights.md: the light matrices of the shadow cascades and the sun of the deferred lighting, from uniform data.

    Observed layout (report 44): in every shadow-caster vertex shader the last
    112 bytes of the uniform block are the light's view (3x4, rotation only)
    and the cascade's projection (4x4); the lighting shaders hold a vec4 with
    minus the light's direction in view space, whose w is the red of the sun's
    colour vec4.
    """
    folder = pathlib.Path(folder); rows, by = load(folder); blob = (folder / 'uniforms.bin').read_bytes()
    uniforms = {u['id']: u for u in by.get('uniform', [])}; passes = {p['i']: p for p in by.get('pass', [])}; pipes = {p['handle']: p for p in by.get('pipeline', [])}
    def floats(i):
        u = uniforms[i]; b = blob[u['offset']:u['offset'] + u['bytes']]; return np.frombuffer(b[:len(b) // 4 * 4], dtype='<f4')
    np.set_printoptions(suppress=True, linewidth=200, precision=5); lines = ['# Lights', '']
    marker = next((m for m in by.get('bridge_marker', []) if m['result'] == 'draw'), None) or (by.get('bridge_marker') or [None])[0]
    cascades = {}
    for d in by.get('draw', []):
        a = passes[d['p']]['attachments']
        if len(a) != 1 or a[0].get('format') != 124 or a[0].get('layers', 1) < 2: continue
        block = next((u for s in d['sets'] if s['set'] == 0 for u in s['ubo'] if u[1] >= 0), None)
        if not block: continue
        tail = floats(block[1])[-28:]
        if len(tail) < 28: continue
        rotation = tail[:12].reshape(3, 4)[:, :3]
        if not np.allclose(rotation @ rotation.T, np.eye(3), atol=2e-3): continue
        entry = cascades.setdefault((a[0]['image'], a[0]['layer'], a[0]['w'], a[0]['h']), {}); key = tuple(np.round(tail, 5)); entry[key] = entry.get(key, 0) + 1
    light = None
    for (image, layer, w, h), tails in sorted(cascades.items(), key=lambda kv: kv[0][1]):
        tail, count = max(tails.items(), key=lambda kv: kv[1]); tail = np.array(tail); light = tail[8:11]
        lines += [f'## Shadow map {w}x{h}, layer {layer}: {count} of {sum(tails.values())} caster draws agree', '', 'Light view (3x4):', '```', str(tail[:12].reshape(3, 4)), '```', 'Projection (4x4):', '```', str(tail[12:].reshape(4, 4)), '```', '']
    if light is not None and marker:
        view = np.array(marker['view']).reshape(3, 4)[:, :3]; towards = view @ light; seen = set()
        lines += [f'Direction to the light, world: {light}; in the view space of the bridge camera: {towards}.', '', '## Deferred lighting uniforms', '']
        for d in by.get('draw', []):
            if len(passes[d['p']]['attachments']) < 5: continue
            fs = pipes.get(d['pipe'], {}).get('fs', '?')[:8]
            for s in d['sets']:
                for u in s['ubo']:
                    if s['set'] != 1 or u[1] < 0 or (fs, u[3]) in seen: continue
                    seen.add((fs, u[3])); v = floats(u[1]); q = v[:len(v) // 4 * 4].reshape(-1, 4)
                    for i, row in enumerate(q):
                        if not np.allclose(row[:3], -towards, atol=2e-3): continue
                        colour = [(j, c) for j, c in enumerate(q) if j != i and abs(c[0] - row[3]) < 1e-4]
                        lines.append(f"- p{d['p']:03d}, fragment shader {fs}, {u[3]} bytes: minus the light direction (view space) at vec4 {i} = {row}; vec4 beginning with its w: " + ', '.join(f'{j} = {c}' for j, c in colour))
    (folder / 'lights.md').write_text('\n'.join(lines), encoding='utf-8'); return len(cascades)


if __name__ == '__main__':
    what = sys.argv[2] if len(sys.argv) > 2 else 'all'
    if what in ('all', 'decode'): print(json.dumps(decode(sys.argv[1]), indent=1))
    if what in ('all', 'sequence'): print('sequence rows', sequence(sys.argv[1]))
    if what in ('all', 'lights'): print('cascades', lights(sys.argv[1]))
