package dev.botwcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.botwcraft.combat.BotwActorEntity;
import dev.botwcraft.link.Proto;
import dev.botwcraft.link.BotwLink;
import dev.botwcraft.world.BotwClip;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.projectile.arrow.AbstractArrow;
import net.minecraft.world.entity.projectile.arrow.Arrow;
import net.minecraft.world.entity.projectile.arrow.SpectralArrow;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Arrows and tridents hit BOTW's exact surfaces. They then stick where they hit: the block state
 * there is air, the same as what they recorded on impact, so vanilla never makes them fall out.
 */
@Mixin(AbstractArrow.class)
public abstract class AbstractArrowMixin {
	@WrapOperation(
		method = "tick",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;clipIncludingBorder(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;")
	)
	private BlockHitResult botwcraft$hitBotw(Level level, ClipContext context, Operation<BlockHitResult> original) {
		return BotwClip.refine(context.getFrom(), context.getTo(), original.call(level, context), BotwClip.Use.PROJECTILE);
	}

	@Unique
	private Vec3 botwcraft$hitAt;

	@Inject(method = "onHitEntity", at = @At("HEAD"))
	private void botwcraft$rememberHit(EntityHitResult hitResult, CallbackInfo ci) {
		this.botwcraft$hitAt = hitResult.getLocation();
	}

	/**
	 * Where Minecraft counts an arrow as stuck in a creature (it hurt it and didn't pierce): if that
	 * creature is a BOTW NPC's stand-in, BOTW pins the arrow to the NPC's skeleton.
	 */
	@WrapOperation(method = "onHitEntity", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/entity/LivingEntity;setArrowCount(I)V"))
	private void botwcraft$stickInBotwActor(LivingEntity mob, int count, Operation<Void> original) {
		original.call(mob, count);
		if (!(mob instanceof BotwActorEntity actor) || this.botwcraft$hitAt == null || !BotwLink.active()) {
			return;
		}
		AbstractArrow self = (AbstractArrow) (Object) this;
		Vec3 v = self.getDeltaMovement();
		float yaw = (float) (Mth.atan2(v.x, v.z) * Mth.RAD_TO_DEG);
		float pitch = (float) (Mth.atan2(v.y, v.horizontalDistance()) * Mth.RAD_TO_DEG);
		int texture = self instanceof SpectralArrow ? 2 : self instanceof Arrow tippable && tippable.getColor() > 0 ? 1 : 0;
		Vec3 at = this.botwcraft$hitAt;
		BotwLink.pushEvent(Proto.EV_ARROW_STUCK, actor.formId(), (float) at.x, (float) at.y, (float) at.z, yaw, Float.floatToRawIntBits(pitch), texture);
	}

	/**
	 * BOTW bridge: the player's arrow that ends on the other game's collision is a blow there, along its
	 * flight (Proto.SWING_ARROW): an arrow from a Minecraft bow counts as
	 * a hit on an object held by Stasis.
	 */
	@Inject(method = "onHitBlock", at = @At("HEAD"))
	private void botwcraft$blowOfTheArrow(BlockHitResult hit, CallbackInfo ci) {
		AbstractArrow self = (AbstractArrow) (Object) this;
		if (!(hit instanceof BotwClip.BotwHitResult) || self.level().isClientSide() || !BotwLink.active() || !(self.getOwner() instanceof Player)) {
			return;
		}
		Vec3 v = self.getDeltaMovement();
		if (v.lengthSqr() < 1.0E-6) {
			return;
		}
		float yaw = (float) (Mth.atan2(v.x, v.z) * Mth.RAD_TO_DEG);
		float pitch = (float) (Mth.atan2(v.y, v.horizontalDistance()) * Mth.RAD_TO_DEG);
		Vec3 at = hit.getLocation();
		BotwLink.pushEvent(Proto.EV_SWING, Float.floatToRawIntBits(pitch), (float) at.x, (float) at.y, (float) at.z, yaw, Proto.SWING_ARROW, Proto.WEAPON_ARROW);
	}
}
