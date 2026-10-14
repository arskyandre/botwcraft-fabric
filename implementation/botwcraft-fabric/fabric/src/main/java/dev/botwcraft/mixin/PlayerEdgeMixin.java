package dev.botwcraft.mixin;

import dev.botwcraft.link.BotwLink;
import dev.botwcraft.world.BotwCollision;
import dev.botwcraft.world.BotwEdge;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Crouching stops at the edges of the native ground as it does at the edges of blocks.
 * Minecraft looks for block collision under the player to decide where an edge is; the native
 * ground the player walks on isn't blocks (the player collides with its exact triangles), so
 * every direction looked like a drop. BotwCraft therefore switched edge detection off while
 * linked. BOTW adaptation: Minecraft's own edge logic stays on, and "can fall here" is also
 * answered from the triangles under the collider's footprint.
 */
@Mixin(Player.class)
public abstract class PlayerEdgeMixin {
	@Inject(method = "canFallAtLeast", at = @At("RETURN"), cancellable = true)
	private void botwcraft$nativeGroundCounts(double deltaX, double deltaZ, double minHeight, CallbackInfoReturnable<Boolean> cir) {
		if (cir.getReturnValue() && BotwLink.active()) {
			Player self = (Player) (Object) this;
			if (BotwCollision.usesSmoothCollider(self) && BotwEdge.groundBelow(self.getBoundingBox(), deltaX, deltaZ, minHeight, self.maxUpStep())) {
				cir.setReturnValue(false);
			}
		}
	}
}
