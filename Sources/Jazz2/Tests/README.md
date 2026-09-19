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

```powershell
$p = Start-Process Jazz2.exe -ArgumentList '/physics-probe','/level','_pt',
    '/log:file:"<path>\test.log"','/max-fps:60' -PassThru
$p.PriorityClass = 'High'
```

- `/level _pt` is needed — the probe does not load the level itself, and without it the game sits in the
  menu and logs nothing. (`/level` is a `DEATH_DEBUG`-only switch, which a Debug build has.)
- `/max-fps:` pins the tick rate being measured. Run 24, 60 and 144 to check frame-rate independence; the
  probe's own counter runs at the original's 70.021 Hz regardless, so every scenario reaches each of its
  steps at the same *real* time whatever this is set to.
- **High priority**, because a sweep runs for the best part of an hour on a machine somebody is still
  using. It is a precaution rather than a fix for a measured problem — four full sweeps captured during
  normal desktop use contain no frame interval slower than 30 ms/tick against a nominal 14.3, and the
  scenario clock runs off `timeMult` rather than wall time, so a hitch cannot move a trajectory. What a
  hitch *can* move is the settle, which ends on real-time pacing and leaves the player a few hundredths of
  a pixel apart from run to run — and that is enough to flip the knife-edge scenarios. Cheap insurance.
- A full sweep takes about **80 minutes** in a Debug build. The window is per scenario, so it does not shrink
  much with a higher frame rate — selecting fewer scenarios is what shortens a run. See the next section.
- **It closes itself when the run ends.** `FinishRun()` logs the marker and then quits, so `WaitForExit` is
  all a runner needs. That matters beyond convenience: the alternative was killing the process by hand,
  which is fine until the kill lands while the trace is still being written. Quitting goes through the
  ordinary shutdown, so the asynchronous trace sink is flushed first.

  **The original's probe does not**, and JJ2+ exposes nothing to make it. Its trace is complete either way —
  it saves after every scenario, not at the end — so a runner there waits for the file to stop growing and
  then kills the process.

### Measuring only some of the scenarios

A full sweep is about **80 minutes on each side**, so most runs should not be one. Comparisons join on the
scenario **name**, so a short trace merges with the committed one rather than replacing it — nothing already
measured is lost by not re-running it.

Three controls, on both probes, and they combine:

| | what it does |
|---|---|
| `ScenarioFilter` | comma-separated name **prefixes**; empty runs everything |
| `FirstScenario` | index to start at |
| `LastScenario` | index to stop after; `-1` for "to the end" |

**Prefer the filter.** It selects by name, and the same string works unchanged on both probes — which matters
because the two have never numbered scenarios alike (`sp_lori_side_rock` is 386 here and 381 there), so a
pair of index ranges has to be re-derived for each side and quietly selects different scenarios when it is
got wrong. Prefixes let a family be named at whatever depth is useful:

```
ScenarioFilter = "sp_lori"                  // all of Lori's
ScenarioFilter = "sp_lori_side_rock"        // just the four pushable ones
ScenarioFilter = "ob_vine,ob_vpole"         // two families at once
```

A skipped scenario costs one frame rather than its settle plus its 800 ticks, so four out of four hundred
take seconds. **Set all three back to their defaults before committing** (`""`, `0`, `-1`): a filtered run is
not a sweep, and once extracted the two look alike — a CSV with fewer scenarios in it is also what a run cut
short produces. Each probe writes a `PARTIAL RUN` line into its own header when any of the three is set, so a
capture says which it is; on the engine's side that line is logged under `[probe-info]` rather than `[probe]`,
because `ExtractTrace.ps1` turns every `[probe]` line into a row.

One input was long believed to exist on the engine's side only: JJ2+ **does** expose `keyUp`, alongside the
other seven, and the original's probe simply never drove it. It does now, so a scenario needing Up mirrors
like any other. What remains true is that `fc_carrot`'s target came from a free-run recording rather than
from a paired run.

A scenario whose *input* changed needs re-running just as much as a new one, and is much easier to miss —
its name is already in the capture, so a comparison pairs the two traces happily and reports a difference
that is really two different manoeuvres. `fc_carrot` did exactly that. When you re-order a scenario's input,
include its name in the filter for the next run.

