"""Build the Donate-a-Cry baseline for the device and report how good it is.

    python -I tools/baseline/build_baseline.py          (from baby-cry-mvp/)

1. Downloads the cleaned Donate-a-Cry corpus (457 WAVs, 8 kHz) into data/donateacry/.
2. Fingerprints every clip with features.py, the same pipeline the device runs.
3. Grouped cross-validation by app instance (a stand-in for "one baby"), so no
   baby is in both the training and the test part. Reports balanced accuracy,
   the confusion matrix and how many red presses a parent would need.
4. Simulates the buttons: for babies with more than one need, feeds their
   clips in order as if confirmed and checks whether the guesses get better.
5. Writes cry/baseline_cries.h: pipeline constants, normalisation, kernel width
   and per-need prototypes.
"""
import argparse
import concurrent.futures
import datetime
import json
import math
import sys
import urllib.request
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import features as fx  # noqa: E402

PROJECT = Path(__file__).resolve().parents[2]
DEFAULT_DATA = PROJECT / "data" / "donateacry"
DEFAULT_HEADER = PROJECT / "cry" / "baseline_cries.h"

REPO_API = "https://api.github.com/repos/gveres/donateacry-corpus/contents/donateacry_corpus_cleaned_and_updated_data"
FOLDERS = ["hungry", "tired", "discomfort", "belly_pain", "burping"]

# The device's needs, in the design's order.
NEEDS = ["Hungry", "Sleepy", "Diaper", "Discomfort"]
HUNGRY, SLEEPY, DIAPER, DISCOMFORT = range(4)
FOLDER_TO_NEED = {"hungry": HUNGRY, "tired": SLEEPY, "discomfort": DISCOMFORT,
                  "belly_pain": DISCOMFORT, "burping": DISCOMFORT}

# Hand-set, not taken from the corpus (84 % hungry says more about who used the app).
PRIOR = np.array([0.40, 0.25, 0.15, 0.20])
# The whole baseline of a need counts as half of one confirmed cry, so the
# baby's own cries outweigh Donate-a-Cry from the first confirmation on.
BASELINE_WEIGHT = 0.5
PROTOS_PER_NEED = 12
SIGMAS = [0.6, 0.8, 1.0, 1.25, 1.5, 2.0, 3.0]
FOLDS = 5
SEED = 7


# ---------- data ----------

def download(data_dir):
    jobs = []
    for folder in FOLDERS:
        with urllib.request.urlopen(f"{REPO_API}/{folder}", timeout=30) as r:
            listing = json.load(r)
        for item in listing:
            if item["type"] == "file" and item["name"].endswith(".wav"):
                dest = data_dir / folder / item["name"]
                if not (dest.exists() and dest.stat().st_size == item["size"]):
                    jobs.append((item["download_url"], dest))
    if not jobs:
        return
    print(f"downloading {len(jobs)} clips ...")

    def fetch(job):
        url, dest = job
        dest.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=60) as r:
            dest.write_bytes(r.read())

    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        list(pool.map(fetch, jobs))


def parse_name(name):
    """uuid(36)-timestamp-version-gender-age-reason.wav -> (instance, unix seconds)."""
    instance = name[:36].lower()
    ts = int(name[37:].split("-")[0])
    return instance, ts / 1000 if ts > 1e11 else ts


def read_wav(path):
    with wave.open(str(path)) as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, fx.FS):
            raise ValueError(f"{path.name}: expected 8 kHz mono 16-bit")
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)


def load_corpus(data_dir):
    clips = []
    for folder in FOLDERS:
        for path in sorted((data_dir / folder).glob("*.wav")):
            instance, ts = parse_name(path.name)
            fp, triggered, frac = fx.clip_fingerprint(read_wav(path))
            clips.append({"folder": folder, "need": FOLDER_TO_NEED[folder], "instance": instance,
                          "ts": ts, "fp": fp, "triggered": triggered, "cry_fraction": frac})
    return clips


# ---------- model (cryPosterior() in cry/cry_dsp.h mirrors posterior()) ----------

def kmeans(x, k, rng, iters=100):
    centres = [x[rng.integers(len(x))]]
    for _ in range(1, k):
        d = np.min([((x - c) ** 2).sum(axis=1) for c in centres], axis=0)
        centres.append(x[rng.choice(len(x), p=d / d.sum())] if d.sum() > 0 else x[rng.integers(len(x))])
    centres = np.array(centres)
    for _ in range(iters):
        assign = np.argmin(((x[:, None, :] - centres[None]) ** 2).sum(axis=2), axis=1)
        moved = np.array([x[assign == j].mean(axis=0) if np.any(assign == j) else centres[j] for j in range(k)])
        if np.allclose(moved, centres):
            break
        centres = moved
    return centres


