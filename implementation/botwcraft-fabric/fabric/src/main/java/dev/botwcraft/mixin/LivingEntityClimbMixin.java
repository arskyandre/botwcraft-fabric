package dev.botwcraft.mixin;

import dev.botwcraft.link.BotwLink;
import dev.botwcraft.world.BotwClimb;
import dev.botwcraft.world.BotwCollision;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyConstant;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * BOTW bridge (report 75). Steve could not climb the game's ladders up
 * and down. A surface the other
 * game's material calls a ladder (Proto.TRI_LADDER) is a climbable block for a player who touches it:
 * Minecraft's own rules then apply (up while pushing into it or jumping, slowly down otherwise, held
 * while sneaking, no fall damage). On both sides of the single-player game: the server resets the fall
 * distance by the same answer. Not while flying.
 * <p>
 * The same for a wall of the other game's that the player climbs (BotwClimb), at that climb's own speed.
 */
@Mixin(LivingEntity.class)
public abstract class LivingEntityClimbMixin {
	@Inject(method = "onClimbable", at = @At("RETURN"), cancellable = true)
	private void botwcraft$nativeLadders(CallbackInfoReturnable<Boolean> cir) {
		if (!cir.getReturnValue() && (Object) this instanceof Player self && BotwLink.active() && !self.isSpectator() && !self.getAbilities().flying
			&& !self.isFallFlying() && (BotwClimb.climbing && BotwClimb.is(self) || BotwCollision.ladderAt(self.getBoundingBox(), 0.2))) {
			cir.setReturnValue(true);
		}
	}

	// Minecraft's ladder lifts the body in the ticks it touches the ladder. A wall of the other game's leans and
	// bulges: the body touched it in one tick of three (route-361: 0.33 blocks a second for 0.8). On a wall the
	// held key is enough.
	@Redirect(method = "handleRelativeFrictionAndCalculateMovement",
		at = @At(value = "FIELD", target = "Lnet/minecraft/world/entity/LivingEntity;horizontalCollision:Z", opcode = org.objectweb.asm.Opcodes.GETFIELD))
	private boolean botwcraft$wallPush(LivingEntity self) {
		return self.horizontalCollision || BotwClimb.pushing && self instanceof Player && BotwClimb.is(self);
	}

	// The speed up a ladder (0.2 before gravity and drag) is the wall's own on a wall.
	@ModifyConstant(method = "handleRelativeFrictionAndCalculateMovement", constant = @Constant(doubleValue = 0.2))
	private double botwcraft$wallSpeed(double up) {
		return BotwClimb.wall && (Object) this instanceof Player self && BotwClimb.is(self) ? BotwClimb.UP : up;
	}
}