The same applies to a scenario whose *props* changed, which is easier still to miss because the input looks
untouched. Both are why the filter takes names: whatever was edited, its name is the thing you already know.

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
#   (set FirstScenario to the index of `dm_chain`, the last case in ApplyInput() — 363 — and rebuild)
Jazz2.exe /physics-probe /level flash/02_diam3 /log:file:"<path>\d3.log" /max-fps:60

# The original — copy Level/Diam3.j2as next to Diam3.j2l, then
Jazz2.exe Diam3.j2l          # writes diam3_trace.asdat
```

The guard is what keeps it out of a normal sweep: on `_pt` scenario 363 falls through to `return false`, the
probe reports finished after 362, and the committed `_pt` traces never contain it. **Its traces are
committed separately** (`Results/*-diam3.csv.gz`) rather than merged into the main pair — one trace, one
level's geometry, or the level-version caveat in `Results/README.md` stops meaning anything.

Adding another level means one guarded case in `ApplyInput()` plus one `<level>.j2as` for the original. Two
things to know: the original auto-loads `<level>.j2as` by base name, so the file name is not free; and a
shipped level has **live enemies**, which the test level deliberately does not. `dm_chain` ends with the
original's player nudged by a Turtle Goon that walks over at tick ~1091 — level content arriving, not
movement, but it is the kind of thing to expect in the tail of a long run.

### Recording a run by hand (free-run mode)

Some mechanics cannot be driven from a script at all. The run-in-place rev-up is the one that forced this:
`jjPLAYER.keyRun` taps produce **nothing** in the original at any cadence from 2 to 16 taps, with or without a
release, because it reads the raw key upstream of the script override. The only way to measure something like
that is to play it by hand and read the trace.

Free-run mode skips the scenario matrix entirely and writes no input at all — the game plays normally, and
every tick is logged under the scenario name `free` with whatever is really being pressed in the key columns.
Every other column is exactly as in a sweep, so the same tools read it.

| | Switch | Where |
|---|---|---|
| This engine | `FreeRunMode = true` | `PhysicsProbe.h` (rebuild) |
| The original | `FreeRun = true` | `Level/_pt.j2as` |

```powershell
# This engine - same command as a sweep; the probe just stops driving
Jazz2.exe /physics-probe /level _pt /log:file:"<path>\free.log" /max-fps:60

# The original - as usual; it saves every 70 ticks rather than at a scenario boundary,
# so closing the game by hand still leaves a complete trace
Jazz2.exe _pt.j2l
```

**Turn both back off before committing.** A recording is not a sweep, and a trace of one would be mistaken for
the other — the scenario name `free` is the only thing that distinguishes them.

Two things to know when reading one. The player is never repositioned, so the trace starts wherever the level
spawned them and there is no settle — compare by `tick` within the recording, never against a sweep's ticks.
And the key columns are now an *observation* rather than the script's own intent, which is what makes them
worth reading: a mechanic that depends on press timing shows up as the pattern in `run`, next to what it
produced in `xs` and `anim`.

### Extracting and comparing

The committed traces are **split by category**, one file per category on each side, because a full sweep is
about eighty minutes a side and adding one scenario should not mean regenerating all of it. `Categories.ps1`
defines them; each is under ten minutes, and the largest are `doublejump` at about nine and `pinball-pad` at
about eight.

```powershell
cd Sources/Jazz2/Tests/Tools
./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination ../Results/engine -Split -Gzip
./ExtractTrace.ps1 -Path <jj2 dir>/_pt_trace.asdat -Destination ../Results/original -Split -Gzip
./CompareTraces.ps1 -Engine ../Results/engine -Original ../Results/original
```

Both scripts take a directory wherever they used to take a file. `-Split` writes only the categories the
capture actually holds, so a filtered run replaces exactly those files and leaves the rest of the committed
set alone. The comparison reads the whole directory back as one set and joins on the scenario **name**, so it
does not matter which file a scenario came out of, nor whether the categories have been redrawn since.

**To re-measure one category**, ask for its filter and paste that into both probes:

```powershell
. ./Categories.ps1; Get-CategoryFilter objects      # -> ob_,fc_
```

Set `ScenarioFilter` to that on both sides, run both, extract each with `-Split`, and the two `objects` files
are replaced while every other category keeps the capture it already had. Then set the filter back to `""`.

A scenario that matches no category lands in `misc`, and the extractor says so by name rather than letting it
disappear — that is the prompt to give it a home in `Categories.ps1`.

**A filtered run does not reproduce a sweep's scenario ordering**, and anything that carries between scenarios
can therefore differ. The reset clears a great deal, but not everything — the morph contamination above is
proof that the list is incomplete in ways nobody has enumerated. So a filtered run is the right tool for
"did my change do what I meant", and the wrong one for "did my change break anything else": two scenarios
moving by 19 px and 2 px in a filtered regression check could not be told apart from their neighbours having
changed. Confirm a regression against a full sweep of both sides, not a filtered one.

**A filter is often narrower than a category**, which is the case it exists for: re-measuring two scenarios of
`doublejump`'s fifty. `-Split` therefore **merges** into an existing category file, scenario by scenario —
what the capture holds replaces what was there, and everything else in that file is kept. It prints how many
rows it kept, so a narrow run says out loud that it did not just overwrite the category. Without that, one
filtered extract silently discarded forty-eight scenarios' traces and the only clue was a row count looking
too small; the ones already committed were only recoverable by re-running them.

`CompareTraces.ps1` compares three things, and the third exists because the first two missed a whole class
of bug. Positions and the speed *distribution* are the trajectory; **`anim` is the pose**, and a pose can be
completely wrong while every position and speed is exact — the stop chain is drawn over the base animation
and moves the player not at all, so a rev-up launch ran its whole length in the sliding-to-a-halt pose and
scored perfect on both other measures. What it reports depends on which two traces are being compared:

- **Two engine traces** (`-Baseline`): the animation ids mean the same thing on both sides, so it reports
  **`AnimSame`**, the percentage of ticks where they are equal. That is the sharpest regression check in the
  tool — a pose that moved by one frame shows up.
- **Engine against the original**: the two games number their animations differently, so the ids cannot be
  compared at all. It reports the **segment count** instead (`AnimSegE`/`AnimSegO`) — how many runs of a
  constant pose each passes through — which is what catches a pose one game shows and the other does not.
  Reading *which* poses differ still means looking at the two traces by hand, as the stop-chain work did.

`AnimSame` is deliberately strict and that makes it noisy between two separate runs: the idle/bored cycle
runs for hundreds of ticks, so a scenario that ends up one step out of phase — which the settle's few
hundredths of a pixel are enough to cause — scores far below 90% while being identical in every way that
matters. Read it as "which scenarios changed", then look at *what* changed before concluding anything. A
real change is usually a different id, not the same two ids swapping places.

### Checking the model is frame-rate independent

The movement model is written in the original's per-tick units and scaled onto whatever frame the engine is
running (`LegacyFrameRateScale`), so a trajectory should depend on elapsed time and not on how finely that
time was sampled. `CompareFrameRates.ps1` checks that directly, across captures of **one build** taken at
different `/max-fps:N`:

```powershell
./CompareFrameRates.ps1 -Traces ../Results/engine,fps60,fps30,fps24 -Labels 144,60,30,24
```

**Run the four sweeps in parallel** — measured, they do not interfere. Four instances at 24, 30, 60 and
144 FPS on an 8-core/16-thread machine each held their target with essentially no jitter: within a scenario
the median frame times were 41.70, 33.30, 16.70 and 7.00 ms with p99 at 41.70, 33.70, 16.70 and 7.00 and
maxima of 41.70, 34.30, 16.90 and 7.20 — the same figures each produces running alone, at 65% CPU. So a
four-rate validation is ~70 minutes rather than four and a half hours. Give each its own log file.

**Measure the achieved rate from within a scenario, never across the whole log.** The `ms` column does not
advance during the reset and settle between scenarios, so a naive diff of consecutive rows reports the gap
as a 300 ms frame and makes a clean run look like it is hitching badly. Group by the scenario name first.
This matters more than it sounds: `timeMult` scales with the real frame time, so a genuine 300 ms frame
would advance the physics by twenty frames' worth in one step and a trajectory really would move.

Nothing needs converting first, which is the part worth knowing: `x`/`y` are absolute pixels, `tick` is
already in the original's tick units on every capture (the probe advances it by `timeMult * 70.021/60`, so a
frame is 2.92 ticks at 24 FPS and 0.49 at 144 and the tick *range* is the same either way), and `xs`/`ys` are
pixels per 60 Hz-equivalent frame by the engine's own convention and so are already normalised — which is
also why `CompareTraces.ps1` multiplies by a fixed 60 whatever rate a capture was taken at. The comparison
is therefore a plain position difference on a shared tick grid, taken from the **coarsest** capture, with
the others interpolated onto it.

**This and the cross-game check answer different questions, and you want both.** All four rates can agree
with each other and all four disagree with the original — that is a model error, not a sampling one — so run
`CompareTraces.ps1` against the original at each rate as well. The reverse also happens: two rates can score
the same against the original while taking visibly different routes, because the speed-distribution metric
is phase-insensitive by design.

Expect the low rates to be the interesting ones. A 24 FPS frame is nearly three of the original's ticks, so
anything decided by a key edge, a tile boundary or a one-tick window is sampled coarsely enough to fall on
the other side of it — and that is a genuine property of running the game at 24 FPS, not a harness artefact.

### What the original's animation ids mean

The original logs `jjPLAYER.curAnim`, a bare number with no name attached, and nothing maps it. These were
read off scenarios whose pose is not in doubt, which is the only way to get at them — the id is whatever
that scenario is visibly doing. They are **Jazz's**; the other two characters number their own sets.

| id | Pose | Read from |
|---|---|---|
| 66 | Idle | `g_stand`, and the tail of most scenarios |
| 59 | Walk | `g_walk` |
| 60 | `dash_start`, the spinning feet between 4 and 8 px/tick | `g_dash`, ticks 12–22 |
| 61 | Dash | `g_dash` |
| 63 / 62 / 64 | The stop chain: skid, slide, settle | `an_slide_stop`, at ticks 91, 103 and 131 |
| 44 | Rising | `a_jump`, `fu_col_none` |
| 16 | Falling | `a_jump`, `fu_col_none` |
| 26 / 27 | Vine hang / vine idle flourish | `ob_vine_low`, alternating 70 ticks each |
| 29 / 28 | Vine shoot pose (held) / vine shoot return (3 frames) | `an_vine_shoot` |
| 58 | The curled ball a pole or tube ride holds | `ob_vpole`, and all 800 ticks of `tb_down` |
| 36, 37, 38 | Sucker tube, other directions — not separated yet | `tb_right`, `tb_row` |
| 10 | Crouch | `sp_jazz_upper` holds it from t21 to the uppercut at t41 |
| 65 | Buttstomp, the hold at the top | `sp_jazz_butt` t71–t105, at `ys` 0.0625 |
| 17 | Buttstomp, the descent | `sp_jazz_butt` t110, at `ys` 10 |

**Lori's** are her own, derived the same way from `sp_lori_side`, `sp_lori_butt`, `sp_lori_copter` and
`sp_lori_copter_fwd`:

| id | Pose | id | Pose |
|---|---|---|---|
| 226 | Idle | 204 | Rising |
| 170 | Crouch | 176 | Falling |
| 229 | Sidekick (the whole kick, 9 frames) | 190 | Copter |
| 225 | Buttstomp, the hover | 177 | Buttstomp, the descent |
| 219 | Walking | 217 / 216 | Rising / falling with speed |

**Spaz's** three ground-run ids fell out of the animation-rate measurement rather than being read by eye, and
that is a second way to get at an id worth knowing about: 137, 138 and 139 hold a frame for exactly the same
number of ticks as Jazz's 59, 60 and 61 do at every speed either of them is ever seen at — 8 ticks at speed 4,
1 tick from 11 up — which no other pair of animations does. So they are his walk, `dash_start` and dash. The
rest of his set is still underived; `sp_spaz_side` shows 145, 143, 94 and 122 cycling, which is where to start.

Add to these rather than re-deriving them: each row cost a scenario read by hand, and a wrong guess about an
id is indistinguishable from a wrong guess about a mechanic.

Two things make that cross-game count mean anything, and without either it is mostly noise:

- What this engine **draws** is `tranim` when a transition is playing and `anim` otherwise; the original
  logs one `curAnim` that already is whichever is on screen. Comparing our base state against that
  undercounts every pose the stop chain and the special moves draw over it — `g_dash_rel` reads 4 poses
  against the original's 7 that way, and 7 against 7 with the transition folded in.
- Poses shorter than **4 ticks** are dropped. The two traces do not have the same number of rows for the
  same scenario (686 against 800), so a pose lasting a tick or two can land in one sampling and fall
  between rows in the other. Four ticks is where the plain scenarios start agreeing exactly — `g_walk`
  1/1, `g_dash` 3/3, `a_jump` 7/7 — instead of differing by sampling alone.

Captures made before the `anim` column existed simply report nothing for it; the column is located by
**name** rather than by index, because the two sides do not agree on column order.

## Columns

Close but not identical on the two sides, and each CSV's header says which it is:

```
original   scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,gametick,ms,objx,objy,camx,camy,anim,frame
engine     scenario,tick,x,y,xs,ys,right,left,run,jump,down,sm,ms,objx,objy,ctrl,trans,crouch,jrel,spr,camx,camy,anim,frame,tranim,susp
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

`camx`/`camy` are the **camera's offset from the player**, not its absolute position, so the two games are
comparable without either one's view size entering into it. They are appended at the end on both sides
rather than inserted, because the two column sets already disagree about where `objx` sits and anything
reading a fixed index would move under it. They are logged on *every* scenario, so the whole existing matrix
doubles as camera coverage — the horizontal pan at both speeds is in `g_walk*`/`g_dash*`, the vertical
behaviour through a jump is in `a_*`, and the sidekick's camera is in `sp_spaz_side*`. No `cm_*` family was
needed.

**The two sides do not log the same convention**, and it was read off the data rather than assumed: on
`g_stand`, where the player is at rest, the original reports exactly `−400.000, −225.000`, so its
`jjPLAYER::cameraX`/`cameraY` are the view's **top-left corner** on an 800×450 subscreen. This engine logs
the **centre** (`LevelHandler::GetCameraPos`). Add half the subscreen to the original's figures to compare
them — `Tools/CompareCam.ps1`-style analysis does exactly that. Forget it and every original row looks like
a camera lagging the player by a third of a screen.

Worth knowing separately: this engine's own `jjPLAYER::get_cameraX()` returns the centre, so it does *not*
match JJ2+ here. That is a script-compatibility bug rather than a probe one, but it is why the engine-side
column needed no adjustment while the original's did.

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
| `sl_up_j*` | Jumping into a 45-degree up-slope, the same face `sl_up_walk`/`sl_up_run` climb, entered with the same run-up. What these measure is **`xs` over the climb, not a height**: the report is that the original keeps its horizontal speed and re-jumps off the slope face over and over, where this engine mostly kills the speed or refuses the jump. `_jhold` dashes, `_jhold_walk` does not, and `_jtap` taps the key — the tapped one is what tells a re-jump that needs a fresh press apart from one continuous jump produces on its own, which a single trace cannot separate |
| `sl_up45_*` | The same question as `sl_up_j*` against a **true** 45-degree face, which is what that trio turned out to lack: the level's designated "up" slope is 6 tiles by 5, shallower than the 45 degrees a dashing jump travels at, so the jump clears it and both games keep their speed. These run **leftwards up the long slope** at tiles (193,41)–(210,57), 17 by 16, entered three tiles clear of its foot. This is the geometry where `TryMoveSubstep()`'s airborne climb gate, `abs(stepX) > abs(stepY)`, is false at exactly equal steps |
| `rt_*` | Tapping Run **on the spot**, nothing else pressed, so any `xs` at all is the mechanic — a standing player has no other way to acquire one. `rt_hold` is the control and the other three tap at 6-, 10- and 20-tick periods; three cadences because one cannot tell "each press adds something" from "a press has to land inside some window". All four end with the same jump, so the launch speeds are directly comparable and the reported extra tile of height is readable off the arc |
| `lc_*` | Ledge climb — **engine only**, the original has no such move, so these have no counterpart |
| `jr_*` | Which rise gravity a spring launch decays under — **engine only**, a regression guard rather than a measurement: both reach the same spring the same way and differ only in whether jump was released on the way in, so the two traces must agree |
| `ow_*` | One-way floors, on the ladder of seven 3-tile platforms at tiles 27..29, rows 33, 31, 29, 26, 23, 19 and 15, standing on the bottom rung. `ow_jump` rises through two and comes down on one, `ow_hop` peaks level with the next one up, `ow_down` and `ow_down_fall` hold Down standing on one and through the fall onto one, and `ow_hold` never lets the jump key go. Read **`ow_hold`**: crossing a platform with jump held relaunches the jump, so any scenario that *releases* the key near a crossing is decided by that tick rather than by the rule (see the hazard below) |

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
| `fu_copter` | **A copter scenario that happens to run over float tiles — its 305 px gap is not a float-up defect.** The original copters into the ladder and climbs 479.7 px; this engine never engages the copter at all and climbs 174.6. The cause is the one-chance-per-airtime rule in `HandleJump()`, which spends the attempt on the first press of the airtime wherever it lands: the first tap here is at tick 20, while the player is still rising, so every later tap is refused — while the original, whose first tap is *also* rising, engages on the second pair at tick 27. The rule is what makes `cp_tap55` and `cp_tap60` match, so loosening it means re-measuring the whole `cp_*` family rather than trading one mismatch for another. |
| `fu_col_butt`, `fu_col_butt_in` | Both cross the column at the right speed — the descent cap is measured and matches to 2% — but they then oscillate in the field for the rest of their 700 ticks, and the two sides sit at different points in that cycle. Read the *descent*, which is what they were built for, not the speed distribution: `fu_col_butt_in` rises 244.8 px against 242.7, and the post-landing coast tops out at 1084.2 against 1080.1. |
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

- **Re-running a category at a different frame rate from the rest of the set.** The committed engine
  capture is at **144 FPS**, not the 60 `Results/README.md` claimed until 2026-09-17, and the whole set is
  at that rate. Four categories re-captured with `/max-fps:60` as documented dropped eleven scenarios out
  of the 25 px/s band with no change capable of reaching most of them — nine of those moving from *exactly*
  12.5 to *exactly* 29.9, which is the shape of a sampling artefact and not of a movement one. The metric
  is a distribution over sampled ticks, so scenarios that spend most of their run standing still
  (`wb_wallfind`, `ob_vine_run`, `wb_dash_j*`) are the ones it moves, and they are exactly the scenarios
  nobody looks at twice. **Check the rate before trusting a partial re-run**: divide a scenario's row count
  by its last `tick`, which is ~2.06 at 144 FPS and ~0.86 at 60, or read the `ms` column, which steps
  7.00 ms against 16.70. Pass `/max-fps:` explicitly every time; an uncapped Debug build lands near 143 on
  one machine and somewhere else on another.
- **Reading one scenario's divergence as a regression.** Most scenarios repeat to a hundredth of a pixel,
  but a few decide something late on a margin of a few pixels — whether the player clears a wall, catches a
  ledge, lands on a platform — and those swing wildly. `sp_dj_a_dashflip` is the known one: three runs of
  the *same* build gave a final x of 1936.8, 1999.6 and 1996.2, and a fourth, inside a full sweep, flew
  1900 px further by missing a wall the other three hit. Against the original that read as a 1899 px travel
  regression. **`lh_g3_walk` is the other**, and it is worse: three runs of the same build gave 10014.3,
  8149.0 and 10014.8 — a 1865 px swing between two outcomes, because the walk reaches the gap's edge with
  the same few hundredths of a pixel that the settle varies by. Four consecutive sweeps had agreed on 8149
  before the fifth did not, which is exactly how it gets mistaken for a regression. Before blaming a change,
  re-run the one scenario two or three times on the current build — it takes a minute, and the alternative
  is bisecting a build.
- **Using the machine while a sweep runs.** Mostly harmless, and measured rather than assumed: across four
  full sweeps captured while the PC was in use there is **not one** frame interval slower than 30 ms/tick
  against a nominal 14.3, and the scenario clock runs off `timeMult` rather than wall time, so even a real
  hitch would not move a trajectory. What *can* reach the player is any input action the probe does not
  overwrite — it drives seven and there are twenty-one. The weapon bindings were the dangerous half, since
  `wb_rf_*`, `wb_sk_t*` and `wb_tnt_t*` select a weapon at setup and assume it is still selected when they
  fire; a stray number key would have read as a knockback constant being wrong. The probe now clears
  `ChangeWeapon` and all ten `SwitchTo*` every tick. `Menu` and `Console` are left alone on purpose: they
  open UI rather than moving the player, and taking them away would trap whoever is at the keyboard.
- **A powerup monitor is *consumed*, so the scenario that uses one takes it away from every scenario after
  it.** The same class as the pushable below and worse, because the object does not merely move — it stops
  existing. Three scenarios were written to land on the same monitor and only the first one found it; the
  other two fell to the floor and measured a player standing on nothing, which looks exactly like the
  mechanic under test failing. The tell was in `objx`: the ammo count changed at the moment of contact, which
  is the powerup being collected. **One monitor per scenario that breaks one**, and if there are not enough
  in the level, add them rather than sharing — a shared one is a scenario that silently stops measuring.
- **The level's own pushable is never reset, so a scenario that moves it poisons every later one.**
  `ClearProps()` takes out what a scenario *spawned*; the rock and the box belong to the level and stay
  wherever they were last shoved. `ob_push_box` and `ob_push_rock` shove them deliberately, and so does
  anything that drives into one. It cost `sp_spaz_side_rock` its whole measurement: `sp_lori_side_rock` ran
  first and pushed the rock 96 px along, and his start — computed from the **event map**, which does not
  move — then put him past it, so he kicked his full unobstructed distance and never met it. The symptom is
  a scenario that measures a clean *unobstructed* number when it was aimed at an obstacle.

  **Restoring it at the reset was tried and reverted — do not repeat it.** Writing `xPos`/`yPos` straight
  onto the object leaves the original's rock hanging **in mid-air**: nothing re-settles it, so it simply
  stays where it was put. It also has to be scoped, because the level holds more than one pushable and
  `PushableX` is only the first the event scan met, so a blanket loop stacks them all on one spot. Neither
  problem is worth solving for this: **order the scenarios so the one that moves it runs last**, which costs
  nothing and cannot break the level. The pair that need a rock in front of them are deliberately run with a
  raised `FirstScenario` for the same reason.
- **`FirstScenario` left raised.** Both probes have one, meant for re-measuring only the newest scenarios
  while iterating. A raised value silently skips everything before it, which has been mistaken for
  scenarios that stopped working more than once. Leave both at 0 when committing.
- **Putting a key edge next to the event it decides.** This engine's probe reaches a scenario's step about
  two of the original's ticks behind the original probe, throughout. That costs nothing where the
  measurement is where the player ends up, and everything where a *release tick* lands near something the
  key state decides. `ow_jump` used to release jump at tick 50, with the original crossing a one-way
  platform at 49 and this engine at 52 — and since crossing one with jump held relaunches the jump, the two
  runs differed by a whole jump's height and it read as a 50 px physics error. Keep such an edge several
  ticks clear of the event, or hold the key for the whole scenario the way `ow_hold` does. The same shape
  as the chaotic-outcome hazard above, but deliberate rather than accidental: here the scenario's own input
  put the edge there.
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
- **A morph at the reset doubles the next scenario's ground acceleration, in the original only.** The worst
  kind of contamination: it is invisible, it is *plausible* — a slightly fast scenario reads as a physics gap
  rather than a harness fault — and it applies to every scenario the original runs after a character change,
  which is a large and scattered set. Found by accident. A new scenario came out 10% short, and chasing it
  through the units (engine columns are px/frame, the original's px/tick — the launch speeds were identical
  once converted) left only the walking approach, where the original reached the 4 px/tick cap in 12 ticks
  against our 24. The original was the odd one: its own `g_walk` accelerates at our rate, and so does
  `ob_spring_red_h` with the same prop and the same input.

  Three runs settled it, each a couple of minutes thanks to the filter. **Alone**, the scenario matched
  perfectly. **After a Jazz scenario** it doubled. **After an already-Spaz scenario** it matched again. Only
  a `morphTo` in between reproduces it, and it is not a held key — clearing the inputs at the reset and
  releasing Up before the boundary both changed nothing.

  The settle is what let it through: a morphed player stands perfectly still, so the at-rest test ends the
  settle immediately and hands the contamination straight to the run. The reset now records that it morphed
  and the settle waits `MorphSettleTicks` out regardless of how still the player is.

  **How much it actually mattered, measured rather than assumed.** Re-sweeping the original with the fix and
  diffing against the previous capture moves **8 scenarios** by more than a pixel, not the large scattered
  set the character lists suggested: `cl_dj_walk` by 214 px, `sl_up45_jhold` by 69, `sp_dj_hspring` by 22 and
  five others by about 4. Most scenarios either never accelerate from rest or reach a cap so quickly that
  twice the acceleration makes no difference by the time anything is measured. Worth knowing before warning
  anyone that a whole baseline is suspect — I did, and it was not.

  It is also worth knowing that the bug this was found while chasing was **not** this one: `sp_dj_hspring`'s
  10% gap survived the fix nearly untouched. Two real faults in the same trace, and the loud one was the
  harness.
- **Writing a scenario whose input cannot reach the prop it places.** The same failure as the one below and
  harder to spot, because the event *is* there. `ob_vine` and `ob_vine_run` placed a vine three tiles up and
  jumped for five ticks: that rises 77 px against the 96 it needed, so for as long as they have existed
  neither has grabbed anything. Both games did nothing, so both agreed, and two scenarios sat in the suite
  measuring a plain short jump under a name that said otherwise. `sp_dj_vine` and `ob_vpole_exit` were
  written by copying them and inherited it on the first run.

  Dropping the vine a tile did not fix it either, and the reason is worth knowing: `ty` in `SetupProps()` is
  derived from `_groundY`, which is where the player's **centre** rests, not where the floor tile is. Two
  tiles up is therefore 80 px and not 64, so a 77 px jump was still 3 px short — and being 3 px short reads
  exactly like being 19 px short. Both now jump for eight ticks instead of five, which clears it with about
  25 px to spare. What catches this is the **`susp` column**: a
  vine or pole scenario that never shows a non-zero suspend state never engaged, whatever its positions look
  like. Worth checking against a scenario known to work — `ob_vine_low`, `ob_vine_drop` and `an_vine_shoot`
  all suspend, and they all meet the level's own low vine from below rather than placing one overhead.
- **Writing a scenario against an event the level does not contain.** It does not fail; it measures a player
  standing still and reads as perfect agreement, because both games have nothing to interact with. The prime
  step now prints a `[census]` line per event type the level holds, with a count and the first tile, so this
  can be checked before a scenario is written rather than after it has quietly passed. What `_pt` contains
  today: vines, one-way modifiers, both pole kinds, float-up and fly-off areas, springs, the pushable, both
  pinball parts, a turtle and the flying carrot. What it does **not** contain: warps, morph monitors and the
  powerup monitors — so the reported behaviours around those have no scenario yet. Most props are placed at
  the reset rather than authored into the level (see `SetupProps()`), so a monitor could be placed the same
  way; a warp could not, because its target is registered when the level loads.
- **The two object columns do not always carry the same thing on the two sides.** `objx`/`objy` are shared
  between a pushable's position, an ammo count, an animation id and an enemy's position, and which one a
  scenario gets is decided by a list of index ranges in each probe separately. One of this engine's was
  open-ended — `scenario >= 142` — where every one of the original's is bounded, so the four scenarios whose
  entire question is what a kick does to a rock were quietly answering with an ammo count while the original
  answered with the rock. Nothing in a diff of the two says so; the column has a number in it either way.
  Check what each side puts in those two columns before reading a single value out of them.

  Chasing that one down found the same split running the other way and much wider: everything from `sl_rf_up`
  onwards — `142` here, `146` there, two hundred-odd scenarios — got the ammo on this side and the pushable
  on the original's, because the original had no clause for the tail at all. Both now read: the weapon
  sweep and everything after it carry the ammo, except the four `*_side_rock` ones, where the rock *is* the
  measurement. The two probes number scenarios differently, so a range written on one side has to be
  re-derived from the **names** on the other, never offset — 142 here is 146 there, but 383 here is 378.
- **The reset does not restore the facing, so a scenario with no direction key inherits it.** Which is fine
  until the scenarios are reordered. Moving the four `sp_*_side_rock` ones so that the one that shoves the
  rock ran last put it straight after the one that turns the player round to kick leftwards — and with no
  direction of its own it kicked a thousand pixels the *other* way, away from the rock entirely. Worse, it
  did so on both sides at once and therefore in perfect agreement: the totals matched to a pixel and the
  scenario measured nothing whatsoever. A two-tick direction tap at the start fixes the facing and is over
  long before anything else in the scenario begins; every one of the four now carries one, including the
  three that already faced the right way.

  Looking for others found `sp_lori_kick_rep`, which had been doing this since it was written: the original's
  runs after a double-jump scenario that ends with Left held and this engine's does not, so the two sides had
  been kicking in **opposite directions** and comparing the distances. Every sidekick scenario without a
  direction of its own now taps too — `sp_spaz_side`, `sp_lori_side`, both `_rel`, `sp_lori_kick1`,
  `sp_lori_kick_rep`, `sp_lori_side_hold`, `sp_lori_side_fire`. A scenario with no direction key is worth a
  second look whenever one near it is reordered, renumbered or added.
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
