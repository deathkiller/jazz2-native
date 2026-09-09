# Captured runs

Committed so a later change can be checked against the original game's measured behaviour without needing
the original game to hand. Regenerate with `Tools/ExtractTrace.ps1`, compare with `Tools/CompareTraces.ps1`.

**They are gzipped** — about 2.8 MB together instead of 21 MB, and still exact, since a trace is only
useful if it is the real thing. Both tools read `.csv` and `.csv.gz` interchangeably and will accept a path
written as `.csv` when only the `.gz` is present, so nothing needs to know which form is on disk. See
`Tools/TraceIO.ps1`.

| File | Captured from | Settings |
|---|---|---|
| `original-70hz.csv.gz` | Original Jazz Jackrabbit 2 with JJ2+ Beta v6.6h, driven by `Level/_pt.j2as` | Its own fixed tick rate, measured at 70.021 Hz. 329 scenarios, 204 400 rows. Assembled from several runs: the original once crashed 3 scenarios short of the end, and each batch of new scenarios since has come from a `FirstScenario`-limited run rather than another full sweep, which costs 30 minutes of foreground time. Its behaviour does not vary between runs and the traces are per-scenario independent, so this is sound — and that has now been checked rather than assumed, see below. |
| `engine-60fps.csv.gz` | This engine, driven by `PhysicsProbe.cpp` | `/max-fps:60`, Debug x64. 334 scenarios, 178 611 rows. One sweep, no assembly. |
| `original-70hz-diam3.csv.gz`, `engine-60fps-diam3.csv.gz` | The same two probes in a **different level** — the original game's own Diamondus 3 — for the single `dm_chain` scenario | Kept apart from the pair above on purpose: a trace holds one level's geometry, and merging two would make the level-version caveat below meaningless. 1 scenario, 1100 / 941 rows. See *Running it in another level* in `../README.md`. |

Both were captured on 2026-09-10/11, after the movement rework described in
`Docs/MovementAccuracyReference.dox`. The engine run is the reference for that document's "Now" column.

**How repeatable the original actually is, measured.** Two full runs a day apart were compared row by row
on the physics columns alone — scenario, tick, x, y, xs, ys and the input flags. Of 203 shared scenarios
**194 are byte-identical**; the nine that are not are:

| Scenario | What differs |
|---|---|
| `wd_stand_r` | `y` by a flat 0.125 px for the whole run — a settle difference, `x` identical |
| `ob_hpole_z0` | Starts its fall two ticks out of phase, converges to the same resting place |
| `sp_pole_loop` | Diverges from tick 53 — the chaotic one, already flagged below |
| `cl_dj_walk`, `cl_walk_j` | From tick ~200, where the traverse walks off the end of the ceiling section |
| `wd_stand_l`, `fu_col_hold` | 12 rows each, 0.001 px |
| `sp_dj_flip`, `sp_dj_flip_mid` | The last few ticks of an 800-tick fall into a pit |

Three columns *never* repeat and must be excluded from any such check: `ms` and `gametick` are clocks, and
`objx`/`objy` track whichever object happens to be nearby. Comparing all of them makes a quarter of the
rows look different and says nothing.

**`objx` and `objy` are not at the same index on the two sides.** The engine's header runs
`…,sm,ms,objx,objy,ctrl,…` and the original's `…,sm,gametick,ms,objx,objy` — so `objx` is field 13 here and
field 14 there. Read the header rather than hardcoding an index: the two columns carry different payloads
per scenario group (ammo and weapon for the RF sweeps, the enemy's position for the stomps, the player's
own animation for the tube and pinball ones), and reading the wrong one cost a full round of "the paddle
pose is not being set" when it was being set correctly all along.

**The caveat on assembling a trace from several runs is the level, not the game.** A run is only
interchangeable with another if `Level/_pt.j2l` did not change between them, and it does change — the test
level grows a new section whenever a mechanic needs one. Before merging, diff the level version the older
rows were captured on against the current one and check that nothing moved inside the tiles those scenarios
use. When the spring chain was added at rows 22–34 the diff came back confined to `x ≤ 36` and `x ≥ 57`
above row 21, leaving the `cl_*` ceiling bays and their row-16 floor byte-identical, which is what made
keeping the older rows legitimate. `git cat-file blob :<path>` gets the staged version out — pipe it
through `cmd /c` redirection, because PowerShell re-encodes binary on `>`.

## What the comparison said when these were captured

```
Compared 328 scenarios present in both traces.
Median speed-distribution error: 0.3 px/s
Scenarios within 25 px/s: 286 / 328
```

The scenarios that fall outside that are listed in `../README.md` under *Scenarios that do not measure the
same thing on both sides*. Every one of them has a known cause, and most are not physics differences at all
— but a few are, so read the cause rather than assuming. Check that list before treating a new outlier as a
regression.

