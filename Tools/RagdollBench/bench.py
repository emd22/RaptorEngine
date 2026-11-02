#!/usr/bin/env python3
"""Runs the ragdoll bench and reports on it. See Docs/RagdollBench.md.

Standard library only, so it runs on any python3.
"""

import argparse
import hashlib
import json
import math
import os
import struct
import subprocess
import sys
import time
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_BINARY = os.path.join(REPO, "build", "RelWithDebInfo", "raptor")
FALL_REASONS = {0: "none", 1: "tilt", 2: "height", 3: "contact", 4: "unviable"}
SIDEWAYS = {90, 270}
# Width of the bins of measured shove speed (m/s)
SPEED_BIN = 0.25

# Measures how much of the source a result depends on
SOURCE_DIRS = ["Src/Ragdoll", "Src/Test", "Src/NPC.cpp", "Src/NPC.hpp"]
SOURCE_FILES = ["RaptorData/Data/Demo/Models/ragdoll2/humanoid.ragdoll"]


def git(*args):
    result = subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True)
    return result.stdout.strip()


def source_hash():
    digest = hashlib.sha256()
    paths = list(SOURCE_FILES)

    for entry in SOURCE_DIRS:
        full = os.path.join(REPO, entry)

        if os.path.isfile(full):
            paths.append(entry)
            continue

        for root, _, names in os.walk(full):
            paths += [os.path.relpath(os.path.join(root, name), REPO) for name in names]

    for path in sorted(paths):
        with open(os.path.join(REPO, path), "rb") as file:
            digest.update(path.encode())
            digest.update(file.read())

    return digest.hexdigest()[:16]


# Ragdoll definition to test instead of the game's, relative to the repository since the engine can't load absolute paths
RAGDOLL_PATH = None


def launch(binary, suite, out, traces, index, count, cvars, render=False):
    command = [binary, "-ragdoll-bench", suite, "-bench-out", out, "-bench-shard", str(index), str(count)]

    if render:
        command.append("-bench-render")

    if RAGDOLL_PATH is not None:
        command += ["-bench-ragdoll", RAGDOLL_PATH]

    if traces is not None:
        command += ["-bench-traces", traces]

    for cvar in cvars:
        command += ["-bench-cvar", cvar]

    env = dict(os.environ)
    env.setdefault("DYLD_LIBRARY_PATH", "/usr/local/lib")

    log = open(out + ".log", "w")
    return subprocess.Popen(command, cwd=REPO, env=env, stdout=log, stderr=subprocess.STDOUT)


def read_shard(path):
    trials, perf, done = [], [], False

    if not os.path.exists(path):
        return trials, perf, done

    with open(path) as file:
        for line in file:
            record = json.loads(line)

            if "name" in record:
                trials.append(record)
            elif "perf" in record:
                perf.append(record["perf"])
            elif "done" in record:
                done = True

    return trials, perf, done


def wait_for_startup(process, path, timeout=300):
    """Waits until a run has got through engine startup, which is when it writes its first line."""
    start = time.time()

    while process.poll() is None and time.time() - start < timeout:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            return

        time.sleep(0.1)


def run_suite(binary, suite, out_dir, shards, cvars, traces, timeout):
    processes = []
    paths = []

    for index in range(shards):
        path = os.path.join(out_dir, f"{suite}.{index}.jsonl")

        if os.path.exists(path):
            os.remove(path)

        paths.append(path)
        processes.append(launch(binary, suite, path, traces, index, shards, cvars))

        # Metal's shader cache can crash when several first launches of a new binary fill it at once
        if index == 0:
            wait_for_startup(processes[0], path)

    start = time.time()

    for process in processes:
        process.wait(timeout=max(timeout - (time.time() - start), 1))

    trials, perf = [], []
    failed = []

    for index, (path, process) in enumerate(zip(paths, processes)):
        shard_trials, shard_perf, done = read_shard(path)

        # A shard only depends on its own batches, so a crash during startup can be run again without changing anything
        if process.returncode != 0 or not done:
            print(f"Shard {index} failed (exit {process.returncode}), retrying", file=sys.stderr)
            retry = launch(binary, suite, path, traces, index, shards, cvars)
            retry.wait(timeout=timeout)
            shard_trials, shard_perf, done = read_shard(path)

            if retry.returncode != 0 or not done:
                failed.append(f"{path} (exit {retry.returncode}, finished={done}), log in {path}.log")

        trials += shard_trials
        perf += shard_perf

    if failed:
        sys.exit("Bench shards failed:\n  " + "\n  ".join(failed))

    return trials, perf, time.time() - start


