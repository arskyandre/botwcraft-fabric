"""The small fixed resources the bridge loads at start (build/fixture, copied to lab/fixture).

The renderer's first form drew one recorded Minecraft block with a recorded atlas and overlay; the live path
draws Minecraft's own stream instead, and only needs these files to exist and to be well formed. They are made
here from nothing: one unit cube in the stream's vertex layout, a two-tone 16x16 atlas and two empty overlays.
"""
import argparse, json, pathlib, struct

VERTEX = struct.Struct('<5f3I')   # x y z, u v, colour, light, flags: 32 bytes, the stream's layout (wire version 11)
# Face direction code (flags >> 4) and its outward normal; each face is a corner and two edges with e1 x e2 = normal.
FACES = {1: ((0, 0, 0), (1, 0, 0), (0, 0, 1)), 2: ((0, 1, 0), (0, 0, 1), (1, 0, 0)), 3: ((0, 0, 0), (0, 1, 0), (1, 0, 0)),
         4: ((0, 0, 1), (1, 0, 0), (0, 1, 0)), 5: ((0, 0, 0), (0, 0, 1), (0, 1, 0)), 6: ((1, 0, 0), (0, 1, 0), (0, 0, 1))}
NORMALS = {1: (0, -1, 0), 2: (0, 1, 0), 3: (0, 0, -1), 4: (0, 0, 1), 5: (-1, 0, 0), 6: (1, 0, 0)}


def cube():
    rows = []
    for direction, (corner, e1, e2) in FACES.items():
        add = lambda *vectors: tuple(float(sum(v[i] for v in vectors)) for i in range(3))
        corners = [add(corner), add(corner, e1), add(corner, e1, e2), add(corner, e2)]
        uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
        for index in (0, 1, 2, 0, 2, 3):
            rows.append((*corners[index], *uvs[index], 0xFFFFFFFF, 15 << 8, direction << 4 | 1))
    for i in range(0, len(rows), 3):
        a, b, c = rows[i:i + 3]; u = [b[j] - a[j] for j in range(3)]; v = [c[j] - a[j] for j in range(3)]
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        assert normal == NORMALS[(a[7] >> 4) & 7], 'outward triangle winding'
    assert len(rows) == 36
    return rows


def image(width, height, texel):
    return struct.pack('<II', width, height) + b''.join(bytes(texel(x, y)) for y in range(height) for x in range(width))


def prepare(out):
    out.mkdir(parents=True, exist_ok=True)
    rows = cube()
    (out / 'vertices.bin').write_bytes(b''.join(VERTEX.pack(*row) for row in rows))
    images = {'atlas': image(16, 16, lambda x, y: (200, 200, 200, 255) if (x // 4 + y // 4) % 2 else (120, 120, 120, 255)),
              'build': image(16, 9, lambda x, y: (0, 0, 0, 0)), 'inventory': image(16, 9, lambda x, y: (0, 0, 0, 0))}
    for name, data in images.items(): (out / (name + '.rgba')).write_bytes(data)
    result = {'wire_version': 11, 'vertex_stride': VERTEX.size, 'vertices': len(rows), 'source': 'generated (fixture.py)',
              'images': {name: list(struct.unpack_from('<II', data)) for name, data in images.items()}}
    (out / 'fixture.json').write_text(json.dumps(result, indent=2)); print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('output', type=pathlib.Path); prepare(p.parse_args().output)
