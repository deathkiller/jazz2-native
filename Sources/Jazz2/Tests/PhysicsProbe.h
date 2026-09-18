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
		// sweep of the test level stops after 394 of these. Anything added has to go *before* it - and that
		// renumbering has to reach the case in ApplyInput(), the window in GetScenarioTicks() and the
		// placement in SetupProps(), all three of which name `dm_chain` by index.
		static constexpr std::int32_t ScenarioCount = 424;
		// Raise this to re-measure only the later scenarios while iterating, which turns a twenty-minute
		// sweep into half a minute. LEAVE IT AT 0 WHEN COMMITTING: a raised value silently skips everything
		// before it, which has been mistaken for scenarios that stopped working more than once.
		//
		// **Everything from 356 up is newer than the committed capture**: the four `sp_upper_*` /
		// `sp_side_tap` ones about what a special move does when a key is let go, plus `sp_side_run`,
		// `sp_side_run_dir`, `an_vine_shoot`, `tb_right_run` / `bl_right_run` / `bl_acc_right_run`
		// at 363-365, the five `ow_*` one-way floor ones at 366-370, and `bl_acc_right_p8_run` at 371.
		// `fc_carrot` at 343 is newer than the capture too - its *input* changed, so the committed
		// trace is of a different manoeuvre.
		// The original's side of all of these **has been measured**; it is this side's capture that a full
		// sweep refreshes. The original's indices for the same set are 351 and 338: the two sides have
		// never numbered scenarios alike, which is why comparisons join on the name.
		static constexpr std::int32_t FirstScenario = 0;
		/**
		 * @brief Last scenario to run, or -1 for "to the end"
		 *
		 * The other half of @ref FirstScenario. On its own that one only sets a *start*, so re-measuring a
		 * handful of scenarios still ran every one after them - a twenty-minute sweep to check four things.
		 * LEAVE IT AT -1 WHEN COMMITTING, for the same reason.
		 */
		static constexpr std::int32_t LastScenario = -1;
		/**
		 * @brief Comma-separated name prefixes to run; empty runs everything
		 *
		 * Selection by **name** rather than by index, which matters because the two probes have never
		 * numbered scenarios alike - `sp_lori_side_rock` is 386 here and 381 there. The same filter string
		 * therefore works unchanged on both sides, where a pair of index ranges would have to be re-derived
		 * for each and would silently select different scenarios when it was got wrong.
		 *
		 * Prefixes, so a family can be named at whatever depth is useful: `"sp_lori"` takes all of hers,
		 * `"sp_lori_side_rock"` takes the four pushable ones, and `"ob_vine,ob_vpole"` takes two families at
		 * once. A skipped scenario costs one frame instead of its settle plus its 800 ticks.
		 *
		 * LEAVE IT EMPTY WHEN COMMITTING. A filtered run is not a sweep, and a trace of one would be taken
		 * for the other - the probe logs the filter in its header so that a capture says which it is.
		 */
		static constexpr const char* ScenarioFilter = "";
		// Recording mode. With this on, the scenario matrix is skipped entirely and the probe writes no input at
		// all: the game plays normally and every frame is logged under the name `free`, with whatever is really
		// being pressed in the key columns. It exists because some mechanics cannot be driven from a script - the
		// run-in-place rev-up reads the raw key in the original, so scripted taps produce nothing at any cadence
		// - and the only way to measure those is to play them by hand and read the trace afterwards. The matching
		// switch on the original's side is `FreeRun` in `_pt.j2as`.
		// LEAVE IT OFF WHEN COMMITTING: a recording is not a sweep, and a trace of one would be taken for the other.
		static constexpr bool FreeRunMode = false;
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
		// Set to scan the whole map for a tile event and report where it is, as runs. Off by default: it is
		// a one-off answer to "where did the level author actually put these", wanted whenever the test level
		// grows a new event family and a scenario has to be aimed at the geometry rather than at a
		// description of it. See ScanTileEvent().
		static constexpr bool ScanOneWayTiles = false;
		// The same for vines. `ob_vine2` asks whether the original's second grab, three tiles above the
		// first, is reachable here at all, and that cannot be told from a trace without knowing which row
		// the second vine is actually on. See ScanTileEvent().
		static constexpr bool ScanVineTiles = false;
		static constexpr std::int32_t EventScanMaxTileX = 400;
		static constexpr std::int32_t EventScanMaxTileY = 120;

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
		// A census of every event the level holds, taken once during the scan and logged as `[census]` rows.
		// It exists so that "there is no scenario for this" can be told apart from "the level has nothing to
		// write one against" - a scenario aimed at an event that is not there measures a player standing
		// still and reads as agreement. Sized well past EventType's range so a new event cannot overflow it.
		std::int32_t _eventCensus[512] = {};
		Vector2i _eventCensusFirst[512] = {};

		// Applies a scenario's scripted input for one tick and logs a row; false for an index past the end.
		// With `nameOut` set it only reports the scenario's name and returns before touching input or the
		// log, which is what lets the filter below select by name without a second copy of the switch - one
		// that would drift out of step with the real one the first time a scenario was renamed.
		bool ApplyInput(Actors::Player* player, std::int32_t scenario, std::int32_t t, StringView* nameOut = nullptr);
		// Whether a scenario passes FirstScenario/LastScenario and ScenarioFilter
		bool IsScenarioSelected(Actors::Player* player, std::int32_t s);
		// Marks the run finished and closes the game, so a capture does not need the process killed by hand
		void FinishRun();
		// Picks the character a scenario's move belongs to and moves the player to where it starts
		void SetupCharacter(Actors::Player* player, std::int32_t s);
		// Takes the previous scenario's props out of the world, at the reset
		void ClearProps();
		// Puts a scenario's props in place, once the player has settled
		void SetupProps(Actors::Player* player, std::int32_t s);
		// Reports the solid and empty runs of one tile row, for aiming gap scenarios
		void ScanFloor(Actors::Player* player, std::int32_t row);
		// Reports every tile carrying a given tile event, as runs - see ScanTileEvent()
		void ScanTileEvent(EventType wanted, StringView label);
		// Length of a scenario, in the original game's ticks
		static float GetScenarioTicks(std::int32_t s);
	};
}

#endif


















