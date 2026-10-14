package dev.botwcraft.client;

import dev.botwcraft.world.BotwCollision;
import dev.botwcraft.world.BotwTri;
import dev.botwcraft.world.TriCollider;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Feeds the local player's movement through {@link TriCollider} against nearby BOTW triangles. */
public final class BotwCollider {
	private BotwCollider() {
	}

	public static Vec3 collide(LocalPlayer player, Vec3 move) {
		AABB box = player.getBoundingBox();
		double step = player.maxUpStep();
		List<BotwTri> tris = new ArrayList<>();
		BotwCollision.trianglesNear(box.expandTowards(move).inflate(1.0, 1.0 + step, 1.0), tris);
		if (tris.isEmpty()) {
			return move;
		}
		// BOTW bridge (report 75). On a ladder the body steps as on the ground: a lip at the top of a BOTW ladder
		// (route-274: Steve went up 4 blocks and then hung at the top, 0.1 over the floor he could not get onto)
		// is stepped over, and the floor there takes the feet, as Minecraft's own ladder ends level with its floor.
		boolean climbs = player.onClimbable();
		double[] r = TriCollider.resolve(
			tris, (box.minX + box.maxX) * 0.5, box.minY, (box.minZ + box.maxZ) * 0.5, box.getXsize() * 0.5, box.getYsize(), step, player.onGround() || climbs,
			move.x, move.y, move.z
		);
		if (r[0] == move.x && r[1] == move.y && r[2] == move.z) {
			return move;
		}
		// The triangle pass (snapping down a slope, pushing out of a wall) can move the player into a
		// Minecraft block placed on the terrain; collide that result with Minecraft blocks again.
		// Solid stand-ins of BOTW's actors count there too, as in vanilla's own pass before this one.
		Vec3 resolved = new Vec3(r[0], r[1], r[2]);
		return Entity.collideBoundingBox(player, resolved, box, player.level(), player.level().getEntityCollisions(player, box.expandTowards(resolved)));
	}

	/** Highest BOTW surface at or below {@code maxAbove} over the feet at (x, y, z), or NaN. */
	public static double groundAt(double x, double y, double z, double maxAbove) {
		List<BotwTri> tris = new ArrayList<>();
		BotwCollision.trianglesNear(new AABB(x - 1, y - 4, z - 1, x + 1, y + maxAbove + 1, z + 1), tris);
		return TriCollider.groundAt(tris, x, y, z, maxAbove);
	}
}
