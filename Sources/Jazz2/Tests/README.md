# Movement trajectory probe

A two-sided measurement harness for the game's movement. It drives the player through a fixed matrix of
scenarios with scripted input and logs its position and speed every tick — **in this engine and in the
original Jazz Jackrabbit 2** — so the two can be compared row for row and the engine's movement can be
checked against the original rather than against somebody's memory of it.

This exists because four rounds of tuning non-Reforged movement by feel all failed. Every constant in
`Player.h` marked `Legacy*` came out of a run of this harness; `Docs/MovementAccuracyReference.dox` (the
*Non-Reforged movement accuracy* page) records what was measured, what 3.8.0 had instead, and where the two
still differ.

## Layout

| Path | What it is |
|---|---|
| `PhysicsProbe.h` / `.cpp` | The engine-side probe. Gated on `WITH_PHYSICS_PROBE`; `LevelHandler` owns one and calls it once per frame when `/physics-probe` is passed. |
| `Level/_pt.j2l` | The test level. Loads in **both** games — same geometry, so the same scenario measures the same thing on both sides. |
| `Level/_pt.j2as` | The original-game probe, in JJ2+ AngelScript. The counterpart of `PhysicsProbe.cpp`: same scenario names, same columns, same state machine. |
| `Level/MLLE-Include-1.8.asc` | Required by `_pt.j2as`; the level is MLLE-generated and the script's first lines pull this in. |
| `Level/Diam3.j2as` | A one-scenario probe for the original game's **own** Diamondus 3, for `dm_chain`. Standalone rather than a copy of `_pt.j2as`: that one opens with an MLLE include its level requires, and Diam3 is an original 1.20 level. Drop it next to `Diam3.j2l` — JJ2+ picks up `<level>.j2as` by itself, and the level file is not modified. |
| `Results/*.csv.gz` | Captured runs, committed so a later change can be checked against them without needing the original game to hand. Gzipped — about 2.2 MB rather than 15, and still exact. |
| `Tools/ExtractTrace.ps1` | Turns a raw capture (game log or `.asdat`) into a CSV, gzipped if the destination ends in `.gz`. |
| `Tools/TraceIO.ps1` | Shared reading and writing, so `.csv` and `.csv.gz` are interchangeable everywhere. |
| `Tools/CompareTraces.ps1` | Compares an engine CSV against an original CSV, scenario by scenario. With `-Baseline` it compares two engine CSVs instead — see *Did anything change at all?* in `Results/README.md`. |

## Building it

Off by default — it takes the first player over completely, so it is a measurement tool rather than a game
feature. Nothing of it is compiled with the option off, and `/physics-probe` is then not a switch at all.

- **CMake**: `-DWITH_PHYSICS_PROBE=ON`
- **`Sources/Jazz2.vcxproj`**: already defined in all three `Debug` configurations, so a local Debug build
  has it. `Release` does not.

## Running it

### This engine

```
Jazz2.exe /physics-probe /level _pt /log:file:"<path>\test.log" /max-fps:60
```

- `/level _pt` is needed — the probe does not load the level itself, and without it the game sits in the
  menu and logs nothing. (`/level` is a `DEATH_DEBUG`-only switch, which a Debug build has.)
- `/max-fps:` pins the tick rate being measured. Run 24, 60 and 144 to check frame-rate independence; the
  probe's own counter runs at the original's 70.021 Hz regardless, so every scenario reaches each of its
  steps at the same *real* time whatever this is set to.
- A full sweep takes about 20 minutes in a Debug build. The window is per scenario, so it does not shrink
  much with a higher frame rate.

### The original game

Put `_pt.j2l`, `_pt.j2as` and `MLLE-Include-1.8.asc` next to the original `Jazz2.exe` (in its data
directory) and run `Jazz2.exe _pt.j2l`. It needs **JJ2+** (measured with Beta v6.6h) for the scripting.
The probe writes `_pt_trace.asdat` next to the game — `jjSTREAM::save()` forces that extension — and saves
after every scenario, so an interrupted run still yields everything up to that point.

