"""Cry feature pipeline. cry/cry_dsp.h on the device mirrors this file exactly.

Everything runs at 8 kHz. The Donate-a-Cry clips are 8 kHz, and the device
decimates its 16 kHz microphone stream before anything else, so both sides
compute the fingerprint from the same kind of signal.

The constants below are written into cry/baseline_cries.h by build_baseline.py,
so the device never carries its own copy. Bump FEATURE_VERSION whenever a
change here alters the fingerprint: the device then drops saved cries that
were made with the old one.
"""
import math

import numpy as np

FEATURE_VERSION = 2

FS_IN = 16000                   # device microphone
FS = 8000                       # pipeline rate
FRAME = 256                     # 32 ms, frames do not overlap
N_BANDS = 16
BAND_LO_HZ = 100.0
BAND_HI_HZ = 3800.0

DECIM_TAPS = 31
DECIM_CUTOFF_HZ = 3600.0

F0_MIN_HZ = 250.0
F0_MAX_HZ = 700.0
LAG_MIN = int(math.floor(FS / F0_MAX_HZ))   # 11
LAG_MAX = int(math.ceil(FS / F0_MIN_HZ))    # 32
OCTAVE_TOL = 0.9                # shortest lag whose peak is within 90 % of the best

# Cry detector
LOUD_DB = 15.0                  # a frame must be this far above the noise floor
FLOOR_RISE_DB = 0.01            # per frame (about 0.3 dB/s); the floor falls at once
FLOOR_INIT_PERCENTILE = 10.0    # clips only: the device starts from its first second
VOICING_MIN = 0.6
FLATNESS_MAX = 0.3

# A cry episode starts at the first cry-like frame and survives pauses. It gives
# a guess as soon as it holds EPISODE_CRY_FRAMES cry-like frames, or, when the
# baby falls silent for EPISODE_GAP_FRAMES, if it held at least
# EPISODE_MIN_CRY_FRAMES. The fingerprint covers the episode up to its last
# cry-like frame, at most EPISODE_MAX_FRAMES long.
EPISODE_CRY_FRAMES = 47         # 1.5 s of crying, pauses not counted
EPISODE_MIN_CRY_FRAMES = 16     # 0.5 s
EPISODE_GAP_FRAMES = 94         # 3 s of silence ends it
EPISODE_MAX_FRAMES = 188        # 6 s
BURST_MIN_FRAMES = 4            # cry-like frames in a burst (128 ms)
BURST_GAP_FRAMES = 2            # a burst survives this many non-cry frames

# Synthetic cry for the parity check (cry_selftest on the board, check_parity.py)
SELFTEST_SECONDS = 4
SELFTEST_F0_HZ = 450.0
SELFTEST_VIBRATO_HZ = 40.0      # depth
SELFTEST_VIBRATO_RATE_HZ = 5.0
SELFTEST_PERIOD_S = 1.1         # one burst and the pause after it
SELFTEST_ON_S = 0.8
SELFTEST_HARMONICS = 6
SELFTEST_AMP = 6000.0
SELFTEST_NOISE = 60.0
SELFTEST_SEED = 0x12345678

N_FEATURES = 2 * N_BANDS + 6
FEATURE_NAMES = (
    [f"band{b}_mean" for b in range(N_BANDS)]
    + [f"band{b}_std" for b in range(N_BANDS)]
    + ["log2_f0_mean", "log2_f0_std", "cry_fraction", "bursts_per_s", "burst_len_s", "centroid_khz"]
)

EPS = 1e-12


def decimator_taps():
    """Windowed-sinc (Hamming) low-pass for 16 kHz -> 8 kHz, unity DC gain."""
    m = DECIM_TAPS - 1
    fc = DECIM_CUTOFF_HZ / FS_IN
    n = np.arange(DECIM_TAPS) - m / 2
    h = 2 * fc * np.sinc(2 * fc * n) * (0.54 - 0.46 * np.cos(2 * np.pi * np.arange(DECIM_TAPS) / m))
    return h / h.sum()


def decimate(x16):
    """y[n] = sum_k h[k] x[n-k] with zero history, keeping n = 0, 2, 4, ..."""
    y = np.convolve(np.asarray(x16, dtype=np.float64), decimator_taps())[: len(x16)]
    return y[::2]


