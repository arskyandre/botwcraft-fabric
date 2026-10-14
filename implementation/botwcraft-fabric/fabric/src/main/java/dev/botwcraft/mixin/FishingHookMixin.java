package dev.botwcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.botwcraft.world.BotwWater;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.projectile.FishingHook;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.material.FluidState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * BOTW bridge: a fishing bobber floats on the bridged world's water as on Minecraft's, for the same
 * reason as a boat ({@link AbstractBoatMixin}): it reads the fluid of its block itself.
 */
@Mixin(FishingHook.class)
public abstract class FishingHookMixin {
	@WrapOperation(
		method = "tick",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;getFluidState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/material/FluidState;")
	)
	private FluidState botwcraft$bridgedWater(Level level, BlockPos pos, Operation<FluidState> original) {
		return BotwWater.fluidOr(level, pos, original.call(level, pos));
	}

	@WrapOperation(
		method = "tick",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/level/material/FluidState;getHeight(Lnet/minecraft/world/level/BlockGetter;Lnet/minecraft/core/BlockPos;)F"
		)
	)
	private float botwcraft$bridgedWaterHeight(FluidState state, BlockGetter level, BlockPos pos, Operation<Float> original) {
		float height = BotwWater.active() ? BotwWater.substitutedHeight(level, pos) : -1.0F;
		return height >= 0.0F ? height : original.call(state, level, pos);
	}
}
