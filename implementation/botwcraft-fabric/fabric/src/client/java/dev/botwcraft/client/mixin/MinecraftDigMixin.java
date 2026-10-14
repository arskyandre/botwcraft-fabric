package dev.botwcraft.client.mixin;

import dev.botwcraft.client.BotwDigClient;
import dev.botwcraft.client.BotwSwing;
import net.minecraft.client.Minecraft;
import net.minecraft.world.InteractionHand;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Attacking BOTW's geometry digs into it (BotwDigClient). */
@Mixin(Minecraft.class)
public abstract class MinecraftDigMixin {
	@Inject(method = "startAttack", at = @At("HEAD"), cancellable = true)
	private void botwcraft$digStart(CallbackInfoReturnable<Boolean> cir) {
		Minecraft minecraft = (Minecraft) (Object) this;
		if (BotwDigClient.attack(minecraft)) {
			BotwSwing.click(minecraft);
			// A swing, not a miss: no miss cooldown before mining the block that appears.
			var held = minecraft.player.getItemInHand(InteractionHand.MAIN_HAND);
			minecraft.player.swing(InteractionHand.MAIN_HAND, held.getAttackAnimation(), false);
			cir.setReturnValue(true);
		}
	}

	/** BOTW bridge: the click acts on what the crosshair is on; the other game swings its weapon there (BotwSwing). */
	@Inject(method = "startAttack", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/phys/HitResult;getType()Lnet/minecraft/world/phys/HitResult$Type;"))
	private void botwcraft$swing(CallbackInfoReturnable<Boolean> cir) {
		BotwSwing.click((Minecraft) (Object) this);
	}

	@Inject(method = "continueAttack", at = @At("HEAD"))
	private void botwcraft$digHold(boolean down, CallbackInfo ci) {
		if (down) {
			BotwDigClient.attack((Minecraft) (Object) this);
		}
	}
}
