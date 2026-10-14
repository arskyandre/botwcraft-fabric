// BotwCraft shared-memory protocol (the bridge's DLL inside Cemu <-> the Minecraft Fabric mod).
// The layout is SkyCraft's (chasmlol, MIT), where the other side is a Skyrim plugin; fields SkyCraft made for
// Skyrim keep their meaning here and say so (FormIDs, skills).
//
// This header is the single source of truth for the byte layout. The Java side mirrors it in
// fabric/src/main/java/dev/botwcraft/link/Proto.java; if you change anything here, change it
// there too and bump kVersion.
//
// All multi-byte values are little-endian. BOTW creates the mapping; Minecraft opens it.
// Coordinates in this protocol are always Minecraft space (blocks, Y up, Z south) unless noted.
#pragma once

#include <cstdint>

namespace botwcraft::proto
{
	inline constexpr std::uint32_t kMagic = 0x43594B53;  // "SKYC": the layout's origin, SkyCraft
	inline constexpr std::uint32_t kVersion = 22;
	inline constexpr wchar_t       kMappingName[] = L"Local\\BotwCraft_v1";

	// SkyCraft's scale for Skyrim (1 block == 70 of its units). Kept for the layout; BOTW's scale is the bridge's own.
	inline constexpr double kUnitsPerBlock = 70.0;

	// ---- region offsets ---------------------------------------------------------------------
	inline constexpr std::uint64_t kOffHeader = 0x0;
	inline constexpr std::uint64_t kOffBotwState = 0x100;
	inline constexpr std::uint64_t kOffMcState = 0x200;
	inline constexpr std::uint64_t kOffOverlayCtl = 0x300;
	inline constexpr std::uint64_t kOffOverlaySlotHdr = 0x340;  // 3 x 0x40
	inline constexpr std::uint64_t kOffWaterGrid = 0x400;       // BOTW -> MC, see WaterGrid
	inline constexpr std::uint64_t kOffInputRing = 0x1000;
	inline constexpr std::uint64_t kOffCollisionRing = 0x20000;
	inline constexpr std::uint64_t kCollisionRingBytes = 32ull << 20;
	inline constexpr std::uint64_t kOffOverlayPixels = kOffCollisionRing + kCollisionRingBytes;
	inline constexpr std::uint32_t kMaxOverlayW = 3840;
	inline constexpr std::uint32_t kMaxOverlayH = 2160;
	inline constexpr std::uint64_t kOverlaySlotBytes = std::uint64_t(kMaxOverlayW) * kMaxOverlayH * 4;
	inline constexpr std::uint32_t kOverlaySlots = 3;
	inline constexpr std::uint64_t kOffActorTable = 0x12000;   // BOTW -> MC, see ActorTable
	inline constexpr std::uint64_t kOffEventRing = 0x17000;    // MC -> BOTW, see McEvent
	inline constexpr std::uint64_t kOffWorldEntities = 0x1C000;  // MC -> BOTW, see WorldEntities
	inline constexpr std::uint64_t kOffRenderRing = kOffOverlayPixels + kOverlaySlotBytes * kOverlaySlots;
	inline constexpr std::uint64_t kRenderRingBytes = 64ull << 20;
	inline constexpr std::uint64_t kMappingBytes = kOffRenderRing + kRenderRingBytes;

	// ---- header @0x0 ------------------------------------------------------------------------
	struct Header
	{
		std::uint32_t magic;
		std::uint32_t version;
		std::uint32_t botwPid;
		std::uint32_t mcPid;
		std::uint64_t botwHeartbeatMs;  // GetTickCount64() at last BOTW frame
		std::uint64_t mcHeartbeatMs;      // GetTickCount64() at last MC frame
	};
	static_assert(sizeof(Header) == 0x20);

	// ---- BOTW -> MC state @0x100 (seqlock: seq odd while writing) -------------------------
	enum BotwFlags : std::uint32_t
	{
		kBotwInGame = 1u << 0,    // a save is loaded and the player exists
		kBotwMenuOpen = 1u << 1,  // a BOTW menu owns input; MC should drop held keys
		kBotwLoading = 1u << 2,   // loading screen / cell transition in progress
		// BOTW (version 12): collision in the direction of travel is not ready. Nothing about the
		// world changes, so MC waits where it is with its keys held and then continues at the
		// velocity it had; no teleport and no wait for ground below, unlike kBotwLoading.
		kBotwHold = 1u << 3,
	};