def kernel(z, points, sigma):
    return np.exp(-((points - z) ** 2).sum(axis=-1) / (2 * sigma * sigma * fx.N_FEATURES))


class Model:
    def __init__(self, fps, needs, sigma):
        self.mean = fps.mean(axis=0)
        std = fps.std(axis=0)
        self.std = np.where(std > 1e-9, std, 1.0)
        z = self.z(fps)
        rng = np.random.default_rng(SEED)
        protos, labels = [], []
        for c in range(len(NEEDS)):
            zc = z[needs == c]
            if len(zc):
                p = kmeans(zc, min(PROTOS_PER_NEED, len(zc)), rng)
                protos.append(p)
                labels += [c] * len(p)
        self.protos = np.concatenate(protos)
        self.proto_need = np.array(labels)
        self.sigma = sigma
        # A need with no data at all gets the likelihood of a typical cry.
        self.flat = 1.0
        self.flat = float(np.median([self.base(zi)[self.has_base].mean() for zi in z]))

    @property
    def has_base(self):
        return np.isin(np.arange(len(NEEDS)), self.proto_need)

    def z(self, fp):
        return (fp - self.mean) / self.std

    def base(self, z):
        out = np.full(len(NEEDS), self.flat)
        k = kernel(z, self.protos, self.sigma)
        for c in range(len(NEEDS)):
            if np.any(self.proto_need == c):
                out[c] = k[self.proto_need == c].mean()
        return out

    def posterior(self, z, baby=()):
        """baby: (z, need) pairs the parent confirmed with the green button."""
        num = BASELINE_WEIGHT * self.base(z)
        den = np.full(len(NEEDS), BASELINE_WEIGHT)
        for bz, c in baby:
            num[c] += kernel(z, bz, self.sigma)
            den[c] += 1
        p = PRIOR * num / den
        return p / p.sum()


def rank_of(p, truth):
    """1 = first guess right; r = r - 1 red presses before the right one."""
    return int(np.sum(p > p[truth])) + 1


def group_folds(groups, k, seed):
    uniq = sorted(set(groups))
    np.random.default_rng(seed).shuffle(uniq)
    fold = {g: i % k for i, g in enumerate(uniq)}
    return np.array([fold[g] for g in groups])


def evaluate(fps, needs, groups, sigma, k=FOLDS, seed=SEED):
    """Grouped CV with a fixed sigma. Returns the posteriors in clip order."""
    folds = group_folds(groups, k, seed)
    post = np.zeros((len(needs), len(NEEDS)))
    for f in range(k):
        train, test = folds != f, folds == f
        m = Model(fps[train], needs[train], sigma)
        for i in np.flatnonzero(test):
            post[i] = m.posterior(m.z(fps[i]))
    return post


def balanced_accuracy(post, needs):
    pred = post.argmax(axis=1)
    return float(np.mean([np.mean(pred[needs == c] == c) for c in np.unique(needs)]))


def nested_cv(fps, needs, groups):
    """Outer folds for the report, inner folds choose sigma: the numbers are not tuned on the test part."""
    folds = group_folds(groups, FOLDS, SEED)
    post = np.zeros((len(needs), len(NEEDS)))
    chosen = []
    for f in range(FOLDS):
        train, test = folds != f, folds == f
        scores = [balanced_accuracy(evaluate(fps[train], needs[train], groups[train], s, FOLDS - 1, SEED + f),
                                    needs[train]) for s in SIGMAS]
        sigma = SIGMAS[int(np.argmax(scores))]
        chosen.append(sigma)
        m = Model(fps[train], needs[train], sigma)
        for i in np.flatnonzero(test):
            post[i] = m.posterior(m.z(fps[i]))
    return post, chosen


def simulate_babies(clips, fps, needs, groups, sigma):
    """Leave one baby out, then confirm its cries in time order with the buttons."""
    by_baby = {}
    for i, c in enumerate(clips):
        by_baby.setdefault(c["instance"], []).append(i)
    babies = [ix for ix in by_baby.values() if len({needs[i] for i in ix}) > 1]
    learned, frozen = [], []         # (position, rank)
    for ix in babies:
        others = groups != clips[ix[0]]["instance"]
        m = Model(fps[others], needs[others], sigma)
        memory = []
        for pos, i in enumerate(sorted(ix, key=lambda i: clips[i]["ts"])):
            z = m.z(fps[i])
            learned.append((pos, rank_of(m.posterior(z, memory), needs[i])))
            frozen.append((pos, rank_of(m.posterior(z), needs[i])))
            memory.append((z, needs[i]))
    return babies, np.array(learned), np.array(frozen)


# ---------- report ----------

