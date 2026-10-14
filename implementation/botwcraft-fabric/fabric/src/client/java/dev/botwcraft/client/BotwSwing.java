package dev.botwcraft.client;

import dev.botwcraft.combat.BotwActorEntity;
import dev.botwcraft.link.Proto;
import dev.botwcraft.link.BotwLink;
import dev.botwcraft.world.BotwClip;
import net.minecraft.client.Minecraft;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.tags.ItemTags;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

/**
 * BOTW bridge: every attack click of the player's is told to the other game, which swings a weapon
 * of its own through where the look ends (Proto.EV_SWING). What is held is told too when its class
 * changes, so the other game has the weapon ready before the first click.
 */
public final class BotwSwing {
	private static int toldHeld = -1;

	private BotwSwing() {
	}

	/** The attack click is about to act on what the crosshair is on (Minecraft.startAttack). */
	public static void click(Minecraft minecraft) {
		HitResult result = minecraft.hitResult;
		if (minecraft.player == null || result == null || !BotwLink.active()) {
			return;
		}
		int on;
		int formId = 0;
		if (result instanceof EntityHitResult entity) {
			if (!(entity.getEntity() instanceof BotwActorEntity actor)) {
				return; // a Minecraft mob: nothing of the other game's is there
			}
			on = Proto.SWING_ACTOR;
			formId = actor.formId();
		} else if (result instanceof BotwClip.BotwHitResult) {
			on = Proto.SWING_SURFACE;
		} else if (result.getType() == HitResult.Type.MISS) {
			on = Proto.SWING_AIR;
		} else {
			return; // a Minecraft block
		}
		ItemStack held = minecraft.player.getMainHandItem();
		Vec3 at = result.getLocation();
		float far = (float) minecraft.player.getEyePosition().distanceTo(at);
		BotwLink.pushEvent(Proto.EV_SWING, formId, (float) at.x, (float) at.y, (float) at.z, far, on | tier(held) << 8, weaponClass(held));
	}

	/** Each client tick: a change of the held item's class or tier is told ahead of any click. */
	public static void tick(Minecraft minecraft) {
		if (minecraft.player == null || !BotwLink.active()) {
			toldHeld = -1;
			return;
		}
		ItemStack held = minecraft.player.getMainHandItem();
		int now = weaponClass(held) | tier(held) << 8;
		if (now != toldHeld) {
			toldHeld = now;
			BotwLink.pushEvent(Proto.EV_SWING, 0, 0.0F, 0.0F, 0.0F, 0.0F, Proto.SWING_HELD | tier(held) << 8, weaponClass(held));
		}
	}

	private static int weaponClass(ItemStack held) {
		if (held.isEmpty()) {
			return Proto.WEAPON_UNARMED;
		}
		if (held.is(ItemTags.SWORDS)) {
			return Proto.WEAPON_BLADE;
		}
		if (held.is(ItemTags.AXES)) {
			return Proto.WEAPON_AXE;
		}
		if (held.is(ItemTags.PICKAXES) || held.is(Items.MACE)) {
			return Proto.WEAPON_PICK;
		}
		if (held.is(Items.TRIDENT) || held.is(ItemTags.SPEARS)) {
			return Proto.WEAPON_PIERCE;
		}
		return Proto.WEAPON_BLUNT;
	}

	/** 0 no tool, 1 wood or gold, 2 stone or copper, 3 iron, 4 diamond, 5 netherite (by the item's name, as Minecraft names its tools). */
	private static int tier(ItemStack held) {
		if (held.isEmpty() || !(held.is(ItemTags.SWORDS) || held.is(ItemTags.AXES) || held.is(ItemTags.PICKAXES) || held.is(ItemTags.SHOVELS)
			|| held.is(ItemTags.HOES) || held.is(ItemTags.SPEARS))) {
			return 0;
		}
		String name = BuiltInRegistries.ITEM.getKey(held.getItem()).getPath();
		if (name.startsWith("netherite_")) {
			return 5;
		}
		if (name.startsWith("diamond_")) {
			return 4;
		}
		if (name.startsWith("iron_")) {
			return 3;
		}
		if (name.startsWith("stone_") || name.startsWith("copper_")) {
			return 2;
		}
		return 1;
	}
}