	// BOTW's water (lakes, rivers, the sea) around the player, for Minecraft to treat as its own
	// water: swimming, floating, drowning. Seqlock like BotwState.
	inline constexpr std::uint32_t kWaterGridSize = 16;
	inline constexpr float         kNoWater = -1.0e30f;

	struct WaterGrid
	{
		std::uint32_t seq;
		std::int32_t  originX, originZ;  // Minecraft block column of surface[0]
		std::uint32_t worldId;           // as in BotwState
		float         surface[kWaterGridSize * kWaterGridSize];  // [z * size + x]: MC y of the water surface; kNoWater: none
	};
	static_assert(sizeof(WaterGrid) <= 0xC00);

	struct BotwState
	{
		std::uint32_t seq;
		std::uint32_t flags;           // BotwFlags
		std::uint32_t worldId;         // worldspace FormID (exterior) or cell FormID (interior)
		std::uint32_t collisionEpoch;  // bumps on world change; MC drops all collision data
		double        posX, posY, posZ;  // BOTW player feet, MC coords
		float         yaw, pitch;        // authoritative look (MC degrees)
		std::uint32_t teleportSeq;       // MC teleports its player to pos when this changes
		std::uint32_t viewportW, viewportH;
		float         gameHour;
	};
	static_assert(sizeof(BotwState) == 0x40);

	// ---- MC -> BOTW state @0x200 (seqlock) ------------------------------------------------
	enum McFlags : std::uint32_t
	{
		kMcInWorld = 1u << 0,
		kMcScreenOpen = 1u << 1,  // an MC GUI screen (inventory, chat, ...) is open
		kMcOnGround = 1u << 2,
		kMcSneaking = 1u << 3,
		kMcSprinting = 1u << 4,
		kMcDead = 1u << 5,
		kMcSwimming = 1u << 6,
		kMcFlying = 1u << 7,
		kMcClimbing = 1u << 8,    // BOTW bridge (version 20): going up a wall of the game's: this costs its stamina
	};

	struct McState
	{
		std::uint32_t seq;
		std::uint32_t flags;          // McFlags
		double        x, y, z;        // interpolated feet position (MC coords)
		float         yaw, pitch;     // MC rotation (degrees)
		float         eyeHeight;      // blocks above feet
		float         sensitivity;    // MC mouse sensitivity option (0..1)
		std::uint32_t teleportAck;    // last BotwState::teleportSeq applied
		std::uint32_t guiScale;
		std::uint64_t frameCounter;
		float         fovDeg;         // effective vertical FOV (includes sprint / fluid modifiers)
		float         bobPhase;       // MC walk-bob phase (interpolated walk distance); 0 if bobbing is off
		float         bobAmount;      // MC walk-bob amplitude
		float         health;         // BOTW bridge (was padding): the player's health in Minecraft health points; 0: none (an older mod, or dead)
		double        eyeX, eyeY, eyeZ;  // MC camera position (interpolated, includes sneak eye lerp)

		// Raw 20 Hz physics ticks, so BOTW can interpolate on its own frame clock exactly like
		// Minecraft's renderer does with partial ticks (no judder from the two games' frame phase).
		std::int64_t tickQpc;             // QueryPerformanceCounter at the (remainder-corrected) tick
		double       prevX, prevY, prevZ;  // feet at the previous tick
		double       curX, curY, curZ;     // feet at the latest tick
		float        tickEyeO, tickEye;      // Camera's smoothed eye height, previous/latest tick
		float        walkDistO, walkDist;    // walk-bob phase inputs
		float        bobO, bob;              // walk-bob amplitude inputs
		float        tickMs;                 // milliseconds per tick (50 unless /tick rate changed)
		float         maxHealth;             // BOTW bridge (was padding): the player's most health; 0: none

		// Minecraft's camera (F5): 0 first person, 1 third person behind, 2 third person in front
		// (looking back at the player). cameraDistance is how far Minecraft's camera sits from
		// the eye, after its own zoom collision (Minecraft blocks and BOTW's triangles).
		std::uint32_t cameraMode;
		float         cameraDistance;

