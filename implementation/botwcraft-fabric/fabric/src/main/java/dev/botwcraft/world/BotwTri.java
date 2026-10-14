package dev.botwcraft.world;

/** One exact BOTW collision triangle in Minecraft space, with precomputed plane and bounds. */
public final class BotwTri {
	/**
	 * Surfaces at most this steep can be walked on. BotwCraft: ny 0.7 (~45 degrees). BOTW
	 * adaptation: 60 degrees by default, because Link walks ground that BOTW's player does not
	 * (the terrain where routes 28-32 failed has normals of ny 0.54-0.69); the launcher may pass
	 * another angle as -Dbotwcraft.walkableDegrees. The bridge uses the same angle for native support.
	 */
	public static final double WALKABLE_NY = walkableNy();

	private static double walkableNy() {
		double degrees = 60;
		try {
			degrees = Double.parseDouble(System.getProperty("botwcraft.walkableDegrees", "60"));
		} catch (NumberFormatException ignored) {
		}
		if (!(degrees >= 30 && degrees <= 75)) {
			degrees = 60;
		}
		// 45 keeps BotwCraft's exact constant.
		return degrees == 45 ? 0.7 : Math.cos(Math.toRadians(degrees));
	}

	public final double ax, ay, az, bx, by, bz, cx, cy, cz;
	public final double nx, ny, nz; // unit normal (winding is not trusted; use |ny|)
	public final double minX, minY, minZ, maxX, maxY, maxZ;
	public final boolean stairHelper;
	public final boolean walkable;
	/** Ground, rock, trees...: can be dug into. Its normal then faces out of the solid side. */
	public final boolean diggable;
	/** What it digs into (Proto.DIG_*). */
	public final int material;
	/** BOTW's land (terrain), not an object on it. */
	public final boolean terrain;
	/** BOTW bridge: the other game calls it a ladder (report 75). */
	public final boolean ladder;
	/** BOTW bridge: the other game says its player cannot climb it (BotwClimb). */
	public final boolean noClimb;
	/** BOTW bridge: the other game's wall code and its material with the sub-material (10 bits); -1: not known. */
	public final int wallCode, gameMaterial;

	public BotwTri(float[] v, int o, boolean stairHelper) {
		this(v, o, stairHelper ? dev.botwcraft.link.Proto.TRI_STAIR_HELPER : 0);
	}

	/** {@code flags}: the triangle's Proto.TRI_* flags as BOTW sent them. */
	public BotwTri(float[] v, int o, int flags) {
		boolean stairHelper = (flags & dev.botwcraft.link.Proto.TRI_STAIR_HELPER) != 0;
		this.diggable = (flags & dev.botwcraft.link.Proto.TRI_DIGGABLE) != 0;
		this.material = (flags >>> dev.botwcraft.link.Proto.TRI_MATERIAL_SHIFT) & 0xFF;
		this.terrain = (flags & dev.botwcraft.link.Proto.TRI_TERRAIN) != 0;
		this.ladder = (flags & dev.botwcraft.link.Proto.TRI_LADDER) != 0;
		this.noClimb = (flags & dev.botwcraft.link.Proto.TRI_NO_CLIMB) != 0;
		boolean known = (flags & dev.botwcraft.link.Proto.TRI_USER_KNOWN) != 0;
		this.wallCode = known ? (flags >>> dev.botwcraft.link.Proto.TRI_WALL_SHIFT) & 31 : -1;
		this.gameMaterial = known ? (flags >>> dev.botwcraft.link.Proto.TRI_USER_SHIFT) & 0x3FF : -1;
		this.ax = v[o];
		this.ay = v[o + 1];
		this.az = v[o + 2];
		this.bx = v[o + 3];
		this.by = v[o + 4];
		this.bz = v[o + 5];
		this.cx = v[o + 6];
		this.cy = v[o + 7];
		this.cz = v[o + 8];
		double ux = this.bx - this.ax, uy = this.by - this.ay, uz = this.bz - this.az;
		double wx = this.cx - this.ax, wy = this.cy - this.ay, wz = this.cz - this.az;
		double qx = uy * wz - uz * wy, qy = uz * wx - ux * wz, qz = ux * wy - uy * wx;
		double len = Math.sqrt(qx * qx + qy * qy + qz * qz);
		if (len < 1e-12) {
			this.nx = 0;
			this.ny = 1;
			this.nz = 0;
		} else {
			this.nx = qx / len;
			this.ny = qy / len;
			this.nz = qz / len;
		}
		this.minX = Math.min(this.ax, Math.min(this.bx, this.cx));
		this.minY = Math.min(this.ay, Math.min(this.by, this.cy));
		this.minZ = Math.min(this.az, Math.min(this.bz, this.cz));
		this.maxX = Math.max(this.ax, Math.max(this.bx, this.cx));
		this.maxY = Math.max(this.ay, Math.max(this.by, this.cy));
		this.maxZ = Math.max(this.az, Math.max(this.bz, this.cz));
		this.stairHelper = stairHelper;
		this.walkable = stairHelper || Math.abs(this.ny) >= WALKABLE_NY;
	}