def cmd_run(args):
    global RAGDOLL_PATH

    if args.ragdoll is not None:
        RAGDOLL_PATH = os.path.relpath(os.path.abspath(args.ragdoll), REPO)

    os.makedirs(args.out, exist_ok=True)
    traces = os.path.join(args.out, "traces")
    os.makedirs(traces, exist_ok=True)

    meta = {
        "commit": git("rev-parse", "--short", "HEAD"),
        "dirty_files": len([line for line in git("status", "--porcelain").splitlines() if line]),
        "source_hash": source_hash(),
        "cvars": args.cvar,
        "ragdoll": RAGDOLL_PATH,
        "shards": args.shards,
        "suite": args.suite,
        "binary_time": os.path.getmtime(args.binary),
    }

    trials, perf, elapsed = run_suite(args.binary, args.suite, args.out, args.shards, args.cvar, traces, args.timeout)
    meta["elapsed_s"] = round(elapsed, 1)

    # Timings taken next to the other shards aren't worth reporting
    perf = []

    if not args.no_perf:
        _, perf, _ = run_suite(args.binary, "perf", args.out, 1, args.cvar, None, args.timeout)

    trials.sort(key=lambda trial: trial["name"])

    with open(os.path.join(args.out, "results.json"), "w") as file:
        json.dump({"meta": meta, "trials": trials, "perf": perf}, file, indent=0)

    write_report(args.out)
    print(os.path.join(args.out, "report.md"))


def load(run_dir):
    with open(os.path.join(run_dir, "results.json")) as file:
        return json.load(file)


def wilson(hits, total, z=1.96):
    if total == 0:
        return 0.0, 0.0

    p = hits / total
    denominator = 1 + z * z / total
    center = (p + z * z / (2 * total)) / denominator
    spread = z * math.sqrt(p * (1 - p) / total + z * z / (4 * total * total)) / denominator

    return max(center - spread, 0.0), min(center + spread, 1.0)


def percentile(values, fraction):
    if not values:
        return 0.0

    ordered = sorted(values)
    return ordered[min(int(fraction * len(ordered)), len(ordered) - 1)]


def mean(values):
    if not values:
        return 0.0

    return sum(values) / len(values)


def survival(trials):
    survived = sum(1 for trial in trials if not trial["fell"])
    low, high = wilson(survived, len(trials))

    return survived, len(trials), low, high


def survival_cell(trials):
    if not trials:
        return "-"

    survived, total, low, high = survival(trials)
    return f"{100 * survived / total:.0f}% ({100 * low:.0f}-{100 * high:.0f}) n={total}"


def fifty_percent_speed(trials):
    """Measured speed where survival crosses 50%, from a logistic curve fitted to every trial.

    Survival isn't monotonic from one speed to the next, so reading off where it first crosses 50% mostly measures noise.
    """
    points = [(trial["delta_v"], 1 - trial["fell"]) for trial in trials]

    if not points:
        return 0.0

    best_speed, best_likelihood = 0.0, -math.inf

    for speed_step in range(0, 301):
        speed = speed_step * 0.01

        for slope in (1.0, 2.0, 4.0, 8.0, 16.0):
            likelihood = 0.0

            for velocity, survived in points:
                alive = 1 / (1 + math.exp(max(min(slope * (velocity - speed), 30.0), -30.0)))
                likelihood += math.log(max(alive if survived else 1 - alive, 1e-9))

            if likelihood > best_likelihood:
                best_speed, best_likelihood = speed, likelihood

    return best_speed