		// BOTW bridge (version 14): the rune item in the player's main hand, as the other game numbers
		// its runes, plus one (1 round bomb, 2 square bomb, 3 Magnesis, 4 Stasis, 5 Cryonis); 0: none.
		std::uint32_t heldRune;
		// BOTW bridge (version 18, was reserved): the player's yellow hearts (absorption) in Minecraft health points.
		float         absorption;
		// BOTW bridge (version 22): the part of the player's step on the ground that collision took away this tick,
		// in blocks a second (x, z): what the player pushes against. The bridge gives Link that push, so that the
		// game's physics moves what he walks into (a round boulder).
		float         pushX, pushZ;
	};
	static_assert(sizeof(McState) == 0xD8);
	static_assert(sizeof(McState) <= 0x100);

	// ---- overlay triple buffer @0x300 --------------------------------------------------------
	// state: bits 0-1 = index of the "middle" slot, bit 2 = middle holds an unread frame.
	// Writer (MC) renders into its private back slot, then xchg(state, back | kDirty) and keeps
	// the returned index as its new back slot. Reader (BOTW) does xchg(state, front) only when
	// the dirty bit is set and keeps the returned index as its new front slot.
	inline constexpr std::uint32_t kOverlayDirty = 1u << 2;

	struct OverlayCtl
	{
		std::uint32_t state;
		std::uint32_t pad;
		std::uint64_t framesPublished;
	};

	struct OverlaySlotHdr
	{
		std::uint32_t width;
		std::uint32_t height;
		std::uint32_t flags;  // bit0: rows are bottom-up
		std::uint32_t pad;
		std::uint64_t frameId;
		std::uint8_t  reserved[0x40 - 0x18];
	};
	static_assert(sizeof(OverlaySlotHdr) == 0x40);

	// ---- input ring @0x1000 (BOTW produces, MC consumes) ----------------------------------
	inline constexpr std::uint32_t kInputRingEntries = 4096;  // power of two
	inline constexpr std::uint64_t kInputRingHeadOff = 0x00;  // u64, written by BOTW
	inline constexpr std::uint64_t kInputRingTailOff = 0x40;  // u64, written by MC
	inline constexpr std::uint64_t kInputRingDataOff = 0x80;

	enum InputType : std::uint16_t
	{
		kInKey = 1,          // code = SDL scancode, a = 1 press / 0 release
		kInMouseButton = 2,  // code = SDL button (1 L, 2 M, 3 R, 4 X1, 5 X2), a = 1 press / 0 release
		kInScroll = 3,       // a = wheel notches * 120 (positive = up)
		kInCursor = 4,       // a, b = absolute cursor position in overlay pixels
		kInText = 5,         // a = unicode code point
		kInReleaseAll = 6,   // release every held key/button (input focus left MC)
		kInHurt = 7,         // BOTW hit the player: code = HurtKind, a = BOTW damage * 100, b = attacker FormID, c = HurtFlags
		kInOpenMenu = 8,     // open Minecraft's pause/options menu
		kInHeal = 9,         // BOTW bridge: the game gave its player life back (food): a = Minecraft health points * 100
		kInMaxHealth = 10,   // BOTW bridge: the game's player's most life: a = Minecraft health points * 100
		kInEffect = 11,      // BOTW bridge: an effect of a thing eaten: code = the game's effect id, a = level, b = seconds (0: until used up)
		kInRunes = 12,       // BOTW bridge: the runes the game's player owns: a = bit i for the rune the game numbers i (McState.heldRune - 1)
		kInLife = 14,        // BOTW bridge (version 18): the game set its player's life anew (a load, its own revive after a
		                     // death): a = red hearts, b = yellow hearts, both in Minecraft health points * 100
		kInStamina = 16,     // BOTW bridge (version 20): the game's stamina: a = what there is, b = the most, both in its units * 10 (a wheel: 10000)
		kInSafeFall = 15,    // BOTW bridge (version 19): the height the game's player falls without harm: a = blocks * 100
		kInSave = 13,        // BOTW bridge (version 17): the game wrote a save (code 0) or loaded one (code 1): a = the save's time
		                     // (the game's LastSaveTime_Lower; 0: a new game), b = the number of this message, c = seconds played
	};

