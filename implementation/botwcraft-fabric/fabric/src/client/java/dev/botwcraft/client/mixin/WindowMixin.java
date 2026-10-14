package dev.botwcraft.client.mixin;

import com.mojang.blaze3d.platform.Window;
import dev.botwcraft.client.BotwClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** The MC window is hidden while linked; BOTW has the real focus, so pretend we do too. */
@Mixin(Window.class)
public abstract class WindowMixin {
	@Inject(method = "isFocused", at = @At("HEAD"), cancellable = true)
	private void botwcraft$focused(CallbackInfoReturnable<Boolean> cir) {
		if (BotwClient.tookOver()) {
			// Focused while BOTW is connected; if BOTW goes away, act unfocused so MC
			// never tries to grab the (hidden) mouse.
			cir.setReturnValue(BotwClient.linked());
		}
	}

	@Inject(method = "isIconified", at = @At("HEAD"), cancellable = true)
	private void botwcraft$notIconified(CallbackInfoReturnable<Boolean> cir) {
		if (BotwClient.linked()) {
			cir.setReturnValue(false);
		}
	}
}
