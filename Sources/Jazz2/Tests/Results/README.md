# Captured runs

Committed so a later change can be checked against the original game's measured behaviour without needing
the original game to hand. Regenerate with `Tools/ExtractTrace.ps1`, compare with `Tools/CompareTraces.ps1`.

**They are gzipped** — about 2.8 MB together instead of 21 MB, and still exact, since a trace is only
useful if it is the real thing. Both tools read `.csv` and `.csv.gz` interchangeably and will accept a path
written as `.csv` when only the `.gz` is present, so nothing needs to know which form is on disk. See
`Tools/TraceIO.ps1`.

| Path | Captured from | Settings |
|---|---|---|
| `original/` | Original Jazz Jackrabbit 2 with JJ2+ Beta v6.6h, driven by `Level/_pt.j2as` | Its own fixed tick rate, measured at 70.021 Hz. 418 scenarios, 275 600 rows across 21 category files. |
| `engine/` | This engine, driven by `PhysicsProbe.cpp` | **`/max-fps:144`**, Debug x64. 423 scenarios, 574 536 rows across 22 category files. |
| `diam3-original.csv.gz`, `diam3-engine.csv.gz` | The same two probes in a **different level** — the original game's own Diamondus 3 — for the single `dm_chain` scenario | Kept apart from the pair above on purpose: a trace holds one level's geometry, and merging two would make the level-version caveat below meaningless. See *Running it in another level* in `../README.md`. |

**Split by category, one file per category on each side.** A full sweep is about eighty minutes a side, so
keeping the whole suite in one file meant adding a single scenario cost both of those. Each category is
under ten minutes, so a change re-runs only what it touched and replaces only those two files. The split
costs nothing in size — 7839 KB across 22 files against about 7843 KB as one, because gzip has as much to
work with either way.

**The trade-off is that a category file reflects whichever build produced it**, so the set as a whole can be
a mix. That is exactly what makes iteration cheap and it is only sound while a change is confined to the
categories re-run with it — a change to something shared, the movement model itself or anything in
`Player.cpp` that is not gated to one mechanic, needs the whole set swept again. When in doubt, sweep. `Tools/Categories.ps1` defines the categories and prints the probe filter that reproduces
one; `../README.md` has the workflow. Both tools take a directory wherever they take a file, and the
comparison joins on the scenario **name**, so it does not matter which file a scenario came out of nor
whether the categories are redrawn later.

Both sides were re-swept whole on 2026-09-18, after the movement rework described in
`Docs/MovementAccuracyReference.dox`. The engine run is the reference for that document's "Now" column.

Six scenarios were added after that sweep — `an_crouch_hold`, `an_crouch_hold_walk`, `an_crouch_carry`,
`an_crouch_spring`, `an_crouch_pole_e` and `an_crouch_spring_l` — and the pole, spring and rev-up families
were re-measured behind the carry-exit clamp and the spring carry's refusal of the crouch. All of them were merged into the affected category file of each
side from their own captures rather than by re-sweeping, the engine's at the same `/max-fps:144` as the sweep
— which is the one thing that has to be kept true when a category is topped up instead of re-run.

**The engine side is captured at 144 FPS, not the 60 this file claimed until 2026-09-17, and the difference
is not cosmetic.** It was found by re-running four categories with `/max-fps:60` as documented and having
eleven scenarios drop out of the 25 px/s band with no physics change capable of reaching them — nine of them
moving from *exactly* 12.5 to *exactly* 29.9, which is a sampling artefact rather than a movement one. The
row density says it plainly: every committed category holds about **2.06 rows per original tick** and a true
60 FPS capture holds **0.86**, and the `ms` column gives 7.00 ms a frame against 16.70. So the whole set,
all twenty-one files, is a 143-144 FPS capture.

Two things follow. **Never mix frame rates within the set** --- a category re-run at a different rate is not
comparable with the rest, and the confound is silent because it moves only the scenarios whose speed
distribution is dominated by standing still. And **the headline scores below are 144 FPS scores**, which the
frame-rate table further down shows is the rate that flatters the engine most (264/280 against 252/280 at
60). Re-basing the set at 60 FPS is the right end state and has not been done, because it rewrites all
twenty-one files and would move every figure the reference page quotes.

