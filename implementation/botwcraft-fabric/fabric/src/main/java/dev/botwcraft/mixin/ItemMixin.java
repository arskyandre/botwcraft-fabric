package dev.botwcraft.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.botwcraft.world.BotwClip;
import dev.botwcraft.world.BotwWater;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** Buckets, boats and other "use on what I'm looking at" items see BOTW surfaces. */
@Mixin(Item.class)
public abstract class ItemMixin {
	@WrapOperation(
		method = "getPlayerPOVHitResult",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;")
	)
	private static BlockHitResult botwcraft$povBotw(Level level, ClipContext context, Operation<BlockHitResult> original) {
		// These items act on hitPos.relative(face), so report the cell the surface is in.
		return BotwClip.refine(context.getFrom(), context.getTo(), original.call(level, context), BotwClip.Use.PROJECTILE);
	}

	/** BOTW bridge: what is used on any fluid's surface (a boat) finds the bridged water's surface. */
	@ModifyReturnValue(method = "getPlayerPOVHitResult", at = @At("RETURN"))
	private static BlockHitResult botwcraft$povBridgedWater(BlockHitResult hit, Level level, Player player, ClipContext.Fluid fluid) {
		if (fluid != ClipContext.Fluid.ANY) {
			return hit;
		}
		BlockHitResult water = BotwWater.surfaceHit(level, player.getEyePosition(), hit);
		if (water != hit) {
			dev.botwcraft.BotwCraft.LOG.info("BotwCraft: use on the bridged water at {} (the look went on to {} {})", water.getLocation(), hit.getType(), hit.getLocation());
		}
		return water;
	}
}
