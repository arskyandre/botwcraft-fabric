package dev.botwcraft.world;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.phys.AABB;

/**
 * BOTW adaptation: where the native ground ends, for crouching at edges. Minecraft finds an edge
 * by looking for block collision under the player; the native ground is triangles, so this
 * answers the same question for them with the collider's own footprint.
 */
public final class BotwEdge {
	private BotwEdge() {
	}

	/**
	 * True if the player, moved by (dx, dz), would still have native ground to stand on within
	 * {@code depth} below the feet. While the feet rest on a native surface that is not walkable
	 * (a steep face landed on from above) nothing counts as an edge, so crouching cannot freeze
	 * the player there. On a Minecraft block with no native ground under it the answer is false
	 * and Minecraft's own block rule decides.
	 */
	public static boolean groundBelow(AABB box, double dx, double dz, double depth, double step) {
		double x = (box.minX + box.maxX) * 0.5, y = box.minY, z = (box.minZ + box.maxZ) * 0.5;
		List<BotwTri> tris = new ArrayList<>();
		BotwCollision.trianglesNear(new AABB(x - 1, y - depth - 0.5, z - 1, x + 1, y + step + 0.5, z + 1).expandTowards(dx, 0, dz), tris);
		if (tris.isEmpty()) {
			return false;
		}
		if (!TriCollider.walkableBelow(tris, x, y, z, depth, step)) {
			return TriCollider.restsOnSteep(tris, x, y, z);
		}
		return TriCollider.walkableBelow(tris, x + dx, y, z + dz, depth, step);
	}
}
