package dev.botwcraft.save;

import dev.botwcraft.BotwCraft;
import dev.botwcraft.link.BotwLink;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.NbtAccounter;
import net.minecraft.nbt.NbtIo;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.util.ProblemReporter;
import net.minecraft.world.ItemStackWithSlot;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.storage.LevelResource;
import net.minecraft.world.level.storage.TagValueInput;
import net.minecraft.world.level.storage.TagValueOutput;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

/**
 * BOTW bridge: what the player carries goes with the other game's saves.
 *
 * <p>The other game tells when it has written a save and when it has loaded one, and names the save by the time
 * it was written (IN_SAVE). On a save, what the player carries now is written under that name. On a load, what
 * was written under the loaded save's name is put back, so going back to an older save of the other game goes
 * back here too; a new game there (name 0) starts with nothing here. A save this mod has never seen (made before
 * the mod, or without it) gets what the player carries now.
 *
 * <p>Kept: the inventory, what is worn and in the off hand, the ender chest, experience, hunger. Not kept yet: the
 * world's blocks and entities, which stay as they are whichever save is loaded. The rune items are given and
 * taken by BotwRunes from the other game's own data, after this.
 *
 * <p>The files are in the world's folder, botwcraft-saves/NAME.dat. Before anything is put back, what the player
 * carried is written to botwcraft-saves/before-load-TIME.dat, so nothing is ever lost by a wrong message.
 * -Dbotwcraft.saves=false switches it off.
 */
public final class BotwSaves {
	public static final int SAVED = 0;
	public static final int LOADED = 1;
	private static final boolean ON = Boolean.parseBoolean(System.getProperty("botwcraft.saves", "true"));
	private static final EquipmentSlot[] WORN = {EquipmentSlot.OFFHAND, EquipmentSlot.FEET, EquipmentSlot.LEGS, EquipmentSlot.CHEST, EquipmentSlot.HEAD};
	private static int lastNumber;
	private static int lastBotw;

	private BotwSaves() {
	}

	/**
	 * One IN_SAVE message, on the server thread. Messages come more than once; each number is taken once, in order.
	 * The numbers begin again with every start of the other game.
	 */
	public static void message(ServerPlayer player, int kind, long save, int number) {
		if (!ON) {
			return;
		}
		int botw = BotwLink.botwPid();
		if (botw != lastBotw) {
			lastBotw = botw;
			lastNumber = 0;
		}
		if (number <= lastNumber) {
			return;
		}
		lastNumber = number;
		Path folder = player.level().getServer().getWorldPath(LevelResource.ROOT).resolve("botwcraft-saves");
		Path file = folder.resolve(save + ".dat");
		try {
			Files.createDirectories(folder);
			if (kind == SAVED) {
				write(player, file);
				BotwCraft.LOG.info("BotwCraft: save {} written by the game; the player's things are kept with it", save);
			} else if (save == 0) {
				write(player, folder.resolve("before-load-" + System.currentTimeMillis() + ".dat"));
				read(player, new CompoundTag());
				BotwCraft.LOG.info("BotwCraft: a new game; the player starts with nothing");
			} else if (Files.exists(file)) {
				write(player, folder.resolve("before-load-" + System.currentTimeMillis() + ".dat"));
				read(player, NbtIo.readCompressed(file, NbtAccounter.unlimitedHeap()));
				BotwCraft.LOG.info("BotwCraft: save {} loaded by the game; the player's things of that save are back", save);
			} else {
				write(player, file);
				BotwCraft.LOG.info("BotwCraft: save {} loaded by the game, never seen here; it gets what the player carries now", save);
			}
		} catch (IOException | RuntimeException e) {
			BotwCraft.LOG.error("BotwCraft: the player's things of save {} could not be {}", save, kind == SAVED ? "kept" : "put back", e);
		}
	}

	private static void write(ServerPlayer player, Path file) throws IOException {
		TagValueOutput out = TagValueOutput.createWithContext(ProblemReporter.DISCARDING, player.registryAccess());
		player.getInventory().save(out.list("Inventory", ItemStackWithSlot.CODEC));
		ValueOutput worn = out.child("Worn");
		for (EquipmentSlot slot : WORN) {
			ItemStack stack = player.getItemBySlot(slot);
			if (!stack.isEmpty()) {
				worn.store(slot.getName(), ItemStack.CODEC, stack);
			}
		}
		player.getEnderChestInventory().storeAsSlots(out.list("EnderItems", ItemStackWithSlot.CODEC));
		out.putInt("XpLevel", player.experienceLevel);
		out.putFloat("XpProgress", player.experienceProgress);
		out.putInt("XpTotal", player.totalExperience);
		out.putInt("Food", player.getFoodData().getFoodLevel());
		out.putFloat("Saturation", player.getFoodData().getSaturationLevel());
		// Written beside and moved over, so a file is whole or not there.
		Path part = file.resolveSibling(file.getFileName() + ".part");
		NbtIo.writeCompressed(out.buildResult(), part);
		Files.move(part, file, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
	}

	/** An empty tag is a new game: nothing carried, no experience, not hungry. */
	private static void read(ServerPlayer player, CompoundTag tag) {
		ValueInput in = TagValueInput.create(ProblemReporter.DISCARDING, player.registryAccess(), tag);
		player.closeContainer();
		player.containerMenu.setCarried(ItemStack.EMPTY);
		player.getInventory().clearContent();
		player.getInventory().load(in.listOrEmpty("Inventory", ItemStackWithSlot.CODEC));
		ValueInput worn = in.childOrEmpty("Worn");
		for (EquipmentSlot slot : WORN) {
			player.setItemSlot(slot, worn.read(slot.getName(), ItemStack.CODEC).orElse(ItemStack.EMPTY));
		}
		player.getEnderChestInventory().fromSlots(in.listOrEmpty("EnderItems", ItemStackWithSlot.CODEC));
		player.experienceProgress = in.getFloatOr("XpProgress", 0.0F);
		player.totalExperience = in.getIntOr("XpTotal", 0);
		player.setExperienceLevels(in.getIntOr("XpLevel", 0));
		player.getFoodData().setFoodLevel(in.getIntOr("Food", 20));
		player.getFoodData().setSaturation(in.getFloatOr("Saturation", 5.0F));
		player.inventoryMenu.broadcastFullState();
	}
}