**Both were re-swept in full on that date and the reason is worth keeping.** The pair they replace was
assembled over 2026-09-10/11 and predated the `camx`, `camy`, `anim` and `frame` columns entirely — the
original side carried 16 columns with no animation data at all. That is invisible until something asks an
animation question, and when one finally did (a rev-up launch running its whole length in the skid pose)
there was nothing to compare against for any scenario older than the column. Assembling from partial runs
stays sound for physics; it silently freezes the *column set* of whatever was captured first.

**How repeatable this engine is, measured — and it is the number you need before calling anything a
regression.** Two full sweeps of the *same binary*, back to back: **385 of 403 scenarios end within 1 px**,
and 18 move. The tail is what matters, because it is long:

| Scenario | Moves between identical runs by |
|---|---|
| `sl_up45_jhold` | **1474 px** |
| `pb_bump_fall`, `pb_bump_hold_r` | 52 px, 27 px |
| `bl_acc_right_run`, `lh_g1_dash`, `lh_g2_dash`, `sp_dj_a_dashflip`, `tb_gap4`, `an_slide_stop` | 3–7 px |
| eight more | 1–3 px |

Nothing is random here: every scenario is scripted and the clock is the probe's own. What varies is the
settle, which ends on real-time frame pacing and leaves the player a few hundredths of a pixel apart from run
to run — and a few hundredths is enough to decide whether a jump clears a slope's lip, whether a bumper is
struck on its left or its right, or whether a gap is cleared or fallen into. Those scenarios amplify it
without limit; `sl_up45_jhold` turns 0.01 px into 1474.

