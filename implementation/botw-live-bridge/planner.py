"""Route planning on the exported native collision (MC space), for test steering only.

The bridge dumps its cached regions (cache-dump-*.bin). This builds a
0.25-block standability grid with SkyCraft's walkable rule (|ny| >= 0.7) and
the MC player box (radius 0.3, height 1.8, step 0.6), then A* toward a target
or the known cell nearest it. It never changes collision or movement; Minecraft
still simulates every step the walker takes.
"""
import hashlib, heapq, math, pathlib, pickle, struct
import numpy as np

CELL, RADIUS, HEIGHT, STEP, DROP = .25, .3, 1.8, .6, 3.0  # MC fall damage starts above 3 blocks


def regions(paths):
    """{region key: triangles}; later dumps replace earlier ones."""
    tris = {}
    for path in paths:
        data = pathlib.Path(path).read_bytes()
        magic, count = struct.unpack_from('<II', data, 0); assert magic == 0x31504D43
        offset = 8
        for _ in range(count):
            key = struct.unpack_from('<3i', data, offset); n = struct.unpack_from('<I', data, offset + 12)[0]; offset += 16
            tris[key] = np.frombuffer(data, '<f4', n * 9, offset).reshape(n, 3, 3).astype(np.float64); offset += n * 36
    return tris


def load(paths):
    tris = regions(paths)
    return np.concatenate(list(tris.values())) if tris else np.zeros((0, 3, 3))


def clip(poly, axis, value, keep_above):
    out = []
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        ina, inb = (a[axis] >= value) == keep_above, (b[axis] >= value) == keep_above
        if ina: out.append(a)
        if ina != inb:
            t = (value - a[axis]) / (b[axis] - a[axis]); out.append(a + t * (b - a))
    return out


class Grid:
    def __init__(self, tris=None):
        self.floors, self.solid, self.known = {}, {}, set()
        self.reach = int(math.ceil(RADIUS / CELL))
        self.cache = {}
        if tris is not None: self.add(tris)

    def update(self, by_region, cache_dir=None):
        """Merge regions; a newer dump of a known region replaces its data.

        Rasterized regions are cached by content hash, so later runs start
        from everything earlier runs harvested without re-clipping it.
        """
        changed = 0
        if not hasattr(self, 'parts'): self.parts = {}
        for k, tris in by_region.items():
            digest = hashlib.sha1(tris.tobytes()).hexdigest()
            if self.parts.get(k, (None,))[0] == digest: continue
            cached = None; path = pathlib.Path(cache_dir) / (digest + '.pkl') if cache_dir else None
            if path and path.exists(): cached = pickle.loads(path.read_bytes())
            if cached is None:
                part = Grid(); part.add(tris); cached = (part.floors, part.solid)
                if path: path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(pickle.dumps(cached))
            self.parts[k] = (digest, cached); self.known.add(k); changed += 1
        if changed:
            self.floors, self.solid = {}, {}
            for _, (floors, solid) in self.parts.values():
                for cell, values in floors.items(): self.floors.setdefault(cell, []).extend(values)
                for cell, values in solid.items(): self.solid.setdefault(cell, []).extend(values)
            self.cache.clear()
        return changed

    def frontier(self, node):
        """True when a neighbouring column at this level lies in an unharvested region."""
        (i, j), f = node
        if not self.known: return False
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            key = (int(math.floor((i + di) * CELL / 8)), int(math.floor(f / 8)), int(math.floor((j + dj) * CELL / 8)))
            if key not in self.known: return True
        return False

    def add(self, tris):
        normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        lengths = np.linalg.norm(normals, axis=1); ok = lengths > 1e-9
        tris, normals = tris[ok], normals[ok] / lengths[ok, None]
        for t, n in zip(tris, normals):
            walkable = abs(n[1]) >= .7
            lo = np.floor(t[:, [0, 2]].min(0) / CELL).astype(int); hi = np.floor(t[:, [0, 2]].max(0) / CELL).astype(int)
            for i in range(lo[0], hi[0] + 1):
                for j in range(lo[1], hi[1] + 1):
                    poly = [p for p in t]
                    for axis, value, above in ((0, i * CELL, True), (0, (i + 1) * CELL, False), (2, j * CELL, True), (2, (j + 1) * CELL, False)):
                        poly = clip(poly, axis, value, above)
                        if not poly: break
                    if not poly: continue
                    ys = [p[1] for p in poly]
                    self.solid.setdefault((i, j), []).append((min(ys), max(ys)))
                    if walkable and n[1] != 0:
                        x, z = (i + .5) * CELL, (j + .5) * CELL
                        # Height of the plane at the cell centre (clamped to the clipped piece).
                        y = t[0][1] - (n[0] * (x - t[0][0]) + n[2] * (z - t[0][2])) / n[1]
                        self.floors.setdefault((i, j), []).append(min(max(y, min(ys)), max(ys)))

    def standable(self, cell, floor):
        key = (cell, round(floor, 2))
        if key in self.cache: return self.cache[key]
        ok = True
        for di in range(-self.reach, self.reach + 1):
            for dj in range(-self.reach, self.reach + 1):
                for y0, y1 in self.solid.get((cell[0] + di, cell[1] + dj), ()):
                    # TriCollider ignores walls below the step height (wallFrom = step).
                    if y1 > floor + STEP and y0 < floor + HEIGHT: ok = False; break
                if not ok: break
            if not ok: break
        self.cache[key] = ok
        return ok

    def nodes(self, cell):
        # Regions repeat triangles that straddle their borders: one node per level.
        key = ('nodes', cell)
        if key not in self.cache:
            self.cache[key] = [f for f in sorted({round(float(f), 3) for f in self.floors.get(cell, ())}) if self.standable(cell, f)]
        return self.cache[key]

    def nearest(self, x, y, z, vertical=2):
        cell = (int(math.floor(x / CELL)), int(math.floor(z / CELL)))
        best = None
        for r in range(0, 8):
            for di in range(-r, r + 1):
                for dj in range(-r, r + 1):
                    if max(abs(di), abs(dj)) != r: continue
                    c = (cell[0] + di, cell[1] + dj)
                    for f in self.nodes(c):
                        if abs(f - y) <= vertical:
                            d = math.hypot(di, dj) + abs(f - y)
                            if best is None or d < best[0]: best = (d, c, f)
            if best: return best[1], best[2]
        return None

    def plan(self, start, target, limit=400000, avoid=()):
        """A* from an MC position to a target (x, z); returns [(x, y, z)] waypoints."""
        begin = self.nearest(*start)
        if not begin: return None
        goal = (target[0] / CELL, target[1] / CELL)
        h = lambda c: math.hypot(c[0] + .5 - goal[0], c[1] + .5 - goal[1]) * CELL
        frontier = [(h(begin[0]), 0.0, begin)]; came = {begin: None}; cost = {begin: 0.0}
        # Goal when the target is not known yet: the reachable frontier cell
        # nearest it (dead-end pockets inside harvested space are not frontier).
        best = (h(begin[0]), begin); edge = None; expanded = 0
        while frontier and expanded < limit:
            _, g, node = heapq.heappop(frontier); expanded += 1
            if g > cost[node] + 1e-9: continue
            if h(node[0]) < best[0]: best = (h(node[0]), node)
            near_dead_end = any(math.hypot((node[0][0] + .5) * CELL - a[0], (node[0][1] + .5) * CELL - a[2]) < 3 and abs(node[1] - a[1]) < 3 for a in avoid)
            # A* pops in g+h order: the first usable frontier has the least
            # estimated total distance, which avoids ping-pong between edges.
            if edge is None and not near_dead_end and self.frontier(node): edge = (h(node[0]), node)
            if h(node[0]) < .5: break
            (i, j), f = node
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
                c = (i + di, j + dj)
                if di and dj and (not self.nodes((i + di, j)) or not self.nodes((i, j + dj))): continue
                for f2 in self.nodes(c):
                    if f2 - f > STEP or f - f2 > DROP: continue
                    step = math.hypot(di, dj) * CELL + max(0, f2 - f) * .5
                    nxt = (c, f2)
                    if g + step < cost.get(nxt, 1e18):
                        cost[nxt] = g + step; came[nxt] = node
                        heapq.heappush(frontier, (g + step + h(c), g + step, nxt))
        node = best[1] if best[0] < .5 or edge is None else edge[1]; path = []
        while node: path.append(((node[0][0] + .5) * CELL, node[1], (node[0][1] + .5) * CELL)); node = came[node]
        return path[::-1]


