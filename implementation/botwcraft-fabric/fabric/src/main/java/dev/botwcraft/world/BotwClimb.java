package dev.botwcraft.world;

/**
 * BOTW bridge: the player climbs the other game's walls (needed to leave the Shrine of
 * Resurrection and to climb the towers). A wall is every surface of the other
 * game's too steep to walk on that it does not mark as not to be climbed; on it Minecraft's own ladder rules
 * apply (up while pushing into it or jumping, slowly down otherwise, held while sneaking, no fall damage), with
 * a speed of its own. The climb starts only in the air after a jump, pushing forward into a wall that reaches
 * more than one block above the ground the jump began on (so that
 * walking into one of the game's round boulders pushes it and does not climb it). It ends when no wall is near, or on the ground when the player no
 * longer pushes.
 * <p>
 * Going up costs the other game's stamina (its bridge takes it while Proto.MC_CLIMBING is told and tells the
 * amount back, Proto.IN_STAMINA). With none left the player falls off and climbs again only when it is full,
 * as the other game's own player.
 * <p>
 * The state is the client's player's; the single-player server in the same process reads it for the same
 * player (the fall distance).
 */
public final class BotwClimb {
	private BotwClimb() {
	}

	/**
	 * Blocks a second up a wall: Minecraft's ladder, 2.35 (the stamina is paid per block of height, so the height a wheel lasts does not change with it).
	 * -Dbotwcraft.climbSpeed; 0: walls are not climbed.
	 */
	public static final double SPEED = setting("botwcraft.climbSpeed", 2.35, 0.0, 4.0);
	/** What LivingEntity's ladder rule sets as the speed up (its own: 0.2) for SPEED: gravity and drag follow it. */
	public static final double UP = SPEED / 20.0 / 0.98 + 0.08;

	/** The player it is about (the client's own). */
	public static volatile java.util.UUID who;
	/** On a wall now: Minecraft's ladder rules apply. */
	public static volatile boolean climbing;
	/** On a wall that is no ladder: SPEED applies. */
	public static volatile boolean wall;
	/** Going up such a wall: this costs stamina. */
	public static volatile boolean rising;
	/** On such a wall with the forward or the jump key held: the player goes up, touching it this tick or not. */
	public static volatile boolean pushing;

	/** The other game's stamina and its most (1000: one wheel), as its bridge last told them; -1: never. */
	public static volatile float stamina = -1.0F, most = -1.0F;
	public static volatile long staminaAt;
	/** None left: no climbing until it is full again. */
	public static volatile boolean exhausted;

	public static void stamina(float has, float most) {
		if (!(most > 0.0F) || !(has >= 0.0F)) {
			return;
		}
		BotwClimb.stamina = Math.min(has, most);
		BotwClimb.most = most;
		staminaAt = System.currentTimeMillis();
	}

	/** The stamina is known (told within the last three seconds). */
	public static boolean staminaKnown() {
		return most > 0.0F && System.currentTimeMillis() - staminaAt < 3000L;
	}

	public static boolean is(net.minecraft.world.entity.Entity entity) {
		java.util.UUID id = who;
		return id != null && id.equals(entity.getUUID());
	}

	/** In the air after a jump (and not yet back on the ground): a wall may be climbed. */
	public static volatile boolean jumped;
	/** Where the feet were when the player last stood on the ground. */
	public static volatile double groundY = Double.NaN;

	public static void clear() {
		climbing = false;
		wall = false;
		rising = false;
		pushing = false;
	}

	private static double setting(String name, double normal, double least, double most) {
		double value = normal;
		try {
			value = Double.parseDouble(System.getProperty(name, Double.toString(normal)));
		} catch (NumberFormatException ignored) {
		}
		return value >= least && value <= most ? value : normal;
	}
}
