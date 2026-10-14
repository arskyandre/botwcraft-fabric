package dev.botwcraft.world;

import java.util.ArrayList;
import java.util.List;

/**
 * Offline check of the collider on synthetic geometry (no Minecraft): the BOTW steep-slope
 * adaptation, and that ordinary slopes, stairs and kerbs still behave as in BotwCraft.
 * Run by implementation/botw-live-bridge/mod_build.py.
 */
public final class TriColliderCheck {
	private static final double RADIUS = 0.3, HEIGHT = 1.8, STEP = 0.6;

	private static void quad(List<BotwTri> out, double x0, double y0, double z0, double x1, double y1, double z1, double x2, double y2, double z2, double x3, double y3, double z3) {
		float[] v = { (float) x0, (float) y0, (float) z0, (float) x1, (float) y1, (float) z1, (float) x2, (float) y2, (float) z2,
			(float) x0, (float) y0, (float) z0, (float) x2, (float) y2, (float) z2, (float) x3, (float) y3, (float) z3 };
		out.add(new BotwTri(v, 0, false));
		out.add(new BotwTri(v, 9, false));
	}

	/** A strip across x in [-20, 20] from (z0, y0) to (z1, y1). */
	private static void strip(List<BotwTri> out, double z0, double y0, double z1, double y1) {
		quad(out, -20, y0, z0, 20, y0, z0, 20, y1, z1, -20, y1, z1);
	}

	private static final class Walker {
		double x, y, z, vy;
		boolean ground = true;
		double lowestBelow = 0; // most negative (feet - surface) seen
		final java.util.ArrayDeque<String> recent = new java.util.ArrayDeque<>();

		void tick(List<BotwTri> tris, double mx, double mz, java.util.function.DoubleBinaryOperator surface) {
			this.vy = (this.vy - 0.08) * 0.98;
			double[] m = TriCollider.resolve(tris, this.x, this.y, this.z, RADIUS, HEIGHT, STEP, this.ground, mx, this.vy, mz);
			this.x += m[0];
			this.y += m[1];
			this.z += m[2];
			this.ground = m[1] != this.vy && this.vy < 0;
			if (this.ground) {
				this.vy = 0;
			}
			if (surface != null) {
				double s = surface.applyAsDouble(this.x, this.z);
				this.lowestBelow = Math.min(this.lowestBelow, this.y - s);
				this.recent.add(String.format("%n  x %.3f y %.3f z %.3f surface %.3f ground %b vy %.3f move %.3f %.3f %.3f (asked %.3f %.3f)", this.x, this.y, this.z, s, this.ground, this.vy, m[0], m[1], m[2], mx, mz));
				if (this.recent.size() > 14) {
					this.recent.removeFirst();
				}
			}
		}
	}

	private static void check(boolean ok, String what) {
		if (!ok) {
			throw new AssertionError(what);
		}
	}

	private static List<BotwTri> hill(double degrees) {
		List<BotwTri> tris = new ArrayList<>();
		strip(tris, -10, 0, 0, 0);
		strip(tris, 0, 0, 8, 8 * Math.tan(Math.toRadians(degrees)));
		return tris;
	}

	/** The collider's walkable limit in degrees, and two test slopes past it. */
	private static final double LIMIT = Math.toDegrees(Math.acos(BotwTri.WALKABLE_NY));
	private static final double STEEP = LIMIT + 4.5, STEEPER = LIMIT + 7.5;