	/** The point of the triangle nearest to (px, py, pz), into {@code out}. */
	public void closest(double px, double py, double pz, double[] out) {
		double abx = this.bx - this.ax, aby = this.by - this.ay, abz = this.bz - this.az;
		double acx = this.cx - this.ax, acy = this.cy - this.ay, acz = this.cz - this.az;
		double apx = px - this.ax, apy = py - this.ay, apz = pz - this.az;
		double d1 = abx * apx + aby * apy + abz * apz, d2 = acx * apx + acy * apy + acz * apz;
		double v = 0, w = 0;
		if (d1 > 0 || d2 > 0) {
			double bpx = px - this.bx, bpy = py - this.by, bpz = pz - this.bz;
			double d3 = abx * bpx + aby * bpy + abz * bpz, d4 = acx * bpx + acy * bpy + acz * bpz;
			double cpx = px - this.cx, cpy = py - this.cy, cpz = pz - this.cz;
			double d5 = abx * cpx + aby * cpy + abz * cpz, d6 = acx * cpx + acy * cpy + acz * cpz;
			double vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
			if (d3 >= 0 && d4 <= d3) {
				v = 1;
			} else if (d6 >= 0 && d5 <= d6) {
				w = 1;
			} else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
				v = d1 / (d1 - d3);
			} else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
				w = d2 / (d2 - d6);
			} else if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
				w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
				v = 1 - w;
			} else {
				double sum = va + vb + vc;
				if (Math.abs(sum) > 1e-300) {
					v = vb / sum;
					w = vc / sum;
				}
			}
		}
		out[0] = this.ax + abx * v + acx * w;
		out[1] = this.ay + aby * v + acy * w;
		out[2] = this.az + abz * v + acz * w;
	}

	public boolean degenerate() {
		return this.maxX - this.minX < 1e-9 && this.maxZ - this.minZ < 1e-9;
	}

	/**
	 * Height of the triangle's plane above (x, z) if that point lies inside the triangle's
	 * horizontal footprint, otherwise NaN. Near-vertical triangles have no meaningful height.
	 */
	public double heightAt(double x, double z) {
		if (x < this.minX - 1e-9 || x > this.maxX + 1e-9 || z < this.minZ - 1e-9 || z > this.maxZ + 1e-9 || Math.abs(this.ny) < 0.05) {
			return Double.NaN;
		}
		double d1 = edge(x, z, this.ax, this.az, this.bx, this.bz);
		double d2 = edge(x, z, this.bx, this.bz, this.cx, this.cz);
		double d3 = edge(x, z, this.cx, this.cz, this.ax, this.az);
		boolean hasNeg = d1 < -1e-12 || d2 < -1e-12 || d3 < -1e-12;
		boolean hasPos = d1 > 1e-12 || d2 > 1e-12 || d3 > 1e-12;
		if (hasNeg && hasPos) {
			return Double.NaN;
		}
		// Plane: n . (p - a) = 0  ->  y = ay - (nx (x - ax) + nz (z - az)) / ny
		return this.ay - (this.nx * (x - this.ax) + this.nz * (z - this.az)) / this.ny;
	}

	private static double edge(double px, double pz, double x0, double z0, double x1, double z1) {
		return (x1 - x0) * (pz - z0) - (z1 - z0) * (px - x0);
	}
}
