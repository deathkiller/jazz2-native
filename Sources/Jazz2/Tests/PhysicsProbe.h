#pragma once

#if defined(WITH_PHYSICS_PROBE) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Actors/Player.h"

namespace Jazz2
{
	class LevelHandler;
}

namespace Jazz2::Tests
{
	/**
		@brief Drives the first player through a fixed matrix of movement scenarios and logs its trajectory

		The counterpart of the JJ2+ AngelScript probe in `Tests/Level/_pt.j2as`, which does the same thing to
		the original game. Both drive the player from scripted input keyed on a scenario index and a tick
		within it, both log one CSV row per tick, and both name their scenarios identically - so a run of one
		can be lined up against a run of the other row for row, and the engine's movement can be compared to
		the original's rather than to somebody's memory of it. `Tests/Results` holds a captured run of each.

		Nothing here depends on a human playing, so two runs of the same build agree to about a hundredth of
		a pixel - not bit for bit, because the settle before each scenario ends on real-time frame pacing and
		leaves the player a few hundredths of a pixel apart from run to run. The
		matrix deliberately covers each input on its own and in combination - held throughout, released early,
		released late, released and re-pressed, reversed, reversed and released, reversed and reversed back -
		on the ground and in the air, with and without Run, which is what makes the whole movement model
		recoverable from the traces rather than just the few constants one manoeuvre happens to expose.

		The tick counter runs at the original game's measured 70.021 Hz rather than this engine's 60, so every
		scenario reaches each of its steps at the same *real* time here as it does there, whatever this build's
		frame rate is set to. That is what makes a 24 FPS run comparable to a 144 FPS one.

		Enabled at build time with `WITH_PHYSICS_PROBE` and at run time with `/physics-probe`; see
		@ref PreferencesCache::PhysicsProbe and `Tests/README.md` for how to capture and compare a run.
	*/
	class PhysicsProbe
	{
	public:
		PhysicsProbe(LevelHandler* levelHandler);

		/**
		 * @brief Advances the probe by one frame
		 *
		 * Called before the actors update, so the input it puts in applies to this tick and the state it logs
		 * is what the previous tick left behind - the same point in the tick the original game's probe logs
		 * at. Does nothing once every scenario has run.
		 */
		void OnUpdate(float timeMult);

	private:
		// PRIME waits for the player to land after the level starts and remembers where that was; every
		// scenario is then RESET to exactly that spot, SETTLEd until it is standing still again, and RUN
		static constexpr std::int32_t StatePrime = 0;
		static constexpr std::int32_t StateReset = 1;
		static constexpr std::int32_t StateSettle = 2;
		static constexpr std::int32_t StateRun = 3;
		static constexpr std::int32_t StateDone = 4;

		// Deliberately NOT the original probe's own count, which is 133: the two agree on scenario *names*,
		// not numbers, because the original has scenarios that were never ported to this side (pole chains,
		// the double-jump window sweep). Comparisons join on the name, so this only has to match the cases
		// in ApplyInput().
		// The last one runs only in a different level (Diamondus 3) and refuses to run in `_pt`, so a normal
		// sweep of the test level stops after 334 of these. Anything added has to go *before* it.
		static constexpr std::int32_t ScenarioCount = 335;
		// Raise this to re-measure only the later scenarios while iterating, which turns a twenty-minute
		// sweep into half a minute. LEAVE IT AT 0 WHEN COMMITTING: a raised value silently skips everything
		// before it, which has been mistaken for scenarios that stopped working more than once.
		static constexpr std::int32_t FirstScenario = 0;
		// Upper bound on the wait for the player to settle, so a scenario still runs if it never does
		static constexpr float MaxWaitTicks = 120.0f;
		// The original game's tick rate, which the probe's own counter runs at. Taken from Player rather than
		// written out again: the probe's clock and the constants it is measuring have to be the same clock, or
		// re-measuring the rate in one place makes every scenario's step land at a slightly different real
		// time than the reference trace - which shows up as a uniform speed error and gets blamed on physics.
		static constexpr float OriginalTickRate = Actors::Player::LegacyTickRate;
		// Tick at which the scenarios that jump press the jump key - late enough that top ground speed has
		// been reached first, which is what makes the speed-dependent launch boost measurable
		static constexpr std::int32_t JumpTick = 45;
		// Set to a tile row to have the prime step report where that row is solid and where it is not, as
		// runs of tiles - which is how a gap-crossing scenario gets aimed at a gap that is really there.
		// 0 disables it. See ScanFloor().
		static constexpr std::int32_t ScanFloorRow = 0;
		// How many rows either side of it to report as well. Which row a floor's solid tiles occupy is not
		// obvious from the height the player rests at, and guessing wrong prints nothing at all - and a
		// ceiling scenario needs the floor *and* the ceiling above it, which can be six tiles apart.
		static constexpr std::int32_t ScanFloorRowSpan = 8;
		// How far along the row to scan
		static constexpr std::int32_t FloorScanMaxTile = 400;

		LevelHandler* _levelHandler;
		std::int32_t _state;
		std::int32_t _scenario;
		float _tick;
		float _waited;
		float _groundX, _groundY;
		float _lastY;
		float _startFrames;
		std::int32_t _still;
		// What the current scenario put in the world, taken back out again before the next one starts. A pole
		// chain writes several tile events, so it has to be a list rather than a single position.
		std::shared_ptr<Actors::ActorBase> _spawned;
		SmallVector<Vector2i, 8> _tileEvents;
		// Where the level's own pushable and tube turtle sit, found once by scanning the event map. Both have
		// to be the level's own: a spawned pushable never becomes solid, and a spawned enemy cannot be
		// stomped - in the original a buttstomp passes straight through one.
		Vector2f _pushable;
		Vector2f _turtle;

		// Applies a scenario's scripted input for one tick and logs a row; false for an index past the end
		bool ApplyInput(Actors::Player* player, std::int32_t scenario, std::int32_t t);
		// Picks the character a scenario's move belongs to and moves the player to where it starts
		void SetupCharacter(Actors::Player* player, std::int32_t s);
		// Takes the previous scenario's props out of the world, at the reset
		void ClearProps();
		// Puts a scenario's props in place, once the player has settled
		void SetupProps(Actors::Player* player, std::int32_t s);
		// Reports the solid and empty runs of one tile row, for aiming gap scenarios
		void ScanFloor(Actors::Player* player, std::int32_t row);
		// Length of a scenario, in the original game's ticks
		static float GetScenarioTicks(std::int32_t s);
	};
}

#endif