def report_cv(post, needs, title):
    pred = post.argmax(axis=1)
    ranks = np.array([rank_of(p, t) for p, t in zip(post, needs)])
    present = [c for c in range(len(NEEDS)) if np.any(needs == c)]
    lines = [title,
             f"  balanced accuracy {balanced_accuracy(post, needs):.1%}   accuracy {np.mean(pred == needs):.1%}"
             f"   mean red presses {np.mean(ranks - 1):.2f}"]
    lines.append("  true \\ guess   " + " ".join(f"{n[:10]:>10}" for n in NEEDS))
    for c in present:
        row = [np.sum((needs == c) & (pred == g)) for g in range(len(NEEDS))]
        lines.append(f"  {NEEDS[c]:<14} " + " ".join(f"{v:>10}" for v in row)
                     + f"   recall {np.mean(pred[needs == c] == c):.0%}")
    return lines


def prior_only(needs):
    return np.tile(PRIOR, (len(needs), 1))


def by_position(sim, cap=4):
    out = []
    for p in range(cap):
        r = sim[sim[:, 0] >= p][:, 1] if p == cap - 1 else sim[sim[:, 0] == p][:, 1]
        label = f"cry {p + 1}" + ("+" if p == cap - 1 else "")
        out.append(f"{label}: {np.mean(r == 1):.0%} (n={len(r)})" if len(r) else f"{label}: -")
    return "   ".join(out)


# ---------- header ----------

def c_floats(values, per_line=6):
    vals = [f"{v:.9g}f" for v in np.ravel(values)]
    return ",\n".join("    " + ", ".join(vals[i:i + per_line]) for i in range(0, len(vals), per_line))


def c_ints(values):
    return ", ".join(str(int(v)) for v in values)