def evaluate_gates(data):
    trials = data["trials"]
    push = [trial for trial in trials if trial["name"].startswith("push_")]
    moving = [trial for trial in push if float(trial["condition"]) > 0]
    gates = []

    def gate(name, target, value, passed, note=""):
        gates.append({"name": name, "target": target, "value": value, "passed": passed, "note": note})

    def at_speed(speed):
        return [trial for trial in push if float(trial["condition"]) == speed]

    def measured(speed):
        return [t for t in moving if abs(t["delta_v"] - speed) <= SPEED_BIN / 2]

    for speed, target in ((1.0, 0.9), (1.5, 0.5)):
        cell = measured(speed)
        rate = 1 - mean([trial["fell"] for trial in cell])
        gate(f"Survive {speed:.1f} m/s (measured)", f">= {100 * target:.0f}%", f"{100 * rate:.0f}% of {len(cell)}",
             bool(cell) and rate >= target)

    forward = fifty_percent_speed([trial for trial in push if trial["dir"] in (0, 45, 315)])
    sideways = fifty_percent_speed([trial for trial in push if trial["dir"] in SIDEWAYS])
    ratio = sideways / max(forward, 1e-6)
    gate("Sideways vs forward 50% speed", ">= 75%", f"{100 * ratio:.0f}% ({sideways:.2f} / {forward:.2f} m/s)",
         ratio >= 0.75)

    wakes = at_speed(0.0)
    wake_falls = sum(trial["fell"] for trial in wakes)
    gate("Falls with no push", "0", f"{wake_falls}/{len(wakes)}", wake_falls == 0)

    assist = mean([trial["assist_impulse"] for trial in moving])
    gate("Force applied outside the legs", "0 N*s", f"{assist:.0f} N*s per trial", assist == 0.0,
         "force on the pelvis that skips the joints")

    slides = [trial["peak_slide"] for trial in moving if trial["stances"] > 0]
    slide = percentile(slides, 0.5)
    gate("Foot slide per stance", "< 1 cm", f"{100 * slide:.1f} cm median worst stance", slide < 0.01,
         f"p95 {100 * percentile(slides, 0.95):.1f} cm")

    loaded_slides = [trial["peak_loaded_slide"] for trial in moving if trial["stances"] > 0 and "peak_loaded_slide" in trial]

    if loaded_slides:
        loaded_slide = percentile(loaded_slides, 0.5)
        gate("Loaded foot slide per stance", "< 1 cm", f"{100 * loaded_slide:.1f} cm median worst stance",
             loaded_slide < 0.01, f"p95 {100 * percentile(loaded_slides, 0.95):.1f} cm")

    nudges = [trial for trial in moving if trial["delta_v"] < 0.15]
    nudge_steps = mean([trial["steps"] for trial in nudges])
    gate("Steps below 0.15 m/s", "0", f"{nudge_steps:.1f} of {len(nudges)} trials", nudge_steps == 0.0)

    # Only bodies that recovered: a run of quick steps on the way to a fall isn't chatter
    gaps = [trial["min_step_gap"] for trial in moving if trial["min_step_gap"] > 0 and not trial["fell"]]
    gap = percentile(gaps, 0.05)
    gate("Time between steps (p5)", ">= 0.25 s", f"{gap:.2f} s", gap >= 0.25)

    speeds = [trial["peak_body_speed"] for trial in trials if "peak_body_speed" in trial]

    if speeds:
        peak = max(speeds)
        gate("Solver stable (fastest body)", "< 30 m/s", f"{peak:.1f} m/s", peak < 30.0,
             f"p99 {percentile(speeds, 0.99):.1f} m/s")

        trunk_spins = [t["trunk_spin"] for t in moving if "trunk_spin" in t and not t["fell"]]
        head_spins = [t["head_spin"] for t in moving if not t["fell"]]

        if trunk_spins:
            gate("Peak trunk tip rate (survivors)", "info", f"{mean(trunk_spins):.0f} deg/s mean", True)

        gate("Peak head spin (survivors)", "info", f"{mean(head_spins):.0f} deg/s mean", True)

        late = [t for t in moving if "late_trunk_spin" in t and not t["fell"]]

        if late:
            gate("Trunk tip rate after the shove", "info", f"{mean([t['late_trunk_spin'] for t in late]):.1f} deg/s mean", True)
            gate("Head spin after the shove", "info", f"{mean([t['late_head_spin'] for t in late]):.1f} deg/s mean", True)

        bent = [t for t in moving if not t["fell"] and t["peak_tilt"] > 25.0]
        gate("Recoveries bending past 25 deg", "> 0", f"{len(bent)}", len(bent) > 0)

        spins = [trial["foot_spin"] for trial in moving if trial["stances"] > 0]
        gate("Planted foot spin", "info", f"{mean(spins):.0f} deg/s mean", True)

    if data["perf"]:
        costs = [(perf["active_ms"] - perf["idle_ms"]) / max(perf["active_npcs"], 1) for perf in data["perf"]]
        cost = mean(costs)
        gate("Cost per active NPC", "<= 0.15 ms", f"{cost:.3f} ms", cost <= 0.15, "60 Hz frame, 2 substeps, serial run")

    return gates