## Other frame rates

The committed engine trace is the 60 FPS one, but the whole set is re-run at 24, 30 and 144 whenever the
model changes — pass `/max-fps:N` and score each against the same `original-70hz.csv.gz`. Those runs are
deliberately **not** committed: they are a check on the model rather than a reference, and four traces of
the same build would quadruple what the repository carries for no extra information.

| | 24 FPS | 30 FPS | 60 FPS | 144 FPS |
|---|---|---|---|---|
| Scenarios within 25 px/s | 245 / 280 | 249 / 280 | 252 / 280 | 264 / 280 |
| Median speed-distribution error | 0.3 px/s | 0.3 px/s | 0.3 px/s | 0.3 px/s |

Captured over 280 shared scenarios, **before the pinball family was added** — the denominator is now 328, so
the three non-60 rates are due another pass.

A flat median with a tail that thins as the frame gets shorter is what sampling error looks like. Read the
per-quantity spread in `Docs/MovementAccuracyReference.dox` under *Frame-rate independence* before treating
a low-frame-rate outlier as a bug — the two quantities that do spread (the tap jump and the uppercut) are
the probe's tick-granular **input** being quantised, not movement.

## Re-verifying later

```powershell
cd ../Tools
# capture a fresh engine run first, then:
./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination ../Results/engine-new.csv
./CompareTraces.ps1 -Engine ../Results/engine-new.csv -Original ../Results/original-70hz.csv
```

A change that moves the median much above 0.3 px/s, or drops a scenario out of the 25 px/s band that was
in it, has changed movement — whether or not that was the intent.

One caveat on the metric: the pole scenarios are the family it is **not** a good guard for. A pole scenario
spends ~140 ticks frozen at zero speed, and those zeros dominate the distribution. `ob_spring_vpole` scored
a **perfect 0.0 while its chain was 265 px short** — a third of the height missing, invisible — and
`sp_pole_loop` currently scores **2.3 while its rise is 1061 px against the original's 592**, because the
launch speeds are identical and only the phase differs. Read the launch speed and the rise directly for
anything involving a pole, and compare a chain launch by launch rather than by its total height.

More generally: a clean sweep means *nothing changed*, not *nothing is wrong*. Most of the bugs found in
this work were invisible to a passing sweep — a spring that only misbehaves when a flag no scenario set is
set, a stomp descent that only runs away from a drop longer than any scenario used, a bounce off an enemy
no scenario contained, a horizontal pole that sticks for ever at an entry speed nothing built out of props
could produce (a prop scenario always approaches a pole by moving towards it), a horizontal spring that
yanks the player 10 px down but only when something *falls* onto one, a float-up area lifting a third of
what it should, and wind, belts and the slide tile — the last of which was **not implemented at all**, its
event converted and then read by nothing. None of those four families had a single scenario until they were
asked about. When adding a mechanic, add the scenario that would fail without it; and when a mechanic exists
in the enum, check that something actually reads it.

## Did anything change at all?

A different and often more useful question, and `-Baseline` is the mode for it:

```powershell
cd ../Tools
./ExtractTrace.ps1 -Path ../../../../x64/Debug/test.log -Destination fresh.csv.gz
./CompareTraces.ps1 -Engine fresh.csv.gz -Original ../Results/engine-60fps.csv.gz -Baseline
```

Both sides are then converted at 60 Hz rather than one of them at the original's 70.021, and because two
engine runs tick in step it also compares the **positions** row by row, which the cross-game metric cannot.

**The speed side is the one to read**, with one exception. Two runs of the same build give a
speed-distribution error of 0 on almost every scenario, so a non-zero figure is nearly always a real change
— but **a scenario whose outcome hinges on a knife edge can flip on the settle alone**. `lh_g3_walk` walks a
3-tile gap at exactly the walk cap: two runs of the same build began 0.044 px apart in `y`, one fell in and
the other cleared, and the speed distribution came out **265 px/s** apart. So check the start position
before believing a large single-scenario change, and be suspicious of any scenario whose measurement is a
binary outcome rather than a trajectory. The position side is far noisier than it
looks: the settle before each scenario ends on real-time frame pacing, so the player starts a few
hundredths of a pixel — occasionally a few *whole* pixels — from where they started last time, and every
row after that carries the offset. Median per-tick difference across a run is about 0.05 px in X and
0.17 px in Y, but well over a tenth of the scenarios exceed 1 px, and one that has to *arrive* somewhere
amplifies it without bound: a horizontal pole grabbed one tick earlier flings the player 400 px further
along at identical speeds throughout. So read the position columns as "which scenarios to look at", never
as a verdict, and never compare maxima or line counts.
