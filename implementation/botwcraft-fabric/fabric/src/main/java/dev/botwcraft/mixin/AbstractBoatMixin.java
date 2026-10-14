package dev.botwcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.botwcraft.world.BotwWater;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.vehicle.boat.AbstractBoat;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.material.FluidState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * BOTW bridge: boats float on the bridged world's water. A boat does not go by the entity fluid
 * check ({@link EntityFluidInteractionMixin}); it reads the fluid of the blocks around it itself,
 * and the bridged water is in no block (boats sank). See {@link BotwWater}.
 */
@Mixin(AbstractBoat.class)
public abstract class AbstractBoatMixin {
	@WrapOperation(
		method = {"getWaterLevelAbove", "checkInWater", "isUnderwater", "checkFallDamage"},
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;getFluidState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/material/FluidState;")
	)
	private FluidState botwcraft$bridgedWater(Level level, BlockPos pos, Operation<FluidState> original) {
		return BotwWater.fluidOr(level, pos, original.call(level, pos));
	}

	@WrapOperation(
		method = {"getWaterLevelAbove", "checkInWater", "isUnderwater"},
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
