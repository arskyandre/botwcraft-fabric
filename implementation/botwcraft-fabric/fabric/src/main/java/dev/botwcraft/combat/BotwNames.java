package dev.botwcraft.combat;

import dev.botwcraft.BotwCraft;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

/**
 * BOTW bridge: the other game's own names of its creatures ("Bokoblin" for Enemy_Bokoblin_Junior), so that
 * Minecraft's death message names the one that killed the player ("BOTWCraft was slain by
 * Bokoblin"). The launcher reads them from the player's copy of the game into a file
 * (actor_names.py) and names it here: -Dbotwcraft.actorNames. Without it a creature keeps the bridge's short name,
 * with spaces for the underscores.
 */
public final class BotwNames {
	private BotwNames() {
	}

	/** The prefixes the bridge takes off an actor's name before it sends it (actors.h). */
	private static final String[] PREFIXES = { "", "Enemy_", "Npc_", "Animal_", "Obj_" };
	private static final Map<String, String> NAMES = load();

	private static Map<String, String> load() {
		Map<String, String> names = new HashMap<>();
		String file = System.getProperty("botwcraft.actorNames");
		if (file == null || file.isBlank()) {
			return names;
		}
		try {
			var json = com.google.gson.JsonParser.parseString(Files.readString(Path.of(file), StandardCharsets.UTF_8)).getAsJsonObject();
			for (var entry : json.entrySet()) {
				names.put(entry.getKey(), entry.getValue().getAsString());
			}
			BotwCraft.LOG.info("BotwCraft: {} names of the game's actors", names.size());
		} catch (Exception e) {
			BotwCraft.LOG.warn("BotwCraft: the game's actor names were not read from {}: {}", file, e.toString());
		}
		return names;
	}

	/** The name to show for an actor whose name the bridge sent as {@code shown}. */
	public static String display(String shown) {
		for (String prefix : PREFIXES) {
			String name = NAMES.get(prefix + shown);
			if (name != null) {
				return name;
			}
		}
		// The bridge sends at most 23 letters (ActorRecord.name): a longer name is found by its start, the shortest first.
		if (shown.length() >= 23) {
			String best = null;
			for (var entry : NAMES.entrySet()) {
				for (String prefix : PREFIXES) {
					if (entry.getKey().startsWith(prefix + shown) && (best == null || entry.getKey().length() < best.length())) {
						best = entry.getKey();
					}
				}
			}
			if (best != null) {
				return NAMES.get(best);
			}
		}
		return shown.replace('_', ' ');
	}
}