	enum HurtKind : std::uint16_t
	{
		kHurtMelee = 0,
		kHurtProjectile = 1,
		kHurtMagic = 2,
		kHurtOther = 3,
		kHurtFire = 4,   // BOTW bridge: the game's fire (a camp's fire, a burning weapon); Minecraft's in-fire damage
		kHurtThrown = 5, // BOTW bridge (version 21): something the attacker threw (a rock, a Bokoblin); Minecraft's thrown damage
	};

	enum HurtFlags : std::uint32_t
	{
		kHurtBlockedInBotw = 1u << 0,
		kHurtPowerAttack = 1u << 1,
	};

	// ---- actor table @0x12000 (BOTW -> MC, seqlock) ----------------------------------------
	// Nearby BOTW actors, mirrored in Minecraft as invisible hittable proxy entities.
	inline constexpr std::uint32_t kMaxActors = 256;

	enum ActorFlags : std::uint32_t
	{
		kActorHostile = 1u << 0,    // hostile to the player right now
		kActorDead = 1u << 1,
		kActorEssential = 1u << 2,
		kActorInCombat = 1u << 3,
		kActorSolid = 1u << 4,      // BOTW bridge: the player cannot walk through it and can stand on it
	};

	struct ActorRecord
	{
		std::uint32_t formId;
		std::uint32_t flags;       // ActorFlags
		float         x, y, z;     // feet, MC coords
		float         yaw;         // MC degrees
		float         width;       // blocks
		float         height;      // blocks
		float         healthFrac;  // 0..1
		std::uint16_t level;
		std::uint16_t pad;
		char          name[24];    // display name, UTF-8, NUL-terminated (truncated)
	};
	static_assert(sizeof(ActorRecord) == 64);

	struct ActorTable
	{
		std::uint32_t seq;
		std::uint32_t count;
		std::uint8_t  pad[0x40 - 8];
		ActorRecord   actors[kMaxActors];
	};
	static_assert(sizeof(ActorTable) == 0x40 + 64 * kMaxActors);

	// ---- event ring @0x17000 (MC -> BOTW) ---------------------------------------------------
	inline constexpr std::uint32_t kEventRingEntries = 512;  // power of two
	inline constexpr std::uint64_t kEventRingHeadOff = 0x00;  // u64, written by MC
	inline constexpr std::uint64_t kEventRingTailOff = 0x40;  // u64, written by BOTW
	inline constexpr std::uint64_t kEventRingDataOff = 0x80;

	enum McEventType : std::uint32_t
	{
		kEvHitActor = 1,    // formId, a = MC damage (after MC's own modifiers), b/c = knockback dir x/z (MC), d = knockback strength
		kEvPlayerDied = 2,  // the Minecraft player died: kill the BOTW player; formId = attacker, flags = DeathCause (version 21)
		kEvExplosion = 3,   // a Minecraft explosion (TNT, creeper, ...): a/b/c = centre (MC coords), d = radius (blocks)
		kEvArrowStuck = 4,  // an arrow stuck in a BOTW actor: formId, a/b/c = where it hit (MC coords), d = flight yaw,
		                    // flags = flight pitch (float bits), weapon = arrow texture (0 plain, 1 tipped, 2 spectral)
		kEvSkillUse = 5,    // the player used a BOTW skill in Minecraft: formId = BOTW skill (ActorValue: 9 Block,
		                    // 10 Smithing, 11 Heavy Armor, 12 Light Armor), a = uses (as BOTW's AdvanceSkill counts them)
		// BOTW (version 13): the player's attack click, whatever it is on but a Minecraft block or mob; the
		// other game swings a weapon of its own through where the look ends. formId = the stand-in under
		// the crosshair (0: none), a/b/c = where the look ends (MC coords), d = how far that is from the
		// eye (blocks), flags = SwingOn | tier << 8 (0 no tool, 1 wood or gold, 2 stone or copper, 3 iron,
		// 4 diamond, 5 netherite), weapon = HitWeapon of the held item. With kSwingHeld nothing is swung:
		// the held item's class or tier changed (sent ahead, so that the weapon is ready).
		kEvSwing = 6,
	};

