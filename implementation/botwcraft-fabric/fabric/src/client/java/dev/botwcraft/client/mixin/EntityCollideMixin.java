package dev.botwcraft.client.mixin;

import dev.botwcraft.client.BotwClient;
import dev.botwcraft.client.BotwCollider;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * After vanilla has collided the local player's movement with Minecraft blocks, collide it with
 * BOTW's exact triangles (smooth slopes instead of voxel stair-steps).
 */
@Mixin(Entity.class)
public abstract class EntityCollideMixin {
	@Inject(method = "collide", at = @At("RETURN"), cancellable = true)
	private void botwcraft$smoothBotwCollision(Vec3 movement, CallbackInfoReturnable<Vec3> cir) {
		if ((Object) this instanceof LocalPlayer player && BotwClient.linked() && !player.noPhysics) {
			Vec3 moved = BotwCollider.collide(player, cir.getReturnValue());
			cir.setReturnValue(moved);
			// What collision took from the step: the player pushes against that (BotwClient: McState.pushX/Z).
			BotwClient.blockedX = movement.x - moved.x;
			BotwClient.blockedZ = movement.z - moved.z;
		}
	}
}