def waypoints(path, spacing=2.0):
    """Every `spacing` blocks along the path, plus its end."""
    if not path: return []
    out, run = [], 0.0
    for a, b in zip(path, path[1:]):
        run += math.hypot(b[0] - a[0], b[2] - a[2])
        if run >= spacing: out.append(b); run = 0.0
    if not out or out[-1] != path[-1]: out.append(path[-1])
    return out


def selftest():
    # Floor at y=0 over x,z in [0,10]; wall at x=5 from z=0..8 (gap z=8..10).
    def quad(a, b, c, d): return [[a, b, c], [a, c, d]]
    tris = quad((0, 0, 0), (0, 0, 10), (10, 0, 10), (10, 0, 0))
    tris += quad((5, 0, 0), (5, 3, 0), (5, 3, 8), (5, 0, 8))
    g = Grid(np.array(tris, dtype=np.float64))
    path = g.plan((2, 0, 2), (8, 2))
    assert path and math.hypot(path[-1][0] - 8, path[-1][2] - 2) < .5, path[-1]
    assert max(p[2] for p in path) > 8, 'route must use the gap'
    assert all(abs(p[1]) < 1e-9 for p in path)
    # Frontier: known pocket nearer the target versus the open edge.
    g = Grid(); g.update({(0, 0, 0): np.array(quad((0, 0, 0), (0, 0, 7.5), (7.5, 0, 7.5), (7.5, 0, 0)), dtype=np.float64)})
    path = g.plan((1, 0, 1), (30, 30))
    assert path and g.frontier(((int(path[-1][0] / CELL), int(path[-1][2] / CELL)), path[-1][1])), path[-1]
    print('PASS planner: gap found around native wall; frontier goal', len(path), 'cells')


if __name__ == '__main__':
    selftest()