	enum SwingOn : std::uint32_t
	{
		kSwingAir = 0,      // nothing within reach
		kSwingSurface = 1,  // the other game's collision
		kSwingActor = 2,    // a stand-in (formId)
		kSwingHeld = 3,     // no swing: what is held
		kSwingArrow = 4,    // BOTW bridge: an arrow of the player's ended on the other game's collision: a, b, c where; d its yaw
		                    // (degrees, atan2(x, z) of its flight) and formId the bits of its pitch (degrees, up positive)
	};

	enum HitFlags : std::uint32_t
	{
		kHitCritical = 1u << 0,
		kHitProjectile = 1u << 1,
		kHitSweep = 1u << 2,
		kHitFire = 1u << 3,
	};

	// What landed a kEvHitActor (BOTW plays that weapon class's impact effect and sounds).
	enum HitWeapon : std::uint32_t
	{
		kWeaponUnarmed = 0,
		kWeaponBlade = 1,   // swords
		kWeaponAxe = 2,
		kWeaponBlunt = 3,   // maces, pickaxes, shovels, hoes, anything else held (in kEvSwing: without pickaxes and the mace)
		kWeaponPierce = 4,  // tridents, spears
		kWeaponArrow = 5,   // arrows and other projectiles
		kWeaponPick = 6,    // kEvSwing only: pickaxes and the mace
	};

	// What killed the Minecraft player (kEvPlayerDied's flags; BOTW bridge, version 21). The game plays a death of its
	// own for some of them (without it Link played the default death animation whatever killed Steve).
	enum DeathCause : std::uint32_t
	{
		kDeathOther = 0,
		kDeathAttack = 1,     // a creature or a player hit him (formId: the game's attacker, if it was one of its own)
		kDeathFall = 2,       // falling, also onto something (Minecraft's IS_FALL)
		kDeathFire = 3,       // fire, lava, a hot floor (IS_FIRE)
		kDeathDrown = 4,      // IS_DROWNING
		kDeathFreeze = 5,     // IS_FREEZING
		kDeathLightning = 6,  // IS_LIGHTNING
		kDeathExplosion = 7,  // IS_EXPLOSION
		kDeathVoid = 8,       // out of the world (Minecraft's fell_out_of_world)
		kDeathStarve = 9,
		kDeathInWall = 10,    // suffocated inside a block
		kDeathProjectile = 11 // an arrow or another projectile (IS_PROJECTILE)
	};

	struct McEvent
	{
		std::uint32_t type;
		std::uint32_t formId;
		float         a, b, c, d;
		std::uint32_t flags;
		std::uint32_t weapon;  // HitWeapon for kEvHitActor
	};
	static_assert(sizeof(McEvent) == 32);

	// ---- world entities @0x1C000 (MC -> BOTW, seqlock) -------------------------------------
	// Minecraft things BOTW draws itself each frame (arrows, dropped items, block cracks) + the
	// block outline.
	inline constexpr std::uint32_t kMaxWorldEntities = 160;

	enum WorldEntityKind : std::uint32_t
	{
		kWeArrow = 1,    // uv[0]: the arrow's item icon
		kWeItem = 2,     // dropped/thrown item: a flat sprite (uv[0]) turning about the vertical
		kWeTrident = 3,  // uv[0]: the trident's item icon
		kWeBlock = 4,    // dropped block item: a spinning cube of side `scale`, uv[0..2] = side, top, bottom
		kWeCrack = 5,    // block-breaking cracks over the box at (x, y, z) of size ext, uv[0] = crack stage
		kWeShadow = 6,   // a player's or mob's feet at (x, y, z), `scale` wide: its soft contact shadow
	};

	struct WorldEntity
	{
		std::uint32_t kind;        // WorldEntityKind
		std::uint32_t id;          // MC entity id (stable while it exists)
		float         x, y, z;     // MC coords (interpolated at MC's render time)
		float         yaw, pitch;  // MC degrees
		float         scale;
		float         ext[3];      // kWeCrack: box size
		float         uv[3][4];    // atlas rects {u0, v0, u1, v1}
		std::uint32_t tint;        // RGBA8 multiplier for the top face (grass, leaves); 0 = none
	};
	static_assert(sizeof(WorldEntity) == 96);