def write_report(run_dir):
    data = load(run_dir)
    trials = data["trials"]
    meta = data["meta"]
    gates = evaluate_gates(data)

    push = [trial for trial in trials if trial["name"].startswith("push_")]
    still = [trial for trial in trials if trial["name"].startswith("still_")]
    chest = [trial for trial in trials if trial["name"].startswith("chest_")]
    weapons = [trial for trial in trials if trial["condition"] in ("pistol", "rifle2", "auto8")]

    lines = ["# Ragdoll bench report", ""]
    lines.append(f"Commit `{meta['commit']}` with {meta['dirty_files']} changed files, "
                 f"source hash `{meta['source_hash']}`, {len(trials)} trials in {meta['elapsed_s']} s.")

    if meta["cvars"]:
        lines.append(f"CVars: `{' '.join(meta['cvars'])}`")

    lines += ["", "## Gates", "", "| Gate | Target | Value | Pass | Note |", "|---|---|---|---|---|"]

    for gate in gates:
        mark = "yes" if gate["passed"] else "**no**"
        lines.append(f"| {gate['name']} | {gate['target']} | {gate['value']} | {mark} | {gate['note']} |")

    def speed_table(title, rows):
        if not rows:
            return

        speeds = sorted({float(trial["condition"]) for trial in rows})
        lines.extend(["", f"## {title}", "", "Survival with 95% interval.", "",
                      "| Asked | All | Forward | Side | Backward | Steps | Measured | Step length | Step time | Tilt |",
                      "|---|---|---|---|---|---|---|---|---|---|"])

        for speed in speeds:
            cell = [trial for trial in rows if float(trial["condition"]) == speed]
            fore = [trial for trial in cell if trial["dir"] in (0, 45, 315)]
            side = [trial for trial in cell if trial["dir"] in SIDEWAYS]
            back = [trial for trial in cell if trial["dir"] in (135, 180, 225)]
            stepped = [trial for trial in cell if trial["steps"] > 0]

            lines.append(f"| {speed:.2f} | {survival_cell(cell)} | {survival_cell(fore)} | {survival_cell(side)} | "
                         f"{survival_cell(back)} | {mean([t['steps'] for t in cell]):.1f} | "
                         f"{mean([t['delta_v'] for t in cell]):.2f} | "
                         f"{percentile([t['step_length'] for t in stepped], 0.5):.2f} | "
                         f"{percentile([t['step_time'] for t in stepped], 0.5):.2f} | "
                         f"{mean([t['peak_tilt'] for t in cell]):.0f} |")

    speed_table("Pushes from the walk clip", push)
    speed_table("Pushes from a still bind pose", still)
    speed_table("Chest hits from the walk clip (prop path, extra push on the chest)", chest)

    if weapons:
        lines.extend(["", "## Weapons", "", "| Weapon | Survival | Steps | Knock at 1 s | Fall time |", "|---|---|---|---|---|"])

        for weapon in ("pistol", "rifle2", "auto8"):
            cell = [trial for trial in weapons if trial["condition"] == weapon]
            falls = [trial["fall_time"] for trial in cell if trial["fell"]]
            lines.append(f"| {weapon} | {survival_cell(cell)} | {mean([t['steps'] for t in cell]):.1f} | "
                         f"{mean([t['knock10'] for t in cell]):.2f} m | {mean(falls):.2f} s |")

    hits = [trial for trial in trials if trial["name"].startswith("hit_")]

    if hits:
        lines.extend(["", "## Reactions to real hits", "",
                      "Metrics cover the whole reaction while physics drives the body, including falls. Jitter is the "
                      "frame-to-frame change in a joint's spin, summed over the bodies of each group (deg/s). Twitch is "
                      "how many frames any joint jumps more than 300 deg/s. Limit is how far joints go past their limits "
                      "(p95 deg). COM is how far the centre of mass moves along the hit by 0.5 s, and Yaw is how often an "
                      "off-centre hit turns the torso the right way.", "",
                      "| Hit | n | Falls | Steps | Head | Trunk | Arms | Legs | Twitch | Limit | Gap (cm) | COM (m) | Yaw | Slip (cm) |",
                      "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"])

        conditions = sorted({trial["condition"] for trial in hits}) + ["all"]

        for condition in conditions:
            cell = [t for t in hits if t["condition"] == condition or condition == "all"]
            off_centre = [t for t in cell if abs(t["yaw_expected"]) > 0.03]
            yaw = "-"

            if off_centre:
                yaw = f"{100 * mean([t['yaw_response'] * t['yaw_expected'] > 0 for t in off_centre]):.0f}%"

            slips = [t["peak_loaded_slide"] for t in cell if t["stances"] > 0]
            lines.append(f"| {condition} | {len(cell)} | {sum(t['fell'] for t in cell)} | "
                         f"{mean([t['steps'] for t in cell]):.1f} | {mean([t['jitter_head'] for t in cell]):.0f} | "
                         f"{mean([t['jitter_trunk'] for t in cell]):.0f} | {mean([t['jitter_arm'] for t in cell]):.0f} | "
                         f"{mean([t['jitter_leg'] for t in cell]):.0f} | {mean([t['twitch_frames'] for t in cell]):.0f} | "
                         f"{percentile([t['limit_excess'] for t in cell], 0.95):.1f} | "
                         f"{100 * percentile([t['joint_gap'] for t in cell], 0.95):.1f} | "
                         f"{mean([t['com_05'] for t in cell]):.2f} | {yaw} | "
                         f"{100 * percentile(slips, 0.5):.1f} |")

    terrain = [trial for trial in trials if trial["name"].startswith("terrain_")]

    if terrain:
        lines.extend(["", "## Terrain", "",
                      "Shoves towards an edge 0.3 m away. Rise is the pelvis height above its start at the end of balance, "
                      "for survivors. Stuck trials neither fell nor got back to animation.", "",
                      "| Terrain | Survival | Tilt | Height | Contact | Unviable | Steps | Retries | Most retries | Stuck | Air (s) | Rise (m) |",
                      "|---|---|---|---|---|---|---|---|---|---|---|---|"])

        for condition in ("flat", "kerb10", "kerb20", "drop10", "drop20", "stairs_up", "stairs_down", "ramp_up10",
                          "ramp_up20", "ramp_down10", "ramp_down20"):
            cell = [t for t in terrain if t["condition"] == condition]

            if not cell:
                continue

            counts = [sum(t["fall_reason"] == reason for t in cell) for reason in (1, 2, 3, 4)]
            stuck = sum(not t["fell"] and t["return_time"] < 0 for t in cell)
            rise = [t["pelvis_rise"] for t in cell if not t["fell"]]
            lines.append(f"| {condition} | {survival_cell(cell)} | {counts[0]} | {counts[1]} | {counts[2]} | {counts[3]} | "
                         f"{mean([t['steps'] for t in cell]):.1f} | {mean([t['swing_retries'] for t in cell]):.1f} | "
                         f"{max(t['swing_retries'] for t in cell)} | {stuck} | "
                         f"{mean([t['foot_air_time'] for t in cell]):.2f} | {mean(rise):.2f} |")

    moving = [trial for trial in push if float(trial["condition"]) > 0]
    lines.extend(["", "## Fall reasons (pushes)", "", "| Reason | Falls |", "|---|---|"])
    reasons = {}

    for trial in moving:
        if trial["fell"]:
            reasons[trial["fall_reason"]] = reasons.get(trial["fall_reason"], 0) + 1

    for reason, count in sorted(reasons.items(), key=lambda item: -item[1]):
        lines.append(f"| {FALL_REASONS.get(reason, reason)} | {count} |")

    stepped = [trial for trial in moving if trial["steps"] > 0]
    lines.extend(["", "## Motion (pushes that stepped)", "",
                  f"- Stance knee bend: {mean([t['stance_knee'] for t in stepped]):.0f} deg mean; "
                  f"swing knee peak: {mean([t['swing_knee'] for t in stepped]):.0f} deg mean",
                  f"- Foot lift: {100 * mean([t['lift'] for t in stepped]):.0f} cm mean peak",
                  f"- Head spin peak: {mean([t['head_spin'] for t in stepped]):.0f} deg/s mean; "
                  f"hand spin peak: {mean([t['hand_spin'] for t in stepped]):.0f} deg/s mean",
                  f"- Head swings against the chest, at least 6 deg: {mean([t['head_swings'] for t in trials]):.2f}/s mean "
                  f"up to a fall, {mean([t['fall_head_swings'] for t in trials if t['fell']]):.2f}/s after one"])

    with open(os.path.join(run_dir, "report.md"), "w") as file:
        file.write("\n".join(lines) + "\n")


