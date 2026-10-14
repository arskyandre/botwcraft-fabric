"""Find test spots in a cache dump of exported native triangles (Minecraft space).

edge:  walkable ground that ends in a drop, with a level approach to it;
slope: a face steeper than SkyCraft's 45 degree limit but inside the current
       walkable limit, with walkable ground at its foot.
Used by native_route.py (plan `terrain`) to check crouching at native edges
and walking up steep ground without a human; also runs offline on a dump.
"""
import math, pathlib, struct, sys
import numpy as np


def load(paths):
    """Triangles of the given dumps as an (n, 9) array; the newest dump of a region wins."""
    regions = {}
    for path in paths:
        data = pathlib.Path(path).read_bytes(); magic, count = struct.unpack_from('<II', data, 0); assert magic == 0x31504D43; at = 8
        for _ in range(count):
            key = struct.unpack_from('<3i', data, at); n, = struct.unpack_from('<I', data, at + 12); at += 16
            regions[key] = np.frombuffer(data, '<f4', n * 9, at).reshape(n, 9).astype(np.float64); at += n * 36
    return np.concatenate([t for t in regions.values() if len(t)]) if regions else np.zeros((0, 9))


class Ground:
    def __init__(self, tris):
        self.t = tris; a, b, c = tris[:, 0:3], tris[:, 3:6], tris[:, 6:9]
        n = np.cross(b - a, c - a); length = np.linalg.norm(n, axis=1); ok = length > 1e-12
        self.t, a, b, c, n = self.t[ok], a[ok], b[ok], c[ok], n[ok] / length[ok, None]
        self.a, self.b, self.c, self.n = a, b, c, n
        self.lo = np.minimum(np.minimum(a, b), c); self.hi = np.maximum(np.maximum(a, b), c)

    def surfaces(self, x, z):
        """(height, |ny|) of every triangle over the point."""
        m = (self.lo[:, 0] <= x) & (self.hi[:, 0] >= x) & (self.lo[:, 2] <= z) & (self.hi[:, 2] >= z) & (np.abs(self.n[:, 1]) > .05)
        a, b, c, n = self.a[m], self.b[m], self.c[m], self.n[m]
        edge = lambda p, q: (q[:, 0] - p[:, 0]) * (z - p[:, 2]) - (q[:, 2] - p[:, 2]) * (x - p[:, 0])
        d1, d2, d3 = edge(a, b), edge(b, c), edge(c, a)
        inside = ~(((d1 < -1e-12) | (d2 < -1e-12) | (d3 < -1e-12)) & ((d1 > 1e-12) | (d2 > 1e-12) | (d3 > 1e-12)))
        h = a[:, 1] - (n[:, 0] * (x - a[:, 0]) + n[:, 2] * (z - a[:, 2])) / n[:, 1]
        return h[inside], np.abs(n[inside, 1])

    def top(self, x, z, below, limit=None):
        """Highest surface at or below `below`: (height, |ny|) or None. `limit`: walkable only."""
        h, ny = self.surfaces(x, z); m = h <= below
        if limit is not None: m &= ny >= limit
        if not m.any(): return None
        i = np.argmax(np.where(m, h, -np.inf)); return float(h[i]), float(ny[i])


DIRECTIONS = [(math.cos(k * math.pi / 4), math.sin(k * math.pi / 4)) for k in range(8)]


def find_edge(ground, start, limit, reach=18.0, drop=1.2):
    """Nearest ledge: level walkable run-up, then a drop. None if there is none."""
    found = find_edges(ground, start, limit, reach, drop)
    return found[0] if found else None


def find_edges(ground, start, limit, reach=18.0, drop=1.2):
    """Ledges nearest first (one per ground cell): approach point, direction, last ground, height."""
    found = []
    for gx in np.arange(start[0] - reach, start[0] + reach, .5):
        for gz in np.arange(start[2] - reach, start[2] + reach, .5):
            here = ground.top(gx, gz, start[1] + 4, limit)
            if not here or abs(here[0] - start[1]) > 4: continue
            h = here[0]
            if (ground.top(gx, gz, h + 2.5) or (h, 1))[0] > h + .05: continue  # something above the spot
            for dx, dz in DIRECTIONS:
                run = [ground.top(gx - dx * k, gz - dz * k, h + .4, limit) for k in (.5, 1, 1.5, 2, 2.5)]
                if any(r is None or abs(r[0] - h) > .4 for r in run): continue
                # Ground must be seen below (a gap in the data is not a drop), low enough to be an
                # edge and near enough to step off without fall damage.
                beyond = [ground.top(gx + dx * k, gz + dz * k, h + 1.9) for k in (.5, .75, 1, 1.5)]
                if any(b is None or b[0] > h - drop or b[0] < h - 3.4 for b in beyond[1:]) or (beyond[0] is not None and beyond[0][0] > h - drop): continue
                found.append((math.hypot(gx - start[0], gz - start[2]), {'approach': [float(gx - dx * 2.5), float(gz - dz * 2.5)], 'direction': [dx, dz], 'last_ground': [float(gx), float(gz)], 'height': h,
                                                                       'below': [round(b[0], 2) for b in beyond]}))
                break
    return [item for _, item in sorted(found, key=lambda f: f[0])]


def find_slope(ground, start, limit, reach=20.0):
    """Nearest face steeper than 45.6 degrees but inside the limit, with level ground at its foot. None if none."""
    found = find_slopes(ground, start, limit, reach)
    return found[0] if found else None


def find_slopes(ground, start, limit, reach=20.0):
    found = []
    for gx in np.arange(start[0] - reach, start[0] + reach, .5):
        for gz in np.arange(start[2] - reach, start[2] + reach, .5):
            here = ground.top(gx, gz, start[1] + 6)
            if not here or abs(here[0] - start[1]) > 6 or not (limit + .02 <= here[1] < .68): continue
            h = here[0]
            for dx, dz in DIRECTIONS:
                up = [ground.top(gx + dx * k, gz + dz * k, h + 3) for k in (.5, 1)]
                if any(u is None or not (limit + .02 <= u[1] < .68) for u in up) or up[1][0] - h < .9: continue
                foot = [ground.top(gx - dx * k, gz - dz * k, h + .3, .7) for k in (1, 1.5, 2, 2.5, 3)]
                if any(f is None for f in foot[1:]) or max(f[0] for f in foot[1:]) - min(f[0] for f in foot[1:]) > .4 or h - foot[1][0] > 1.5: continue
                if (ground.top(gx, gz, h + 3) or (h, 1))[0] > h + .05: continue
                found.append((math.hypot(gx - start[0], gz - start[2]), {'approach': [float(gx - dx * 3), float(gz - dz * 3)], 'direction': [dx, dz], 'on_slope': [float(gx), float(gz)], 'height': h, 'ny': here[1],
                                                                       'foot_height': foot[-1][0], 'rise_over_1m': up[1][0] - h}))
                break
    return [item for _, item in sorted(found, key=lambda f: f[0])]


if __name__ == '__main__':
    tris = load(sys.argv[4:]); start = [float(v) for v in sys.argv[1:4]]; ground = Ground(tris)
    print(len(tris), 'triangles')
    print('edge ', find_edge(ground, start, .5))
    print('slope', find_slope(ground, start, .5))
