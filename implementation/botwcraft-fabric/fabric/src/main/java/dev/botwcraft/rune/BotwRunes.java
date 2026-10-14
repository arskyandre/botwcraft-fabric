package dev.botwcraft.rune;

import dev.botwcraft.BotwCraft;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;

/**
 * BOTW bridge: the other game's runes as Minecraft items. The rune of the
 * item in the main hand is told to the other game every frame (McState.heldRune), which chooses
 * that rune and works it; the mouse's buttons and wheel are the other game's while one is held.
 * The items follow what the other game's player owns (owned()), and are not thrown away (ServerPlayerMixin).
 */
public final class BotwRunes {
	// In the order the other game numbers its runes.
	public static final Item BOMB_ROUND = register("rune_bomb_round");
	public static final Item BOMB_SQUARE = register("rune_bomb_square");
	public static final Item MAGNESIS = register("rune_magnesis");
	public static final Item STASIS = register("rune_stasis");
	public static final Item CRYONIS = register("rune_cryonis");
	private static final Item[] RUNES = { BOMB_ROUND, BOMB_SQUARE, MAGNESIS, STASIS, CRYONIS };

	private BotwRunes() {
	}

	public static void init() {
	}

	/** McState.heldRune: the other game's number of the stack's rune plus one; 0 when it is none. */
	public static int of(ItemStack stack) {
		for (int i = 0; i < RUNES.length; i++) {
			if (stack.is(RUNES[i])) {
				return i + 1;
			}
		}
		return 0;
	}

	/**
	 * The runes the other game's player owns (IN_RUNES: bit i is the rune it numbers i). The player has one
	 * item of each with him and none of the others: one that is missing is given (thrown away, lost at death, or
	 * the rune is new), one too many or of a rune not owned is taken.
	 */
	/**
	 * The other game's player owns the paraglider (bit 5 of IN_RUNES): Minecraft's player glides as with an elytra,
	 * with no item (LivingEntityGlideMixin; report 76).
	 */
	public static volatile boolean paraglider;

	public static void owned(ServerPlayer player, int mask) {
		paraglider = (mask & 32) != 0;
		Inventory inventory = player.getInventory();
		boolean changed = false;
		for (int i = 0; i < RUNES.length; i++) {
			boolean owns = (mask & 1 << i) != 0;
			int seen = 0;
			if (player.containerMenu.getCarried().is(RUNES[i])) {
				if (owns) {
					seen = 1;
				} else {
					player.containerMenu.setCarried(ItemStack.EMPTY);
					changed = true;
				}
			}
			for (int slot = 0; slot < inventory.getContainerSize(); slot++) {
				if (inventory.getItem(slot).is(RUNES[i]) && (!owns || ++seen > 1)) {
					inventory.setItem(slot, ItemStack.EMPTY);
					changed = true;
					BotwCraft.LOG.info("BotwCraft: rune item {} taken from slot {} ({})", RUNES[i], slot, owns ? "one too many" : "the rune is not owned");
				}
			}
			if (owns && seen == 0 && inventory.add(new ItemStack(RUNES[i]))) {
				changed = true;
				BotwCraft.LOG.info("BotwCraft: rune item {} given (the other game's player owns the rune)", RUNES[i]);
			}
		}
		if (changed) {
			player.containerMenu.broadcastChanges();
		}
	}

	private static Item register(String name) {
		ResourceKey<Item> key = ResourceKey.create(Registries.ITEM, Identifier.fromNamespaceAndPath(BotwCraft.MOD_ID, name));
		return Registry.register(BuiltInRegistries.ITEM, key, new Item(new Item.Properties().setId(key).stacksTo(1)));
	}
}