def cmd_report(args):
    write_report(args.run)
    print(os.path.join(args.run, "report.md"))


def cmd_validate(args):
    """Runs a few batches drawn through the real game loop against the same batches without, and compares them."""
    results = {}

    for label, render in (("headless", False), ("rendered", True)):
        path = os.path.join(args.out, f"validate_{label}.jsonl")
        os.makedirs(args.out, exist_ok=True)
        process = launch(args.binary, args.suite, path, None, args.shard, args.of, [], render)

        if process.wait(timeout=args.timeout) != 0:
            sys.exit(f"The {label} run failed, log in {path}.log")

        trials, _, done = read_shard(path)

        if not done:
            sys.exit(f"The {label} run did not finish, log in {path}.log")

        results[label] = {trial["name"]: trial for trial in trials}

    names = sorted(results["headless"])
    same_hash = sum(results["headless"][n]["hash"] == results["rendered"][n]["hash"] for n in names)
    same_fall = sum(results["headless"][n]["fell"] == results["rendered"][n]["fell"] for n in names)

    print(f"{len(names)} trials: {same_hash} identical pose hashes, {same_fall} same fall outcome")

    if same_hash != len(names):
        sys.exit(1)


def cmd_compare(args):
    a, b = load(args.a), load(args.b)
    trials_a = {trial["name"]: trial for trial in a["trials"]}
    trials_b = {trial["name"]: trial for trial in b["trials"]}
    shared = sorted(set(trials_a) & set(trials_b))

    identical = sum(trials_a[name]["hash"] == trials_b[name]["hash"] for name in shared)
    flips_up = [n for n in shared if trials_a[n]["fell"] and not trials_b[n]["fell"]]
    flips_down = [n for n in shared if not trials_a[n]["fell"] and trials_b[n]["fell"]]

    print(f"{len(shared)} shared trials, {identical} identical pose hashes")

    if a["meta"]["shards"] != b["meta"]["shards"]:
        print(f"Shard counts differ ({a['meta']['shards']} vs {b['meta']['shards']}), so pose hashes can't match")

    print(f"A only {len(trials_a) - len(shared)}, B only {len(trials_b) - len(shared)}")
    print(f"Falls A {sum(trials_a[n]['fell'] for n in shared)}, B {sum(trials_b[n]['fell'] for n in shared)}; "
          f"{len(flips_up)} now survive, {len(flips_down)} now fall")

    # Matched trials: only the disagreements carry information, so an exact binomial sign test on them
    changed = len(flips_up) + len(flips_down)

    if changed > 0:
        tail = sum(math.comb(changed, k) for k in range(0, min(len(flips_up), len(flips_down)) + 1)) / 2 ** changed
        print(f"Sign test p = {min(2 * tail, 1.0):.4f}")

    print()
    gates_a = {gate["name"]: gate for gate in evaluate_gates(a)}

    for gate in evaluate_gates(b):
        before = gates_a.get(gate["name"], {"value": "-"})
        print(f"{gate['name']:<32} {before['value']:<32} -> {gate['value']}")

    if args.strict and identical != len(shared):
        sys.exit(1)


