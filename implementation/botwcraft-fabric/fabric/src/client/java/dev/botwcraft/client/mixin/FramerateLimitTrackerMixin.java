package dev.botwcraft.client.mixin;

import com.mojang.blaze3d.platform.FramerateLimitTracker;
import dev.botwcraft.client.BotwClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** BotwClient.paceFrame() locks us to BOTW's frame rate; don't let MC throttle on its own. */
@Mixin(FramerateLimitTracker.class)
public abstract class FramerateLimitTrackerMixin {
	@Inject(method = "getFramerateLimit", at = @At("HEAD"), cancellable = true)
	private void botwcraft$unlimited(CallbackInfoReturnable<Integer> cir) {
		if (BotwClient.linked()) {
			cir.setReturnValue(260);
		}
	}
}
