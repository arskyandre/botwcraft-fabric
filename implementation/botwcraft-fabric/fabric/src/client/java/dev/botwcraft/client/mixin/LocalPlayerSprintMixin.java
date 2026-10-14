package dev.botwcraft.client.mixin;

import dev.botwcraft.link.BotwLink;
import dev.botwcraft.world.BotwClimb;
import net.minecraft.client.player.LocalPlayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * BOTW bridge: no sprint while Link's stamina is used up (BotwClimb.exhausted, until it is full again), as Link
 * cannot run then. The player's sprint is the game's dash, which costs that stamina (the bridge's walk.h).
 */
@Mixin(LocalPlayer.class)
public abstract class LocalPlayerSprintMixin {
	@Inject(method = "isSprintingPossible", at = @At("HEAD"), cancellable = true)
	private void botwcraft$noSprintWhenTired(boolean flying, CallbackInfoReturnable<Boolean> cir) {
		if (BotwClimb.exhausted && BotwLink.active()) {
			cir.setReturnValue(false);
		}
	}
}