def read_trace(path):
    with open(path, "rb") as file:
        data = file.read()

    segments, frames, stride, hit_frame, fall_frame, direction = struct.unpack_from("<6i", data, 0)
    offset = 24
    parents = struct.unpack_from(f"<{segments}i", data, offset)
    offset += 4 * segments
    count = segments * frames * 6
    values = struct.unpack_from(f"<{count}f", data, offset)

    return {
        "segments": segments, "frames": frames, "stride": stride, "hit": hit_frame, "fall": fall_frame,
        "direction": direction, "parents": parents, "values": values,
    }


class Canvas:
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.pixels = bytearray([255]) * (width * height * 3)

    def dot(self, x, y, color, radius=1):
        for py in range(y - radius, y + radius + 1):
            for px in range(x - radius, x + radius + 1):
                if 0 <= px < self.width and 0 <= py < self.height:
                    index = (py * self.width + px) * 3
                    self.pixels[index:index + 3] = bytes(color)

    def line(self, x0, y0, x1, y1, color, radius=1):
        steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1

        for i in range(steps + 1):
            t = i / steps
            self.dot(int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t), color, radius)

    def save(self, path):
        rows = bytearray()

        for y in range(self.height):
            rows.append(0)
            rows += self.pixels[y * self.width * 3:(y + 1) * self.width * 3]

        def chunk(tag, payload):
            body = tag + payload
            return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0)

        with open(path, "wb") as file:
            file.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) +
                       chunk(b"IEND", b""))


