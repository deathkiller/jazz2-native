#include "PhysicsProbe.h"

#if defined(WITH_PHYSICS_PROBE)

#include "../LevelHandler.h"
#include "../Events/EventMap.h"
#include "../Events/EventSpawner.h"
// The probe puts these in front of the player
#include "../Actors/Environment/Spring.h"
#include "../Actors/Solid/PushableBox.h"
#include "../Actors/Enemies/TurtleTube.h"

#include "../../nCine/Base/FrameTimer.h"

using namespace nCine;

namespace Jazz2::Tests
{
	PhysicsProbe::PhysicsProbe(LevelHandler* levelHandler)
		: _levelHandler(levelHandler), _state(StatePrime), _scenario(FirstScenario), _tick(0.0f), _waited(0.0f),
			_groundX(0.0f), _groundY(0.0f), _lastY(0.0f), _startFrames(0.0f), _still(0),
			_pushable(0.0f, 0.0f), _turtle(0.0f, 0.0f)
	{
	}

	void PhysicsProbe::ScanFloor(Actors::Player* player, std::int32_t row)
	{
		// Reports where a floor row is solid and where it is not, one line per run of tiles, so a
		// gap-crossing scenario can be aimed at a gap that is really there instead of a remembered one.
		// Uses the same emptiness test the movement does, so what it reports is what the player will meet.
		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
		Actors::ActorBase* collider = nullptr;
		std::int32_t runStart = 0;
		bool runSolid = false;
		bool first = true;
		for (std::int32_t tx = 0; tx <= FloorScanMaxTile; tx++) {
			AABBf tile(tx * 32.0f + 2.0f, row * 32.0f + 2.0f, tx * 32.0f + 30.0f, row * 32.0f + 30.0f);
			bool solid = (tx < FloorScanMaxTile ? !_levelHandler->IsPositionEmpty(player, tile, params, &collider) : !runSolid);
			if (first) {
				runStart = tx;
				runSolid = solid;
				first = false;
			} else if (solid != runSolid) {
				if (runSolid) {
					LOGI("[scan] row {} floor {}..{}", row, runStart, tx - 1);
				} else if (runStart > 0) {
					LOGI("[scan] row {} GAP {}..{} ({} wide)", row, runStart, tx - 1, tx - runStart);
				}
				runStart = tx;
				runSolid = solid;
			}
		}
	}

	float PhysicsProbe::GetScenarioTicks(std::int32_t s)
	{
		// A pole takes about 140 ticks from grab to launch, so a chain of five needs well over 700, and the
		// wall scenarios spend most of their length just travelling to the wall. Giving every scenario that
		// window would make a full sweep take three times as long, so only those that need it get it - kept
		// identical to ScenarioTicksFor() in the original game's probe.
		//
		// The spring chains get longer still: they are the scenarios measured by how far they get in a set
		// *time* rather than by a single manoeuvre. `dm_chain` needs 15 seconds; `sp_chain`'s layout was
		// later extended with a zig-zag of horizontal springs down the shaft at x=59-64 and now needs 30.
		if (s == 166) {
			return 2200.0f;
		}
		if (s == 334) {
			return 1100.0f;
		}
		// The pinball chamber left to itself, the same way `sp_chain` is
		if (s == 299) {
			return 1100.0f;
		}
		// The apex sweep is thirty plain standing jumps, so it needs no more than the default - and thirty
		// scenarios at the long window would add six minutes to every sweep for nothing
		if (s >= 256 && s <= 285) {
			return 250.0f;
		}
		return (s == 52 || s == 53 || s >= 82 ? 800.0f : 250.0f);
	}

