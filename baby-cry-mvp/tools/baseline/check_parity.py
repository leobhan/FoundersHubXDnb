"""Check that the board's C++ computes what features.py computes.

    python -I tools/baseline/check_parity.py            (from baby-cry-mvp/)
    python -I tools/baseline/check_parity.py --device

The default builds cry/cry_dsp.h for the laptop (clang++ or g++) and runs the
synthetic cry and every Donate-a-Cry clip through both sides, plus the guess
with no saved cries. --device asks the board for the synthetic cry instead
(action cry_selftest), which also covers its compiler and float maths.

Differences are in standard deviations of each feature. Exits non-zero when
the two sides disagree.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_baseline as bb  # noqa: E402
import features as fx  # noqa: E402

HERE = Path(__file__).resolve().parent
TOL_Z = 0.01          # synthetic cry, and clips whose window matches
CLIP_SHARE = 0.98     # share of clips that must match within TOL_Z


def header_sigma():
    text = bb.DEFAULT_HEADER.read_text(encoding="utf-8")
    return float(re.search(r"#define CRY_KERNEL_SIGMA ([0-9.eE+-]+)f", text).group(1))


def build_native(out_dir):
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        sys.exit("no C++ compiler found (set CXX)")
    exe = Path(out_dir) / ("parity_native.exe" if os.name == "nt" else "parity_native")
    subprocess.run([cxx, "-O2", "-std=c++17", str(HERE / "parity_native.cpp"), "-o", str(exe)], check=True)
    return exe


def zdiff(a, b, std):
    return float(np.max(np.abs(np.asarray(a) - np.asarray(b)) / std))


def check_selftest(other, std, label):
    fp, triggered, _ = fx.clip_fingerprint(fx.decimate(fx.selftest_signal()))
    d = zdiff(fp, other["fp"], std)
    same = (other["result"] == 1) == triggered
    print(f"synthetic cry, {label}: detector {'agrees' if same else 'DISAGREES'}, largest difference {d:.2g} sd")
    return same and d < TOL_Z


def check_clips(exe, data_dir, clips, std):
    # Same order as bb.load_corpus(), which made `clips`.
    paths = [p for folder in bb.FOLDERS for p in sorted((data_dir / folder).glob("*.wav"))]
    if not paths:
        print(f"no clips in {data_dir}: run build_baseline.py first")
        return False
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(f"{p}\t{c['need']}" for p, c in zip(paths, clips)))
        listing = f.name
    try:
        out = subprocess.run([str(exe), "clips", listing], check=True, capture_output=True, text=True).stdout
    finally:
        os.unlink(listing)
    native = [json.loads(line) for line in out.splitlines()]

    fps = np.array([c["fp"] for c in clips])
    model = bb.Model(fps, np.array([c["need"] for c in clips]), header_sigma())
    trig_same, close, post_diff, worst = 0, 0, 0.0, 0.0
    for i, (c, n) in enumerate(zip(clips, native)):
        same = (n["result"] == 1) == c["triggered"]
        trig_same += same
        d = zdiff(c["fp"], n["fp"], std)
        worst = max(worst, d)
        close += same and d < TOL_Z
        # The guess from the board's own fingerprints, alone and with the 3 clips before saved.
        z = model.z(np.array(n["fp"]))
        baby = [(model.z(np.array(native[j]["fp"])), clips[j]["need"]) for j in range(max(0, i - 3), i)]
        post_diff = max(post_diff, float(np.max(np.abs(model.posterior(z) - n["post"]))),
                        float(np.max(np.abs(model.posterior(z, baby) - n["learned"]))))
    print(f"clips: detector agrees on {trig_same}/{len(clips)}, fingerprint within {TOL_Z} sd on "
          f"{close}/{len(clips)} (largest {worst:.2g} sd), guess differs by at most {post_diff:.2g}")
    return close >= CLIP_SHARE * len(clips) and post_diff < 1e-3


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", action="store_true", help="ask the board on USB instead of building natively")
    ap.add_argument("--data", type=Path, default=bb.DEFAULT_DATA)
    args = ap.parse_args()

    clips = bb.load_corpus(args.data) if args.data.exists() else []
    std = np.array([c["fp"] for c in clips]).std(axis=0) if clips else np.ones(fx.N_FEATURES)
    std = np.where(std > 1e-9, std, 1.0)

    if args.device:
        from atech import Board
        with Board.connect() as board:
            value = board.query("cry_selftest", reply="cry_selftest_result", timeout=10)
        ok = check_selftest(value, std, "board")
    else:
        with tempfile.TemporaryDirectory() as tmp:
            exe = build_native(tmp)
            self_out = subprocess.run([str(exe), "selftest"], check=True, capture_output=True, text=True).stdout
            ok = check_selftest(json.loads(self_out), std, "native")
            ok = check_clips(exe, args.data, clips, std) and ok
    print("parity OK" if ok else "PARITY FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