	struct WorldEntities
	{
		std::uint32_t seq;
		std::uint32_t count;
		std::uint32_t hasSelection;           // draw an outline around the targeted block
		float         selMin[3], selMax[3];   // MC coords
		std::uint8_t  pad[0x40 - 36];
		WorldEntity   entities[kMaxWorldEntities];
	};
	static_assert(sizeof(WorldEntities) == 0x40 + sizeof(WorldEntity) * kMaxWorldEntities);
	static_assert(kOffWorldEntities + sizeof(WorldEntities) <= kOffCollisionRing);

	// ---- render ring (MC -> BOTW) -----------------------------------------------------------
	// Byte ring like the collision ring. Minecraft ships its own block meshes (built by Minecraft's
	// block renderer: models, tint, AO, lighting) and its block atlas; BOTW draws them in its own
	// frame so blocks stay locked to the world and are occluded by BOTW geometry.
	inline constexpr std::uint64_t kRenRingHeadOff = 0x00;
	inline constexpr std::uint64_t kRenRingTailOff = 0x40;
	inline constexpr std::uint64_t kRenRingDataOff = 0x80;
	inline constexpr std::uint64_t kRenRingDataBytes = kRenderRingBytes - kRenRingDataOff;

	enum RenType : std::uint32_t
	{
		kRenPad = 0,
		kRenAtlas = 1,     // RenAtlas + RGBA8 pixels (w * h * 4), top row first
		kRenSection = 2,   // RenSection + RenVertex[vertexCount] (triangle list); 0 vertices = remove
		kRenClearAll = 3,  // drop every section (world change)
		kRenTexture = 4,   // RenTexture + RGBA8 pixels: an entity texture (player skin, armour, ...)
		kRenAvatar = 5,    // RenAvatar + RenBatch[batchCount] + RenVertex[vertexCount]: the player's
		                   // model this frame; 0 batches = not shown (first person)
		kRenScene = 6,     // RenScene + RenBatch[batchCount] + RenVertex[vertexCount]: every other
		                   // entity and all particles this frame, relative to RenScene's origin
		kRenAtlasRegion = 7,  // RenAtlasRegion + RGBA8 pixels: an animated sprite's current frame
		kRenLights = 8,       // RenLights + RenLight[count]: a section's light-emitting blocks (sent
		                      // after its kRenSection; 0 = none)
		kRenSolids = 10,      // RenSolids + 512-byte bitset (bit x + 16z + 256y): which blocks of a
		                      // section NPCs collide with (sent after its kRenSection; 0 = none)
		kRenDug = 11,         // RenDug + 512-byte bitset (bit x + 16z + 256y): which blocks of a section
		                      // were dug out of BOTW's world (its geometry there is gone); 0 = none
		kRenRagdoll = 9,      // RenAvatar + RenBatch[] + RenVertex[]: the player's body standing still,
		                      // relative to the feet and facing +Z, split into its parts (RenBatch
		                      // flags bits 8-11: RagdollPart). Sent about once a second while alive;
		                      // BOTW hangs the parts on its ragdoll when the player dies.
	};

	struct RenSolids
	{
		std::int32_t  sx, sy, sz;  // section coords, as in RenSection
		std::uint32_t count;       // solid blocks (0: none, and no bitset follows)
	};

	struct RenDug
	{
		std::int32_t  sx, sy, sz;  // section coords, as in RenSection
		std::uint32_t count;       // dug blocks (0: none, and no bitset follows)
		std::uint32_t worldId;     // the BOTW world (BotwState::worldId) the bits belong to
		std::uint32_t pad;
	};
	static_assert(sizeof(RenDug) == 24);

