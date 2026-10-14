"""Read-only look into one of the game's actor packs: its files, and the parameters of its physics file.

  python -B actor_pack.py AirWall            files of the pack
  python -B actor_pack.py AirWall physics    the parameter tree of every .bphysics in it
  python -B actor_pack.py AirWall <part>     the parameter tree of the files whose name contains <part>
  python -B actor_pack.py --layers           which collision layers meet which (the boot pack's EntityLayerTable)

A pack is a Yaz0-compressed SARC; a parameter file (AAMP, version 2, little-endian) is lists of
objects of parameters named by the CRC32 of their names. Names this script knows are printed, the
others as their hash. The game's own files are not written to.
"""
import pathlib, struct, sys, zlib

import local, os
# The content folders of the update and of the base game, the update first: local.json "game_content" (a list).
PACKS = [pathlib.Path(os.path.expandvars(folder)) / 'Actor/Pack' for folder in local.SETTINGS.get('game_content', [])]
NAMES = '''param_root RigidBodySet RigidBodySetHeader RigidBody ShapeParam ParamSet RigidBodySetNum set_name type num calc_type
rigid_body_name mass inertia linear_damping angular_damping max_impulse col_impulse_scale ignore_normal_for_impulse volume toi
center_of_mass max_linear_velocity bounding_center bounding_extents max_angular_velocity_rad motion_type contact_point_info
collision_info bone use_ground_friction_scale layer groundhit no_hit_ground no_hit_water friction restitution water_buoyancy_scale
water_flow_effective_rate magne_mass_scaling always_character_mass_scaling shape_num navmesh navmesh_sub_material link_matrix
link_entity_set link_entity_body use_entity_shape shape_type radius convex_radius translate_0 translate_1 rotate_0 vertex_num
material sub_material wall_code floor_code item_code_disable_stick use_ragdoll use_rigid_body_set_num use_character_controller
use_contact_info use_edge_rigid_body_num use_system_group_handler use_support_bone use_cloth Character RigidContactInfo
ragdoll_setup_file_path support_bone_setup_file_path cloth_setup_file_path cloth_header_file_path ParamSetHeader
SupportBone Cloth Form CharacterController Ragdoll EdgeRigidBody file_path scale no_char_standing_on max_impulse
mass_scaling col_impulse_scale buoyancy_scale LinkTarget ActorNameJpn AIProgramUser AIScheduleUser ASUser AttentionUser
AwarenessUser BoneControlUser ActorCaptureUser ChemicalUser DamageParamUser DropTableUser ElinkUser GParamUser LifeConditionUser
LODUser ModelUser PhysicsUser ProfileUser RgBlendWeightUser RgConfigListUser RecipeUser ShopDataUser SlinkUser UMiiUser XlinkUser
AnimationInfo ActorScale PriorityForDebug Tags'''.split()
for n in range(40):
    NAMES += [f'RigidBodySet_{n}', f'RigidBody_{n}', f'ShapeParam_{n}', f'vertex_{n}', f'Form_{n}']
KNOWN = {zlib.crc32(n.encode()): n for n in NAMES}


def yaz0(data):
    if data[:4] != b'Yaz0': return data
    size = struct.unpack('>I', data[4:8])[0]; out = bytearray(); at = 16
    while len(out) < size:
        code = data[at]; at += 1
        for bit in range(8):
            if len(out) >= size: break
            if code & (0x80 >> bit): out.append(data[at]); at += 1
            else:
                pair = (data[at] << 8) | data[at + 1]; at += 2; back = (pair & 0xFFF) + 1; count = pair >> 12
                if not count: count = data[at] + 0x12; at += 1
                else: count += 2
                for _ in range(count): out.append(out[-back])
    return bytes(out)


