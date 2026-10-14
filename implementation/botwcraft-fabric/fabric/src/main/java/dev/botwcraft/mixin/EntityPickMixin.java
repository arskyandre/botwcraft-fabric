package dev.botwcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.botwcraft.world.BotwClip;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * The crosshair targets BOTW surfaces: blocks can be placed on terrain and walls, and NPCs behind a
 * wall can't be hit through it. A BOTW hit points at the cell a placed block would occupy.
 */
@Mixin(Entity.class)
public abstract class EntityPickMixin {
	@WrapOperation(
		method = "pick",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;")
	)
	private BlockHitResult botwcraft$pickBotw(Level level, ClipContext context, Operation<BlockHitResult> original) {
		return BotwClip.refine(context.getFrom(), context.getTo(), original.call(level, context), BotwClip.Use.PICK);
	}
}