	public static void main(String[] args) {
		// 1) Sprinting straight into slopes steeper than walkable: stopped at the foot, on the ground.
		for (double degrees : new double[] { LIMIT + 0.5, LIMIT + 2.5, LIMIT + 6.5, LIMIT + 11.5, Math.min(84, LIMIT + 16.5), Math.min(86, LIMIT + 24.5) }) {
			double rise = Math.tan(Math.toRadians(degrees));
			List<BotwTri> tris = hill(degrees);
			Walker w = new Walker();
			w.z = -3;
			for (int i = 0; i < 80; i++) {
				w.tick(tris, 0, 0.28, (x, z) -> z > 0 ? z * rise : 0);
			}
			check(w.lowestBelow > -0.02 && w.ground && w.y > -1e-6 && w.z < 0.35, degrees + " degree slope: beneath by " + w.lowestBelow + " at z " + w.z + " y " + w.y);
		}
		// 2) A walkable slope is still climbed exactly.
		{
			double rise = Math.tan(Math.toRadians(30));
			List<BotwTri> tris = hill(30);
			Walker w = new Walker();
			w.z = -3;
			for (int i = 0; i < 40; i++) {
				w.tick(tris, 0, 0.2, null);
			}
			check(w.z > 4.5 && Math.abs(w.y - (w.z + 0.15) * rise) < 0.02 && w.ground, "30 degree slope: z " + w.z + " y " + w.y);
		}
		// 2b) So is a slope just inside the walkable limit, and crouch-edge detection sees it as ground.
		{
			double degrees = LIMIT - 2, rise = Math.tan(Math.toRadians(degrees));
			List<BotwTri> tris = hill(degrees);
			Walker w = new Walker();
			w.z = -3;
			for (int i = 0; i < 40; i++) {
				w.tick(tris, 0, 0.2, null);
			}
			check(w.z > 4.5 && Math.abs(w.y - (w.z + 0.15) * rise) < 0.02 && w.ground, degrees + " degree slope: z " + w.z + " y " + w.y);
			check(TriCollider.walkableBelow(tris, 0, w.y, w.z - 0.05, 0.6, 0.6) && TriCollider.walkableBelow(tris, 0, w.y, w.z + 0.05, 0.6, 0.6), "walkable slope is ground for edges");
		}
		// 2c) Crouch edges: a plateau ending in a drop, and in a too-steep face, is an edge; the
		// footprint may overhang by its radius, as a block's box may.
		for (double face : new double[] { 90, LIMIT + 10 }) {
			List<BotwTri> tris = new ArrayList<>();
			strip(tris, -10, 0, 0, 0);
			if (face < 90) {
				strip(tris, 0, 0, 8, -8 * Math.tan(Math.toRadians(face)));
			}
			strip(tris, -10, -30, 10, -30);
			check(TriCollider.walkableBelow(tris, 0, 0, -0.3, 0.6, 0.6) && TriCollider.walkableBelow(tris, 0, 0, 0.1, 0.6, 0.6), "plateau is ground up to the footprint radius");
			check(!TriCollider.walkableBelow(tris, 0, 0, 0.2, 0.6, 0.6) && !TriCollider.walkableBelow(tris, 0, 0, 1, 0.6, 0.6), "past the plateau (face " + face + ") is an edge");
			check(!TriCollider.restsOnSteep(tris, 0, 0, 1) && TriCollider.restsOnSteep(tris, 0, 0, -1), "restsOnSteep");
		}
		// 3) Stairs with vertical risers, and with 70 degree risers, are still climbed.
		for (double riserRun : new double[] { 0, 0.5 / Math.tan(Math.toRadians(70)) }) {
			List<BotwTri> tris = new ArrayList<>();
			strip(tris, -10, 0, 0, 0);
			for (int k = 0; k < 6; k++) {
				strip(tris, k, k * 0.5, k + riserRun, (k + 1) * 0.5);       // riser
				strip(tris, k + riserRun, (k + 1) * 0.5, k + 1, (k + 1) * 0.5); // tread
			}
			strip(tris, 6, 3, 12, 3);
			Walker w = new Walker();
			w.z = -2;
			for (int i = 0; i < 70; i++) {
				w.tick(tris, 0, 0.2, null);
			}
			check(w.z > 8 && Math.abs(w.y - 3) < 1e-6 && w.ground, "stairs with riser run " + riserRun + ": z " + w.z + " y " + w.y);
		}
		// 4) Standing on a steep face (landed there): downhill and along it are free, uphill is not.
		{
			double rise = Math.tan(Math.toRadians(STEEP));
			List<BotwTri> tris = hill(STEEP);
			Walker down = new Walker();
			down.z = 3;
			down.y = 3 * rise;
			for (int i = 0; i < 30; i++) {
				down.tick(tris, 0, -0.2, null);
			}
			check(down.z < -1 && down.y > -1e-6 && down.y < 0.05, "down a steep face: z " + down.z + " y " + down.y);
			Walker along = new Walker();
			along.z = 3;
			along.y = 3 * rise;
			for (int i = 0; i < 10; i++) {
				along.tick(tris, 0.2, 0, null);
			}
			check(along.x > 1.5, "along a steep face: x " + along.x);
			Walker up = new Walker();
			up.z = 3;
			up.y = 3 * rise;
			for (int i = 0; i < 20; i++) {
				up.tick(tris, 0, 0.2, (x, z) -> z * rise);
			}
			check(up.z < 3.2 && up.lowestBelow > -0.2, "up a steep face: z " + up.z + " beneath " + up.lowestBelow);
		}
		// 5) Diagonal approach slides along the foot instead of sticking.
		{
			double rise = Math.tan(Math.toRadians(STEEP));
			List<BotwTri> tris = hill(STEEP);
			Walker w = new Walker();
			w.z = -2;
			for (int i = 0; i < 60; i++) {
				w.tick(tris, 0.2, 0.2, (x, z) -> z > 0 ? z * rise : 0);
			}
			check(w.x > 8 && w.z < 0.35 && w.lowestBelow > -0.05 && w.ground, "diagonal: x " + w.x + " z " + w.z + " beneath " + w.lowestBelow);
		}
		// 6) Jumping at the slope: never beneath the surface, and comes to rest on the ground.
		{
			double rise = Math.tan(Math.toRadians(STEEP));
			List<BotwTri> tris = hill(STEEP);
			Walker w = new Walker();
			w.z = -1.2;
			for (int i = 0; i < 80; i++) {
				if (w.ground && i % 14 == 0) {
					w.vy = 0.42 / 0.98 + 0.08;
					w.ground = false;
				}
				w.tick(tris, 0, 0.28, (x, z) -> z > 0 ? z * rise : 0);
			}
			check(w.lowestBelow > -0.05 && w.y > -1e-6, "jumping at a steep slope: beneath by " + w.lowestBelow + " at z " + w.z + " y " + w.y);
		}
		// 7) A kerb below step height in front of open ground is still stepped onto.
		{
			List<BotwTri> tris = new ArrayList<>();
			strip(tris, -10, 0, 0, 0);
			strip(tris, 0, 0, 0.2, 0.4); // 63 degree kerb face
			strip(tris, 0.2, 0.4, 10, 0.4);
			Walker w = new Walker();
			w.z = -2;
			for (int i = 0; i < 30; i++) {
				w.tick(tris, 0, 0.2, null);
			}
			check(w.z > 3 && Math.abs(w.y - 0.4) < 1e-6, "kerb: z " + w.z + " y " + w.y);
		}
		// 8) Crossing a steep face whose ground rises along the way: slowed or stopped, never beneath it.
		for (double climb : new double[] { 0.05, 0.15, 0.3, -0.1 }) {
			double rise = Math.tan(Math.toRadians(STEEPER));
			List<BotwTri> tris = new ArrayList<>();
			quad(tris, -20, -20 * climb, 0, 20, 20 * climb, 0, 20, 20 * climb + 8 * rise, 8, -20, -20 * climb + 8 * rise, 8);
			quad(tris, -20, -20 * climb, -10, 20, 20 * climb, -10, 20, 20 * climb, 0, -20, -20 * climb, 0); // ground at its foot (a very steep face is slid down)
			strip(tris, -10, -30, 8, -30); // far below: falling through would land here
			Walker w = new Walker();
			w.z = 2;
			w.y = 2 * rise + 0.3;
			w.ground = false;
			for (int i = 0; i < 80; i++) { // the face ends at x = 20
				w.tick(tris, 0.2, 0, (x, z) -> z > 0 ? z * rise + x * climb : x * climb);
			}
			check(w.lowestBelow > -0.02 && w.y > -5, "crossing a steep face (climb " + climb + "): beneath by " + w.lowestBelow + " at x " + w.x + " z " + w.z + " y " + w.y);
			if (climb < 0) {
				check(w.x > 3, "crossing a descending steep face is free: x " + w.x);
			}
		}
		// 9) Lower walkable ground beside a steep face: stepping down never goes beneath the face.
		{
			double rise = Math.tan(Math.toRadians(STEEPER));
			List<BotwTri> tris = new ArrayList<>();
			strip(tris, 0, 0, 8, 8 * rise);  // steep face rising in +z from z = 0
			strip(tris, -10, -0.3, 0, -0.3); // walkable ground 0.3 below its foot
			strip(tris, -10, -30, 8, -30);
			Walker w = new Walker();
			w.z = 0.1;
			w.y = 0.1 * rise + 0.2;
			w.ground = false;
			for (int i = 0; i < 80; i++) {
				w.tick(tris, 0.2, i < 40 ? 0.02 : -0.02, (x, z) -> z > 0 ? z * rise : -0.3);
			}
			check(w.lowestBelow > -0.02 && w.y > -1, "beside lower ground: beneath by " + w.lowestBelow + " z " + w.z + " y " + w.y);
		}
		// 10) Random hilly terrain (single-valued, slopes from flat to near-vertical), random
		// running, turning and jumping: the feet are never beneath the surface under the axis.
		{
			final int n = 48;
			final double cell = 1.25;
			long ticks = 0;
			double worst = 0;
			for (int seed = 1; seed <= 40; seed++) {
				java.util.Random random = new java.util.Random(seed);
				double[][] h = new double[n + 1][n + 1];
				double amplitude = 0.4 + random.nextDouble() * 2.6;
				double[] phase = new double[6];
				for (int i = 0; i < 6; i++) {
					phase[i] = random.nextDouble() * 6.283;
				}
				for (int i = 0; i <= n; i++) {
					for (int j = 0; j <= n; j++) {
						h[i][j] = amplitude * (Math.sin(i * 0.45 + phase[0]) * Math.cos(j * 0.38 + phase[1]) + 0.5 * Math.sin(i * 1.1 + j * 0.9 + phase[2]))
							+ (random.nextDouble() - 0.5) * amplitude * 0.6;
					}
				}
				List<BotwTri> all = new ArrayList<>();
				BotwTri[][][] grid = new BotwTri[n][n][2];
				for (int i = 0; i < n; i++) {
					for (int j = 0; j < n; j++) {
						quad(all, i * cell, h[i][j], j * cell, (i + 1) * cell, h[i + 1][j], j * cell, (i + 1) * cell, h[i + 1][j + 1], (j + 1) * cell, i * cell, h[i][j + 1], (j + 1) * cell);
						grid[i][j][0] = all.get(all.size() - 2);
						grid[i][j][1] = all.get(all.size() - 1);
					}
				}
				java.util.function.DoubleBinaryOperator surface = (x, z) -> {
					int i = Math.max(0, Math.min(n - 1, (int) Math.floor(x / cell))), j = Math.max(0, Math.min(n - 1, (int) Math.floor(z / cell)));
					double a = grid[i][j][0].heightAt(x, z), b = grid[i][j][1].heightAt(x, z);
					return Double.isNaN(a) ? b : Double.isNaN(b) ? a : Math.max(a, b);
				};
				Walker w = new Walker();
				w.x = w.z = n * cell / 2;
				w.y = surface.applyAsDouble(w.x, w.z) + 2;
				w.ground = false;
				double angle = 0, speed = 0.28;
				for (int i = 0; i < 4000; i++, ticks++) {
					if (i % 17 == 0) {
						angle = random.nextDouble() * 6.283;
						speed = random.nextBoolean() ? 0.28 : 0.13;
					}
					double middle = n * cell / 2;
					if (Math.abs(w.x - middle) > middle - 6 || Math.abs(w.z - middle) > middle - 6) {
						angle = Math.atan2(middle - w.z, middle - w.x); // stay on the terrain
					}
					if (w.ground && random.nextInt(9) == 0) {
						w.vy = 0.42 / 0.98 + 0.08;
						w.ground = false;
					}
					// The collider sees the triangles near the player, as in the game.
					List<BotwTri> near = new ArrayList<>();
					int ci = (int) Math.floor(w.x / cell), cj = (int) Math.floor(w.z / cell);
					for (int a = Math.max(0, ci - 3); a <= Math.min(n - 1, ci + 3); a++) {
						for (int b = Math.max(0, cj - 3); b <= Math.min(n - 1, cj + 3); b++) {
							near.add(grid[a][b][0]);
							near.add(grid[a][b][1]);
						}
					}
					w.tick(near, Math.cos(angle) * speed, Math.sin(angle) * speed, surface);
					// Up to step height beneath is the kerb/riser allowance; deeper is a fall-through.
					check(w.lowestBelow > -0.65, "random terrain seed " + seed + " tick " + i + ": beneath by " + w.lowestBelow + w.recent);
				}
				worst = Math.min(worst, w.lowestBelow);
			}
			System.out.println("random terrain: 40 seeds, " + ticks + " ticks, deepest beneath the surface " + worst);
		}
		System.out.println("PASS TriCollider (walkable limit " + Math.round(LIMIT * 10) / 10.0 + " degrees): steeper slopes block at the foot; 30 degree slope, stairs, sloped risers, kerb climbed; steep face down/along free; diagonal slides; jump stays above; crossing a rising steep face and stepping down beside one never go beneath; random terrain never beneath");
	}
}