def sarc(data):
    assert data[:4] == b'SARC'; e = '>' if data[6:8] == b'\xFE\xFF' else '<'
    start = struct.unpack(e + 'I', data[0xC:0x10])[0]; count = struct.unpack(e + 'H', data[0x1A:0x1C])[0]
    nodes = 0x20; names = nodes + count * 0x10 + 8; files = {}
    for n in range(count):
        _, attr, begin, end = struct.unpack(e + '4I', data[nodes + n * 0x10:nodes + n * 0x10 + 0x10])
        at = names + (attr & 0xFFFFFF) * 4; name = data[at:data.index(b'\0', at)].decode()
        files[name] = data[start + begin:start + end]
    return files


def aamp(data, out=print):
    assert data[:4] == b'AAMP' and struct.unpack('<I', data[4:8])[0] == 2
    root = 0x30 + struct.unpack('<I', data[0x14:0x18])[0]
    name = lambda h: KNOWN.get(h, f'#{h:08x}')
    def value(at, kind):
        if kind == 0: return bool(struct.unpack('<I', data[at:at + 4])[0])
        if kind == 1: return round(struct.unpack('<f', data[at:at + 4])[0], 6)
        if kind == 2: return struct.unpack('<i', data[at:at + 4])[0]
        if kind in (3, 4, 5, 6, 16): count = {3: 2, 4: 3}.get(kind, 4); return [round(v, 6) for v in struct.unpack(f'<{count}f', data[at:at + count * 4])]
        if kind in (7, 8, 15, 20): return data[at:data.index(b'\0', at)].decode('utf-8', 'replace')
        if kind == 17: return struct.unpack('<I', data[at:at + 4])[0]
        return f'type {kind}'
    def obj(at, depth):
        h, params, count = struct.unpack('<IHH', data[at:at + 8]); out('  ' * depth + name(h) + ':')
        for n in range(count):
            p = at + params * 4 + n * 8; ph, word = struct.unpack('<II', data[p:p + 8])
            out('  ' * (depth + 1) + f'{name(ph)} = {value(p + (word & 0xFFFFFF) * 4, word >> 24)}')
    def lst(at, depth):
        h, lists, nl, objects, no = struct.unpack('<IHHHH', data[at:at + 12]); out('  ' * depth + name(h) + '/')
        for n in range(no): obj(at + objects * 4 + n * 8, depth + 1)
        for n in range(nl): lst(at + lists * 4 + n * 12, depth + 1)
    lst(root, 0)


def pack(actor):
    for folder in PACKS:
        if (folder / f'{actor}.sbactorpack').exists(): return sarc(yaz0((folder / f'{actor}.sbactorpack').read_bytes()))
    raise SystemExit(f'no pack for {actor}')


LAYERS = ('Object SmallObject GroundObject Player NPC Ragdoll Water AirWall Ground GroundSmooth GroundRough Rope Tree NPC_NoHitPlayer '
          'HitOnlyWater WallForClimb HitOnlyGround QueryCustomReceiver Forbidden18 NoHit MeshVisualizer').split()


def layers():
    """The game's entity layer matrix, a row a layer in the game's own order (the decompilation's ContactLayer); 0: the two do not meet."""
    for n in LAYERS: KNOWN[zlib.crc32(('Entity' + n).encode())] = n
    files = sarc(yaz0((PACKS[0].parents[1] / 'Pack/Bootup.pack').read_bytes())); rows = []; aamp(files['Physics/System/EntityLayerTable.bphyslayer'], rows.append)
    print(' ' * 22 + ' '.join(n[:3] for n in LAYERS), end='')
    for row in rows:
        s = row.strip()
        if s.endswith(':'): print(chr(10) + f'{s[:-1]:22s}', end='')
        elif ' = ' in s: print(s.split(' = ')[1] + '   ', end='')
    print()


if __name__ == '__main__':
    if sys.argv[1:] == ['--layers']: layers(); raise SystemExit
    sys.stdout.reconfigure(errors='replace'); files = pack(sys.argv[1]); part = sys.argv[2] if len(sys.argv) > 2 else None
    for name, body in sorted(files.items()):
        if part is None: print(f'{len(body):8d} {name}'); continue
        if ('.bphysics' in name if part == 'physics' else part in name) and body[:4] == b'AAMP': print(name); aamp(body)
