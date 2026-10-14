package dev.botwcraft.client;

import dev.botwcraft.world.BotwClimb;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.tags.FluidTags;

/**
 * BOTW bridge: the other game's stamina as a row of Minecraft's HUD, in place of the
 * game's own wheel. One icon is a tenth of a wheel (100 of the game's units),
 * so the first wheel is a row of ten over the food, as Minecraft's air bubbles are, and each wheel the game adds
 * is another row. It shows while the stamina is not full and a second longer, as the game's wheel does. With
 * none left the icons are red until it is full again: no climbing meanwhile.
 * <p>
 * The icon is drawn here, from the rows below: no picture file, nothing of either game's.
 */
public final class StaminaHud {
	private StaminaHud() {
	}

	// '#': the dark edge, 'h': the light spot, 'o': the inside, '.': nothing.
	private static final String[] ICON = {
		"....####.",
		"...#hoo#.",
		"..#hoo#..",
		".#hoo###.",
		".#ooooo#.",
		".###oo#..",
		"..#oo#...",
		".#oo#....",
		".###.....",
	};
	private static final int EDGE = 0xFF10240A, EMPTY = 0xFF2B2B2B;
	private static final int FULL = 0xFF55D92B, LIGHT = 0xFFC4F79A, OUT = 0xFFD9412B, OUT_LIGHT = 0xFFF7A99A;

	private static long shownUntil;

	public static void extract(GuiGraphicsExtractor graphics, DeltaTracker delta) {
		Minecraft minecraft = Minecraft.getInstance();
		LocalPlayer player = minecraft.player;
		if (player == null || minecraft.gui.hud.isHidden() || minecraft.gameMode == null || !minecraft.gameMode.canHurtPlayer() || !BotwClimb.staminaKnown()) {
			return;
		}
		float most = BotwClimb.most, has = BotwClimb.stamina;
		long now = System.currentTimeMillis();
		if (has < most - 0.5F || BotwClimb.exhausted) {
			shownUntil = now + 1000L;
		}
		if (now > shownUntil) {
			return;
		}
		int icons = Math.min(30, (int) Math.ceil(most / 100.0F - 1.0E-3F));
		int right = graphics.guiWidth() / 2 + 91, y = graphics.guiHeight() - 49;
		// Minecraft's air bubbles have this row when they show.
		if (player.isEyeInFluid(FluidTags.WATER) || player.getAirSupply() < player.getMaxAirSupply()) {
			y -= 10;
		}
		for (int i = 0; i < icons; i++) {
			float part = Math.max(0.0F, Math.min(1.0F, (has - i * 100.0F) / 100.0F));
			icon(graphics, right - (i % 10) * 8 - 9, y - (i / 10) * 10, part, BotwClimb.exhausted);
		}
	}

	/** One icon with its top left at (x, y), its {@code part} (0 to 1) filled from the right, as Minecraft's food. */
	private static void icon(GuiGraphicsExtractor graphics, int x, int y, float part, boolean out) {
		int filledFrom = 9 - Math.round(part * 9.0F);
		for (int row = 0; row < ICON.length; row++) {
			String line = ICON[row];
			int start = 0;
			int color = 0;
			for (int column = 0; column <= line.length(); column++) {
				int next = 0;
				if (column < line.length()) {
					char c = line.charAt(column);
					if (c == '#') {
						next = EDGE;
					} else if (c != '.') {
						next = column < filledFrom ? EMPTY : c == 'h' ? (out ? OUT_LIGHT : LIGHT) : (out ? OUT : FULL);
					}
				}
				if (next != color) {
					if (color != 0) {
						graphics.fill(x + start, y + row, x + column, y + row + 1, color);
					}
					start = column;
					color = next;
				}
			}
		}
	}
}