def draw_trial_row(canvas, trace, top, cell, seconds, count):
    """One row of stick figures seen side-on to the shove: travel along it is left to right."""
    radians = math.radians(trace["direction"])
    along = (math.sin(radians), math.cos(radians))
    frame_rate = 60 / trace["stride"]
    hit = trace["hit"] // trace["stride"]
    segments = trace["segments"]
    values = trace["values"]
    scale = (cell - 8) / 2.2

    def point(frame, segment, inner):
        base = (frame * segments + segment) * 6 + inner
        return values[base], values[base + 1], values[base + 2]

    origin = point(min(hit, trace["frames"] - 1), 0, 0)
    floor_y = origin[1] - 1.05
    has_children = {parent for parent in trace["parents"] if parent >= 0}

    for column in range(count):
        frame = hit + int(column * seconds * frame_rate)

        if frame >= trace["frames"]:
            break

        left = column * cell
        ground = top + cell - 6
        canvas.line(left + 2, ground, left + cell - 3, ground, (170, 170, 170))

        def project(p):
            x = (p[0] - origin[0]) * along[0] + (p[2] - origin[2]) * along[1]
            return left + cell * 0.3 + x * scale, ground - (p[1] - floor_y) * scale

        # After the fall the figure is drawn in a different colour, so the moment it gave up reads at a glance
        color = (40, 70, 160)

        if trace["fall"] >= 0 and (frame - hit) * trace["stride"] >= trace["fall"]:
            color = (190, 60, 50)

        for segment in range(segments):
            parent = trace["parents"][segment]
            start = project(point(frame, segment, 0))

            if parent >= 0:
                end = project(point(frame, parent, 0))
                canvas.line(start[0], start[1], end[0], end[1], color)

            if segment not in has_children:
                # A limb's end isn't in the trace, so its centre of mass, doubled from the joint, stands in for it
                center = project(point(frame, segment, 3))
                tip = (start[0] + (center[0] - start[0]) * 2, start[1] + (center[1] - start[1]) * 2)
                canvas.line(start[0], start[1], tip[0], tip[1], color)


