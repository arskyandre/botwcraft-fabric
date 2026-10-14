package dev.botwcraft.client;

import dev.botwcraft.link.BotwLink;
import dev.botwcraft.world.BotwClimb;
import dev.botwcraft.world.BotwCollision;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.tags.BlockTags;
import net.minecraft.util.Mth;
import net.minecraft.world.phys.AABB;

/** BOTW bridge: decides each tick whether the player is on one of the other game's walls (BotwClimb). */
public final class BotwClimbClient {
	private BotwClimbClient() {
	}

	/** How far from the body a wall still counts, in blocks (the ladders' reach). */
	private static final double REACH = 0.2;
	/** A wall must reach this much above the ground the jump began on: a step of one block is jumped onto, not climbed. */
	private static final double TALLER_THAN = 1.05;

	private static boolean wasOnGround;

	public static void tick(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		if (BotwClimb.staminaKnown()) {
			if (BotwClimb.stamina <= 0.5F) {
				BotwClimb.exhausted = true;
			} else if (BotwClimb.stamina >= BotwClimb.most - 0.5F) {
				BotwClimb.exhausted = false;
			}
		} else {
			BotwClimb.exhausted = false;
		}
		if (player == null || BotwClimb.SPEED <= 0.0 || BotwClimb.exhausted || !BotwLink.active() || !player.isAlive() || player.isSpectator()
			|| player.getAbilities().flying || player.isFallFlying() || player.isInWater() || player.isPassenger() || player.noPhysics) {
			BotwClimb.clear();
			return;
		}
		var keys = player.input.keyPresses;
		// A jump: the tick the feet leave the ground going up with the jump key held. Over on the ground again.
		boolean ground = player.onGround();
		if (ground) {
			BotwClimb.jumped = false;
			BotwClimb.groundY = player.getY();
		} else if (wasOnGround && keys.jump() && player.getDeltaMovement().y > 0.0) {
			BotwClimb.jumped = true;
		}
		wasOnGround = ground;
		AABB box = player.getBoundingBox();
		boolean on = BotwClimb.climbing;
		// To start, only a wall that reaches more than a block above the ground the jump began on counts.
		double[] to = on ? BotwCollision.climbAt(box, REACH) : BotwCollision.climbAt(box, REACH, BotwClimb.groundY + TALLER_THAN);
		if (to == null) {
			on = false;
		} else if (!on) {
			// It starts in the air after a jump, pushing into the wall and facing it.
			float yaw = player.getYRot() * Mth.DEG_TO_RAD;
			on = !ground && BotwClimb.jumped && keys.forward() && player.horizontalCollision && -Mth.sin(yaw) * to[0] + Mth.cos(yaw) * to[1] >= 0.3;
		} else if (ground && !keys.forward()) {
			on = false;
		}
		if (on && !BotwClimb.climbing) {
			var t = BotwCollision.climbTri;
			dev.botwcraft.BotwCraft.LOG.info("BotwCraft: climb starts at {} {} {}; surface normal {} {} {}, terrain {}, diggable {}, material {}, the game's wall code {} and material {} (sub {}), stamina {} of {}",
				String.format("%.2f", player.getX()), String.format("%.2f", player.getY()), String.format("%.2f", player.getZ()),
				t == null ? "" : String.format("%.2f", t.nx), t == null ? "" : String.format("%.2f", t.ny), t == null ? "" : String.format("%.2f", t.nz),
				t != null && t.terrain, t != null && t.diggable, t == null ? -1 : t.material, t == null ? -1 : t.wallCode, t == null || t.gameMaterial < 0 ? -1 : t.gameMaterial & 63, t == null || t.gameMaterial < 0 ? -1 : t.gameMaterial >> 6,
				BotwClimb.stamina, BotwClimb.most);
		}
		boolean ladder = on && (BotwCollision.ladderAt(box, REACH) || player.getInBlockState().is(BlockTags.CLIMBABLE));
		BotwClimb.who = player.getUUID();
		BotwClimb.climbing = on;
		BotwClimb.wall = on && !ladder;
		BotwClimb.pushing = on && !ladder && (keys.forward() || keys.jump());
		BotwClimb.rising = on && !ladder && !player.onGround() && player.getY() > player.yo + 1.0E-3;
	}
}
