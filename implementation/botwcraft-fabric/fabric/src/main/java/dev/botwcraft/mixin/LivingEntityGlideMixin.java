package dev.botwcraft.mixin;

import dev.botwcraft.link.BotwLink;
import dev.botwcraft.rune.BotwRunes;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.gameevent.GameEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * BOTW bridge (report 76). Steve glides as with an elytra that never wears out, with
 * no item (the chest slot stays free), only after the Old Man has given the paraglider, and rockets boost
 * it. So a player may glide with nothing worn once the other game's player owns the paraglider
 * (BotwRunes.paraglider). Everything else is Minecraft's own gliding: the jump key in the air starts it, the
 * ground ends it, a firework rocket in the hand pushes.
 */
@Mixin(LivingEntity.class)
public abstract class LivingEntityGlideMixin {
	@Inject(method = "canGlide", at = @At("RETURN"), cancellable = true)
	private void botwcraft$paraglider(CallbackInfoReturnable<Boolean> cir) {
		LivingEntity self = (LivingEntity) (Object) this;
		if (!cir.getReturnValue() && self instanceof Player && BotwRunes.paraglider && BotwLink.active() && !self.onGround() && !self.isPassenger()
			&& !self.hasEffect(MobEffects.LEVITATION)) {
			cir.setReturnValue(true);
		}
	}

	/**
	 * Every second of a glide Minecraft wears one of the worn things that let the body glide, picked at
	 * random. With none worn there is nothing to pick from (and the pick fails on an empty list): the glide
	 * is told to the world as usual and nothing is worn out.
	 */
	@Inject(method = "updateFallFlying", at = @At(value = "INVOKE", target = "Ljava/util/List;stream()Ljava/util/stream/Stream;"), cancellable = true)
	private void botwcraft$nothingToWearOut(CallbackInfo ci) {
		LivingEntity self = (LivingEntity) (Object) this;
		for (EquipmentSlot slot : EquipmentSlot.VALUES) {
			if (LivingEntity.canGlideUsing(self.getItemBySlot(slot), slot)) {
				return;
			}
		}
		self.gameEvent(GameEvent.ELYTRA_GLIDE);
		ci.cancel();
	}
}