**So a scenario moving is not evidence of anything on its own.** This was measured because a change that
could only reach three scenarios appeared to move twelve, and there was no way to tell which nine were noise.
Seven of the nine are in the table above. Take this baseline before attributing a movement to a change, and
prefer a scenario's *mechanism* over its end position when the mechanism is what is being tested.

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
Compared 406 scenarios present in both traces.
Median speed-distribution error: 0.1 px/s
Scenarios within 25 px/s: 376 / 406
Scenarios whose animation-segment count differs by over 25%: 70 / 397
```

The scenarios that fall outside that are listed in `../README.md` under *Scenarios that do not measure the
same thing on both sides*. Every one of them has a known cause, and most are not physics differences at all
— but a few are, so read the cause rather than assuming. Check that list before treating a new outlier as a
regression.

The animation line is new with this pair, since it is the first one where both sides carry the column. Its
71 are **not** a list of known causes — nothing has been through them yet. The trajectory is matched far
better than the pose is, which is what you would expect of work that has only ever measured trajectories.
See *Animation structure across the two games* in the gaps table.

## Other frame rates

The committed engine trace is the 144 FPS one (see above), and the whole set is re-run at 24, 30 and 60
whenever the model changes — pass `/max-fps:N` and score each against the same original. Those runs are
deliberately **not** committed: they are a check on the model rather than a reference, and four traces of
the same build would quadruple what the repository carries for no extra information.

Re-measured 2026-09-18, four full sweeps of one build over all 413 scenarios, 406 of which the original side
also carries. The score broadly improves with the rate but not monotonically — 24 edges 30 by two scenarios,
which is inside the chaotic scenarios' own run-to-run swing:

| | 24 FPS | 30 FPS | 60 FPS | 144 FPS |
|---|---|---|---|---|
| Scenarios within 25 px/s of the original | 353 / 406 | 351 / 406 | 364 / 406 | **376 / 406** |
| Median speed-distribution error | 0.1 px/s | 0.1 px/s | 0.1 px/s | 0.1 px/s |

**Run the four in parallel** — measured, they do not interfere. Four instances on an 8-core/16-thread
machine each held their target with no jitter worth the name: over every frame of all four sweeps the median
intervals were 41.70, 33.30, 16.70 and 7.00 ms against nominals of 41.67, 33.33, 16.67 and 6.94, the p99s
41.80, 33.60, 16.80 and 7.30, and the single worst frame of each 41.80, 34.50, 19.20 and 14.00 — the same
figures each produces alone, at 65% CPU. A four-rate validation is
therefore ~70 minutes, not four and a half hours. Measure the rate *within* a scenario, though: `ms` does
not advance across the reset between scenarios, so a naive diff reports the gap as a 300 ms frame.

**Most of that spread is one artefact, and it is not movement.** Pressed against a wall the original reads
`xs` = 0.0000 and this engine reads one acceleration step, which is `accel * timeMult` and therefore
proportional to the frame time: **0.2081 px/frame at 144, 0.4988 at 60, 0.9973 at 30**, or 12.5, 29.9 and
59.8 px/s. Those are exactly the scores ten of the affected scenarios move between, and the player does not
move at all — `wb_wallfind` travels 1221.0 px at 144, 60 and 30 alike. It does not reach the jump launch
boost either: `wb_dash_j164` jumps after the longest press into the wall and launches at −9.9970 at *every*
rate, against the original's −10.0000. So it costs the metric and nothing else. This is the same residual
the camera lead had to be re-keyed around, and the reason the old reading of this table — "a tail that thins
as the frame gets shorter is what sampling error looks like" — described the right shape for the wrong
reason.

**Do the four rates agree with each other?** That is the sharper question, and `Tools/CompareFrameRates.ps1`
answers it by comparing positions on a shared tick grid. Nothing needs converting: `x`/`y` are absolute
pixels and `tick` is in the original's units on every capture. The control is two runs at the *same* rate:

| compared | median worst per-tick difference | agree within 1 px | travel spread > 10 px |
|---|---|---|---|
| **same rate, two runs** | **0.28 px** | **342 / 413** | **2 / 413** |
| 144 vs 60 | 2.71 px | 158 / 413 | 19 / 413 |
| 60 vs 30 | 2.77 px | 154 / 413 | 49 / 413 |
| 30 vs 24 | 3.93 px | 148 / 413 | 69 / 413 |
| 144 vs 24 | 10.06 px | 134 / 413 | 98 / 413 |
| all four at once | 12.08 px | 129 / 413 | 124 / 413 |

**The movement model itself is frame-rate independent** — which is the result worth having, because it is
what the per-tick constants and the velocity-Verlet integration are for. Rise heights across 24/30/60/144:
`ap_r30` 128.0 / 128.1 / 128.0 / 128.0, `a_jump` 128.3 / 128.2 / 128.3 / 128.3, `ob_vpole` 270.4 / 271.1 /
270.7 / 272.5, `ow_hold` 704.0 / 704.3 / 704.1 / 704.1, `sp_spaz_side` 213.6 / 213.4 / 213.4 / 213.3.
Fractions of a pixel over hundreds.

What does not survive falls into three groups, and only the third is a real defect:

1.  **Scenarios that are chaotic at any rate.** `pb_bump_*`, `sl_up45_jhold`, `sp_dj_a_dashflip`, `sp_chain`
    — all of them also move between two runs of the same build, some by over a thousand pixels. The control
    row above is what tells them apart from a regression.
2.  **Scenarios decided by a short input window.** A 24 FPS frame is **2.92 of the original's ticks**, so a
    four-tick window is sampled 1.4 times. `sp_slide_*` reads 3.8 / 9.2 / 18.7 / 0.3 px of rise against the
    original's 9.6 for exactly that reason. This is the probe's scripted input being quantised, not the
    model — but it is also a fair warning that a *player* at 24 FPS cannot hit a four-tick window either.
3.  **One mechanic genuinely sampled too coarsely — a real low-frame-rate gameplay bug, now fixed.**
    `fu_jump_right_rel` climbed 495.0 / 495.2 / 503.4 px at 144/60/30 and **176.6** at 24. The float-up
    area was evaluated once per frame at the position the player ended up in, where the trigger events
    (poles, tubes, warps) are swept along the whole path at half-tile steps. Counting the rows the
    assignment actually fired on settles it: **41, 34, 17 and two**. At 24 FPS a 12 px/tick fall covers
    35 px in a frame, more than a tile, so the tile is never seen. It now walks the same samples.

    Re-swept at all four rates afterwards: 24 FPS goes **176.6 → 513.8 px** with the hit count 2 → 15
    (the original's is 18), `fu_butt` 176.6 → 200.0, and **60 FPS is untouched** — before and after agree
    to 0.16 px median with 394/406 inside a pixel, which is better than the run-to-run noise floor. The
    aggregate cross-rate figure does not move (9.16 → 9.17 px over 60/30/24), and that is the honest
    reading: the fix corrects one family rather than the tail, which is chaotic scenarios and input
    quantisation.

    `ow_jump` (224.5 / 224.7 / 225.6 / **202.3**) looks identical and is *not* the same thing — its
    launches all happen, and only their timing moves, because the jump key is sampled on a frame boundary:
    the original launches at tick 21 and the engine at 20, 22, 23, 23. That is group 2. Worth stating
    because the two are indistinguishable from the summary numbers alone.

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




