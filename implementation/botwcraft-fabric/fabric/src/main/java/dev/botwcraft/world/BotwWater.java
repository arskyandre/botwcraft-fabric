package dev.botwcraft.world;

import dev.botwcraft.link.BotwLink;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;

/**
 * BOTW's lakes, rivers and sea as Minecraft water: BOTW sends the water surface over the block
 * columns around the player (see WaterGrid in the protocol), and wherever Minecraft has air below
 * that surface, entities treat it as water, so the player swims, floats, sinks slowly and drowns
 * there as in Minecraft water. Only entity physics sees it; no blocks change.
 */
public final class BotwWater {
	private record Grid(int originX, int originZ, int size, float[] surface) {
	}

	private static volatile @Nullable Grid grid;

	private BotwWater() {
	}

	/** Once a frame on the client: pick up BOTW's latest grid. */
	public static void refresh() {
		BotwLink.WaterGrid read = BotwLink.readWaterGrid();
		if (read != null) {
			grid = new Grid(read.originX, read.originZ, read.size, read.surface);
		}
	}

	public static void clear() {
		grid = null;
	}

	public static boolean active() {
		return grid != null;
	}

	/** Minecraft y of BOTW's water surface over this column, or NaN where there is none. */
	public static double surfaceAt(int x, int z) {
		Grid g = grid;
		if (g == null) {
			return Double.NaN;
		}
		int dx = x - g.originX(), dz = z - g.originZ();
		if (dx < 0 || dz < 0 || dx >= g.size() || dz >= g.size()) {
			return Double.NaN;
		}
		float s = g.surface()[dz * g.size() + dx];
		return s < -1.0e20F ? Double.NaN : s;
	}

	/** How much of this block (0..1) is under BOTW's water; 0 above the surface. */
	public static float depthIn(BlockPos pos) {
		double s = surfaceAt(pos.getX(), pos.getZ());
		if (Double.isNaN(s)) {
			return 0.0F;
		}
		double h = s - pos.getY();
		return h < 0.02 ? 0.0F : (float) Math.min(1.0, h);
	}

	/** True if BOTW water reaches up into the box of block cells (inclusive). */
	public static boolean anyIn(int x0, int y0, int z0, int x1, int y1, int z1) {
		if (grid == null) {
			return false;
		}
		for (int x = x0; x <= x1; x++) {
			for (int z = z0; z <= z1; z++) {
				double s = surfaceAt(x, z);
				if (!Double.isNaN(s) && s > y0) {
					return true;
				}
			}
		}
		return false;
	}

	/** BOTW water in an otherwise empty (air) Minecraft cell, as a Minecraft fluid; null if none. */
	public static @Nullable FluidState fluidAt(BlockGetter level, BlockPos pos) {
		if (depthIn(pos) <= 0.0F || !level.getBlockState(pos).isAir()) {
			return null;
		}
		return Fluids.WATER.getSource(false);
	}

	/** BOTW bridge: what a block's own fluid check has to see: the bridged water where the block has none. */
	public static FluidState fluidOr(BlockGetter level, BlockPos pos, FluidState state) {
		if (state.isEmpty() && grid != null) {
			FluidState water = fluidAt(level, pos);
			if (water != null) {
				return water;
			}
		}
		return state;
	}

	/**
	 * BOTW bridge: where a look from {@code from} to the hit it ended in first goes under the bridged
	 * water, as a hit on that water's surface; the hit itself where it does not. For what is used on
	 * water (a boat): Minecraft finds its own water as a block on the way, and this water is in none.
	 */
	public static BlockHitResult surfaceHit(BlockGetter level, Vec3 from, BlockHitResult hit) {
		if (grid == null || under(level, from)) {
			return hit;
		}
		Vec3 to = hit.getLocation();
		int steps = Math.max(1, (int) Math.ceil(from.distanceTo(to) / 0.1));
		Vec3 above = from;
		for (int i = 1; i <= steps; i++) {
			Vec3 point = from.lerp(to, (double) i / steps);
			if (under(level, point)) {
				Vec3 below = point;
				for (int k = 0; k < 8; k++) {
					Vec3 middle = above.lerp(below, 0.5);
					if (under(level, middle)) {
						below = middle;
					} else {
						above = middle;
					}
				}
				return new BlockHitResult(below, Direction.UP, BlockPos.containing(below), false);
			}
			above = point;
		}
		return hit;
	}

	private static boolean under(BlockGetter level, Vec3 point) {
		BlockPos pos = BlockPos.containing(point);
		double s = surfaceAt(pos.getX(), pos.getZ());
		return !Double.isNaN(s) && point.y < s && level.getBlockState(pos).isAir();
	}

	/** The exact water height in a cell only BOTW fills (so floating matches its surface); -1 otherwise. */
	public static float substitutedHeight(BlockGetter level, BlockPos pos) {
		float depth = depthIn(pos);
		if (depth <= 0.0F || !level.getFluidState(pos).isEmpty() || !level.getBlockState(pos).isAir()) {
			return -1.0F;
		}
		return depth;
	}
}
