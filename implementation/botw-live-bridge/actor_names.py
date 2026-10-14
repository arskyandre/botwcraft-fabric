"""The game's own names of its actors, read from the player's copy of the game (nothing of the game's is kept in
the project): its English message files (Bootup_USen.pack, Message/Msg_USen.product.ssarc, ActorType/*.msbt),
label `<actor>_Name`, for example Enemy_Bokoblin_Junior: "Bokoblin". Written once to lab/actor-names.json, which
the launcher hands to Minecraft (-Dbotwcraft.actorNames); the mod names the game's creatures by it, so that its
death message says which one killed the player ("BOTWCraft was slain by Bokoblin").

  python -B actor_names.py           writes the file if it is missing, prints how many names
  python -B actor_names.py --again   writes it anew
"""
import json, os, pathlib, struct, sys

import local
from actor_pack import sarc, yaz0

LAB = pathlib.Path(__file__).resolve().parent / 'lab'
OUT = LAB / 'actor-names.json'


def msbt(data):
    """The labels of a message file and their texts, without the control tags (MSBT, either byte order)."""
    assert data[:8] == b'MsgStdBn', data[:8]
    e = '>' if data[8:10] == b'\xFE\xFF' else '<'; utf = 'utf-16-be' if e == '>' else 'utf-16-le'
    sections = struct.unpack(e + 'H', data[0xE:0x10])[0]; at = 0x20; labels = {}; texts = []
    for _ in range(sections):
        magic = data[at:at + 4]; size = struct.unpack(e + 'I', data[at + 4:at + 8])[0]; body = at + 0x10
        if magic == b'LBL1':
            slots = struct.unpack(e + 'I', data[body:body + 4])[0]
            for s in range(slots):
                count, offset = struct.unpack(e + 'II', data[body + 4 + s * 8:body + 12 + s * 8]); p = body + offset
                for _ in range(count):
                    length = data[p]; name = data[p + 1:p + 1 + length].decode('ascii', 'replace')
                    labels[name] = struct.unpack(e + 'I', data[p + 1 + length:p + 5 + length])[0]; p += 5 + length
        elif magic == b'TXT2':
            count = struct.unpack(e + 'I', data[body:body + 4])[0]
            offsets = [struct.unpack(e + 'I', data[body + 4 + 4 * k:body + 8 + 4 * k])[0] for k in range(count)] + [size]
            for k in range(count):
                raw = data[body + offsets[k]:body + offsets[k + 1]]; out = []; p = 0
                while p + 1 < len(raw):
                    unit = struct.unpack(e + 'H', raw[p:p + 2])[0]
                    if unit == 0: break
                    if unit == 0x0E:   # a control tag: group, type, size of its parameters, the parameters
                        p += 8 + struct.unpack(e + 'H', raw[p + 6:p + 8])[0]; continue
                    if unit == 0x0F:   # the end of a tag that spans text
                        p += 6; continue
                    out.append(raw[p:p + 2]); p += 2
                texts.append(b''.join(out).decode(utf, 'replace'))
        at = body + size; at = (at + 15) & ~15
    return {name: texts[index] for name, index in labels.items() if index < len(texts)}


def read():
    """{actor: name} from the first content folder that has the English messages: local.json "game_content" (the
    update before the game), else the game's own content folder beside its code/U-King.rpx ("game")."""
    folders = list(local.SETTINGS.get('game_content', []))
    if local.SETTINGS.get('game'):
        folders.append(str(pathlib.Path(os.path.expandvars(local.SETTINGS['game'])).parent.parent / 'content'))
    for folder in folders:
        pack = pathlib.Path(os.path.expandvars(folder)) / 'Pack/Bootup_USen.pack'
        if not pack.is_file():
            continue
        messages = sarc(yaz0(sarc(pack.read_bytes())['Message/Msg_USen.product.ssarc']))
        names = {}
        for file, data in messages.items():
            if file.startswith('ActorType/') and file.endswith('.msbt'):
                for label, text in msbt(yaz0(data)).items():
                    if label.endswith('_Name') and text.strip():
                        names[label[:-5]] = ' '.join(text.split())
        return names
    return {}


def ensure(again=False):
    """The file's path, written first if needed; None when the game's messages are not found."""
    if OUT.is_file() and not again:
        return OUT
    try:
        names = read()
    except (OSError, KeyError, AssertionError, struct.error) as error:
        print(f'actor names: not read ({error})', file=sys.stderr); return None
    if not names:
        return None
    LAB.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(names, ensure_ascii=False, sort_keys=True, indent=0), encoding='utf-8')
    return OUT


if __name__ == '__main__':
    path = ensure(again='--again' in sys.argv)
    print(path, len(json.loads(path.read_text(encoding='utf-8'))) if path else 'no names')
