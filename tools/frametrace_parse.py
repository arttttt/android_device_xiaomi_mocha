#!/usr/bin/env python3
"""Break the frames captured by frametrace.sh into per-stage timings.

Reads every runNN_*.txt in a directory, finds the transition burst in each
run (the largest group of frames not separated by more than 100 ms of
IntendedVsync), and prints a per-frame stage table plus a cross-run summary:
burst duration spread, skipped ticks, and which stage the frame that caused
each skip spent its time in.

All values ns in the dumps, printed as ms.

Usage: frametrace_parse.py OUTDIR
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

VSYNC_MS = 16.663
GAP_MS = 100.0          # burst boundary
SKIP_MS = 25.0          # IntendedVsync delta that means a skipped tick

# 0-based columns of a ---PROFILEDATA--- row on Android 9.
COLS = dict(flags=0, ivsync=1, vsync=2, hinput=5, anim=6, trav=7, draw=8,
            squeued=9, sstart=10, issue=11, swap=12, done=13, dq=14, qb=15)

# Stage name -> (start column, end column); dq/qb are standalone durations.
STAGES = [
    ("input",  "hinput",  "anim"),
    ("anim",   "anim",    "trav"),
    ("trav",   "trav",    "draw"),
    ("record", "draw",    "squeued"),
    ("syncw",  "squeued", "sstart"),
    ("sync",   "sstart",  "issue"),
    ("issue",  "issue",   "swap"),
    ("swap",   "swap",    "done"),
]


def parse_file(path):
    """Return {window: [row, ...]} for every PROFILEDATA block in the dump."""
    frames = defaultdict(list)
    window = "?"
    in_block = False
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"\s*Window:\s+(.*)", line)
        if m:
            window = m.group(1).strip()
            continue
        if line.strip() == "---PROFILEDATA---":
            in_block = not in_block
            continue
        if not in_block or line.startswith("Flags"):
            continue
        fields = [f for f in line.strip().split(",") if f != ""]
        if len(fields) < 16:
            continue
        try:
            row = [int(f) for f in fields[:16]]
        except ValueError:
            continue
        frames[window].append(row)
    return frames


def bursts(rows):
    """Split rows (sorted by IntendedVsync) into bursts at >GAP_MS gaps."""
    out, cur = [], []
    prev = None
    for r in rows:
        iv = r[COLS["ivsync"]]
        if prev is not None and (iv - prev) / 1e6 > GAP_MS:
            out.append(cur)
            cur = []
        cur.append(r)
        prev = iv
    if cur:
        out.append(cur)
    return out


def stage_ms(row):
    d = {}
    for name, a, b in STAGES:
        d[name] = (row[COLS[b]] - row[COLS[a]]) / 1e6
    d["dq"] = row[COLS["dq"]] / 1e6
    d["qb"] = row[COLS["qb"]] / 1e6
    return d


def analyze_run(tag, files):
    rows_by_window = defaultdict(list)
    for path in files:
        for window, rows in parse_file(path).items():
            key = f"{path.stem.split('_', 1)[1]} / {window}"
            rows_by_window[key].extend(rows)

    best_key, best = None, []
    for key, rows in rows_by_window.items():
        rows.sort(key=lambda r: r[COLS["ivsync"]])
        for b in bursts(rows):
            if len(b) > len(best):
                best_key, best = key, b
    if not best:
        print(f"{tag}: no frames captured")
        return None

    iv0 = best[0][COLS["ivsync"]]
    dur = (best[-1][COLS["done"]] - iv0) / 1e6
    print(f"\n{tag}: {best_key}")
    print(f"  {len(best)} frames, {dur:.1f} ms, "
          f"{(len(best) - 1) / max(dur - VSYNC_MS, 1) * 1000:.1f} fps")

    hdr = ["#", "dIV", "lat"] + [s[0] for s in STAGES] + ["dq", "qb", "fl"]
    print("  " + " ".join(f"{h:>6}" for h in hdr))
    skips = []
    prev_iv = None
    for n, row in enumerate(best):
        iv = row[COLS["ivsync"]]
        div = (iv - prev_iv) / 1e6 if prev_iv is not None else 0.0
        prev_iv = iv
        lat = (row[COLS["done"]] - iv) / 1e6
        st = stage_ms(row)
        mark = ""
        if div > SKIP_MS:
            culprit_st = stage_ms(best[n - 1])
            culprit = max(culprit_st, key=culprit_st.get)
            skips.append((n, div, culprit, culprit_st[culprit]))
            mark = "  <-- skip"
        vals = [f"{n:>6}", f"{div:>6.1f}", f"{lat:>6.1f}"]
        vals += [f"{st[s[0]]:>6.1f}" for s in STAGES]
        vals += [f"{st['dq']:>6.1f}", f"{st['qb']:>6.1f}",
                 f"{row[COLS['flags']]:>6x}"]
        print("  " + " ".join(vals) + mark)

    for n, div, culprit, ms in skips:
        print(f"  skip before frame {n}: dIV {div:.1f} ms; "
              f"previous frame's largest stage: {culprit} {ms:.1f} ms")
    return dur, len(best), skips


def main():
    outdir = Path(sys.argv[1])
    by_run = defaultdict(list)
    for path in sorted(outdir.glob("run*_*.txt")):
        by_run[path.stem.split("_")[0]].append(path)
    if not by_run:
        sys.exit(f"no runNN_*.txt files in {outdir}")

    results = []
    for tag in sorted(by_run):
        r = analyze_run(tag, by_run[tag])
        if r:
            results.append(r)

    if not results:
        return
    durs = [r[0] for r in results]
    mean = sum(durs) / len(durs)
    print(f"\n== summary over {len(results)} runs ==")
    print(f"  duration: min {min(durs):.1f}  mean {mean:.1f}  "
          f"max {max(durs):.1f} ms  spread ±{(max(durs) - min(durs)) / 2 / mean * 100:.1f}%")
    print(f"  frames per burst: {sorted(r[1] for r in results)}")
    all_skips = [s for r in results for s in r[2]]
    print(f"  skipped ticks: {len(all_skips)} in {len(results)} runs")
    hist = defaultdict(int)
    for _, _, culprit, _ in all_skips:
        hist[culprit] += 1
    for culprit, cnt in sorted(hist.items(), key=lambda kv: -kv[1]):
        print(f"    caused by {culprit}: {cnt}")


if __name__ == "__main__":
    main()