def band_bins():
    """Per band, the FFT bins [start, end) whose centre lies in that band."""
    start = np.zeros(N_BANDS, dtype=int)
    end = np.zeros(N_BANDS, dtype=int)
    span = math.log(BAND_HI_HZ / BAND_LO_HZ)
    owner = []
    for k in range(FRAME // 2 + 1):
        f = k * FS / FRAME
        owner.append(int(N_BANDS * math.log(f / BAND_LO_HZ) / span) if BAND_LO_HZ <= f < BAND_HI_HZ else -1)
    for b in range(N_BANDS):
        ks = [k for k, o in enumerate(owner) if o == b]
        if not ks:
            raise ValueError(f"band {b} has no FFT bin")
        start[b], end[b] = ks[0], ks[-1] + 1
    return start, end


_HANN = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(FRAME) / FRAME)   # periodic
_BAND_START, _BAND_END = band_bins()
_BIN_HZ = np.arange(FRAME // 2 + 1) * FS / FRAME


def frame_features(frames):
    """Features of each row of `frames` (n x FRAME, int16 scale).

    Returns a dict of arrays with one entry per frame: db, bands (n x N_BANDS,
    dB relative to the frame's own band total), centroid_hz, flatness, f0_hz
    (0 when unvoiced), voicing.
    """
    x = np.asarray(frames, dtype=np.float64) / 32768.0
    n = len(x)
    db = 10 * np.log10(np.mean(x * x, axis=1) + EPS)

    p = np.abs(np.fft.rfft(x * _HANN, axis=1)) ** 2
    bp = np.stack([p[:, s:e].sum(axis=1) for s, e in zip(_BAND_START, _BAND_END)], axis=1)
    bands = 10 * np.log10(bp + EPS) - 10 * np.log10(bp.sum(axis=1, keepdims=True) + EPS)

    lo, hi = _BAND_START[0], _BAND_END[-1]
    pr = p[:, lo:hi]
    centroid = (pr * _BIN_HZ[lo:hi]).sum(axis=1) / (pr.sum(axis=1) + EPS)
    flatness = np.exp(np.mean(np.log(pr + EPS), axis=1)) / (np.mean(pr, axis=1) + EPS)

    # Normalised autocorrelation, one lag past each end for the interpolation.
    xc = x - x.mean(axis=1, keepdims=True)
    lags = np.arange(LAG_MIN - 1, LAG_MAX + 2)
    r = np.zeros((n, len(lags)))
    for j, lag in enumerate(lags):
        a, b = xc[:, : FRAME - lag], xc[:, lag:]
        r[:, j] = (a * b).sum(axis=1) / np.sqrt((a * a).sum(axis=1) * (b * b).sum(axis=1) + EPS)

    f0 = np.zeros(n)
    voicing = np.zeros(n)
    for i in range(n):
        f0[i], voicing[i] = _pick_pitch(r[i])
    return {"db": db, "bands": bands, "centroid_hz": centroid, "flatness": flatness, "f0_hz": f0, "voicing": voicing}


def _pick_pitch(r):
    """r[j] is the correlation at lag LAG_MIN - 1 + j. Returns (f0_hz, voicing)."""
    inner = range(1, len(r) - 1)                       # lags LAG_MIN..LAG_MAX
    best = max(r[j] for j in inner)
    if best <= 0:
        return 0.0, 0.0
    pick = None
    for j in inner:                                    # shortest period first
        if r[j] >= OCTAVE_TOL * best and r[j] >= r[j - 1] and r[j] >= r[j + 1]:
            pick = j
            break
    if pick is None:
        pick = max(inner, key=lambda j: r[j])
    y0, y1, y2 = r[pick - 1], r[pick], r[pick + 1]
    denom = y0 - 2 * y1 + y2
    delta = 0.5 * (y0 - y2) / denom if denom < 0 else 0.0
    delta = min(0.5, max(-0.5, delta))
    lag = LAG_MIN - 1 + pick + delta
    return FS / lag, float(y1)


def cry_like(feats, floor_init):
    """Streams the detector over the frames. Returns the cry-like flag per frame."""
    floor = floor_init
    flags = np.zeros(len(feats["db"]), dtype=bool)
    for i in range(len(flags)):
        db = feats["db"][i]
        f0 = feats["f0_hz"][i]
        flags[i] = (
            db >= floor + LOUD_DB
            and feats["voicing"][i] >= VOICING_MIN
            and F0_MIN_HZ <= f0 <= F0_MAX_HZ
            and feats["flatness"][i] <= FLATNESS_MAX
        )
        floor = min(db, floor + FLOOR_RISE_DB)
    return flags


def bursts(flags):
    """Lengths (frames, first to last cry-like frame) of the cry bursts in `flags`."""
    out = []
    start = last = None
    on = 0
    for i, f in enumerate(flags):
        if f:
            if start is None:
                start, on = i, 0
            last = i
            on += 1
        elif start is not None and i - last > BURST_GAP_FRAMES:
            if on >= BURST_MIN_FRAMES:
                out.append(last - start + 1)
            start = None
    if start is not None and on >= BURST_MIN_FRAMES:
        out.append(last - start + 1)
    return out


class Episode:
    """One cry with its pauses, fed one frame at a time (CryEpisode in cry_dsp.h).

    step() and finish() return the (first, last) frames to fingerprint when the
    episode gives a guess, else None.
    """

    def __init__(self):
        self.reset()

    def reset(self):
        self.start = self.last = None
        self.cry = 0

    def _span(self):
        return max(self.start, self.last - EPISODE_MAX_FRAMES + 1), self.last

    def step(self, i, flag):
        if self.start is None:
            if flag:
                self.start = self.last = i
                self.cry = 1
            return None
        if flag:
            self.last = i
            self.cry += 1
            if self.cry >= EPISODE_CRY_FRAMES:
                span = self._span()
                self.reset()
                return span
        elif i - self.last >= EPISODE_GAP_FRAMES:
            span = self._span() if self.cry >= EPISODE_MIN_CRY_FRAMES else None
            self.reset()
            return span
        return None

    def finish(self):
        """The recording ended: treat it like the baby falling silent."""
        span = self._span() if self.start is not None and self.cry >= EPISODE_MIN_CRY_FRAMES else None
        self.reset()
        return span


def fingerprint(feats, flags):
    """The N_FEATURES fingerprint of a run of frames (an episode)."""
    sel = np.asarray(flags, dtype=bool)
    if sel.sum() < 2:
        # Clips only: the device fingerprints an episode, which has cry-like
        # frames, so it never gets here (cry_dsp.h falls back to every frame).
        sel = feats["db"] >= np.median(feats["db"])
    bands = feats["bands"][sel]
    f0 = feats["f0_hz"][sel]
    log_f0 = np.log2(f0[f0 > 0]) if np.any(f0 > 0) else np.array([math.log2(450.0)])
    b = bursts(flags)
    seconds = len(flags) * FRAME / FS
    return np.concatenate([
        bands.mean(axis=0),
        bands.std(axis=0),
        [
            log_f0.mean(),
            log_f0.std(),
            np.mean(flags),
            len(b) / seconds,
            np.mean(b) * FRAME / FS if b else 0.0,
            feats["centroid_hz"][sel].mean() / 1000.0,
        ],
    ])


def percentile(values, q):
    """numpy's default (linear) percentile, written out so the C++ can match it."""
    s = np.sort(values)
    pos = q / 100.0 * (len(s) - 1)
    lo = int(math.floor(pos))
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (pos - lo) * (s[hi] - s[lo])


def selftest_signal():
    """The synthetic cry at 16 kHz: harmonic bursts with vibrato over a little noise.

    crySelftestSample() in cry_dsp.h computes the same samples.
    """
    n = SELFTEST_SECONDS * FS_IN
    out = np.zeros(n)
    state = SELFTEST_SEED
    depth = SELFTEST_VIBRATO_HZ / (2 * math.pi * SELFTEST_VIBRATO_RATE_HZ)
    for i in range(n):
        t = i / FS_IN
        u = math.fmod(t, SELFTEST_PERIOD_S)
        env = math.sin(math.pi * u / SELFTEST_ON_S) if u < SELFTEST_ON_S else 0.0
        phase = 2 * math.pi * (SELFTEST_F0_HZ * t + depth * (1 - math.cos(2 * math.pi * SELFTEST_VIBRATO_RATE_HZ * t)))
        phase = math.fmod(phase, 2 * math.pi)
        tone = sum(math.sin(h * phase) / h for h in range(1, SELFTEST_HARMONICS + 1))
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        noise = ((state & 0xFFFF) / 65535.0 - 0.5) * 2 * SELFTEST_NOISE
        out[i] = SELFTEST_AMP * env * tone + noise
    return out


def clip_fingerprint(x8):
    """Fingerprint of a recorded clip at 8 kHz, as the device would take it.

    The noise floor starts at a low percentile of the clip's own frames, since a
    clip has no minutes of room sound before it. The first episode that gives a
    guess is fingerprinted; the end of the clip counts as the baby falling
    silent. Without one, the EPISODE_MAX_FRAMES run with the most cry-like frames
    is used and `triggered` is False. Returns (fingerprint, triggered, cry_fraction_of_clip).
    """
    x8 = np.asarray(x8, dtype=np.float64)
    n = len(x8) // FRAME
    if n < 1:
        raise ValueError("clip shorter than one frame")
    feats = frame_features(x8[: n * FRAME].reshape(n, FRAME))
    flags = cry_like(feats, percentile(feats["db"], FLOOR_INIT_PERCENTILE))

    def run(first, last):
        sl = slice(first, last + 1)
        return {k: v[sl] for k, v in feats.items()}, flags[sl]

    episode = Episode()
    for i in range(n):
        span = episode.step(i, flags[i])
        if span:
            return fingerprint(*run(*span)), True, float(np.mean(flags))
    span = episode.finish()
    if span:
        return fingerprint(*run(*span)), True, float(np.mean(flags))
    w = min(EPISODE_MAX_FRAMES, n)
    counts = np.convolve(flags.astype(int), np.ones(w, dtype=int), mode="valid")
    first = int(np.argmax(counts))
    return fingerprint(*run(first, first + w - 1)), False, float(np.mean(flags))
