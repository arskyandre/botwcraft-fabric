package dev.botwcraft.client;

import dev.botwcraft.combat.BotwCombat;
import dev.botwcraft.link.Proto;
import dev.botwcraft.link.BotwLink;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.PauseScreen;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonInfo;
import org.lwjgl.sdl.SDLKeyboard;

/**
 * Replays BOTW-captured input into Minecraft's own input handlers, as if the (hidden) MC
 * window had focus. Keeps a virtual keyboard so InputConstants.isKeyDown() still works.
 */
public final class InputBridge {
	private static final boolean[] KEYS = new boolean[512];
	private static final boolean[] BUTTONS = new boolean[8];
	private static double cursorX, cursorY;
	private static int modifiers;
	private static int clickLogs;

	private InputBridge() {
	}

	public static boolean isKeyDown(int scancode) {
		return scancode >= 0 && scancode < KEYS.length && KEYS[scancode];
	}

	public static void drain(Minecraft minecraft) {
		BotwLink.drainInput((type, code, a, b, c) -> dispatch(minecraft, type, code, a, b, c));
		life(minecraft);
	}

	private static void dispatch(Minecraft minecraft, int type, int code, int a, int b, int c) {
		long handle = minecraft.getWindow().handle();
		switch (type) {
			case Proto.IN_KEY -> key(minecraft, handle, code, a != 0);
			case Proto.IN_MOUSE_BUTTON -> {
				if (code > 0 && code < BUTTONS.length) {
					BUTTONS[code] = a != 0;
				}
				if (a != 0 && clickLogs++ < 20) {
					var hit = minecraft.hitResult;
					dev.botwcraft.BotwCraft.LOG.info("BotwCraft: click {} -> {} {} (grabbed {}, screen {})", code, hit == null ? "null" : hit.getType(),
						hit instanceof net.minecraft.world.phys.EntityHitResult eh ? eh.getEntity().getName().getString() : hit == null ? "" : hit.getLocation(),
						minecraft.mouseHandler.isMouseGrabbed(), minecraft.gui.screen());
				}
				minecraft.mouseHandler.onButton(handle, new MouseButtonInfo(code, modifiers), a != 0 ? 1 : 0);
			}
			case Proto.IN_SCROLL -> minecraft.mouseHandler.onScroll(handle, 0.0, a / 120.0);
			case Proto.IN_CURSOR -> {
				double dx = a - cursorX;
				double dy = b - cursorY;
				cursorX = a;
				cursorY = b;
				minecraft.mouseHandler.onMove(handle, a, b, dx, dy);
			}
			case Proto.IN_TEXT -> {
				if (minecraft.gui.screen() != null) {
					minecraft.keyboardHandler.textInput(handle, new String(Character.toChars(a)));
				}
			}
			case Proto.IN_RELEASE_ALL -> releaseAll();
			case Proto.IN_HURT -> hurt(minecraft, code, a / 100.0F, b, c);
			case Proto.IN_HEAL -> heal(minecraft, a / 100.0F);
			case Proto.IN_MAX_HEALTH -> maxHealth(minecraft, a / 100.0F);
			case Proto.IN_EFFECT -> effect(minecraft, code, a, b);
			case Proto.IN_RUNES -> runes(minecraft, a);
			case Proto.IN_SAVE -> save(minecraft, code, a, b);
			case Proto.IN_SAFE_FALL -> safeFall(minecraft, a / 100.0F);
			case Proto.IN_STAMINA -> dev.botwcraft.world.BotwClimb.stamina(a / 10.0F, b / 10.0F);
			case Proto.IN_LIFE -> {
				lifeRed = a / 100.0F;
				lifeYellow = Math.max(0.0F, b / 100.0F);
				lifeUntil = System.currentTimeMillis() + 10_000L;
			}
			case Proto.IN_OPEN_MENU -> {
				if (minecraft.gui.screen() == null && minecraft.player != null) {
					releaseAll();
					minecraft.gui.setScreen(new PauseScreen(true));
				}
			}
			default -> {
			}
		}
	}

