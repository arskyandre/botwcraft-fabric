package dev.botwcraft.client.mixin;

import dev.botwcraft.client.BotwClient;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Minecraft.class)
public abstract class MinecraftMixin {
	@Inject(method = "runTick", at = @At("HEAD"))
	private void botwcraft$beginFrame(boolean advanceGameTime, CallbackInfo ci) {
		BotwClient.beginFrame();
	}

	@Inject(
		method = "renderFrame",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/GameRenderer;render()V", shift = At.Shift.AFTER)
	)
	private void botwcraft$afterRender(boolean advanceGameTime, CallbackInfo ci) {
		BotwClient.afterRender();
	}

	@Inject(method = "renderFrame", at = @At("TAIL"))
	private void botwcraft$pace(boolean advanceGameTime, CallbackInfo ci) {
		BotwClient.paceFrame();
	}
}