	bool PhysicsProbe::ApplyInput(Actors::Player* player, std::int32_t scenario, std::int32_t t)
	{
		constexpr std::int32_t J = JumpTick;

		bool right = false, left = false, run = false, jump = false, down = false, fire = false;
		StringView name;
		// Every scenario name below is a literal except the apex sweep's, which is built from the index. It
		// lives here rather than in the case because `name` is read by the LOGI at the end of the function.
		char apexName[] = "ap_r00";

		// Each of these covers one input on its own or in combination - held throughout, released early,
		// released late, released and re-pressed, reversed, reversed and released, reversed back - on the
		// ground and in the air, with and without Run. Kept identical to the scenario list the original
		// game's probe runs, so the two traces line up scenario for scenario.
		switch (scenario) {
			// Ground only - acceleration, the caps, deceleration and turning around
			case 0: name = "g_stand"_s; break;
			case 1: name = "g_walk"_s; right = true; break;
			case 2: name = "g_dash"_s; right = true; run = true; break;
			case 3: name = "g_walk_rel20"_s; right = (t < 20); break;
			case 4: name = "g_walk_rel45"_s; right = (t < 45); break;
			case 5: name = "g_dash_rel"_s; right = (t < 60); run = (t < 60); break;
			case 6: name = "g_dash_relrun"_s; right = true; run = (t < 60); break;
			case 7: name = "g_walk_opp"_s; right = (t < 45); left = (t >= 45); break;
			case 8: name = "g_dash_opp"_s; run = true; right = (t < 60); left = (t >= 60); break;
			case 9: name = "g_dash_oppnr"_s; right = (t < 60); run = (t < 60); left = (t >= 60); break;
			case 10: name = "g_walk_repress"_s; right = (t < 45 || t >= 60); break;
			case 11: name = "g_runlate"_s; right = true; run = (t >= 45); break;
			// Vertical only - the launch, the rise, and what releasing jump does
			case 12: name = "a_jump"_s; jump = (t >= J); break;
			case 13: name = "a_jump_r5"_s; jump = (t >= J && t < J + 5); break;
			case 14: name = "a_jump_r10"_s; jump = (t >= J && t < J + 10); break;
			case 15: name = "a_jump_r20"_s; jump = (t >= J && t < J + 20); break;
			case 16: name = "a_jump_r30"_s; jump = (t >= J && t < J + 30); break;
			// Jump with horizontal speed - the launch boost and the arc
			case 17: name = "a_wjump"_s; right = true; jump = (t >= J); break;
			case 18: name = "a_wjump_r10"_s; right = true; jump = (t >= J && t < J + 10); break;
			case 19: name = "a_djump"_s; right = true; run = true; jump = (t >= J); break;
			case 20: name = "a_djump_r10"_s; right = true; run = true; jump = (t >= J && t < J + 10); break;
			// Air control - the direction released, reversed, reversed and released, reversed back
			case 21: name = "a_wjump_rel50"_s; right = (t < 50); jump = (t >= J); break;
			case 22: name = "a_wjump_rel75"_s; right = (t < 75); jump = (t >= J); break;
			case 23: name = "a_wjump_opp50"_s; right = (t < 50); left = (t >= 50); jump = (t >= J); break;
			case 24: name = "a_wjump_opp75"_s; right = (t < 75); left = (t >= 75); jump = (t >= J); break;
			case 25: name = "a_wjump_opp90"_s; right = (t < 90); left = (t >= 90); jump = (t >= J); break;
			case 26: name = "a_wjump_opp75_rel"_s; right = (t < 75); left = (t >= 75 && t < 90); jump = (t >= J); break;
			case 27: name = "a_wjump_opp75_bk"_s; right = (t < 75 || t >= 100); left = (t >= 75 && t < 100); jump = (t >= J); break;
			case 28: name = "a_djump_opp83"_s; run = true; right = (t < 83); left = (t >= 83); jump = (t >= J); break;
			case 29: name = "a_djump_opp83nr"_s; right = (t < 83); run = (t < 83); left = (t >= 83); jump = (t >= J); break;
			case 30: name = "a_djump_rel83"_s; right = (t < 83); run = (t < 83); jump = (t >= J); break;
			case 31: name = "a_jump_press60"_s; jump = (t >= J); right = (t >= 60); break;
			case 32: name = "a_jump_press60r"_s; jump = (t >= J); right = (t >= 60); run = (t >= 60); break;
			case 33: name = "a_wjump_runmid"_s; right = true; run = (t >= 60); jump = (t >= J); break;
			case 34: name = "a_djump_relrun"_s; right = true; run = (t < 60); jump = (t >= J); break;
			case 35: name = "a_wjump_opp60_bk"_s; right = (t < 60 || t >= 75); left = (t >= 60 && t < 75); jump = (t >= J); break;

			// Special moves. SetupCharacter() has already morphed to whichever rabbit owns each.
			case 36: name = "sp_jazz_upper"_s; down = (t >= 20); jump = (t >= 40); break;
			case 37: name = "sp_spaz_dj_hold"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			case 38: name = "sp_spaz_dj_tap"_s; jump = (t >= J && t < 50) || (t >= 60 && t < 64); break;
			case 39: name = "sp_spaz_side"_s; down = (t >= 20); jump = (t >= 40); break;
			case 40: name = "sp_lori_side"_s; down = (t >= 20); jump = (t >= 40); break;
			case 41: name = "sp_jazz_butt"_s; jump = (t >= J && t < 55); down = (t >= 70); break;
			case 42: name = "sp_spaz_butt"_s; jump = (t >= J && t < 55); down = (t >= 70); break;
			case 43: name = "sp_lori_butt"_s; jump = (t >= J && t < 55); down = (t >= 70); break;
			case 44: name = "sp_jazz_dj"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			// Springs. The vertical ones sit under the player's feet and fire from rest; the horizontal ones
			// are a few tiles ahead and get walked into.
			case 45: name = "ob_spring_red"_s; break;
			case 46: name = "ob_spring_green"_s; break;
			case 47: name = "ob_spring_blue"_s; break;
			case 48: name = "ob_spring_frozen"_s; break;
			case 49: name = "ob_spring_red_h"_s; right = true; break;
			case 50: name = "ob_spring_green_h"_s; right = true; break;
			case 51: name = "ob_spring_blue_h"_s; right = true; break;
			case 52: name = "ob_vpole"_s; jump = (t >= J); break;
			case 53: name = "ob_hpole"_s; right = true; run = true; break;
			case 54: name = "ob_push_box"_s; right = true; break;
			case 55: name = "ob_push_rock"_s; right = true; run = true; break;
			// Copter ears: one jump to get airborne, then a 2-tick tap every 6 to keep them going
			case 56: name = "sp_jazz_copter"_s; jump = (t >= J && t < J + 5) || (t >= 75 && ((t - 75) % 6) < 2); break;
			case 57: name = "sp_jazz_copter_fwd"_s; right = true; jump = (t >= J && t < J + 5) || (t >= 75 && ((t - 75) % 6) < 2); break;
			case 58: name = "sp_lori_copter"_s; jump = (t >= J && t < J + 5) || (t >= 75 && ((t - 75) % 6) < 2); break;
			case 59: name = "sp_lori_copter_fwd"_s; right = true; jump = (t >= J && t < J + 5) || (t >= 75 && ((t - 75) % 6) < 2); break;
			case 60: name = "sp_spaz_copter"_s; jump = (t >= J && t < J + 5) || (t >= 75 && ((t - 75) % 6) < 2); break;
			// Steering during a buttstomp
			case 61: name = "sp_butt_right"_s; jump = (t >= J && t < 55); down = (t >= 70); right = (t >= 75); break;
			case 62: name = "sp_butt_right_run"_s; jump = (t >= J && t < 55); down = (t >= 70); right = (t >= 75); run = (t >= 75); break;
			case 63: name = "sp_butt_moving"_s; right = true; jump = (t >= J && t < 55); down = (t >= 70); break;
			// The sidekick with jump released, so it ends with the player on the floor instead of jumping out
			case 64: name = "sp_spaz_side_rel"_s; down = (t >= 20 && t < 44); jump = (t >= 40 && t < 44); break;
			case 65: name = "sp_lori_side_rel"_s; down = (t >= 20 && t < 44); jump = (t >= 40 && t < 44); break;
			// Copter steering, reversed twice
			case 66: name = "sp_copter_lrl"_s; jump = (t >= J && t < J + 5) || (t >= 75 && ((t - 75) % 6) < 2);
				right = (t >= 80 && t < 130) || (t >= 180); left = (t >= 130 && t < 180); break;
			case 67: name = "ob_vine"_s; jump = (t >= J && t < J + 5); right = (t >= 60); break;
			case 68: name = "ob_vine_run"_s; jump = (t >= J && t < J + 5); right = (t >= 60); run = true; break;
			// Slopes, reached by starting beside them rather than travelling there
			case 69: name = "sl_walk_right"_s; right = true; break;
			case 70: name = "sl_run_right"_s; right = true; run = true; break;
			case 71: name = "sl_run_left"_s; left = true; run = true; break;
			case 72: name = "sp_butt_high"_s; down = (t >= 20); right = (t >= 40 && t < 70) || (t >= 90 && t < 140) || (t >= 160); break;
			case 73: name = "sl_down_walk"_s; right = true; break;
			case 74: name = "sl_down_run"_s; right = true; run = true; break;
			case 75: name = "sl_down_jump"_s; right = true; run = true; jump = (t >= 30 && t < 40); break;
			case 76: name = "sl_up_walk"_s; right = true; break;
			case 77: name = "sl_up_run"_s; right = true; run = true; break;
			case 78: name = "ob_vine2"_s; right = (t >= 10); break;
			case 79: name = "ob_vine2_run"_s; right = (t >= 10); run = true; break;
			case 80: name = "sp_butt_tap_r"_s; down = (t >= 20 && t < 24); right = (t >= 26); break;
			case 81: name = "sp_butt_hold_r"_s; down = (t >= 20); right = (t >= 26); break;
			// Delay before the second jump press, sweeping across the original's double jump window. The
			// first jump is held from t45 to its apex at ~t72 and the second press comes at an offset past
			// it; in the original, +5 through +28 fire and +30 and later do nothing, as does a press that
			// comes in before the apex.
			case 82: name = "sp_dj_d05"_s; jump = (t >= J && t < 72) || (t >= 77); break;
			case 83: name = "sp_dj_d20"_s; jump = (t >= J && t < 72) || (t >= 92); break;
			case 84: name = "sp_dj_d28"_s; jump = (t >= J && t < 72) || (t >= 100); break;
			case 85: name = "sp_dj_d30"_s; jump = (t >= J && t < 72) || (t >= 102); break;
			case 86: name = "sp_dj_d40"_s; jump = (t >= J && t < 72) || (t >= 112); break;
			case 87: name = "sp_dj_early"_s; jump = (t >= J && t < 60) || (t >= 65); break;
			// Lori kicks over and over while crouched, so the jump key is tapped far more often than the
			// move can retrigger - the gaps between the kicks are the move's own cadence, not the input's
			case 88: name = "sp_lori_kick_rep"_s; down = (t >= 20); jump = (t >= 40 && ((t - 40) % 15) < 2); break;
			// Wall bounce: dash at the wall closing the right end of the upper floor, jump, and turn back
			// to land on the platform above and to the left. The wall face is at tile 234, which a dash
			// from the left end of this floor reaches at t164 and a walk at about t305, so the jump tick
			// is what sets how far out the jump happens - and it matters, because pressed flat against
			// the wall the player has no horizontal speed left and the jump gets no speed bonus at all.
			// These need the long scenario window; see GetScenarioTicks().
			case 89: name = "wb_dash_j130"_s; right = (t < 130); run = true; jump = (t >= 130 && t < 160); left = (t >= 134); break;
			case 90: name = "wb_dash_j145"_s; right = (t < 145); run = true; jump = (t >= 145 && t < 175); left = (t >= 149); break;
			case 91: name = "wb_dash_j155"_s; right = (t < 155); run = true; jump = (t >= 155 && t < 185); left = (t >= 159); break;
			case 92: name = "wb_dash_j164"_s; right = (t < 164); run = true; jump = (t >= 164 && t < 194); left = (t >= 168); break;
			case 93: name = "wb_walk_j300"_s; right = (t < 300); jump = (t >= 300 && t < 330); left = (t >= 304); break;
			// Blasting yourself off the wall with an RF shot fired into it, from the ground and from a
			// jump, point blank and a few tiles out
			case 94: name = "wb_rf_g0"_s; right = (t < 164); run = true; fire = (t >= 166 && t < 172); break;
			case 95: name = "wb_rf_g5"_s; right = (t < 145); run = true; fire = (t >= 147 && t < 153); break;
			case 96: name = "wb_rf_j0"_s; right = (t < 164); run = true; jump = (t >= 164 && t < 194); fire = (t >= 168 && t < 174); break;
			case 97: name = "wb_rf_j5"_s; right = (t < 145); run = true; jump = (t >= 145 && t < 175); fire = (t >= 149 && t < 155); break;
			// Dash right for the whole scenario, which finds where the wall actually is: x simply stops
			case 98: name = "wb_wallfind"_s; right = true; run = true; break;
			// Climbing the test wall at the left end of the upper floor. The climb needs the player off
			// the ground and pressing into the wall, so it is a jump with the direction held through it -
			// which is also the approach that sets `_jumpReleased` and used to leave them short. There is
			// no ledge climb in the original, so these five have no counterpart to compare against.
			case 99: name = "lc_wallfind_l"_s; left = true; run = true; break;
			case 100: name = "lc_walk"_s; left = true; jump = (t >= 45 && t < 60); break;
			case 101: name = "lc_dash"_s; left = true; run = true; jump = (t >= 45 && t < 60); break;
			case 102: name = "lc_walk_hold"_s; left = true; jump = (t >= 45); break;
			// How close to the detonation the player has to be for an RF blast to throw them. The player is
			// parked a set distance from the wall by SetupCharacter() and fires from a standstill, so the
			// shot's flight is over in a frame or two and the detonation lands at the wall, that distance
			// away. Firing while dashing does not measure this: the shot inherits the player's speed, so it
			// reaches the wall long before the player does and the distance at detonation is not the one
			// that was set up. Two ticks of Right at the start only fix which way the player is facing.
			case 103: name = "wb_rf_r12"_s; right = (t < 2); fire = (t >= 60 && t < 66); break;
			case 104: name = "wb_rf_r24"_s; right = (t < 2); fire = (t >= 60 && t < 66); break;
			case 105: name = "wb_rf_r36"_s; right = (t < 2); fire = (t >= 60 && t < 66); break;
			case 106: name = "wb_rf_r48"_s; right = (t < 2); fire = (t >= 60 && t < 66); break;
			case 107: name = "wb_rf_r64"_s; right = (t < 2); fire = (t >= 60 && t < 66); break;
			// Chained poles, and poles entered off a spring. A pole hands back everything it was given plus
			// a fixed bonus (vertical) or triples it (horizontal), so chaining compounds - which is what
			// exposed an earlier flat-value model as a regression. These verify the compounding end to end
			// rather than one pole at a time, and they need the long scenario window.
			case 108: name = "ob_vpole_x2"_s; jump = (t >= J); break;
			case 109: name = "ob_vpole_x3"_s; jump = (t >= J); break;
			case 110: name = "ob_vpole_x5"_s; jump = (t >= J); break;
			case 111: name = "ob_hpole_x3"_s; right = true; run = true; break;
			case 112: name = "ob_spring_vpole"_s; break;			// the vertical spring fires on its own
			case 113: name = "ob_spring_hpole"_s; right = true; break;	// walk into the horizontal spring first
			case 114: name = "ob_hpole_walk"_s; right = true; break;	// the same pole at walking speed, a second fit point
			// Crossing a gap. Row 11's floor runs from tile 153 and is broken by gaps of 1, 2, 3, 4, 5 and 6
			// tiles at 180, 209, 238, 269, 300 and 336, each with a full 27+ tiles of floor before it - so
			// every gap is met at true top speed rather than at whatever survived the previous one.
			// SetupCharacter() parks the player 24 tiles short of the gap its scenario is named for.
			// Whether they crossed or fell is obvious from `y`, and the speed they crossed at is in the
			// same rows. The original does this ballistically, with no upward boost of any kind.
			case 115: name = "lh_g1_walk"_s; right = true; break;
			case 116: name = "lh_g1_dash"_s; right = true; run = true; break;
			case 117: name = "lh_g2_walk"_s; right = true; break;
			case 118: name = "lh_g2_dash"_s; right = true; run = true; break;
			case 119: name = "lh_g3_walk"_s; right = true; break;
			case 120: name = "lh_g3_dash"_s; right = true; run = true; break;
			case 121: name = "lh_g4_walk"_s; right = true; break;
			case 122: name = "lh_g4_dash"_s; right = true; run = true; break;
			case 123: name = "lh_g5_walk"_s; right = true; break;
			case 124: name = "lh_g5_dash"_s; right = true; run = true; break;
			case 125: name = "lh_g6_walk"_s; right = true; break;
			case 126: name = "lh_g6_dash"_s; right = true; run = true; break;
			// Which rise gravity a spring launch decays at. Both fall onto the same spring from the same
			// height and differ only in the jump tap on the way down, so the two rises have to match - and
			// without the fix in OnHitSpring() they do not: 602 px against 399. The tap sets
			// `_jumpReleased`, and a spring is not a solid object, so nothing calls OnHitFloor() to clear it
			// again before the player lands on it. Note the tap also puts Jazz into a copter, which is why
			// the second one takes twice as long to come down - the flag is what is being tested, and it
			// stays set for the whole descent either way. **Engine only**: the original's spring ascent was
			// measured with no key pressed at all, so the held rate is what this holds the engine to rather
			// than a measurement of this particular case.
			case 127: name = "jr_spring_fall"_s; break;
			case 128: name = "jr_spring_fall_jrel"_s; jump = (t >= 5 && t < 12); break;
			// Explosive knockback, one weapon at a time, against the upper floor's wall. Only the RF blast
			// is known to move the player: this engine has an `ApplyBlastKnockback()` for RF, a bare
			// `AddExternalForce(8, 0)` for the Seeker that was never measured, and nothing at all for TNT.
			// Running the same distances with each is what says whether the original has one explosion
			// mechanic or three. Parked and fired from rest on purpose - a shot fired while moving inherits
			// the player's speed and detonates at a distance nobody chose, which is the mistake that made
			// the first RF reach sweep meaningless.
			case 129: name = "wb_rf_t1"_s; fire = (t >= 60 && t < 66); break;
			case 130: name = "wb_rf_t2"_s; fire = (t >= 60 && t < 66); break;
			case 131: name = "wb_rf_t3"_s; fire = (t >= 60 && t < 66); break;
			case 132: name = "wb_rf_t4"_s; fire = (t >= 60 && t < 66); break;
			case 133: name = "wb_rf_t5"_s; fire = (t >= 60 && t < 66); break;
			case 134: name = "wb_rf_t6"_s; fire = (t >= 60 && t < 66); break;
			case 135: name = "wb_sk_t1"_s; fire = (t >= 60 && t < 66); break;
			case 136: name = "wb_sk_t2"_s; fire = (t >= 60 && t < 66); break;
			case 137: name = "wb_sk_t3"_s; fire = (t >= 60 && t < 66); break;
			// TNT is placed at the player rather than fired forward, so the wall distance is not the
			// variable here - what these ask is whether its own detonation moves the player at all
			case 138: name = "wb_tnt_t1"_s; fire = (t >= 60 && t < 66); break;
			case 139: name = "wb_tnt_t2"_s; fire = (t >= 60 && t < 66); break;
			// Buttstomping an enemy, and specifically the bounce at the end of it. A TurtleTube because it
			// holds still, so the impact lands where it was aimed; the two differ in the drop height, which
			// is what sets the speed going in - the question is whether the bounce depends on it.
			case 140: name = "en_butt_turtle"_s; down = (t >= 20); break;
			case 141: name = "en_butt_turtle_low"_s; down = (t >= 20); break;
			// The RF blast taken on a slope. The throw is a horizontal speed, and the slope search turns
			// horizontal travel into vertical, so an incline could change how far it carries even with the
			// knockback itself identical. Each fires into the *rising* side, which is the only way the shot
			// meets anything within its one-tile reach: uphill needs no turn, downhill needs the player
			// turned round first, which is what the brief left press is for.
			case 142: name = "sl_rf_up"_s; fire = (t >= 60 && t < 66); break;
			case 143: name = "sl_rf_down"_s; left = (t < 10); fire = (t >= 60 && t < 66); break;
			// Jumping into a ceiling. The test section is a staircase of 2-tile-wide bays over the row-16
			// floor, with 2, 3, 4, 5 and 6 tiles of clearance; SetupCharacter() parks the player in the middle
			// of one bay so a standing jump stays inside it. A standing jump rises 132 px, so 2 and 3 tiles
			// are struck hard, 4 is marginal and 5 and 6 clear - which is the range the reaction is worth
			// reading across. Jump is held throughout: what happens to the rest of the ascent after the head
			// hits is the measurement, and releasing would confound it with the released-gravity switch.
			case 144: name = "cl_j2"_s; jump = (t >= J); break;
			case 145: name = "cl_j3"_s; jump = (t >= J); break;
			case 146: name = "cl_j4"_s; jump = (t >= J); break;
			case 147: name = "cl_j5"_s; jump = (t >= J); break;
			case 148: name = "cl_j6"_s; jump = (t >= J); break;
			// The same bays with Spaz's double jump, which adds a second launch part way up - so it can be
			// spent before the ceiling, against it, or after bouncing off it. Same press pattern as
			// `sp_spaz_dj_hold`, so the open-air arc it produces is already on record to compare against.
			case 149: name = "cl_dj2"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			case 150: name = "cl_dj3"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			case 151: name = "cl_dj4"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			case 152: name = "cl_dj5"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			case 153: name = "cl_dj6"_s; jump = (t >= J && t < 50) || (t >= 60); break;
			// Moving, which cannot be pinned to one bay - a walk crosses a bay in 16 ticks and a dash in 7,
			// so the player is under a different ceiling by the time the arc finishes. Which bay each strike
			// happened in is read from the `x` column, and this is also where the launch boost varies the
			// jump strength, since it scales with the speed carried in.
			//
			// Heading **left** from the east end, so the ceiling above them drops from 6 tiles to 2 as they
			// go. Going the other way sends them past the section entirely and into the 17-tile wall at
			// tiles 55-56, which they then climb - a first attempt measured that instead, all the way to
			// tile 83 and above the top of the level.
			case 154: name = "cl_walk_j"_s; left = true; jump = (t >= J); break;
			case 155: name = "cl_dash_j"_s; left = true; run = true; jump = (t >= J); break;
			case 156: name = "cl_dj_walk"_s; left = true; jump = (t >= J && t < 50) || (t >= 60); break;
			case 157: name = "cl_dj_dash"_s; left = true; run = true; jump = (t >= J && t < 50) || (t >= 60); break;
			// Pinning the *lower* edge of the double-jump window. Read off the original by the fall speed at
			// the second press, it is refused at 0.0000 and accepted at 1.2500, and the `sp_dj_d*` family
			// samples nothing in between - so these four press at ticks that land there. The number is the
			// tick of the second press.
			//
			// The first press is released just *past* the apex rather than at 72 like `sp_dj_d*`: releasing
			// changes only the rise gravity, so once the player is falling it cannot alter the arc, and
			// releasing any earlier would.
			case 158: name = "sp_dj_p64"_s; jump = (t >= J && t < 62) || (t >= 64); break;
			case 159: name = "sp_dj_p68"_s; jump = (t >= J && t < 62) || (t >= 68); break;
			case 160: name = "sp_dj_p72"_s; jump = (t >= J && t < 62) || (t >= 72); break;
			case 161: name = "sp_dj_p76"_s; jump = (t >= J && t < 62) || (t >= 76); break;
			// Two pole events immediately next to each other. `InitialPoleStage()` refuses any pole within
			// `SamePoleTolerance` tiles of the one just used, which is meant to stop the launch re-grabbing
			// the same pole - but two *distinct* poles can be adjacent, and then the second is ignored.
			// Tested both ways round for each type, since the check covers X and Y alike.
			case 162: name = "ob_vpole_adjx"_s; jump = (t >= J); break;
			case 163: name = "ob_vpole_adjy"_s; jump = (t >= J); break;
			case 164: name = "ob_hpole_adjx"_s; right = true; run = true; break;
			case 165: name = "ob_hpole_adjy"_s; right = true; run = true; break;
			// The level's own spring-and-pole chain, which is the only scenario built entirely out of the
			// level rather than out of props the probe places. Dropped in at the top of the shaft at tile
			// (34,16) with nothing held for fifteen seconds: the fall lands on the horizontal blue spring at
			// (34,24), and from there the springs at (51,26), (56,22), (54,24), (56,29) and (51,32) and the
			// poles at (44,31) and (54,10) carry the player around on their own.
			//
			// Nothing is held on purpose. A chain this long amplifies any difference between the two games,
			// so the comparison is read as *when each spring fires*, not as a position at tick 900 - by then
			// a single tick of divergence early on has put the player somewhere else entirely.
			case 166: name = "sp_chain"_s; break;
			// The part of that chain the player was reported to get stuck in, on its own: the vertical blue
			// spring at (54,24) fires up the one-tile shaft at x=54 into the vertical pole at (54,10). What
			// the pole does with a player who arrives *falling* is the question - a vertical pole launches in
			// the direction it was entered, so a player who lands back on it is thrown back down the shaft
			// into the spring, and round again.
			case 167: name = "sp_pole_loop"_s; break;
			// A horizontal pole entered with *no* horizontal speed, which `sp_chain` turned up as the one
			// entry the launch curve was never fitted on. The three points it was fitted on - 4 -> 12,
			// 9.34 -> 20, 16 -> 20 - leave the low end undetermined, since only the first is unclamped, and
			// `3|v|` extrapolates to zero. The original launches at 8 and *leftward*, so both the magnitude
			// and the sign at zero need pinning: hence the same jump into the same overhead pole three times,
			// facing whichever way the tap at the start left the player.
			//
			// A jump straight up reaches it with x speed exactly 0 rather than merely small, and the ground
			// friction from the tap has 40 ticks to bleed off before the jump, so all three enter identically
			// apart from the facing.
			//
			// The tap is three ticks and not twelve: twelve moves the player 33 px, more than half a tile, and
			// the pole is then no longer overhead - the first attempt at these two missed it entirely and
			// measured a player jumping on the spot instead. Three ticks moves about a pixel.
			case 168: name = "ob_hpole_z0"_s; jump = (t >= J); break;
			case 169: name = "ob_hpole_zl"_s; left = (t < 3); jump = (t >= J); break;
			case 170: name = "ob_hpole_zr"_s; right = (t < 3); jump = (t >= J); break;
			// Two tiles away rather than four, so the player is still accelerating when it arrives and enters
			// below the walk cap - a fourth fit point, in the stretch of the curve that has none
			case 171: name = "ob_hpole_near"_s; right = true; break;
			// The one scenario that runs in a **different level**: the original game's own Diamondus 3, which
			// has a spring chain of its own at the far west end - a one-tile shaft at x=1 with a horizontal
			// blue spring at (1,45) at the bottom, feeding red and green springs and a horizontal pole at
			// (11,52). Shipped level geometry nobody built for a test, which is what makes it worth having:
			// it exercises the same mechanics against a layout the constants were not fitted on.
			//
			// Float-up areas, which lift the player against gravity. The test level has a diagonal ladder of
			// them climbing right from (34,49) to (46,37), reached by jumping off the block at (32,49) - so
			// riding it needs Right held as well as the lift, and the interesting question is what each of
			// those two contributes. None of this is measured against the original yet: `AreaFloatUp` has no
			// `!IsReforged()` branch anywhere, and its lift is written as an *external force* scaled by
			// `timeMult`, which is the shape that made push speed frame-rate dependent before it was measured.
			//
			// The set covers the intended route and then each way of departing from it: jump held versus
			// released (which picks the rise gravity, and may or may not matter once something else is
			// lifting), walking in before jumping rather than jumping first, the same ride *against* the
			// ladder's direction, no direction at all, and a buttstomp inside it - which this engine refuses
			// outright, since the whole float branch is skipped while one is running. `fu_stand` presses
			// nothing: a float area under a standing player either lifts them off the ground or does not, and
			// which it is has never been checked.
			case 172: name = "fu_jump_right"_s; jump = (t >= 5); right = (t >= 5); break;
			case 173: name = "fu_jump_right_rel"_s; jump = (t >= 5 && t < 25); right = (t >= 5); break;
			case 174: name = "fu_right_then_jump"_s; right = true; jump = (t >= 20); break;
			case 175: name = "fu_jump_left"_s; jump = (t >= 5); left = (t >= 5); break;
			case 176: name = "fu_jump_only"_s; jump = (t >= 5); break;
			case 177: name = "fu_butt"_s; jump = (t >= 5 && t < 25); right = (t >= 5); down = (t >= 60); break;
			case 178: name = "fu_stand"_s; break;
			// The same mechanic in isolation. The ladder in the level answers "does riding it work", but not
			// what the rule *is*: its tiles are discrete and unevenly spaced, so a re-arm that looks like it
			// happens 1 tick after the rise expires with jump held and 6 with it released may be the rule or
			// may just be where the next tile was. A solid column placed by the probe removes the geometry -
			// no gaps, and no horizontal motion to carry the player out of it - so the `ys` profile alone is
			// the rule.
			//
			// `fu_col_none` is also the test `fu_stand` was meant to be and is not: the level has no float
			// event at (32,49), so `fu_stand` only shows that nothing happens where there is nothing. Here
			// the column covers the player's own tile, so a grounded player really is inside one.
			case 179: name = "fu_col_hold"_s; jump = (t >= 5); break;
			case 180: name = "fu_col_rel"_s; jump = (t >= 5 && t < 25); break;
			case 181: name = "fu_col_none"_s; break;
			// Wind, belts and the slide tile - the three remaining event families that move the player
			// without being touched, and none of them measured against the original before. All three are
			// probe-placed, which is what the float column showed to be worth doing: a controlled field
			// beats whatever a level happens to contain.
			//
			// Wind applies anywhere, air or ground, as a direct position move. The three standing variants
			// exist because the *conversion* is under test as much as the mechanic: this engine folds
			// WIND_LEFT and WIND_RIGHT onto one `AreaHForce` with a left slot and a right slot, and reads
			// `MODIFIER_WIND_LEFT` with a positive parameter into the **right** slot - so `wd_stand_l` and
			// `wd_stand_r` are the same native event here while being different events in the original.
			// `wd_stand_l248` is the same wind-left event with 248, which the converter does read leftward.
			case 182: name = "wd_stand_r"_s; break;
			case 183: name = "wd_stand_l"_s; break;
			case 184: name = "wd_stand_l248"_s; break;
			// Walking into the wind, and taking it in the air, where a level actually uses one
			case 185: name = "wd_walk_into"_s; left = true; break;
			case 186: name = "wd_air_r"_s; jump = (t >= 5); break;
			// Belts, which are floor events: read from the tile below the player and only while grounded.
			// The plain pair move the player by position; the accelerating pair add to the speed instead -
			// and do it *without* scaling by `timeMult`, which is the shape that made push speed
			// frame-rate dependent before it was measured. All four are placed with the parameter a level
			// author gets by default, which the converter turns into a strength of 3.
			case 187: name = "bl_right"_s; break;
			case 188: name = "bl_left"_s; break;
			case 189: name = "bl_acc_right"_s; break;
			case 190: name = "bl_acc_left"_s; break;
			case 191: name = "bl_walk_against"_s; left = true; break;
			// The slide tile, which is meant to make the floor slippery - the player keeps their speed
			// longer after letting go. `EventType::ModifierSlide` is converted and then read by nothing at
			// all in this engine, so the expectation is that these two match `g_dash_rel` and `g_walk_rel45`
			// exactly while the original's slide further. Same release tick as those two, for that reason.
			case 192: name = "sd_dash"_s; right = (t < 60); run = (t < 60); break;
			case 193: name = "sd_walk"_s; right = (t < 45); break;
			// A second strength for each, because one point does not determine a factor - the mistake the
			// horizontal pole's launch curve was recorded with. The default belt gives 2.0 px/tick in the
			// original, which fits `strength 4 x 0.5` and `strength 3 x 0.667` equally well, and those
			// disagree everywhere else. An explicit 8 separates them: 4.0 against 5.33. The accelerating
			// belt's default drives to a cap of 6.0, which fits `2 x strength`, and 8 should then cap at 16.
			// The extra wind strength is there to confirm the factor is linear rather than fitted at one point.
			case 194: name = "bl_right_p8"_s; break;
			case 195: name = "bl_acc_right_p8"_s; break;
			case 196: name = "wd_stand_r_p4"_s; break;
			// A slide tile carries a 2-bit Strength parameter. The measurement that produced
			// LegacySlideWalkDecel was taken at 0, so this says whether the number means anything at all.
			case 197: name = "sd_dash_s3"_s; right = (t < 60); run = (t < 60); break;
			// Sucker tubes. The event assigns a speed outright and takes control for a fixed window, so the
			// four directions and a high speed measure the assignment, and `tb_wait` measures a parameter
			// this engine parses and then ignores - there is a `TODO: Implement other parameters` on it.
			case 198: name = "tb_right"_s; break;
			case 199: name = "tb_left"_s; break;
			case 200: name = "tb_up"_s; break;
			case 201: name = "tb_down"_s; break;
			case 202: name = "tb_diag"_s; break;
			case 203: name = "tb_fast"_s; break;
			case 204: name = "tb_wait"_s; break;
			// Tubes in a row, one to five empty tiles apart. This engine holds the tube state for a fixed 10
			// ticks, and at the 8 px/tick these are set to that is 80 px - so a 1-tile gap is comfortably
			// inside the window and a 5-tile gap (192 px) is well outside it. Where the chain stops carrying
			// the player is the measurement, and whether the two games put that boundary in the same place.
			case 205: name = "tb_gap1"_s; break;
			case 206: name = "tb_gap2"_s; break;
			case 207: name = "tb_gap3"_s; break;
			case 208: name = "tb_gap4"_s; break;
			case 209: name = "tb_gap5"_s; break;
			// The spring parameters, none of which any scenario had ever set: every spring measured so far
			// was placed with its defaults. A vertical spring can be flipped to fire *downwards*, either
			// axis' entry speed can be kept rather than zeroed, the whole thing can be delayed, and a
			// horizontal one can be reversed.
			// A downward spring can only act on a player who hits it from below, so this one jumps into it
			case 210: name = "ob_spring_down"_s; jump = (t >= 5); break;
			case 211: name = "ob_spring_keepx"_s; right = true; run = true; break;
			case 212: name = "ob_spring_keepy"_s; break;
			case 213: name = "ob_spring_delay"_s; right = true; run = true; break;
			case 214: name = "ob_spring_h_rev"_s; right = true; run = true; break;
			// The two middle slide strengths. Strength is a 2-bit field, and 0 and 3 measured 4000/12000 and
			// 1600/4500 of a 65536th - ratios of 2.5 and 2.67, so not one divisor applied to both. A 2-bit
			// parameter with four unrelated pairs is a lookup table, and then all four are needed and enough.
			case 215: name = "sd_dash_s1"_s; right = (t < 60); run = (t < 60); break;
			case 216: name = "sd_dash_s2"_s; right = (t < 60); run = (t < 60); break;
			// A row and a column of tubes, to separate the hold's duration from its exit condition
			case 217: name = "tb_row"_s; break;
			case 218: name = "tb_col_up"_s; break;
			// The double jump across every combination the original's probe already covers and this side
			// never did: the second press held or tapped, taken while walking, dashing, or with the
			// direction flipped either *on* the press or after it, plus three more samples of the window.
			// Ported with the original's exact input patterns, so they compare against the trace already
			// captured - `sp_dj_*` names match, and the window opens at the apex around tick 85 here where
			// the `sp_dj_d*` family releases the first press at 72 instead of 50.
			case 219: name = "sp_dj_hold"_s; jump = (t >= J && t < 50) || (t >= 85); break;
			case 220: name = "sp_dj_tap"_s; jump = (t >= J && t < 50) || (t >= 85 && t < 89); break;
			case 221: name = "sp_dj_walk"_s; right = true; jump = (t >= J && t < 50) || (t >= 85); break;
			case 222: name = "sp_dj_dash"_s; right = true; run = true; jump = (t >= J && t < 50) || (t >= 85); break;
			case 223: name = "sp_dj_flip"_s; right = (t < 85); left = (t >= 85); jump = (t >= J && t < 50) || (t >= 85); break;
			case 224: name = "sp_dj_flip_mid"_s; right = (t < 100); left = (t >= 100); jump = (t >= J && t < 50) || (t >= 85); break;
			case 225: name = "sp_dj_d10"_s; jump = (t >= J && t < 72) || (t >= 82); break;
			case 226: name = "sp_dj_d22"_s; jump = (t >= J && t < 72) || (t >= 94); break;
			case 227: name = "sp_dj_d25"_s; jump = (t >= J && t < 72) || (t >= 97); break;
			// Three the original does not have either: letting the direction *go* rather than reversing it,
			// before and after the double jump, and a second press held for twelve ticks - between the
			// four-tick tap and the indefinite hold, which is where the released-gravity switch decides how
			// much of the arc survives.
			case 228: name = "sp_dj_relbefore"_s; right = (t < 80); jump = (t >= J && t < 50) || (t >= 85); break;
			case 229: name = "sp_dj_relafter"_s; right = (t < 100); jump = (t >= J && t < 50) || (t >= 85); break;
			case 230: name = "sp_dj_tap12"_s; jump = (t >= J && t < 50) || (t >= 85 && t < 97); break;
			// Is the window a fall *speed* or a *timer*? The `sp_dj_d*` family cannot tell: it releases the
			// first press at one fixed tick, so the delay and the fall speed grow together. The nine above
			// release at 50 instead and are refused at a fall speed of 3.00 that `sp_dj_d25` is accepted at,
			// which says timer - and every one of the fifteen fits "accepted up to 28 ticks after the first
			// press is released, refused from 30". These three pin it: a *third* release tick either side of
			// that boundary, plus the missing 29 on the original release tick.
			case 231: name = "sp_dj_r60_d28"_s; jump = (t >= J && t < 60) || (t >= 88); break;
			case 232: name = "sp_dj_r60_d30"_s; jump = (t >= J && t < 60) || (t >= 90); break;
			case 233: name = "sp_dj_d29"_s; jump = (t >= J && t < 72) || (t >= 101); break;
			// Every direction combination above is measured at a delay of 35, which the timer refuses - so
			// they say what a *refused* press does and nothing about what the move itself does with the
			// arrow keys. These repeat the same combinations inside the window: first press released at 72
			// like the `sp_dj_d*` family, second at 85, thirteen ticks later.
			case 234: name = "sp_dj_a_none"_s; jump = (t >= J && t < 72) || (t >= 85); break;
			case 235: name = "sp_dj_a_walk"_s; right = true; jump = (t >= J && t < 72) || (t >= 85); break;
			case 236: name = "sp_dj_a_dash"_s; right = true; run = true; jump = (t >= J && t < 72) || (t >= 85); break;
			case 237: name = "sp_dj_a_flip"_s; right = (t < 85); left = (t >= 85); jump = (t >= J && t < 72) || (t >= 85); break;
			case 238: name = "sp_dj_a_flipmid"_s; right = (t < 100); left = (t >= 100); jump = (t >= J && t < 72) || (t >= 85); break;
			case 239: name = "sp_dj_a_dashflip"_s; run = true; right = (t < 85); left = (t >= 85); jump = (t >= J && t < 72) || (t >= 85); break;
			case 240: name = "sp_dj_a_relb"_s; right = (t < 80); jump = (t >= J && t < 72) || (t >= 85); break;
			case 241: name = "sp_dj_a_rela"_s; right = (t < 100); jump = (t >= J && t < 72) || (t >= 85); break;
			case 242: name = "sp_dj_a_lr"_s; left = (t < 85); right = (t >= 85); jump = (t >= J && t < 72) || (t >= 85); break;
			case 243: name = "sp_dj_a_press"_s; right = (t >= 85); jump = (t >= J && t < 72) || (t >= 85); break;
			case 244: name = "sp_dj_a_tap"_s; jump = (t >= J && t < 72) || (t >= 85 && t < 89); break;
			case 245: name = "sp_dj_a_tap12"_s; jump = (t >= J && t < 72) || (t >= 85 && t < 97); break;
			// A *third* press, after the double jump has been spent. Letting go re-arms the timer, so the
			// window is open again and only `_canDoubleJump` stands between this and a third launch.
			case 246: name = "sp_dj_a_re"_s; jump = (t >= J && t < 72) || (t >= 85 && t < 91) || (t >= 100); break;
			// A fourth release tick, and the only other one the 60/70 Hz input sampling resolves exactly:
			// ticks 50, 60, 72 and 85 all land on a frame boundary, 55 does not. Delays 15/25/29 accepted
			// and 30 refused, the same boundary as at 60 and 72, at fall speeds that overlap all three.
			// Not 10: released this early the apex is only at 61, so a press at 60 is refused for still
			// rising rather than for the timer - which is exactly what `cl_dj2` relies on a ceiling to fix.
			case 247: name = "sp_dj_r50_d15"_s; jump = (t >= J && t < 50) || (t >= 65); break;
			case 248: name = "sp_dj_r50_d25"_s; jump = (t >= J && t < 50) || (t >= 75); break;
			case 249: name = "sp_dj_r50_d29"_s; jump = (t >= J && t < 50) || (t >= 79); break;
			case 250: name = "sp_dj_r50_d30"_s; jump = (t >= J && t < 50) || (t >= 80); break;
			// The jump key held to well *past* the apex, which is the one way to get a delay inside the
			// window at a high fall speed - the release opens the timer at tick 85 where the player is
			// already twelve ticks into the fall. d20 and d25 sit at about 4.0 and 4.6, and the original
			// double jumps at both: that is what retired the fall-speed cap the window was first read as,
			// which had been refusing them. Nothing else samples that corner.
			case 251: name = "sp_dj_r85_d10"_s; jump = (t >= J && t < 85) || (t >= 95); break;
			case 252: name = "sp_dj_r85_d20"_s; jump = (t >= J && t < 85) || (t >= 105); break;
			case 253: name = "sp_dj_r85_d25"_s; jump = (t >= J && t < 85) || (t >= 110); break;
			// No first jump at all - dropped in from 420 px up, the same way `jr_spring_fall` is. The timer
			// is armed by *letting go* of the jump key, so on this model a player who never touched it has
			// no window and a fall off a ledge cannot be double jumped out of. `ff` presses once and holds;
			// `ff_rel` taps first, which arms the timer with no jump ever having happened, then presses
			// again fifteen ticks later. Between them they say whether the arming really is the release or
			// whether a plain fall is enough.
			//
			// Nothing before tick 40. The scenario ahead of these holds jump to its last tick, so the probe
			// clearing the input at the reset is itself a release and arms the timer - a first version
			// pressed at 5 and 20 and both games double jumped, off a window opened by the *previous*
			// scenario. The fall lasts about 82 ticks, so there is room to wait it out.
			case 254: name = "sp_dj_ff"_s; jump = (t >= 55); break;
			case 255: name = "sp_dj_ff_rel"_s; jump = (t >= 40 && t < 45) || (t >= 60); break;
			// The apex of the rise, swept one release tick at a time. Where the rise deceleration carries the
			// speed across zero the original takes a *partial* step, and what it lands on cannot be a function
			// of the speed and the rate alone: from -0.625, -0.500 and -0.375 the step is 0.625, but from
			// -0.250 it is 0.375 and from -0.125 it is 0.25. Every existing scenario samples that transition
			// only incidentally, so the values that appear are whatever the four release ticks happen to give.
			//
			// Thirty plain standing jumps, released after 1 to 30 ticks, sample it on purpose: the launch is
			// the same -10 every time and the only thing that varies is how many ticks of the light rise
			// gravity come before the heavy one, which walks the pre-apex speed through every value it can
			// take. Nothing else is pressed, so there is no horizontal speed and no launch boost to confound
			// it, and the arc fits the default window.
			case 256: case 257: case 258: case 259: case 260:
			case 261: case 262: case 263: case 264: case 265:
			case 266: case 267: case 268: case 269: case 270:
			case 271: case 272: case 273: case 274: case 275:
			case 276: case 277: case 278: case 279: case 280:
			case 281: case 282: case 283: case 284: case 285: {
				std::int32_t hold = scenario - 255;
				apexName[4] = (char)('0' + hold / 10);
				apexName[5] = (char)('0' + hold % 10);
				name = apexName;
				jump = (t >= J && t < J + hold);
				break;
			}
			// The pinball section, tiles 57-80 of row 4-19: bumpers at (62,15), (69,15) and (66,9) over a
			// floor at row 20, and a right-facing paddle at (80,18) mounted against the wall at x=81.
			//
			// Every one of these is aimed at a question the engine's implementation currently answers by
			// guesswork - the bumper's trigger radius is 16 px where the section is built for something
			// nearer two tiles, and the paddle fires the moment the player is inside it rather than waiting
			// to be activated. The scenarios are therefore approach-shaped: the same bumper met from below,
			// from above and from either side, with and without a key held into it.
			case 286: name = "pb_bump_tap"_s; jump = (t >= J && t < J + 5); break;
			case 287: name = "pb_bump_hold"_s; jump = (t >= J); break;
			case 288: name = "pb_bump_hold_r"_s; jump = (t >= J); right = true; break;
			case 289: name = "pb_bump_fall"_s; break;
			case 290: name = "pb_bump_fall_j"_s; jump = true; break;
			// Arriving at the bumper's own height with dash speed on, from either side. `_in` holds the
			// direction that carries the player *into* it, which is what says whether the launch is the
			// player's momentum reflected or a fixed impulse the input cannot alter.
			case 291: name = "pb_bump_right"_s; break;
			case 292: name = "pb_bump_right_in"_s; left = true; break;
			case 293: name = "pb_bump_left"_s; break;
			case 294: name = "pb_bump_left_in"_s; right = true; break;
			// The trigger radius, swept by dropping past the bumper at a measured horizontal offset. A miss
			// lands on the floor below; a hit does not.
			//
			// Swept **leftwards**, because the bumper at (66,9) sits above and to the right of the one being
			// measured: a first version stepped right and the 80 and 112 px drops hit that one on the way
			// down instead, at an offset and a height nobody chose. Nothing is above tiles 55-61.
			case 295: name = "pb_bump_off16"_s; break;
			case 296: name = "pb_bump_off48"_s; break;
			case 297: name = "pb_bump_off80"_s; break;
			case 298: name = "pb_bump_off112"_s; break;
			// Dropped into the top of the chamber with nothing held - the "press nothing for fifteen seconds"
			// guard that has caught every mechanic bug in this harness so far, here let loose on three
			// bumpers that can throw the player at each other.
			case 299: name = "pb_bump_chain"_s; break;
			// The paddle. `pb_pad_fall` is the decisive one: dropped on with **nothing pressed at all**, so
			// whether it launches on its own is read straight off the trace.
			case 300: name = "pb_pad_fall"_s; break;
			case 301: name = "pb_pad_tap"_s; jump = (t >= 60 && t < 64); break;
			case 302: name = "pb_pad_hold"_s; jump = (t >= 60); break;
			case 303: name = "pb_pad_h10"_s; jump = (t >= 60 && t < 70); break;
			case 304: name = "pb_pad_h20"_s; jump = (t >= 60 && t < 80); break;
			// The same activation at three distances from the mounted end, which is what the launch is said
			// to scale with. The paddle's tile is its right-hand end, so these step away from it leftwards.
			case 305: name = "pb_pad_x1"_s; jump = (t >= 60); break;
			case 306: name = "pb_pad_x2"_s; jump = (t >= 60); break;
			case 307: name = "pb_pad_x3"_s; jump = (t >= 60); break;
			case 308: name = "pb_pad_below"_s; jump = (t >= J); break;
			case 309: name = "pb_pad_side"_s; break;
			// Launched, then jump worked repeatedly - Jazz should get a copter out of it and Spaz a double
			// jump, and neither should be swallowed by whatever state the paddle leaves the player in
			case 310: name = "pb_pad_copter"_s; jump = (t >= 60 && t < 70) || (t >= 110 && ((t - 110) % 6) < 2); break;
			case 311: name = "pb_pad_dj"_s; jump = (t >= 60 && t < 70) || (t >= 110 && ((t - 110) % 6) < 2); break;
			// Two more steps of the right paddle's own length, since 64 px out already misses it entirely -
			// whatever the launch scales with, the body it scales across is barely two tiles
			case 312: name = "pb_pad_xm16"_s; jump = (t >= 60); break;
			case 313: name = "pb_pad_xm48"_s; jump = (t >= 60); break;
			// The **left** paddle at (83,18), mounted against the same wall from the other side and extending
			// right, so its distance-from-the-mount axis runs the opposite way. Measured rather than assumed
			// to be the right one mirrored: the engine implements both from one sprite and one code path with
			// a `facingLeft` flag, and that is exactly where a sign error would live.
			case 314: name = "pb_lpad_fall"_s; break;
			case 315: name = "pb_lpad_tap"_s; jump = (t >= 60 && t < 64); break;
			case 316: name = "pb_lpad_hold"_s; jump = (t >= 60); break;
			case 317: name = "pb_lpad_x1"_s; jump = (t >= 60); break;
			case 318: name = "pb_lpad_x2"_s; jump = (t >= 60); break;
			case 319: name = "pb_lpad_below"_s; jump = (t >= J); break;
			// The launch against distance from the mounted end, finely enough to fit rather than guess. Three
			// points measured 4, 15 and 31 at 16, 32 and 48 px out, which is a steep ramp no straight line or
			// square fits - and three points is how the horizontal pole's launch was "fitted exactly" to a
			// curve that turned out to be wrong in two ways. Eight px apart across both paddles instead.
			case 320: name = "pb_pad_d08"_s; jump = (t >= 60); break;
			case 321: name = "pb_pad_d24"_s; jump = (t >= 60); break;
			case 322: name = "pb_pad_d40"_s; jump = (t >= 60); break;
			case 323: name = "pb_pad_d56"_s; jump = (t >= 60); break;
			case 324: name = "pb_lpad_d08"_s; jump = (t >= 60); break;
			case 325: name = "pb_lpad_d16"_s; jump = (t >= 60); break;
			case 326: name = "pb_lpad_d24"_s; jump = (t >= 60); break;
			case 327: name = "pb_lpad_d40"_s; jump = (t >= 60); break;
			case 328: name = "pb_lpad_d48"_s; jump = (t >= 60); break;
			case 329: name = "pb_lpad_d56"_s; jump = (t >= 60); break;
			// Does a paddle block a player coming up from underneath, or is it one-way? `pb_pad_below` was
			// meant to answer that and does not: the chamber floor is 48 px under the paddle and the player
			// is taller than that, so one placed below it lands *inside* and is pushed out on top - its
			// trace is another stand at 48 px out. These start just above the floor with upward momentum
			// instead, which is the only way to meet the underside at all in this geometry. Nothing is
			// pressed, so a rise that stops dead at the paddle is a block and one that carries on is not.
			case 330: name = "pb_pad_thru"_s; break;
			case 331: name = "pb_lpad_thru"_s; break;
			// Is a paddle launch shortened by letting the jump key go, the way an ordinary jump is? Same
			// spot as `pb_pad_x1` - 48 px out, a -31 launch, big enough to tell apart - but the key is
			// released four ticks after it fires instead of being held. If the first apex matches
			// `pb_pad_x1`'s the launch is immune and the rise gravity must not be picked by `_jumpReleased`;
			// if it is lower, the original shortens it like a jump. Nothing re-fires either way, because a
			// released key cannot re-trigger the paddle.
			case 332: name = "pb_pad_rel"_s; jump = (t >= 60 && t < 64); break;
			// And does it *stay* shortened? Launch, let go, then hold the key again from tick 100 so the
			// second launch fires on landing. If that one is tall again the original clears whatever makes a
			// released rise heavy when the paddle fires, and this engine must too - it clears it only on an
			// ordinary jump, which is why a player who lets go once gets short launches until they touch the
			// floor again.
			case 333: name = "pb_pad_rel2"_s; jump = (t >= 60 && t < 64) || (t >= 100); break;
			// Guarded on the level, and the guard is what keeps it out of a normal sweep: on `_pt` this falls
			// through to `return false`, the probe reports finished after 333, and the committed trace is
			// unaffected. To run it, load that level and raise `FirstScenario` to 334 - see `Tests/README.md`.
			// It has to stay **last**: a sweep of the test level ends here, so anything after it never runs.
			case 334:
				if (!_levelHandler->GetLevelName().contains("diam3"_s)) {
					return false;
				}
				name = "dm_chain"_s;
				break;

			default: return false;
		}

		// The probe always drives player 0, but the index is still checked rather than trusted - this writes
		// straight into a fixed-size array and a stray index would corrupt whatever follows it
		std::int32_t playerIndex = player->GetPlayerIndex();
		if (playerIndex < 0 || playerIndex >= ControlScheme::MaxSupportedPlayers) {
			return false;
		}

		auto& input = _levelHandler->_playerInputs[playerIndex];
		auto setAction = [&input](PlayerAction action, bool value) {
			std::uint64_t bit = (1ull << (std::int32_t)action);
			if (value) {
				input.PressedActions |= bit;
			} else {
				input.PressedActions &= ~bit;
			}
		};
		setAction(PlayerAction::Left, left);
		setAction(PlayerAction::Right, right);
		setAction(PlayerAction::Run, run);
		setAction(PlayerAction::Jump, jump);
		// Crouching and buttstomping are separate bindings here, and HandleLookupAndCrouch() picks between
		// them on whether the player is grounded - so driving only Down means an airborne stomp never fires.
		// The original has a single key for both, so the probe presses both.
		setAction(PlayerAction::Down, down);
		setAction(PlayerAction::Buttstomp, down);
		setAction(PlayerAction::Fire, fire);
		// The directions are also read as an axis, which is what the movement code actually uses, so both
		// representations have to be kept in step
		input.RequiredMovement.X = (right ? 1.0f : (left ? -1.0f : 0.0f));
		input.RequiredMovement.Y = (down ? 1.0f : 0.0f);

		// `_elapsedFrames` accumulates timeMult, which is real-time derived, so this is genuine elapsed
		// milliseconds whatever the frame rate - and comparing the two games has to happen on real time,
		// not on row numbers, because their tick rates differ
		float ms = (_levelHandler->_elapsedFrames - _startFrames) * (1000.0f / FrameTimer::FramesPerSecond);

		// The pushable's own position is logged too: pushing and the buttstomp drift both move the player
		// without touching its speed in the original, so only the positions show what actually happened
		float objX = -1.0f, objY = -1.0f;
		// The tube and pinball scenarios carry the player's own **animation** instead, because what is being
		// asked of them is partly a pose: the paddle is supposed to hold the player in the same curled-up
		// ball a sucker tube does, and no position column can show whether it does. Logging the tube rides
		// alongside gives the comparison its own reference - whatever id the original shows during `tb_right`
		// is the ball, and the paddle either matches it or does not.
		if ((scenario >= 198 && scenario <= 209) || scenario == 217 || scenario == 218 ||
			(scenario >= 286 && scenario <= 329)) {
			// `objx` on this side but `objy` on the original's - the two probes order those two columns
			// differently, which cost a round of "the pose is not being set" that was really the wrong column
			// being read. Check the header before comparing them.
			objX = (float)(std::uint32_t)player->_currentAnimation->State;
			objY = (float)(player->_currentTransition != nullptr ? (std::uint32_t)player->_currentTransition->State : 0u);
		} else
		// The RF scenarios have no pushable, so the two object columns carry the remaining ammo and the
		// current weapon instead - without which a trace that shows no blast cannot be told from one where
		// the shot was never fired in the first place
		if ((scenario >= 94 && scenario <= 97) || (scenario >= 103 && scenario <= 107) || (scenario >= 129 && scenario <= 139) || scenario >= 142) {
			// Whichever weapon the scenario selected, so a trace that shows no knockback can be told apart
			// from one where the shot was never fired - the difference between a measurement and nothing
			objX = (float)player->GetWeaponAmmo()[(std::int32_t)player->_currentWeapon];
			objY = (float)(std::int32_t)player->_currentWeapon;
		} else if (scenario == 140 || scenario == 141) {
			// The enemy's own live position, so a stomp that hit nothing is distinguishable from an enemy
			// that was never there - and so it is visible whether the stomp killed it
			for (auto& actor : _levelHandler->_actors) {
				if (auto* turtle = runtime_cast<Actors::Enemies::TurtleTube>(actor.get())) {
					objX = turtle->GetPos().X;
					objY = turtle->GetPos().Y;
					break;
				}
			}
		}
		if (objX < 0.0f) {
			for (auto& actor : _levelHandler->_actors) {
				if (auto* box = runtime_cast<Actors::Solid::PushableBox>(actor.get())) {
					objX = box->GetPos().X;
					objY = box->GetPos().Y;
					break;
				}
			}
		}

		// `ctrl`/`trans`/`crouch` tell whether a move that did not happen was refused because the player was
		// not in control yet, was still animating out of the previous one, or was simply not crouching any
		// more. `jrel`/`spr` are the two flags that pick the rise gravity: which one an ascent decays under
		// is invisible in the position until several ticks later, and a stale `jrel` once cut a blue spring
		// from 597 px to 256 - so the state that chose it is logged next to the trajectory it produced.
		LOGI("[probe] {},{},{:.3f},{:.3f},{:.4f},{:.4f},{},{},{},{},{},{},{:.1f},{:.1f},{:.1f},{},{},{},{},{}",
			name, t, player->_pos.X, player->_pos.Y, player->_speed.X, player->_speed.Y,
			right ? 1 : 0, left ? 1 : 0, run ? 1 : 0, jump ? 1 : 0, down ? 1 : 0,
			(std::int32_t)player->GetSpecialMove(), ms, objX, objY,
			player->_controllable ? 1 : 0, player->_currentTransition != nullptr ? 1 : 0,
			(player->_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch ? 1 : 0,
			player->_jumpReleased ? 1 : 0, player->_isSpring ? 1 : 0);
		return true;
	}

	void PhysicsProbe::SetupCharacter(Actors::Player* player, std::int32_t s)
	{
		// Done at the reset, before the settle, so a morph has time to finish - morphing at the start of the
		// run leaves the player mid-transition and its very first jump comes out wrong
		PlayerType wanted = PlayerType::Jazz;
		if (s == 37 || s == 38 || s == 39 || s == 42 || s == 60 || s == 61 || s == 62 || s == 63 || s == 64 || s == 80 || s == 81 || (s >= 82 && s <= 87) ||
			(s >= 149 && s <= 153) || s == 156 || s == 157 || (s >= 158 && s <= 161) || (s >= 219 && s <= 255) ||
			s == 311) {
			// The `cl_dj*` ceiling and `sp_dj_*` window scenarios need the double jump, so they need Spaz
			wanted = PlayerType::Spaz;
		} else if (s == 40 || s == 43 || s == 58 || s == 59 || s == 65 || s == 88) {
			wanted = PlayerType::Lori;
		}
		if (player->GetPlayerType() != wanted) {
			player->MorphTo(wanted);
		}

		// The pushable has to be approached with a couple of tiles of clearance above the floor: dropped in
		// level with it the player ends up embedded and cannot move at all. The settle then lowers it on.
		if ((s == 54 || s == 55) && _pushable.X > 0.0f) {
			player->MoveInstantly(Vector2f(_pushable.X - 96.0f, _groundY - 64.0f), Actors::MoveType::Absolute | Actors::MoveType::Force);
		} else if (s == 73 || s == 74 || s == 75) {
			// Long slope down, tiles (193,41) to (210,57) - started a few tiles short so the player is up to speed
			player->MoveInstantly(Vector2f(190 * 32 + 16, 41 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
		} else if (s == 76 || s == 77) {
			// Slope up, tiles (222,57) to (228,52)
			player->MoveInstantly(Vector2f(219 * 32 + 16, 57 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
		} else if (s >= 144 && s <= 157) {
			// The ceiling staircase. Row 16 is the floor (tiles 35..53) and the bays above it are 2 tiles
			// wide: 43-44 clears 2 tiles, 45-46 three, 47-48 four, 49-50 five, 51-52 six. Parked in the
			// middle of a bay so a standing jump cannot drift into the next one, or on the open run-up west
			// of the section for the moving ones. Placed a tile above the floor so the settle drops the
			// player onto it. Set ScanFloorRow to 15 to have the probe print the whole layout.
			constexpr float BayCentres[] = { 1408.0f, 1472.0f, 1536.0f, 1600.0f, 1664.0f };
			float x;
			if (s <= 148) { x = BayCentres[s - 144]; }
			else if (s <= 153) { x = BayCentres[s - 149]; }
			else { x = 53 * 32.0f + 16.0f; }	// east end of the floor, just clear of the highest bay
			player->MoveInstantly(Vector2f(x, 15 * 32.0f), Actors::MoveType::Absolute | Actors::MoveType::Force);
		} else if (s == 142 || s == 143) {
			// Partway onto the same two slopes, standing still, so the blast is taken on the incline itself
			// rather than on the flat before it
			if (s == 142) {
				player->MoveInstantly(Vector2f(225 * 32 + 16, 54 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
			} else {
				player->MoveInstantly(Vector2f(200 * 32 + 16, 47 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
			}
			player->AddAmmo(WeaponType::RF, 50);
			player->SetCurrentWeapon(WeaponType::RF, Actors::Player::SetCurrentWeaponReason::User);
		} else if (s >= 89 && s <= 107) {
			// The upper floor, tiles (192,28) to (233,28), with a wall at its right end whose face is at
			// tile 234, and a platform at (221,22) to (225,22). A couple of tiles up so the settle drops the
			// player onto the floor rather than into it.
			if (s >= 103) {
				// Parked a set distance short of the wall face, which is the distance the blast has to cross
				constexpr float WallFace = 234 * 32;
				constexpr float Gaps[] = { 12.0f, 24.0f, 36.0f, 48.0f, 64.0f };
				player->MoveInstantly(Vector2f(WallFace - Gaps[s - 103], 26 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
			} else {
				// Started at the left end, with room to reach top speed before the wall
				player->MoveInstantly(Vector2f(195 * 32 + 16, 26 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
			}
			// RF is the only weapon whose own blast moves the player, so it has to be both stocked and
			// selected - the probe never touches the weapon-cycling keys
			if ((s >= 94 && s <= 97) || (s >= 103 && s <= 107)) {
				player->AddAmmo(WeaponType::RF, 50);
				player->SetCurrentWeapon(WeaponType::RF, Actors::Player::SetCurrentWeaponReason::User);
			}
		} else if (s >= 129 && s <= 139) {
			// The same upper floor and wall as the `wb_*` scenarios above, but parked at whole tiles rather
			// than the fractions the RF reach was pinned down with, and with one of three weapons stocked
			// and selected - the probe never touches the weapon-cycling keys, so it has to be selected here
			constexpr float WallFace = 234 * 32;
			constexpr float TileGaps[] = { 32.0f, 64.0f, 96.0f, 128.0f, 160.0f, 192.0f };
			WeaponType weapon;
			std::int32_t step;
			if (s <= 134) { weapon = WeaponType::RF; step = s - 129; }
			else if (s <= 137) { weapon = WeaponType::Seeker; step = s - 135; }
			else { weapon = WeaponType::TNT; step = s - 138; }
			player->MoveInstantly(Vector2f(WallFace - TileGaps[step], 26 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
			player->AddAmmo(weapon, 50);
			player->SetCurrentWeapon(weapon, Actors::Player::SetCurrentWeaponReason::User);
		} else if (s >= 172 && s <= 178) {
			// The block at (32,50), west of the float-up ladder, which is where jumping right catches the
			// first of them. Placed half a tile above it so the settle drops the player on, the same way the
			// ceiling scenarios do - and at the reset rather than at the run, because these start *grounded*.
			player->MoveInstantly(Vector2f(32 * 32 + 16, 49 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
		} else if (s >= 115 && s <= 126) {
			// The gap floor on row 11. Parked 24 tiles short of the gap this scenario is named for, which is
			// enough for either speed to reach its cap, and two tiles up so the settle drops the player onto
			// the floor rather than into it. Set ScanFloorRow to 11 to have the probe print the layout.
			constexpr std::int32_t StartTiles[] = { 156, 156, 185, 185, 214, 214, 245, 245, 276, 276, 312, 312 };
			player->MoveInstantly(Vector2f(StartTiles[s - 115] * 32.0f + 16.0f, 9 * 32), Actors::MoveType::Absolute | Actors::MoveType::Force);
		}
	}

	void PhysicsProbe::ClearProps()
	{
		// Takes out whatever the previous scenario put in the world. Called at the RESET rather than just
		// before the run: a spring left under the player's feet fires during the settle and launches them,
		// so the run then starts mid-flight - which is what made the green and blue spring scenarios look
		// like they began 60 px up with a third of their speed already spent. A pole left in the event map
		// does the same by being grabbable while the player is lining up.
		if (_spawned != nullptr) {
			_spawned->DecreaseHealth(INT32_MAX);
			_spawned = nullptr;
		}
		for (Vector2i tile : _tileEvents) {
			_levelHandler->_eventMap->StoreTileEvent(tile.X, tile.Y, EventType::Empty);
		}
		_tileEvents.clear();
	}
	void PhysicsProbe::SetupProps(Actors::Player* player, std::int32_t s)
	{
		// Whatever the previous scenario left in the world is already gone - ClearProps() takes it out at
		// the reset, before the settle

		std::int32_t tx = (std::int32_t)_groundX / 32;
		std::int32_t ty = (std::int32_t)_groundY / 32;

		// A chain scenario places several of these, so what was written has to be remembered as a list
		auto placeTileEvent = [&](std::int32_t x, std::int32_t y, EventType ev) {
			_levelHandler->_eventMap->StoreTileEvent(x, y, ev);
			_tileEvents.push_back(Vector2i(x, y));
		};

		// The wind and belt events carry their strength in the event parameters, so those scenarios cannot
		// use the plain helper. The six slots of an `AreaHForce` are, in order: belt left, belt right,
		// accelerating belt left, accelerating belt right, wind left, wind right - see EventConverter.cpp,
		// which folds four JJ2 events onto this one type.
		auto placeAreaHForce = [&](std::int32_t x, std::int32_t y, std::uint8_t beltL, std::uint8_t beltR,
			std::uint8_t accL, std::uint8_t accR, std::uint8_t windL, std::uint8_t windR) {
			std::uint8_t params[Events::EventSpawner::SpawnParamsSize] {};
			params[0] = beltL; params[1] = beltR;
			params[2] = accL; params[3] = accR;
			params[4] = windL; params[5] = windR;
			_levelHandler->_eventMap->StoreTileEvent(x, y, EventType::AreaHForce, Actors::ActorState::None, params);
			_tileEvents.push_back(Vector2i(x, y));
		};
		// Wind fills the space the player is standing in; a belt or a slide tile is read from the tile
		// *below* them, so those go one row down. Both are laid a few tiles either side so the player does
		// not simply walk out of the field being measured.
		auto fillWind = [&](std::uint8_t windL, std::uint8_t windR) {
			for (std::int32_t dx = -2; dx <= 2; dx++) {
				for (std::int32_t dy = -1; dy <= 0; dy++) { placeAreaHForce(tx + dx, ty + dy, 0, 0, 0, 0, windL, windR); }
			}
		};
		// The span has to match FillFloor() in the original's probe exactly. It did not at first - 24 tiles
		// here against 40 there - and a belt whose per-tick rate was already exact still showed 798 px of
		// travel against 1296, because the two players were riding fields of different lengths. The rate is
		// the physics; the total is the rate times however much belt there is.
		auto fillFloor = [&](std::uint8_t beltL, std::uint8_t beltR, std::uint8_t accL, std::uint8_t accR) {
			for (std::int32_t dx = -6; dx <= 40; dx++) { placeAreaHForce(tx + dx, ty + 1, beltL, beltR, accL, accR, 0, 0); }
		};

		// A sucker tube. The native parameter order is not the JJ2 one - the converter moves Wait Time from
		// last to third - so it is spelled out here rather than passed through: X speed, Y speed, wait time,
		// trigger sample, become-noclip, noclip-only.
		auto placeTube = [&](std::int32_t x, std::int32_t y, std::int32_t xSpeed, std::int32_t ySpeed, std::uint8_t wait) {
			std::uint8_t params[Events::EventSpawner::SpawnParamsSize] {};
			params[0] = (std::uint8_t)(std::int8_t)xSpeed;
			params[1] = (std::uint8_t)(std::int8_t)ySpeed;
			params[2] = wait;
			_levelHandler->_eventMap->StoreTileEvent(x, y, EventType::ModifierTube, Actors::ActorState::None, params);
			_tileEvents.push_back(Vector2i(x, y));
		};
		// A spring with its parameters set, rather than the bare colour the other spring scenarios use.
		// `orientation` 0 fires up, 2 down, 1 right and 5 left; `flags` are gravitation, frozen, keep-X and
		// keep-Y. The offset is given rather than derived, because it has to match where the original's
		// probe puts the same spring - and there it goes six tiles aside at the *reset*, so that the settle
		// has time to activate the event and spawn it. See PlaceSpring() in `_pt.j2as`.
		auto spawnSpringP = [&](std::uint8_t orientation, std::uint8_t flags, std::uint8_t delay, float dx, float dy) {
			std::uint8_t params[Events::EventSpawner::SpawnParamsSize] {};
			params[0] = 2;	// Blue, the strongest, so the launch is furthest above the noise
			params[1] = orientation;
			params[2] = flags;
			params[3] = delay;
			_spawned = _levelHandler->_eventSpawner.SpawnEvent(EventType::Spring, params, Actors::ActorState::None,
				Vector3i((std::int32_t)(_groundX + dx), (std::int32_t)(_groundY + dy), ILevelHandler::MainPlaneZ - 10));
			if (_spawned != nullptr) {
				_levelHandler->AddActor(_spawned);
			}
		};

		// Params are [type, orientation, flags, delay]. The orientation enum is private to Spring, so the two
		// values are spelled out: 0 = Bottom (the one that fires upwards), 1 = Right.
		auto spawnSpring = [&](std::uint8_t type, bool horizontal) {
			std::uint8_t params[Events::EventSpawner::SpawnParamsSize] {};
			params[0] = type;
			params[1] = (horizontal ? 1 : 0);
			float x = _groundX + (horizontal ? 96.0f : 0.0f);
			_spawned = _levelHandler->_eventSpawner.SpawnEvent(EventType::Spring, params, Actors::ActorState::None,
				Vector3i((std::int32_t)x, (std::int32_t)_groundY, ILevelHandler::MainPlaneZ - 10));
			if (_spawned != nullptr) {
				_levelHandler->AddActor(_spawned);
			}
		};

		// Drops the player into the pinball chamber at an absolute spot, optionally already moving. Several
		// of those scenarios have to arrive at a bumper's own height from the side, and the chamber's floor
		// is five tiles below it - there is no way to get there by walking.
		auto placePinball = [&](float x, float y, float sx, float sy) {
			player->MoveInstantly(Vector2f(x, y), Actors::MoveType::Absolute | Actors::MoveType::Force);
			player->_speed = Vector2f(sx, sy);
		};

		switch (s) {
			case 45: spawnSpring(0, false); break;
			case 46: spawnSpring(1, false); break;
			case 47: spawnSpring(2, false); break;
			case 48: spawnSpring(1, false); break;	// The frozen variant differs only in its start state
			case 49: spawnSpring(0, true); break;
			case 50: spawnSpring(1, true); break;
			case 51: spawnSpring(2, true); break;
			case 52: placeTileEvent(tx, ty - 3, EventType::ModifierVPole); break;
			case 53: placeTileEvent(tx + 4, ty, EventType::ModifierHPole); break;
			case 67:
			case 68: placeTileEvent(tx, ty - 3, EventType::ModifierVine); break;
			// Poles chained five tiles apart, matching the original probe's spacing exactly
			case 108: for (std::int32_t k = 0; k < 2; k++) { placeTileEvent(tx, ty - 3 - k * 5, EventType::ModifierVPole); } break;
			case 109: for (std::int32_t k = 0; k < 3; k++) { placeTileEvent(tx, ty - 3 - k * 5, EventType::ModifierVPole); } break;
			case 110: for (std::int32_t k = 0; k < 5; k++) { placeTileEvent(tx, ty - 3 - k * 5, EventType::ModifierVPole); } break;
			case 111: for (std::int32_t k = 0; k < 3; k++) { placeTileEvent(tx + 4 + k * 5, ty, EventType::ModifierHPole); } break;
			// Entering a pole off a spring rather than off a jump or a run, which is what carries the most
			// entry speed into it - a blue spring assigns 32
			case 112: spawnSpring(2, false); placeTileEvent(tx, ty - 6, EventType::ModifierVPole); break;
			case 113: spawnSpring(2, true); placeTileEvent(tx + 8, ty, EventType::ModifierHPole); break;
			case 114: placeTileEvent(tx + 4, ty, EventType::ModifierHPole); break;
			// Two adjacent poles, side by side and stacked, for each type
			case 162:
				placeTileEvent(tx, ty - 3, EventType::ModifierVPole);
				placeTileEvent(tx + 1, ty - 3, EventType::ModifierVPole);
				break;
			case 163:
				placeTileEvent(tx, ty - 3, EventType::ModifierVPole);
				placeTileEvent(tx, ty - 4, EventType::ModifierVPole);
				break;
			case 164:
				placeTileEvent(tx + 4, ty, EventType::ModifierHPole);
				placeTileEvent(tx + 5, ty, EventType::ModifierHPole);
				break;
			case 165:
				placeTileEvent(tx + 4, ty, EventType::ModifierHPole);
				placeTileEvent(tx + 4, ty - 1, EventType::ModifierHPole);
				break;
			// Overhead, where `ob_vpole` puts its pole, so a straight-up jump reaches it with no x speed
			case 168:
			case 169:
			case 170: placeTileEvent(tx, ty - 3, EventType::ModifierHPole); break;
			case 171: placeTileEvent(tx + 2, ty, EventType::ModifierHPole); break;
			// A solid column of float-up areas, ten tiles tall and including the player's own tile, so a
			// grounded player is inside one and a rising player never leaves it. No gaps and no horizontal
			// motion, which is what the level's diagonal ladder cannot offer.
			case 179:
			case 180:
			case 181:
				for (std::int32_t k = 0; k <= 9; k++) { placeTileEvent(tx, ty - k, EventType::AreaFloatUp); }
				break;
			// Wind, with the strengths the converter produces from a JJ2 parameter of 8. Both `_r` and `_l`
			// come out in the *right* slot, which is the conversion under test - see ApplyInput().
			case 182: fillWind(0, 8); break;
			case 183: fillWind(0, 8); break;
			case 184: fillWind(8, 0); break;
			case 185: fillWind(0, 8); break;
			case 186: fillWind(0, 8); break;
			// Belts, at the strength a parameter of 0 converts to - 2 for a plain belt and 4 for an
			// accelerating one, which are the measured defaults. These have to track EventConverter.cpp:
			// the original's probe places a parameterless JJ2 event and lets its own game substitute, while
			// this side writes the *converted* parameters directly, so a stale number here would compare
			// two different belts and look like a physics difference.
			case 187: fillFloor(0, 2, 0, 0); break;
			case 188: fillFloor(2, 0, 0, 0); break;
			case 189: fillFloor(0, 0, 0, 4); break;
			case 190: fillFloor(0, 0, 4, 0); break;
			case 191: fillFloor(0, 2, 0, 0); break;
			// The slide tile, over a long enough run that the player cannot leave it while decelerating
			case 192:
			case 193:
				for (std::int32_t dx = -6; dx <= 40; dx++) { placeTileEvent(tx + dx, ty + 1, EventType::ModifierSlide); }
				break;
			// The second strength of each, for the fit
			case 194: fillFloor(0, 8, 0, 0); break;
			case 195: fillFloor(0, 0, 0, 8); break;
			case 196: fillWind(0, 4); break;
			// Slide tiles at each of the four Strength values its 2-bit field allows
			case 197:
			case 215:
			case 216: {
				std::uint8_t strength = (s == 197 ? 3 : (std::uint8_t)(s - 214));
				for (std::int32_t dx = -6; dx <= 40; dx++) {
					std::uint8_t params[Events::EventSpawner::SpawnParamsSize] {};
					params[0] = strength;
					_levelHandler->_eventMap->StoreTileEvent(tx + dx, ty + 1, EventType::ModifierSlide, Actors::ActorState::None, params);
					_tileEvents.push_back(Vector2i(tx + dx, ty + 1));
				}
				break;
			}
			// One tube on the player's own tile, so it triggers from rest with nothing held
			case 198: placeTube(tx, ty, 8, 0, 0); break;
			case 199: placeTube(tx, ty, -8, 0, 0); break;
			case 200: placeTube(tx, ty, 0, -8, 0); break;
			case 201: placeTube(tx, ty, 0, 8, 0); break;
			case 202: placeTube(tx, ty, 6, -6, 0); break;
			case 203: placeTube(tx, ty, 20, 0, 0); break;
			case 204: placeTube(tx, ty, 8, 0, 3); break;
			// Nine tubes in a row with 1..5 empty tiles between them, all pulling right
			case 205:
			case 206:
			case 207:
			case 208:
			case 209: {
				std::int32_t spacing = (s - 205) + 2;
				for (std::int32_t k = 0; k < 9; k++) { placeTube(tx + k * spacing, ty, 8, 0, 0); }
				break;
			}
			// The spring parameters. `orientation` 2 is a vertical spring firing *down*, 5 a horizontal one
			// reversed; the flag bits are gravitation, frozen, keep-X and keep-Y in that order, and the
			// fourth parameter is the delay - see GetSpringConverter() in EventConverter.cpp.
			// Fires downwards, three tiles overhead: a standing jump reaches ~132 px, so five tiles was out of
			// below, so this scenario jumps into it
			case 210:
				spawnSpringP(2, 0x00, 0, 1280.0f, -96.0f);
				player->MoveInstantly(Vector2f(_groundX + 1280.0f, _groundY), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			// Keep X Speed needs entry speed, so the spring goes six tiles beyond the player, who runs into it
			case 211:
				spawnSpringP(0, 0x01 | 0x04, 0, 1472.0f, 0.0f);
				player->MoveInstantly(Vector2f(_groundX + 1280.0f, _groundY), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			case 212:
				// Keep Y Speed only shows on a spring *hit while falling*, so drop onto it from well above -
				// and onto the far one, never the player's own spot: a tile-spawned spring that appears
				// *inside* the player never fires, which is how this one measured nothing twice over
				spawnSpringP(0, 0x01 | 0x08, 0, 1472.0f, 0.0f);
				player->MoveInstantly(Vector2f(_groundX + 1472.0f, _groundY - 420.0f), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			case 213:
				// Delay: run into rather than stood on, for the same reason as case 212 above
				spawnSpringP(0, 0x01, 8, 1472.0f, 0.0f);
				player->MoveInstantly(Vector2f(_groundX + 1280.0f, _groundY), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			// Reversed horizontal, six tiles beyond: run into it and be thrown back the way you came
			case 214:
				spawnSpringP(5, 0x00, 0, 1472.0f, 0.0f);
				player->MoveInstantly(Vector2f(_groundX + 1280.0f, _groundY), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			// Does the tube hold for a fixed time, or for as long as the player is still inside one? A single
			// tile cannot tell the two apart - the player leaves it in four ticks either way. A *row* of them
			// can: on a timer the hold still lapses after ~19 ticks, and if it is the tile the player is
			// carried the whole length. The column does the same for the vertical case, which let go after 3.
			case 217: for (std::int32_t k = 0; k < 30; k++) { placeTube(tx + k, ty, 8, 0, 0); } break;
			case 218: for (std::int32_t k = 0; k < 30; k++) { placeTube(tx, ty - k, 0, -8, 0); } break;
			// Both drop onto the same spring from the same height, so the two runs differ in nothing but the
			// jump tap on the way down. Dropped in rather than stood on it because a spring under the
			// player's feet fires on the tick it appears, before any input has been applied.
			case 127:
			case 128:
				spawnSpring(2, false);
				player->MoveInstantly(Vector2f(_groundX, _groundY - 420.0f), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			// Dropped in above the level's own tube turtle, from two different heights. Placed at the run
			// start rather than the reset because a settle would undo being airborne, and directly above it
			// rather than beside it so the stomp lands on the turtle instead of the floor next to it.
			case 140:
			case 141:
				if (_turtle.X > 0.0f) {
					player->MoveInstantly(Vector2f(_turtle.X, _turtle.Y - (s == 140 ? 420.0f : 200.0f)), Actors::MoveType::Absolute | Actors::MoveType::Force);
					player->_speed = Vector2f::Zero;
				}
				break;
			case 72:
			case 80:
			case 81:
			// The two double-jump scenarios that must reach the air without ever jumping - same drop, no
			// spring under it, so the whole run is one long fall with nothing else happening in it
			case 254:
			case 255:
				// Dropped in from well above the floor, so a stomp has room to run its course. Placed here
				// rather than at the reset because the settle would undo being airborne.
				player->MoveInstantly(Vector2f(_groundX, _groundY - 420.0f), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			case 78:
			case 79:
				// The level's own vine spans tiles (241,50) to (254,50); come in just above it to grab on
				player->MoveInstantly(Vector2f(244 * 32 + 16, 50 * 32 + 4), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			// Dropped into the level's own spring chain. Placed here rather than at the reset for the same
			// reason as the stomp scenarios: both start in mid-air, and the settle would undo that by waiting
			// for the player to land.
			case 166:
				// Top of the shaft at tile (34,16), which drops onto the horizontal blue spring at (34,24)
				player->MoveInstantly(Vector2f(34 * 32 + 16, 16 * 32 + 16), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			case 167:
				// Six tiles above the vertical blue spring at (54,24), in the shaft that leads up to the pole
				player->MoveInstantly(Vector2f(54 * 32 + 16, 18 * 32 + 16), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			case 334:
				// Diamondus 3's own chain: the top of the one-tile shaft at tile (1,36), which drops onto the
				// horizontal blue spring at (1,45)
				player->MoveInstantly(Vector2f(1 * 32 + 16, 36 * 32 + 16), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				break;
			// The pinball section. Every one of these starts the player somewhere in the chamber rather than
			// at the scenario origin, and several start them already moving - a bumper met at speed from the
			// side cannot be reached any other way, since the chamber's floor is five tiles below it.
			case 286: case 287: case 288:
				// On the floor directly under the bumper at (62,15), a jump's height below it
				placePinball(62 * 32 + 16, 600.0f, 0.0f, 0.0f);
				break;
			case 289: case 290: placePinball(62 * 32 + 16, 208.0f, 0.0f, 0.0f); break;
			// Arriving at the bumper's own height with a dash's worth of speed. Started a little above it:
			// 320 px of travel at that speed takes about seventeen frames, in which gravity pulls the player
			// down some 24 px, so entering level with the bumper would mean arriving below it.
			case 291: case 292: placePinball(62 * 32 + 16 + 320, 472.0f, -Actors::Player::LegacyDashSpeed, 0.0f); break;
			case 293: case 294: placePinball(62 * 32 + 16 - 320, 472.0f, Actors::Player::LegacyDashSpeed, 0.0f); break;
			// The radius sweep: dropped past the bumper at a measured horizontal offset, leftwards so the
			// bumper at (66,9) is not in the way
			case 295: placePinball(62 * 32 + 16 - 16, 208.0f, 0.0f, 0.0f); break;
			case 296: placePinball(62 * 32 + 16 - 48, 208.0f, 0.0f, 0.0f); break;
			case 297: placePinball(62 * 32 + 16 - 80, 208.0f, 0.0f, 0.0f); break;
			case 298: placePinball(62 * 32 + 16 - 112, 208.0f, 0.0f, 0.0f); break;
			// The top of the chamber, midway between the three bumpers
			case 299: placePinball(66 * 32 + 16, 160.0f, 0.0f, 0.0f); break;
			// Above the paddle at (80,18). Its tile is its mounted end, so the offsets step leftwards.
			case 300: case 301: case 302: case 303: case 304:
			case 310: case 311:
				placePinball(80 * 32 + 16, 400.0f, 0.0f, 0.0f);
				break;
			case 305: placePinball(80 * 32 + 16 - 32, 400.0f, 0.0f, 0.0f); break;
			case 306: placePinball(80 * 32 + 16 - 64, 400.0f, 0.0f, 0.0f); break;
			case 307: placePinball(80 * 32 + 16 - 96, 400.0f, 0.0f, 0.0f); break;
			// On the floor below the paddle, so the jump arrives from underneath
			case 308: placePinball(80 * 32 + 16 - 32, 600.0f, 0.0f, 0.0f); break;
			// And along the paddle's own row from the far side of the chamber
			case 309: placePinball(80 * 32 + 16 - 320, 570.0f, Actors::Player::LegacyDashSpeed, 0.0f); break;
			case 312: placePinball(80 * 32 + 16 - 16, 400.0f, 0.0f, 0.0f); break;
			case 313: placePinball(80 * 32 + 16 - 48, 400.0f, 0.0f, 0.0f); break;
			// The left paddle at (83,18). Its tile is its mounted end too, so its offsets step *rightwards*.
			case 314: case 315: case 316:
				placePinball(83 * 32 + 16, 400.0f, 0.0f, 0.0f);
				break;
			case 317: placePinball(83 * 32 + 16 + 32, 400.0f, 0.0f, 0.0f); break;
			case 318: placePinball(83 * 32 + 16 + 64, 400.0f, 0.0f, 0.0f); break;
			case 319: placePinball(83 * 32 + 16 + 32, 600.0f, 0.0f, 0.0f); break;
			// The fine offset sweep, stepping away from each paddle's mounted end 8 px at a time
			case 320: placePinball(80 * 32 + 16 - 8, 400.0f, 0.0f, 0.0f); break;
			case 321: placePinball(80 * 32 + 16 - 24, 400.0f, 0.0f, 0.0f); break;
			case 322: placePinball(80 * 32 + 16 - 40, 400.0f, 0.0f, 0.0f); break;
			case 323: placePinball(80 * 32 + 16 - 56, 400.0f, 0.0f, 0.0f); break;
			case 324: placePinball(83 * 32 + 16 + 8, 400.0f, 0.0f, 0.0f); break;
			case 325: placePinball(83 * 32 + 16 + 16, 400.0f, 0.0f, 0.0f); break;
			case 326: placePinball(83 * 32 + 16 + 24, 400.0f, 0.0f, 0.0f); break;
			case 327: placePinball(83 * 32 + 16 + 40, 400.0f, 0.0f, 0.0f); break;
			case 328: placePinball(83 * 32 + 16 + 48, 400.0f, 0.0f, 0.0f); break;
			case 329: placePinball(83 * 32 + 16 + 56, 400.0f, 0.0f, 0.0f); break;
			// Just above the chamber floor, thrown upward at the underside of each paddle. The speed is well
			// over the applied rise cap on purpose - the cap holds the actual travel to 8 px a tick, so the
			// player cannot tunnel straight through whatever is above them.
			case 330: placePinball(80 * 32 + 16 - 32, 624.0f, 0.0f, -20.0f); break;
			case 331: placePinball(83 * 32 + 16 + 32, 624.0f, 0.0f, -20.0f); break;
			// Same spot as `pb_pad_x1`, so the two apexes can be compared directly
			case 332: placePinball(80 * 32 + 16 - 32, 400.0f, 0.0f, 0.0f); break;
			case 333: placePinball(80 * 32 + 16 - 32, 400.0f, 0.0f, 0.0f); break;
		}
	}

	void PhysicsProbe::OnUpdate(float timeMult)
	{
		// Same shape as the original game's probe: wait for the player to land after the level starts and
		// remember where, then run every scenario from exactly that spot
		if (_scenario < FirstScenario) {
			_scenario = FirstScenario;
		}
		if (_state == StateDone || _levelHandler->_players.empty()) {
			return;
		}

		auto* player = _levelHandler->_players[0];
		// The counter runs at the original game's measured 70 Hz rather than this engine's 60, so a scenario
		// step scheduled for tick 45 happens 45/70 s in whatever this build's frame rate is set to - which is
		// the whole point of the comparison
		float tickAdvance = timeMult * (OriginalTickRate / FrameTimer::FramesPerSecond);

		// Standing still is detected by the position going quiet rather than by an exactly zero speed, which
		// never happens reliably
		// Standing still is not enough on its own: a player hanging on a pole is perfectly stable, so a pole
		// left over from the previous scenario passed this check on the very first tick and the next scenario
		// began several tiles up with a launch already under way. Being back at the recorded ground height is
		// what separates "landed" from "held", and the threshold has to be tight - a pole at the ground tile
		// centre sits only ~3.5 px above the resting height. The reset takes the pole out, so the player
		// falls and this waits for the landing; MaxWaitTicks covers a whole pole cycle if it has to.
		// PRIME is what discovers the ground height, so it cannot test against it - there the position going
		// quiet is all there is.
		bool quiet = (std::abs(player->_pos.Y - _lastY) < 0.05f &&
			(_state == StatePrime || std::abs(player->_pos.Y - _groundY) < 1.5f));
		_still = (quiet ? _still + 1 : 0);
		_lastY = player->_pos.Y;
		bool atRest = (_still >= 5);

		switch (_state) {
			case StatePrime: {
				_waited += tickAdvance;
				if (atRest || _waited >= MaxWaitTicks) {
					_groundX = player->_pos.X;
					_groundY = player->_pos.Y;

					// Find the objects the level itself contains. The event map can be read whether or not an
					// object has been instantiated, which the actor list cannot - it only holds one once the
					// player is already near it, which is too late to use for placing the player. Spawning
					// one instead does not work: a `jjAddObject`ed pushable never became solid in the
					// original, and a spawned enemy is inert there too - a buttstomp passes straight through
					// it - so anything that has to be interacted with has to be placed in the level.
					Vector2i eventMapSize = _levelHandler->_eventMap->GetSize();
					std::uint8_t* eventParams;
					for (std::int32_t y = 0; y < eventMapSize.Y; y++) {
						for (std::int32_t x = 0; x < eventMapSize.X; x++) {
							EventType ev = _levelHandler->_eventMap->GetEventByPosition(x, y, &eventParams);
							if (ev == EventType::PushableBox && _pushable.X == 0.0f) {
								_pushable = Vector2f(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
							} else if (ev == EventType::EnemyTurtleTube && _turtle.X == 0.0f) {
								_turtle = Vector2f(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
							}
						}
					}
					LOGI("[scan] pushable at {:.0f},{:.0f} turtle at {:.0f},{:.0f}",
						_pushable.X, _pushable.Y, _turtle.X, _turtle.Y);

					if (ScanFloorRow > 0) {
						// A band rather than one row: which row a floor's solid tiles occupy is not obvious from
						// the height the player rests at, and guessing it wrong reports nothing at all
						for (std::int32_t row = ScanFloorRow - ScanFloorRowSpan; row <= ScanFloorRow + ScanFloorRowSpan; row++) {
							ScanFloor(player, row);
						}
					}

					// Has to match Tools/ExtractTrace.ps1's engine header exactly - `jrel` and `spr` were added
					// to the rows without being added here, so the log's own header named 18 columns for rows
					// that carry 20 and anyone reading a raw log mis-attributed the last two
					LOGI("[probe] scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr");
					_startFrames = _levelHandler->_elapsedFrames;
					_state = StateReset;
				}
				return;
			}
			case StateReset: {
				// Anything the previous scenario left the player *attached* to has to go before the move,
				// or the pole logic snaps them straight back onto it and the next scenario runs from up
				// there with gravity off - which is what made a chain of poles hand its state to whatever
				// followed it, and looked exactly like a physics bug in the scenario after
				player->_isAttachedToPole = false;
				player->_lastPoleTime = 0.0f;
				// A pole is driven by a *chain of transition callbacks* - each stage schedules the next, and
				// the launch itself happens in the last one. Clearing the attachment does not cancel that
				// chain, so a scenario cut off part way up a pole handed the pending callback to whatever ran
				// next, which then launched at -24.79 nine ticks in, from a standstill, with no jump pressed.
				// ForceCancelTransition() rather than CancelTransition(): the latter *runs* the callback,
				// which is precisely the launch being suppressed here.
				player->ForceCancelTransition();
				player->_suspendType = SuspendType::None;
				player->_currentSpecialMove = Actors::Player::SpecialMoveType::None;
				player->_controllable = true;
				player->_controllableTimeout = 0.0f;
				player->SetState(Actors::ActorState::ApplyGravitation, true);
				// The rest of the non-Reforged state goes too. Several of these choose a *rate* rather than a
				// speed - `_jumpReleased` and `_isSpring` between them pick which rise gravity an ascent
				// decays under - so a stale one does not move the player anywhere by itself and only shows up
				// twenty ticks into the next scenario's arc, as a height that is quietly wrong. Reusing the
				// game's own reset keeps this list from drifting out of step with the fields it has to clear.
				player->ResetLegacyMovementState();
				// The props go with it, so neither a leftover pole nor a leftover spring can act on the
				// player during the settle. SetupProps() places the new scenario's own, once it is at rest.
				ClearProps();

				player->MoveInstantly(Vector2f(_groundX, _groundY), Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->_speed = Vector2f::Zero;
				player->_externalForce = Vector2f::Zero;
				player->_internalForceY = 0.0f;

				SetupCharacter(player, _scenario);

				_tick = 0.0f;
				_waited = 0.0f;
				_still = 0;
				_state = StateSettle;
				return;
			}
			case StateSettle: {
				_waited += tickAdvance;
				if (atRest || _waited >= MaxWaitTicks) {
					SetupProps(player, _scenario);
					_state = StateRun;
				}
				return;
			}
		}

		if (!ApplyInput(player, _scenario, (std::int32_t)_tick)) {
			// Also a finish, and it has to say so. This is not the rare path it looks like: the last scenario
			// is guarded on the level, so *every* sweep of the test level ends here rather than by running out
			// of `ScenarioCount`. Without the marker a capture has no end, and both the documented way to wait
			// for a run ("poll the log for `[probe] finished`") and ExtractTrace.ps1's detection of a second
			// run in one log have nothing to key on.
			LOGI("[probe] finished");
			_state = StateDone;
			return;
		}

		_tick += tickAdvance;
		if (_tick >= GetScenarioTicks(_scenario)) {
			_scenario++;
			if (_scenario >= ScenarioCount) {
				LOGI("[probe] finished");
				_state = StateDone;
			} else {
				_state = StateReset;
			}
		}
	}
}

#endif