def write_header(path, model, report):
    start, end = fx.band_bins()
    today = datetime.date.today().isoformat()
    comment = "\n".join("// " + line if line else "//" for line in report)
    text = f"""// Generated by tools/baseline/build_baseline.py on {today}. Do not edit by hand:
// change tools/baseline/features.py or build_baseline.py and run it again.
//
// Baseline built from the Donate-a-Cry corpus, cleaned and updated set:
// https://github.com/gveres/donateacry-corpus (Open Database License 1.0,
// contents under the Database Contents License 1.0). The prototypes below are
// derived from it.
//
{comment}

#pragma once
#include <stdint.h>

// ---- pipeline (tools/baseline/features.py) ----
#define CRY_FEATURE_VERSION {fx.FEATURE_VERSION}
#define CRY_N_FEATURES {fx.N_FEATURES}
#define CRY_FS_IN {fx.FS_IN}
#define CRY_FS {fx.FS}
#define CRY_FRAME {fx.FRAME}
#define CRY_N_BANDS {fx.N_BANDS}
#define CRY_LAG_MIN {fx.LAG_MIN}
#define CRY_LAG_MAX {fx.LAG_MAX}
#define CRY_F0_MIN_HZ {fx.F0_MIN_HZ}f
#define CRY_F0_MAX_HZ {fx.F0_MAX_HZ}f
#define CRY_OCTAVE_TOL {fx.OCTAVE_TOL}f
#define CRY_LOUD_DB {fx.LOUD_DB}f
#define CRY_FLOOR_RISE_DB {fx.FLOOR_RISE_DB}f
#define CRY_FLOOR_INIT_PERCENTILE {fx.FLOOR_INIT_PERCENTILE}f
#define CRY_VOICING_MIN {fx.VOICING_MIN}f
#define CRY_FLATNESS_MAX {fx.FLATNESS_MAX}f
#define CRY_WINDOW_FRAMES {fx.WINDOW_FRAMES}
#define CRY_EPISODE_FRACTION {fx.EPISODE_FRACTION}f
#define CRY_EPISODE_MIN_BURSTS {fx.EPISODE_MIN_BURSTS}
#define CRY_BURST_MIN_FRAMES {fx.BURST_MIN_FRAMES}
#define CRY_BURST_GAP_FRAMES {fx.BURST_GAP_FRAMES}
#define CRY_DECIM_TAPS {fx.DECIM_TAPS}

#define CRY_SELFTEST_SECONDS {fx.SELFTEST_SECONDS}
#define CRY_SELFTEST_F0_HZ {fx.SELFTEST_F0_HZ}
#define CRY_SELFTEST_VIBRATO_HZ {fx.SELFTEST_VIBRATO_HZ}
#define CRY_SELFTEST_VIBRATO_RATE_HZ {fx.SELFTEST_VIBRATO_RATE_HZ}
#define CRY_SELFTEST_PERIOD_S {fx.SELFTEST_PERIOD_S}
#define CRY_SELFTEST_ON_S {fx.SELFTEST_ON_S}
#define CRY_SELFTEST_HARMONICS {fx.SELFTEST_HARMONICS}
#define CRY_SELFTEST_AMP {fx.SELFTEST_AMP}f
#define CRY_SELFTEST_NOISE {fx.SELFTEST_NOISE}f
#define CRY_SELFTEST_SEED {fx.SELFTEST_SEED:#x}u

static const float CRY_DECIM_COEFFS[CRY_DECIM_TAPS] = {{
{c_floats(fx.decimator_taps())}
}};

// FFT bins [start, end) of each band.
static const uint8_t CRY_BAND_START[CRY_N_BANDS] = {{{c_ints(start)}}};
static const uint8_t CRY_BAND_END[CRY_N_BANDS] = {{{c_ints(end)}}};

// ---- classifier (tools/baseline/build_baseline.py) ----
#define CRY_N_NEEDS {len(NEEDS)}
enum CryNeed : uint8_t {{ {", ".join(f"NEED_{n.upper()} = {i}" for i, n in enumerate(NEEDS))} }};
static const float CRY_PRIOR[CRY_N_NEEDS] = {{{", ".join(f"{p:.9g}f" for p in PRIOR)}}};
#define CRY_BASELINE_WEIGHT {BASELINE_WEIGHT}f
#define CRY_KERNEL_SIGMA {model.sigma:.9g}f
#define CRY_FLAT_LIKELIHOOD {model.flat:.9g}f

// z = (fingerprint - mean) / std
static const float CRY_FEAT_MEAN[CRY_N_FEATURES] = {{
{c_floats(model.mean)}
}};
static const float CRY_FEAT_STD[CRY_N_FEATURES] = {{
{c_floats(model.std)}
}};

// Prototypes, already z-scored. A need with none uses CRY_FLAT_LIKELIHOOD.
#define CRY_N_PROTOS {len(model.protos)}
static const uint8_t CRY_PROTO_NEED[CRY_N_PROTOS] = {{{c_ints(model.proto_need)}}};
static const float CRY_PROTOS[CRY_N_PROTOS][CRY_N_FEATURES] = {{
""" + ",\n".join("  {\n" + c_floats(p) + "\n  }" for p in model.protos) + "\n};\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=DEFAULT_DATA)
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    ap.add_argument("--offline", action="store_true", help="use the clips already in --data")
    args = ap.parse_args()

    if not args.offline:
        download(args.data)
    clips = load_corpus(args.data)
    fps = np.array([c["fp"] for c in clips])
    needs = np.array([c["need"] for c in clips])
    groups = np.array([c["instance"] for c in clips])
    trig = np.array([c["triggered"] for c in clips])

    report = [f"Corpus: {len(clips)} clips from {len(set(groups))} app instances."]
    for folder in FOLDERS:
        n = sum(c["folder"] == folder for c in clips)
        report.append(f"  {folder:<11} -> {NEEDS[FOLDER_TO_NEED[folder]]:<10} {n:>4}")
    report.append("")
    report.append(f"Cry detector, every clip is a cry: fired on {np.mean(trig):.1%} "
                  f"({trig.sum()}/{len(trig)}); cry-like frames per clip, median "
                  f"{np.median([c['cry_fraction'] for c in clips]):.0%}.")
    for c in sorted(set(needs)):
        report.append(f"  {NEEDS[c]:<10} fired on {np.mean(trig[needs == c]):.0%}")
    report.append("")

    post, chosen = nested_cv(fps, needs, groups)
    report += report_cv(post, needs, f"Baseline, nested grouped {FOLDS}-fold CV (sigma per fold: {chosen}):")
    report.append("")
    report += report_cv(prior_only(needs), needs, "Prior only (no audio), for comparison:")
    report.append("")

    scores = {s: balanced_accuracy(evaluate(fps, needs, groups, s), needs) for s in SIGMAS}
    sigma = max(scores, key=scores.get)
    report.append("Kernel width, grouped CV balanced accuracy: "
                  + ", ".join(f"{s}: {v:.1%}" for s, v in scores.items()) + f"  -> {sigma}")
    report.append("")

    babies, learned, frozen = simulate_babies(clips, fps, needs, groups, sigma)
    report.append(f"Buttons, simulated on {len(babies)} babies with more than one need ({len(learned)} cries),")
    report.append("each left out of the baseline, cries confirmed in time order:")
    report.append(f"  first guess right   baseline only {np.mean(frozen[:, 1] == 1):.0%}"
                  f"   with the buttons {np.mean(learned[:, 1] == 1):.0%}")
    report.append(f"  mean red presses    baseline only {np.mean(frozen[:, 1] - 1):.2f}"
                  f"   with the buttons {np.mean(learned[:, 1] - 1):.2f}")
    report.append(f"  first guess right by position, with the buttons: {by_position(learned)}")

    print("\n".join(report))
    model = Model(fps, needs, sigma)
    write_header(args.header, model, report)
    print(f"\nwrote {args.header} ({len(model.protos)} prototypes, sigma {sigma}, flat {model.flat:.4g})")


if __name__ == "__main__":
    main()