Input injection uses the writable `jjPLAYER.keyLeft/keyRight/keyRun/keyJump/keyDown/keyFire` from
`onPlayerInput`, which runs *before* movement, so the input applies to the tick that is about to happen.

### Running it in another level

`dm_chain` is the one scenario that does not use `_pt.j2l`, and the reason it exists is that `_pt.j2l` is a
level written to measure things — so every constant was fitted against layouts chosen to expose it. Shipped
geometry is the independent check, and it earned its place immediately by finding the horizontal spring's
position snap, which no prop scenario could reach.

```powershell
# This engine — the scenario is guarded on the level, so FirstScenario has to be raised to reach it
#   (set FirstScenario to the index of `dm_chain`, the last case in ApplyInput(), and rebuild)
Jazz2.exe /physics-probe /level flash/02_diam3 /log:file:"<path>\d3.log" /max-fps:60

# The original — copy Level/Diam3.j2as next to Diam3.j2l, then
Jazz2.exe Diam3.j2l          # writes diam3_trace.asdat
```

The guard is what keeps it out of a normal sweep: on `_pt` scenario 172 falls through to `return false`, the
probe reports finished after 171, and the committed `_pt` traces never contain it. **Its traces are
committed separately** (`Results/*-diam3.csv.gz`) rather than merged into the main pair — one trace, one
level's geometry, or the level-version caveat in `Results/README.md` stops meaning anything.

Adding another level means one guarded case in `ApplyInput()` plus one `<level>.j2as` for the original. Two
things to know: the original auto-loads `<level>.j2as` by base name, so the file name is not free; and a
shipped level has **live enemies**, which the test level deliberately does not. `dm_chain` ends with the
original's player nudged by a Turtle Goon that walks over at tick ~1091 — level content arriving, not
movement, but it is the kind of thing to expect in the tail of a long run.

### Extracting and comparing

```powershell
cd Sources/Jazz2/Tests/Tools
./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination ../Results/engine-60fps.csv.gz
./ExtractTrace.ps1 -Path <jj2 dir>/_pt_trace.asdat -Destination ../Results/original-70hz.csv.gz
./CompareTraces.ps1 -Engine ../Results/engine-60fps.csv.gz -Original ../Results/original-70hz.csv.gz
```

## Columns

Close but not identical on the two sides, and each CSV's header says which it is:

```
original   scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,gametick,ms,objx,objy
engine     scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr
```

`x`/`y`/`xs`/`ys` are sampled **after** the tick's movement; the key columns are the input applied *during*
it. `sm` is the active special move. `ms` is a wall-clock offset from the start of the run, which is what
makes the two tick rates comparable — and is how the original's 70.021 Hz was measured in the first place.
`objx`/`objy` carry the pushable's position, or the remaining RF ammo and selected weapon in the `wb_rf_*`
scenarios. `ctrl`/`trans`/`crouch` say whether the player was in control, still animating out of the
previous move, and crouching, which is what tells a move that *did not happen* apart from one that happened
differently. `jrel`/`spr` are the two flags that pick the rise gravity — jump released, and launched by a
spring — logged because which one an ascent decays under does not show up in the position for several
ticks, and a stale `jrel` once cut a blue spring from 597 px to 256.

## Scenario groups

