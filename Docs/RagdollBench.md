# Ragdoll bench

The ragdoll bench runs headless and performs scripted shoves and shots on NPCs. It is scored against baselines.

```bash
python3 Tools/RagdollBench/bench.py run --out Private/bench/my_change
```

The benchmark writes `report.md` and `results.json` to the output folder, with one `.trace` file per first-phase trial in `traces/`. The command needs an editor build at `build/RelWithDebInfo/raptor`, or pass `--binary`.

The values to measure against are in `Tools/RagdollBench/baselines/`, with one folder per balance milestone.

## Comparing a change

```bash
python3 Tools/RagdollBench/bench.py compare Tools/RagdollBench/baselines/footslip Private/bench/my_change
```

This prints the fall counts, how many trials changed outcome in each direction and a sign test on those changes.

To try a tuning value without rebuilding, pass `--cvar name=value`. To try a changed ragdoll definition, pass `--ragdoll path/to/file.ragdoll`.

## Viewing the results

```bash
python3 Tools/RagdollBench/bench.py sheet Private/bench/my_change
```

This tool draws a stick representation of an NPC seen side-on and is output as one row per trial. A stick visualization is red if the body is marked as fallen. Name trials after the run folder to choose them, for example `push_v1.00_d090_p0`. Only phase 0 trials have traces.

## Trials

Each suite runs eight push directions, except `chest`, which runs four. `d000` is a push along the way the NPC faces.

| Suite    | What it does                                                    |
| -------- | --------------------------------------------------------------- |
| `push`   | Even shove of the whole body from eight points in the walk clip |
| `still`  | Runs shoves from the bind pose                                  |
| `chest`  | Cracks NPCs with props                                          |
| `weapon` | Pistol, rifle double tap and an 8-round burst to the fellas     |
| `reaction` | Pistol shots with real damage on each body part, from each direction. Also head and body kills. Scored on the whole reaction, including falls |
| `terrain` | Shoves from the walk clip towards a kerb, a drop, stairs or a ramp 0.3 m away, with a flat control. Not part of the default run, so ask for it with `--suite terrain` |
| `perf`   | Eight NPCs on their own for timing. `run` also executes this    |

## Capturing what reaches the screen

The bench steps physics at a fixed rate with nothing drawn and reads the bodies, so it can't see a problem between the physics and the drawn skeleton. The reaction capture plays scripted hits in the configured map through the game's real loop, with a window open, and records the drawn pose of each bone every frame.

```bash
mkdir -p Private/capture/run
```

```bash
DYLD_LIBRARY_PATH=/usr/local/lib build/RelWithDebInfo/raptor -reaction-capture Private/capture/run
```

```bash
python3 Tools/RagdollBench/capture/find_pops.py Private/capture/run/frames.csv
```

`find_pops.py` lists every moment the drawn pose jumps, with the NPC's state at the time. The jump at 26 s is the script replacing the NPC.

| Option | What it does |
| ------ | ------------ |
| `-capture-dt 0.00833` | Fixed time per frame instead of the real clock, which repeats exactly |
| `-capture-images 4 1.95 3.55` | Saves every 4th rendered frame between those times, 900 x 900 raw BGRA |

`frame_sheet.py <run> <out.png> <columns> <crop width> <crop height>` turns saved frames into one contact sheet.