	// What a piece of diggable BOTW geometry is made of, as the Minecraft block it digs into
	// (ColTri flags bits 8-15). Chosen on the BOTW side from Havok materials and object types.
	enum DigMaterial : std::uint8_t
	{
		kDigNone = 0,  // not known: stone
		kDigGrass = 1,
		kDigDirt = 2,
		kDigStone = 3,
		kDigCobble = 4,
		kDigSnow = 5,
		kDigIce = 6,
		kDigSand = 7,
		kDigGravel = 8,
		kDigMud = 9,
		kDigOakLog = 10,
		kDigSpruceLog = 11,
		kDigBirchLog = 12,
		kDigPlanks = 13,
		kDigMetal = 14,
		kDigGlass = 15,
		kDigOrganic = 16,
		kDigCloth = 17,
		kDigBone = 18,
		kDigWeb = 19,
		kDigAsh = 20,
		kDigBedrock = 21,  // Minecraft only: a few blocks under the land
		kDigMaterialCount
	};

	enum RagdollPart : std::uint32_t
	{
		kPartNone = 0,
		kPartHead = 1,
		kPartBody = 2,
		kPartRightArm = 3,
		kPartLeftArm = 4,
		kPartRightLeg = 5,
		kPartLeftLeg = 6,
		kPartCount = 7,
	};

	struct RenLights
	{
		std::int32_t  sx, sy, sz;  // section coords, as in RenSection
		std::uint32_t count;
	};

	enum LightKind : std::uint8_t
	{
		kLightSteady = 0,
		kLightFlame = 1,  // torches, fire, campfires, candles: flicker
		kLightLava = 2,   // lava, magma: a slow glow
	};

	enum BlockHazard : std::uint8_t
	{
		kHazardNone = 0,
		kHazardFire = 1,   // fire, soul fire, campfires: burns what stands in it
		kHazardLava = 2,   // lava: burns hard
		kHazardMagma = 3,  // magma block: hurts what stands on top of it
	};

	struct RenLight
	{
		std::uint8_t  x, y, z;  // block within the section
		std::uint8_t  level;    // Minecraft light emission, 1-15
		std::uint32_t color;    // RGB8 (r low byte); top byte: LightKind in bits 0-3, BlockHazard in 4-7
	};
	static_assert(sizeof(RenLight) == 8);

	struct RenAtlasRegion
	{
		std::uint32_t x, y, width, height;  // pixels in the combined atlas (kRenAtlas)
	};

	struct RenScene
	{
		double        originX, originY, originZ;  // MC block the positions are relative to
		std::uint32_t batchCount;
		std::uint32_t vertexCount;
	};

	struct RenTexture
	{
		std::uint32_t id;  // 1+, referenced by RenBatch::texture
		std::uint32_t width, height;
		std::uint32_t pad;
	};

	// The player as Minecraft's own entity renderer draws it (skin, armour, held items, cape),
	// posed and animated, with positions in blocks relative to the player's feet: BOTW puts
	// it at its own interpolated feet position, the one its camera follows.
	struct RenAvatar
	{
		std::uint32_t batchCount;
		std::uint32_t vertexCount;
	};

	struct RenBatch
	{
		std::uint32_t texture;  // 0: the block/item atlas, else a RenTexture id
		std::uint32_t first;    // first vertex
		std::uint32_t count;    // vertices (multiple of 3)
		std::uint32_t flags;    // bit0: translucent (blended, after the solid pass; casts no shadow)
	};

	struct RenAtlas
	{
		std::uint32_t width, height;
	};

	struct RenSection
	{
		std::int32_t  sx, sy, sz;   // section coords (16-block cubes)
		std::uint32_t vertexCount;  // multiple of 3
	};

	struct RenVertex
	{
		float         x, y, z;  // MC coords relative to the section origin (sx*16, sy*16, sz*16)
		float         u, v;     // atlas UV
		std::uint32_t color;    // RGBA8 (tint * ambient occlusion; Minecraft's fixed face shading is left out)
		std::uint32_t light;    // low byte: block light 0-15, next byte: sky light 0-15
		std::uint32_t flags;    // bit0: cutout (alpha test), bit1: translucent,
		                        // bits 4-6: face normal as MC Direction ordinal + 1 (0 = none: lit without a normal)
	};
	static_assert(sizeof(RenVertex) == 32);

	struct InputEvent
	{
		std::uint16_t type;
		std::uint16_t code;
		std::int32_t  a;
		std::int32_t  b;
		std::int32_t  c;
	};
	static_assert(sizeof(InputEvent) == 16);