def cmd_sheet(args):
    names = args.trials

    if not names:
        names = [f"push_v{speed:.2f}_d{direction:03d}_p0" for speed in (0.5, 1.0, 1.5) for direction in (0, 90, 180)]

    traces = []

    for name in names:
        path = os.path.join(args.run, "traces", name + ".trace")

        if os.path.exists(path):
            traces.append((name, read_trace(path)))
        else:
            print(f"No trace for {name}")

    if not traces:
        sys.exit("Nothing to draw")

    cell, count = args.cell, args.columns
    canvas = Canvas(cell * count, cell * len(traces))

    for row, (name, trace) in enumerate(traces):
        draw_trial_row(canvas, trace, row * cell, cell, args.interval, count)
        print(f"row {row}: {name}")

    out = args.out or os.path.join(args.run, "sheet.png")
    canvas.save(out)
    print(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    run = commands.add_parser("run", help="run a suite and write a report")
    run.add_argument("--out", required=True)
    run.add_argument("--suite", default="all", choices=["all", "push", "chest", "still", "weapon", "reaction", "terrain"])
    run.add_argument("--binary", default=DEFAULT_BINARY)
    run.add_argument("--shards", type=int, default=5,
                     help="runs are only bit-identical at the same shard count, so keep it fixed between runs")
    run.add_argument("--cvar", action="append", default=[], help="name=value, may be repeated")
    run.add_argument("--ragdoll", help="ragdoll definition to test instead of the game's")
    run.add_argument("--no-perf", action="store_true", help="skip the serial timing run")
    run.add_argument("--timeout", type=float, default=3600)
    run.set_defaults(func=cmd_run)

    report = commands.add_parser("report", help="rewrite report.md for a finished run")
    report.add_argument("run")
    report.set_defaults(func=cmd_report)

    compare = commands.add_parser("compare", help="compare two finished runs trial by trial")
    compare.add_argument("a")
    compare.add_argument("b")
    compare.add_argument("--strict", action="store_true", help="exit 1 unless every pose hash matches")
    compare.set_defaults(func=cmd_compare)

    validate = commands.add_parser("validate", help="check that skipping the renderer changes nothing")
    validate.add_argument("--out", required=True)
    validate.add_argument("--suite", default="push")
    validate.add_argument("--binary", default=DEFAULT_BINARY)
    validate.add_argument("--shard", type=int, default=0)
    validate.add_argument("--of", type=int, default=11, help="one batch in this many runs")
    validate.add_argument("--timeout", type=float, default=1200)
    validate.set_defaults(func=cmd_validate)

    sheet = commands.add_parser("sheet", help="draw traced trials as a contact sheet PNG")
    sheet.add_argument("run")
    sheet.add_argument("trials", nargs="*", help="trial names, a default set if none are given")
    sheet.add_argument("--out")
    sheet.add_argument("--cell", type=int, default=120)
    sheet.add_argument("--columns", type=int, default=14)
    sheet.add_argument("--interval", type=float, default=0.2, help="seconds between figures")
    sheet.set_defaults(func=cmd_sheet)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
