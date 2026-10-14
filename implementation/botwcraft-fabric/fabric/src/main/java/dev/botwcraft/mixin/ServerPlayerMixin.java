package dev.botwcraft.mixin;

import dev.botwcraft.BotwCraft;
import dev.botwcraft.combat.BotwCombat;
import dev.botwcraft.combat.BotwActorEntity;
import dev.botwcraft.link.Proto;
import dev.botwcraft.link.BotwLink;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.server.level.ServerPlayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ServerPlayer.class)
public abstract class ServerPlayerMixin {
	/** Critical hits on a BOTW actor are flagged so BOTW can play them up. */
	@Inject(method = "crit", at = @At("HEAD"))
	private void botwcraft$critBotw(Entity entity, CallbackInfo ci) {
		if (entity instanceof BotwActorEntity proxy) {
			proxy.markCritical();
		}
	}

	/** Dying in Minecraft is dying in BOTW: the host's through the link, a guest's through theirs. */
	@Inject(method = "die", at = @At("HEAD"))
	private void botwcraft$diesInBotw(DamageSource source, CallbackInfo ci) {
		ServerPlayer self = (ServerPlayer) (Object) this;
		int attacker = BotwCombat.attackerFormId(source), cause = BotwCombat.deathCause(source);
		if (!dev.botwcraft.net.BotwNet.isHost(self)) {
			if (net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking.canSend(self, dev.botwcraft.net.BotwNet.Died.TYPE)) {
				net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking.send(self, new dev.botwcraft.net.BotwNet.Died(attacker, cause));
			}
			BotwCraft.LOG.info("BotwCraft: guest {} died ({}); telling their BOTW", self.getPlainTextName(), source.getMsgId());
			return;
		}
		if (BotwLink.active()) {
			BotwLink.pushEvent(Proto.EV_PLAYER_DIED, attacker, 0, 0, 0, 0, cause);
			BotwCraft.LOG.info("BotwCraft: player died ({}, cause {}); telling BOTW", source.getMsgId(), cause);
		}
	}

	/**
	 * BOTW bridge: a rune's item is not thrown away. The key for it does nothing: the
	 * item stays in its slot, and the client, which had taken it out of its own copy, is told the inventory again.
	 */
	@Inject(method = "drop(Z)V", at = @At("HEAD"), cancellable = true)
	private void botwcraft$keepsRune(boolean all, CallbackInfo ci) {
		ServerPlayer self = (ServerPlayer) (Object) this;
		if (dev.botwcraft.rune.BotwRunes.of(self.getInventory().getSelectedItem()) != 0) {
			self.containerMenu.sendAllDataToRemote();
			ci.cancel();
		}
	}

	/**
	 * BOTW bridge: a rune's item that would lie in the world (thrown out of a screen, or at death) does not: it
	 * is gone, and the player has it again when the other game next says he owns the rune (BotwRunes.owned).
	 */
	@Inject(method = "drop(Lnet/minecraft/world/item/ItemStack;ZLnet/minecraft/util/Prediction;)Lnet/minecraft/world/entity/item/ItemEntity;", at = @At("HEAD"), cancellable = true)
	private void botwcraft$runeDoesNotLie(net.minecraft.world.item.ItemStack stack, boolean scatter, net.minecraft.util.Prediction prediction, CallbackInfoReturnable<net.minecraft.world.entity.item.ItemEntity> cir) {
		if (dev.botwcraft.rune.BotwRunes.of(stack) != 0) {
			cir.setReturnValue(null);
		}
	}
}