	// ---- collision ring @0x20000 (BOTW produces, MC consumes) -----------------------------
	// Byte ring. Every message starts 8-byte aligned with {u32 type, u32 payloadBytes}.
	// A kColPad message means "skip to the start of the ring".
	inline constexpr std::uint64_t kColRingHeadOff = 0x00;  // u64 total bytes written
	inline constexpr std::uint64_t kColRingTailOff = 0x40;  // u64 total bytes consumed
	inline constexpr std::uint64_t kColRingDataOff = 0x80;
	inline constexpr std::uint64_t kColRingDataBytes = kCollisionRingBytes - kColRingDataOff;

	enum ColType : std::uint32_t
	{
		kColPad = 0,
		kColClear = 1,   // payload: u32 epoch
		kColRegion = 2,  // payload: ColRegion + ColBlock[count]
		kColTris = 3,    // payload: ColRegion (count = triangles) + ColTri[count]; sent before kColRegion
		kColBody = 4,    // BOTW bridge (version 16): payload ColBody + ColTri[count]: one moving body, whole, where it is now
	};

	// Exact BOTW collision triangle (MC space) for the player's smooth collider.
	enum ColTriFlags : std::uint32_t
	{
		kTriStairHelper = 1u << 0,  // BOTW's invisible stair ramp: walkable, never a wall
		kTriDiggable = 1u << 1,     // ground, rock, trees...: can be dug into (bits 8-15: DigMaterial).
		                            // Its normal faces out of the solid side (winding is outward).
		kTriGhost = 1u << 2,        // a diggable triangle as it was before blocks were dug out of it:
		                            // not collision, only for telling what's inside BOTW's geometry
		kTriTerrain = 1u << 3,      // the land (a height field)
		kTriNoClimb = 1u << 5,      // BOTW bridge (version 20): the game's material says its player cannot climb it
		kTriLadder = 1u << 4,       // BOTW bridge (version 16): the game's material calls it a ladder; the player climbs it as one of Minecraft's
	};

	inline constexpr std::uint32_t kTriMaterialShift = 8;
	// BOTW bridge (version 20): what the game's own material word says of the triangle, where the bridge knows it
	// (kTriUserKnown): its wall code (5 bits from kTriWallShift) and its material with the sub-material (the word's
	// low 10 bits, from kTriUserShift). For the log of a climb.
	inline constexpr std::uint32_t kTriWallShift = 16, kTriUserKnown = 1u << 21, kTriUserShift = 22;

	struct ColTri
	{
		float         v[9];
		std::uint32_t flags;
	};
	static_assert(sizeof(ColTri) == 40);

	struct ColMsgHeader
	{
		std::uint32_t type;
		std::uint32_t payloadBytes;
	};

	// Replaces all BOTW collision inside the inclusive block box [min, max].
	struct ColRegion
	{
		std::int32_t  minX, minY, minZ;
		std::int32_t  maxX, maxY, maxZ;
		std::uint32_t epoch;
		std::uint32_t count;
	};
	static_assert(sizeof(ColRegion) == 32);

	// BOTW bridge (version 16). A body that moves (a box, a platform, a pillar of ice) is not part of any
	// region: it is sent whole whenever it has moved or changed, and replaces what was sent of it before.
	// transform: three columns of rotation, then the body's place, in Minecraft's space; what stands on
	// the body is carried by the change of it from one message to a later one.
	enum ColBodyFlags : std::uint32_t
	{
		kBodyGone = 1u << 0,  // it is no more (count 0)
	};

	struct ColBody
	{
		std::uint32_t id;      // the body, for as long as it lives; another may get the number later
		std::uint32_t flags;   // ColBodyFlags
		std::uint32_t serial;  // counts the messages of this body
		std::uint32_t pad[3];
		std::uint32_t epoch;   // at the same place as ColRegion's
		std::uint32_t count;   // triangles
		float         transform[12];
	};
	static_assert(sizeof(ColBody) == 80);

	// One block's worth of BOTW collision as an 8x8x8 occupancy mask.
	// bits[y] bit (z * 8 + x) is sub-voxel (x, y, z), each 1/8 block, in MC axes.
	struct ColBlock
	{
		std::int32_t  x, y, z;
		std::uint32_t pad;
		std::uint64_t bits[8];
	};
	static_assert(sizeof(ColBlock) == 80);
}