	/** BOTW hit the player: apply it as Minecraft damage on the integrated server (or the host's). */
	private static void hurt(Minecraft minecraft, int kind, float botwDamage, int attacker, int flags) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null) {
			return;
		}
		if (server == null) {
			// A guest in a friend's world: the host's server applies it.
			if (net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking.canSend(dev.botwcraft.net.BotwNet.Hurt.TYPE)) {
				net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking.send(new dev.botwcraft.net.BotwNet.Hurt(kind, botwDamage, attacker, flags));
			}
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null) {
				BotwCombat.hurtPlayer(player, kind, botwDamage, attacker, flags);
			}
		});
	}

	/**
	 * BOTW bridge: the game gave its player life back (he ate). Minecraft's player gets it as health, on the
	 * integrated server. Minecraft's own food is untouched: this does nothing to hunger.
	 */
	// BOTW bridge: the life the game last set anew for its player (IN_LIFE), waiting to be the player's here.
	private static float lifeRed, lifeYellow;
	private static long lifeUntil;

	/**
	 * BOTW bridge: the game set its player's life anew (a save loaded, or its own revive after a death: a fairy,
	 * Mipha's Grace). The player here gets the same: the red hearts as health, the yellow hearts as absorption
	 * (none: the absorption there was is taken away). It waits for a living player, for ten seconds at most: after
	 * a death the message comes before Minecraft has made the player anew.
	 */
	private static void life(Minecraft minecraft) {
		if (lifeUntil == 0L) {
			return;
		}
		var server = minecraft.getSingleplayerServer();
		if (System.currentTimeMillis() > lifeUntil || server == null) {
			lifeUntil = 0L;
			return;
		}
		if (minecraft.player == null || !minecraft.player.isAlive()) {
			return;
		}
		float red = lifeRed;
		float yellow = lifeYellow;
		lifeUntil = 0L;
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player == null || !player.isAlive()) {
				return;
			}
			float before = player.getHealth();
			float beforeYellow = player.getAbsorptionAmount();
			player.setHealth(Math.max(0.5F, Math.min(red, player.getMaxHealth())));
			player.removeEffect(net.minecraft.world.effect.MobEffects.ABSORPTION);
			if (yellow > 0.0F) {
				int amplifier = Math.max(0, (int) Math.ceil(yellow / 4.0F) - 1);
				player.addEffect(new net.minecraft.world.effect.MobEffectInstance(net.minecraft.world.effect.MobEffects.ABSORPTION, net.minecraft.world.effect.MobEffectInstance.INFINITE_DURATION, amplifier));
			}
			player.setAbsorptionAmount(yellow);
			dev.botwcraft.BotwCraft.LOG.info("BotwCraft: the game set its player's life: health {} -> {}, yellow hearts {} -> {}", before, player.getHealth(), beforeYellow, player.getAbsorptionAmount());
		});
	}

	/**
	 * BOTW bridge: the player falls as far without harm as the game's own player does (parts
	 * of the game expect a drop from a ledge to cost nothing, and Minecraft's three blocks are much less). Beyond
	 * that height the harm is Minecraft's own, half a heart a block. The message comes again every few seconds.
	 */
	private static void safeFall(Minecraft minecraft, float blocks) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null || blocks < 3.0F || blocks > 1024.0F) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			var attribute = player == null ? null : player.getAttribute(net.minecraft.world.entity.ai.attributes.Attributes.SAFE_FALL_DISTANCE);
			if (attribute != null && Math.abs(attribute.getBaseValue() - blocks) > 1.0E-3) {
				double before = attribute.getBaseValue();
				attribute.setBaseValue(blocks);
				dev.botwcraft.BotwCraft.LOG.info("BotwCraft: the height of a fall without harm is the game's: {} -> {} blocks", before, blocks);
			}
		});
	}

	private static void heal(Minecraft minecraft, float health) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null || health <= 0.0F) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null && player.isAlive()) {
				float before = player.getHealth();
				player.heal(health);
				dev.botwcraft.BotwCraft.LOG.info("BotwCraft: the game healed the player for {}: health {} -> {}", health, before, player.getHealth());
			}
		});
	}

	/**
	 * BOTW bridge: the runes the game's player owns (bit i: the rune the game numbers i). Their items are with
	 * Minecraft's player, one of each (the mod gives the item when the game marks the rune as
	 * owned).
	 */
	/** The other game wrote a save (kind 0) or loaded one (kind 1); see BotwSaves. Without a player, it comes again. */
	private static void save(Minecraft minecraft, int kind, int save, int number) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null && player.isAlive()) {
				dev.botwcraft.save.BotwSaves.message(player, kind, Integer.toUnsignedLong(save), number);
			}
		});
	}

	private static void runes(Minecraft minecraft, int mask) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null && player.isAlive()) {
				dev.botwcraft.rune.BotwRunes.owned(player, mask);
			}
		});
	}

	/**
	 * BOTW bridge: the game's player has this much life at most (his hearts: if Link has
	 * 3 hearts, Steve has 3). Minecraft's player gets it as the base of his max health.
	 */
	private static void maxHealth(Minecraft minecraft, float health) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null || health < 1.0F || health > 1024.0F) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			var attribute = player == null ? null : player.getAttribute(net.minecraft.world.entity.ai.attributes.Attributes.MAX_HEALTH);
			if (attribute != null && Math.abs(attribute.getBaseValue() - health) > 1.0E-3) {
				double before = attribute.getBaseValue();
				attribute.setBaseValue(health);
				if (player.getHealth() > player.getMaxHealth()) {
					player.setHealth(player.getMaxHealth());
				}
				dev.botwcraft.BotwCraft.LOG.info("BotwCraft: the game's hearts set the player's max health: {} -> {} (health {})", before, health, player.getHealth());
			}
		});
	}

	/**
	 * BOTW bridge: a thing the game's player ate has an effect. The game's effect ids (its SpecialStatus table):
	 * 2 LifeMaxUp (its yellow hearts: level is their amount in quarter hearts), 10 AttackUp, 11 DefenseUp,
	 * 13 AllSpeed, 16 Fireproof. The others have no match in Minecraft and stay the game's own.
	 */
	private static void effect(Minecraft minecraft, int id, int level, int seconds) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null || level <= 0) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player == null || !player.isAlive()) {
				return;
			}
			int ticks = seconds > 0 ? seconds * 20 : net.minecraft.world.effect.MobEffectInstance.INFINITE_DURATION;
			String what;
			if (id == 2) {
				// Yellow hearts: Minecraft's absorption, of the same amount (a quarter heart is half a health point).
				float extra = level * 0.5F;
				int amplifier = Math.max(0, (int) Math.ceil(extra / 4.0F) - 1);
				player.addEffect(new net.minecraft.world.effect.MobEffectInstance(net.minecraft.world.effect.MobEffects.ABSORPTION, net.minecraft.world.effect.MobEffectInstance.INFINITE_DURATION, amplifier));
				player.setAbsorptionAmount(extra);
				what = "absorption " + extra;
			} else {
				var kind = switch (id) {
					case 10 -> net.minecraft.world.effect.MobEffects.STRENGTH;
					case 11 -> net.minecraft.world.effect.MobEffects.RESISTANCE;
					case 13 -> net.minecraft.world.effect.MobEffects.SPEED;
					case 16 -> net.minecraft.world.effect.MobEffects.FIRE_RESISTANCE;
					default -> null;
				};
				if (kind == null) {
					return;
				}
				player.addEffect(new net.minecraft.world.effect.MobEffectInstance(kind, ticks, Math.min(level, 4) - 1));
				what = kind.getRegisteredName() + " " + level;
			}
			dev.botwcraft.BotwCraft.LOG.info("BotwCraft: the game's effect {} level {} for {} s: {}", id, level, seconds, what);
		});
	}

	private static void key(Minecraft minecraft, long handle, int scancode, boolean down) {
		if (scancode <= 0 || scancode >= KEYS.length) {
			return;
		}
		boolean wasDown = KEYS[scancode];
		KEYS[scancode] = down;
		updateModifiers();
		int action = down ? (wasDown ? -1 : 1) : 0; // -1 = repeat
		int keycode = SDLKeyboard.SDL_GetKeyFromScancode(scancode, (short) modifiers, true);
		minecraft.keyboardHandler.keyPress(handle, action, new KeyEvent(scancode, keycode, modifiers));
	}

	private static void updateModifiers() {
		int m = 0;
		if (KEYS[225]) m |= 0x0001; // SDL_KMOD_LSHIFT
		if (KEYS[229]) m |= 0x0002; // SDL_KMOD_RSHIFT
		if (KEYS[224]) m |= 0x0040; // SDL_KMOD_LCTRL
		if (KEYS[228]) m |= 0x0080; // SDL_KMOD_RCTRL
		if (KEYS[226]) m |= 0x0100; // SDL_KMOD_LALT
		if (KEYS[230]) m |= 0x0200; // SDL_KMOD_RALT
		modifiers = m;
	}

	/** Lift every key and button we think is held (focus moved to BOTW, link dropped, ...). */
	public static void releaseAll() {
		Minecraft minecraft = Minecraft.getInstance();
		long handle = minecraft.getWindow().handle();
		for (int sc = 0; sc < KEYS.length; sc++) {
			if (KEYS[sc]) {
				KEYS[sc] = false;
				updateModifiers();
				minecraft.keyboardHandler.keyPress(handle, 0, new KeyEvent(sc, SDLKeyboard.SDL_GetKeyFromScancode(sc, (short) 0, true), modifiers));
			}
		}
		for (int button = 1; button < BUTTONS.length; button++) {
			if (BUTTONS[button]) {
				BUTTONS[button] = false;
				minecraft.mouseHandler.onButton(handle, new MouseButtonInfo(button, 0), 0);
			}
		}
	}
}