| Prefix | What it covers |
|---|---|
| `g_*` | Ground: acceleration, the caps, deceleration, turning around |
| `a_*` | Air: the launch, the rise, releasing jump, and air control — released, reversed, reversed and released, reversed back |
| `sp_*` | Special moves: uppercut, double jump, sidekick, buttstomp, copter |
| `ob_*` | Objects: springs of each colour, poles, the pushable, vines |
| `sl_*` | Slopes, up and down, walking and dashing — plus `sl_rf_up`/`sl_rf_down`, an RF blast taken standing on each incline. Both fire into the rising side, the only way the shot meets anything inside its one-tile reach |
| `wb_*` | The upper floor: bouncing off the wall onto the platform, and the RF self-blast — including `wb_rf_r*`, which parks the player a set distance from the wall to measure how far the blast reaches |
| `lh_*` | Gaps of one to six tiles, walking and dashing, each with its own run-up so every gap is met at true top speed — which gap the player falls into is the measurement |
| `wb_rf_t*`, `wb_sk_t*`, `wb_tnt_t*` | Explosive knockback by weapon, fired from a standstill at whole-tile distances from the wall. This engine has three unrelated implementations — a measured blast for RF, an unmeasured flat force for the Seeker, nothing for TNT — so these say whether the original has one mechanic or three |
| `en_*` | Buttstomping the level's own TurtleTube, dropped onto from two heights. Settled it: the original bounces at a flat −13, faster than the 10 px/tick impact that caused it, so no fraction of the impact could produce it. **The enemy must be one the level contains** — a spawned one is inert in the original and a stomp passes straight through it. Read `en_butt_turtle`; the `_low` variant is engine-only, because our stomp kills the turtle where the original's leaves it alive |
| `cl_*` | Hitting a ceiling. A staircase of 2-tile-wide bays over one floor clearing 2, 3, 4, 5 and 6 tiles (tiles 43–52, floor on row 16), so a ~132 px standing jump strikes the first three and clears the last two. `cl_j*` is a standing jump per bay, `cl_dj*` the same with Spaz's double jump, and the four moving ones traverse **westward** — east runs out of the section into a 17-tile wall the player then climbs. Set `ScanFloorRow` to 15 to have the probe print the layout |
| `ob_*pole_adj*` | Two pole events immediately next to each other, side by side and stacked, for each type. Stacked V-Poles chain in the original (406.7 px against one pole's 270) and used to be ignored here; side by side chains in neither, because a launch up the column never overlaps the neighbouring tile |
| `sp_dj_p*` | The double-jump window's lower edge, sampled by fall speed at the second press — refused at 0.0000, accepted from 0.2500. The number is the tick of the press |
| `sp_dj_d*`, `sp_dj_r60_*`, `sp_dj_r50_*`, `sp_dj_r85_*` | The double-jump window swept as a delay, from four different release ticks. `d*` releases the first press at 72, `r60_*` at 60, `r50_*` at 50 and `r85_*` at 85; the number after `d` is how many ticks later the second press lands. **This is what showed the window is a timer and not a band of fall speeds**: `r60_d28` is accepted at a fall speed of 3.00 that `sp_dj_hold` is refused at, and `r85_d20`/`r85_d25` are accepted at 4.0 and 4.6, which retired the 3.75 cap the rule was first written as. Only 50, 60, 72 and 85 are usable release ticks — the 60/70 Hz input sampling does not resolve every original tick, and a release at 55 lands a tick late on this side and flips the boundary cases |
| `sp_dj_a_*` | The direction combinations, taken at a delay of 13 so the move actually fires: held, tapped for 4 ticks and for 12, standing, walking, dashing, the direction flipped on the tick of the press, flipped after it, flipped at dash speed, held the *other* way first, pressed only from the double jump on, released before it, released after it, and a third press once the move is spent. The `sp_dj_hold`/`walk`/`dash`/`flip*`/`rel*`/`tap*` family covers the same inputs at a delay of 35, which the window refuses — those check what a *refused* press does, these check the move. `a_dash` is what found that the original zeroes the horizontal speed |
| `pb_bump_*` | Pinball bumpers, in the chamber at tiles 57–80 of rows 4–19: one at (62,15) met from below, from above, and at its own height from either side at dash speed, each with and without a key held into it. `pb_bump_off*` sweeps the trigger radius by dropping past at a set horizontal offset — **leftwards**, because the bumper at (66,9) sits above and to the right and a first version had the 80 and 112 px drops hitting *that* one instead, at an offset and a height nobody chose. `pb_bump_chain` is the drop-in-and-press-nothing guard |
| `pb_pad_*`, `pb_lpad_*` | The pinball paddles — right-facing at (80,18), left-facing at (83,18), mounted against opposite sides of the same wall. `pb_pad_fall` is the decisive one: dropped on with **nothing pressed at all**, so whether it fires unasked is read straight off the trace (the original sits still for 746 ticks). The `_d08`…`_d56` family steps 8 px at a time away from each mounted end, which is what gave the launch law — three points had suggested a curve, and three points is how the horizontal pole was once "fitted exactly" to something wrong in two ways. Read the **first** launch: it is taken at the tick the paddle fires, so it is independent of the level ceiling, which the pumped multi-bounce scenarios do hit |
| `ap_r01`–`ap_r30` | The apex of the rise, swept one release tick at a time: a plain standing jump with the key let go after 1 to 30 ticks, nothing else pressed. Built because every other scenario samples the apex only incidentally, so the `ys` values that turn up there are whatever four release ticks happen to give — and read that way the transition looked like it could not be a function of the speed and the rate at all. The dense sweep gave the rule in one run, and it reproduces all thirty of the original's peaks to 0.01 px. **This is the family to read for anything about the jump arc**, and the one to extend if the remaining ~3% is ever chased: past `ap_r27` the peak saturates at 132.00, because by then the release is later than the apex |
| `sp_dj_ff`, `sp_dj_ff_rel` | Dropped in 420 px up with **no first jump**, to find what arms the window. Neither game double jumps out of a plain fall; both do once the key has been tapped and let go, with no jump ever having happened. Nothing may be pressed before tick 40 in these: the scenario ahead of them holds jump to its last tick, so the probe clearing the input at the reset is itself a release and arms the timer — a first version pressed at 5 and 20 and *both* games fired off that inherited window |
| `sp_chain`, `sp_pole_loop` | The level's own hand-built spring-and-pole chain, the only scenarios made of **level** rather than of props the probe places: dropped in at tile (34,16) and (54,18) with nothing held, for 30 s and 11 s. `sp_chain`'s window was 15 s until the layout gained a zig-zag of blue horizontal springs down the shaft at x=59–64, which the player descends in five alternating bounces. Read them as *when each spring fires*, not as a position at tick 900 — a chain this long amplifies any difference, so one early tick of divergence puts the player somewhere else entirely. `sp_chain` found the horizontal pole's zero-entry launch, which no prop scenario could reach because they all approach a pole by moving towards it; `sp_pole_loop` isolates the part of it that oscillates, and its only remaining difference is the excursion above the top of the level |
| `ob_hpole_z*` | A horizontal pole entered with **no** horizontal speed, by jumping straight up into one placed overhead. The original launches −8 and *leftward* — not the entry sign, and not the facing either, which `_zl` and `_zr` establish by tapping each way first. Three ticks of tap, not twelve: twelve moves the player 33 px, the pole is no longer overhead, and the first attempt measured a player jumping on the spot in both games. `_zl` still misses the pole in **both** games, by the same 2 px, which is worth keeping as a grab-boundary check |
| `ob_hpole_near` | The same pole two tiles away rather than four, so the player is still accelerating on arrival — a fourth fit point for a curve that had only one unclamped one |
| `fu_*` | Float-up areas, ridden up the level's diagonal ladder of them from the block at (32,49) to about (48,35). The intended route is `fu_jump_right`; the rest each depart from it one way — jump released rather than held, walking in before jumping, riding *against* the ladder, no direction, and a buttstomp inside it. Found the mechanic gaining 149.8 px against the original's 520.3. **`fu_stand` is a null test** — the level has no float event at (32,49), so it only shows that nothing happens where there is nothing; `fu_col_none` is the real version |
| `fu_col_*` | The same mechanic in a **probe-placed** column, ten tiles tall and covering the player's own tile. This is where the model came from: the ladder's tiles are discrete and unevenly spaced, so its sawtooth `ys` looks like a repeating impulse with a decay-rate-dependent period, which is geometry rather than a rule. In a solid column the original reads a flat −8.0 for every tick inside, and `fu_col_none` — nothing pressed — is identical to jumping, which is what proves the lift needs no jump and works on a grounded player |
| `wd_*` | Wind areas, probe-placed as a 5×2 block around the player. Read the **per-tick dx**, not the travel: the field is only five tiles wide, so the total measures how far it pushes before the player leaves it and comes out the same for every strength. `wd_stand_l` and `wd_stand_r` look redundant and are not — they settled whether the event's name or the sign of its parameter decides the direction (it is the parameter; both blow right at 8) |
| `bl_*` | Belts, floor events read from the tile below and only while grounded. The plain pair move by position, the accelerating pair drive the speed to a cap. Each has a `_p8` variant because one strength cannot determine a factor: the parameterless belt's 2.0 px/tick fits both "4 × 0.5" and "2 × 1.0", and only an explicit 8 separates them. **The engine side writes converted parameters directly while the original substitutes its own defaults, so these numbers have to track `EventConverter.cpp`** — a stale default here compares two different belts and looks like physics |
| `sd_*` | The slide tile, which makes a floor slippery by swapping the no-direction brake. Same release ticks as `g_dash_rel` and `g_walk_rel45`, which are the no-slide baselines. `sd_dash_s1/s2/s3` sweep the 2-bit Strength parameter — all four values were needed, since strengths 0 and 3 alone give ratios of 2.5 and 2.67 and fit no single divisor |
| `tb_*` | Sucker tubes. `tb_right/left/up/down/diag` measure the speed assignment on each axis, `tb_fast` at 20 px/tick, `tb_wait` a parameter this engine parses and ignores, and `tb_gap1`–`tb_gap5` nine tubes in a row with one to five empty tiles between them. Read the **speed profile**, not the travel: the assignment matches exactly on both sides and what differs is how long the hold lasts and what the release does |
| `ob_spring_down`, `ob_spring_keepx`, `ob_spring_keepy`, `ob_spring_delay`, `ob_spring_h_rev` | The spring parameters — orientation-down, Keep X, Keep Y, Delay and Reverse, none of which any other spring scenario sets. **Two rules make these work, and both were learned the hard way.** A spring must be placed as a *tile event at the reset* and **forty tiles east** of the origin: the game only spawns an object from an event when it first scans that region, and the region around the origin was scanned at level start, so an event written there never appears at all — at the reset or at the run start. And it must not spawn **on top of** the player, or it never fires; the two scenarios that stood on their own spring measured nothing until they were moved aside and the player made to arrive. `ClearProps()` deletes the spawned object as well as the tile, out to 1600 px, or each scenario runs in the pile the last one left |
| `dm_chain` | **Runs in a different level**: the original game's own Diamondus 3, whose west end holds a spring chain nobody built for a test — a one-tile shaft at x=1 with a horizontal blue spring at (1,45) at the bottom, feeding red and green springs and a horizontal pole at (11,52). Dropped in at (1,36) with nothing held for 15 s. Shipped geometry the constants were never fitted against, which is exactly what makes it worth running: it found the horizontal spring's position snap. See *Running it in another level* below |
| `lc_*` | Ledge climb — **engine only**, the original has no such move, so these have no counterpart |
| `jr_*` | Which rise gravity a spring launch decays under — **engine only**, a regression guard rather than a measurement: both reach the same spring the same way and differ only in whether jump was released on the way in, so the two traces must agree |

## Scenarios that do not measure the same thing on both sides

These show up as outliers in every comparison and are not engine bugs. Check this list before chasing one.

| Scenario | Why |
|---|---|
| `lc_*` | Engine only — the original has no ledge climb. |
| `ob_spring_frozen` | The original's frozen spring never fires (it needs a Freezer shot to thaw), so its trace is a player standing still. The engine side spawns the same spring in its normal state and does fire. |
| `sp_spaz_side` | The probe holds **Down and Jump** for the whole scenario. The original jumps once the sidekick ends — a 216 px rise on the end of its trace — while this engine's standard jump is gated on Down not being pressed, so the player stays crouched. `sp_spaz_side_rel` releases both and is the one to read for the sidekick itself. |
| `ob_push_box`, `ob_push_rock` | The pushable sits at a different spot in the two games' copies of the level, so the approach differs. |
| `sp_lori_kick_rep` | Her kicks start ~6 ticks earlier here, which fits one extra kick into the window — about 205 px of extra travel. The per-kick distance and the 45-tick period both match. |
| `ob_spring_red`, `ob_spring_green`, `ob_spring_blue` | Heights are within 2-6%, the gap growing as the launch weakens. The two ticks a spring holds its launch speed in the original account for blue's deficit exactly but would make red and green worse, so they are not applied. |
| `wb_rf_r12` | The original's muzzle ends up inside the wall at that range, so no shot is created and nothing happens — while firing from *against* the wall does blast. That contradiction is not reproduced; ours blasts at both. |
| `wb_dash_j145` | Both games land within 2 px of the platform's left edge; they then disagree on whether the player walks off it. A knife-edge outcome, not a different arc. |
| `wb_wallfind`, `wb_dash_j130`, `wb_dash_j155`, `wb_dash_j164`, `ob_hpole`, `ob_vine2_run`, plus the four `cl_*` moving ones | These score **exactly 29.9 px/s** — or 559.9, the dash cap, for the two that hold *Run* into the boundary, and all six travel the same distance as the original to within a pixel or two. They are the scenarios that spend most of their length with the player held against a wall or a slope face while still pressing into it, and 29.9 px/s is one acceleration step: `OnUpdatePhysics()` zeroes `_speed.X` on the wall hit, then `HandleHorizontalMovement()` — which runs *after* it in `OnUpdate()` — puts a single step back, and the probe samples between the two. The original's probe samples on the other side of its own acceleration and reads 0. Nothing moves differently; the two probes read the same state at different points in the tick. |
| `ob_spring_vpole` | The chain itself now matches — 806.3 px against 804.5, with the same launch speeds and the same decay — but ours runs it **three times** in the 800-tick window where the original runs it once, which is what the speed distribution is reacting to. The original grabs the pole again on the way down, lands back on the spring at tick ~400, and then rests on it for the remaining 400 ticks *without it firing again*; ours re-fires on that landing. A spring re-trigger difference, not a pole one, and only reachable in a scenario where a spring sits permanently underfoot. Note this scenario scored a perfect **0.0** back when the chain was a third short — the pole blindness below. |
| Most `pb_pad_*` / `pb_lpad_*` | **They hit the top of the level, and the two games do different things there.** Read the `RiseOrig` column: `pb_pad_x1`, `pb_pad_d24`, `pb_lpad_x1`, `pb_pad_hold` and `pb_lpad_hold` all report *exactly* 400.0, which is the drop height — the original stops the player dead at y = 0 while this engine lets them keep going, so anything that reaches the ceiling compares two different things. The launches themselves match: read the **first** launch, taken at the tick the paddle fires, which is before the player has gone anywhere. A held key then pumps each bounce higher, so reaching the ceiling is the normal outcome rather than a scenario flaw, and no amount of headroom in the chamber would avoid it — a −39 launch alone needs ~550 px. Same treatment as the spring chains: compare launch by launch, not by total height |
| `lh_g3_walk` | **A coin flip, not a stable outlier.** Walking a 3-tile gap at exactly the walk cap either clears it or does not, and the settle decides which. Two runs of the *same* build started 0.044 px apart in `y`; one fell in at x=8149 (matching the original's 8147) and the other cleared and ran on to x=10015. Expect it to change sides between captures with nothing else different, and do not read a flip as a regression. |
| `fu_col_hold`, `fu_col_rel`, `fu_col_none` | The probe-placed float column, 38.8 / 47.5 / 113.3 px/s, and both causes are listed as gaps in `Docs/MovementAccuracyReference.dox`: while a float area holds the speed the original travels exactly what it assigned with no gravity that tick, where ours applies the usual half-step and comes out 2.7% short; and on leaving, the original decays at 0.875 whenever jump is not *currently* held — including never pressed — which ours reads as held and decays at 0.375. Both cancel on the level's real ladder, where `fu_jump_right` is 0.4 px out over 520. |

Twenty-eight scenarios currently sit outside the 25 px/s band, out of 280 shared. The rows above account
for twenty-six of them — the ones that do not measure the same thing, `lh_g3_walk`'s coin flip,
`ob_spring_vpole` (whose chain now matches and whose residual is the spring re-trigger), the three
`fu_col_*`, the three spring colours whose heights are still 2–6% out, and `sp_dj_a_dashflip`, an 800-tick
bounce chain that agrees on travel to 0.06% and fits one extra bounce into the window. The other two —
`sp_jazz_upper` and `sp_jazz_dj` — are real differences too, small ones, both listed under *Known remaining
gaps* in `Docs/MovementAccuracyReference.dox`. `sp_jazz_dj`'s is the standing jump's 3%, which is the 60 Hz
time step sampling the capped part of the rise; the rise *rule* itself is now exact, and `ap_r01`–`ap_r30`
are the family that established it.

Several that used to be listed here have since come inside the band and are no longer worth chasing:
`sp_spaz_side` (4.4 — its rise still reads 0.4 against 216, which is the held-jump re-jump, so read the
travel), `tb_down` (0.2), `sp_copter_lrl`, `cl_dj_walk` (21.0) and all five `ob_spring_*` parameter
scenarios, which now work.

### Two that pass the band while being visibly different

Worth knowing about, because they are the clearest live examples of what the speed distribution cannot see:

| Scenario | Score | What it hides |
|---|---|---|
| `sp_pole_loop` | **2.3 px/s** | The rise is **1061 px against the original's 592** — 79% out. The launch off the upper pole carries the player above the top of the level, where the original stops them dead at y = 0 and this engine does not, 469 px past it, costing 51 ticks getting back down and making the loop's period 8% long. The distribution barely notices because the launch speeds themselves are identical and the four pole holds contribute ~280 ticks of zero. |
| `ob_hpole_z0`, `ob_hpole_zr` | 18.1 / 14.0 px/s | The pole launch matches exactly, −8.0 on both sides. What differs is where the player ends up after *landing*, ~200 px apart, each then jumping on the spot for the rest of the window. Read the `xs` at the launch tick, not the end position. |

`sp_chain` is the opposite case and the reassuring one: **0.0 px/s** across thirty seconds of springs and
poles, matching launch for launch through the whole chain including all five bounces down the zig-zag at
x=59–64. Its timing drifts ~4.5% by the end and the two part company in the last few seconds, once the
geometry at the bottom opens out — read it launch by launch, as with any chain.

## Things that have gone wrong before

- **`FirstScenario` left raised.** Both probes have one, meant for re-measuring only the newest scenarios
  while iterating. A raised value silently skips everything before it, which has been mistaken for
  scenarios that stopped working more than once. Leave both at 0 when committing.
- **Extracting while the run is still going.** Neither game is a console application, so PowerShell's call
  operator does **not** wait for it: `& .\Jazz2.exe /physics-probe ...` returns immediately and
  `$LASTEXITCODE` comes back empty. Extract at that point and you get however much of the log had been
  flushed — a scenario came out with 456 of its 686 rows and a trajectory that simply stopped mid-flight,
  which reads like the player getting stuck. Poll for the end instead: `[probe] finished` in this engine's
  log, or the last scenario's final tick in the original's `.asdat`, which it only writes at scenario
  boundaries — so two equal sizes a few seconds apart is not a stall, it is the middle of a scenario.
  `Start-Process -Wait` is no help either: neither game exits when the probe is done.
- **Comparing endpoints.** It hides a wrong *curve*: Lori's kick once passed a distance check while being
  three times too fast, because it covered the right distance in the wrong way. `CompareTraces.ps1`
  compares the sorted speed *distribution* instead, which catches wrong shapes and ignores the few ticks of
  phase offset that make a naive per-instant diff useless.
- **Reading a speed column when the position is what moved.** The original moves the player directly for
  the buttstomp drift and for pushing, leaving `xSpeed` at zero the whole time. Four scenarios "proved"
  there was no drift before anyone checked the position.
- **A scenario can hand its state to the next one.** The reset moves the player back to their mark and
  zeroes the speeds, but a pole or vine leaves them *attached* — so the pole logic snapped them straight
  back up onto the previous scenario's pole and the next scenario ran from up there with gravity off. It
  looks exactly like a physics bug in the scenario that follows. The reset now clears the attachment, the
  suspend state, the special move and the controllable flags, and takes the tile events out before the
  settle rather than after it. Any trace captured before that fix may be contaminated in whatever followed
  a `ob_vpole*`, `ob_hpole*` or `ob_vine*` scenario.
- **A prop left over from the previous scenario acts on the player during the settle.** Spawned objects
  used to be removed just before the run rather than at the reset — so a spring still sitting under the
  player's feet launched them while the next scenario was lining up, and that scenario's first logged tick
  was already 60 px up with a third of its speed spent. It hit the second and third spring in the row, in
  *both* games, and produced the figure that made the spring heights look impossible for months: green
  appearing to rise less than red despite the stronger launch. `ClearProps()` now runs at the reset in both
  probes. Any trace captured before that has bad `ob_spring_green`, `ob_spring_blue` and `ob_spring_frozen`
  rows.
- **A player hanging on a pole passes the "at rest" check.** The settle waits for the position to go quiet,
  and a pole holds the player perfectly still — so it passed on the first tick and the scenario started
  with the player up on the pole. This is what contaminated the original's five-pole reference, and it
  survived clearing the tile events earlier, because the attachment outlives the event. Both probes now
  also require the player to be within **1.5 px of the recorded ground height**; 8 px was not enough,
  because a pole at the ground tile centre sits only 3.5 px above where the player rests. Note the check
  cannot apply during PRIME, which is what discovers that height in the first place.
- **Scenario indices are not shared.** The two probes agree on scenario *names*, not numbers — the original
  has extra scenarios (pole chains, the double-jump window sweep) that were never ported to the engine
  side. Always join on the name.
- **The engine's own scenario numbering matters to three other places**: `ScenarioCount`, the character
  list in `SetupCharacter()`, and the placement ranges in `SetupCharacter()`/`SetupProps()`. Inserting a
  scenario in the middle silently re-points those.
- **Speeds are not in the same unit on the two sides.** The original's are pixels per one of its 70.021 Hz
  ticks; the engine's are pixels per 60 Hz-equivalent frame, because position advances by
  `speed * timeMult` and `timeMult` is 1.0 at 60 FPS. Converting both with one factor is worth a flat
  ~187 px/s of phantom error on every scenario that reaches the dash cap — which is most of them, so it
  looks like a systematic physics difference rather than an arithmetic mistake. `CompareTraces.ps1` uses
  60 for the engine and 70.021 for the original.
