// Cry DSP: from microphone samples to a fingerprint, and from a fingerprint to
// a guess. Plain C++ with nothing from Arduino, so tools/baseline/parity_native.cpp
// runs this exact code on the laptop.
//
// It mirrors tools/baseline/features.py and Model.posterior() in
// tools/baseline/build_baseline.py. Every constant comes from baseline_cries.h,
// which that script generates; change them there, not here.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "baseline_cries.h"

#define CRY_PI 3.14159265358979323846
#define CRY_EPS 1e-12f
#define CRY_N_LAGS (CRY_LAG_MAX - CRY_LAG_MIN + 3)   // one lag past each end

struct CryFrame {
  float db;                    // frame energy, dB re full scale
  float bands[CRY_N_BANDS];    // dB relative to the frame's band total
  float centroid;              // Hz
  float flatness;
  float f0;                    // Hz
  float voicing;               // normalised autocorrelation at the chosen lag
};

// One confirmed cry, in RAM and in the store file alike.
struct CrySavedCry {
  uint8_t need;
  uint8_t pad[3];
  float fp[CRY_N_FEATURES];
};

// ---------- 16 kHz -> 8 kHz ----------

// y[n] = sum_k h[k] x[n-k] with zero history, keeping n = 0, 2, 4, ...
struct CryDecimator {
  float hist[CRY_DECIM_TAPS];
  int pos;
  uint32_t n;

  void reset() {
    memset(hist, 0, sizeof(hist));
    pos = 0;
    n = 0;
  }

  // Returns true when x completes an output sample, written to y.
  bool push(float x, float& y) {
    hist[pos] = x;
    bool out = (n & 1) == 0;
    if (out) {
      float acc = 0;
      int i = pos;
      for (int k = 0; k < CRY_DECIM_TAPS; k++) {
        acc += CRY_DECIM_COEFFS[k] * hist[i];
        i = i == 0 ? CRY_DECIM_TAPS - 1 : i - 1;
      }
      y = acc;
    }
    pos = (pos + 1) % CRY_DECIM_TAPS;
    n++;
    return out;
  }
};

// ---------- per-frame features ----------

static float cryHann[CRY_FRAME];
static float cryCos[CRY_FRAME / 2];
static float crySin[CRY_FRAME / 2];

static void cryInitTables() {
  static bool ready = false;
  if (ready) return;
  for (int i = 0; i < CRY_FRAME; i++) cryHann[i] = (float)(0.5 - 0.5 * cos(2 * CRY_PI * i / CRY_FRAME));   // periodic
  for (int i = 0; i < CRY_FRAME / 2; i++) {
    cryCos[i] = (float)cos(2 * CRY_PI * i / CRY_FRAME);
    crySin[i] = (float)sin(2 * CRY_PI * i / CRY_FRAME);
  }
  ready = true;
}

// In-place radix-2 FFT of CRY_FRAME points.
static void cryFft(float* re, float* im) {
  for (int i = 1, j = 0; i < CRY_FRAME; i++) {
    int bit = CRY_FRAME >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= CRY_FRAME; len <<= 1) {
    int step = CRY_FRAME / len;
    for (int i = 0; i < CRY_FRAME; i += len) {
      for (int k = 0; k < len / 2; k++) {
        float wr = cryCos[k * step], wi = -crySin[k * step];
        int a = i + k, b = a + len / 2;
        float xr = re[b] * wr - im[b] * wi;
        float xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr;
        im[b] = im[a] - xi;
        re[a] += xr;
        im[a] += xi;
      }
    }
  }
}

// r[j] is the correlation at lag CRY_LAG_MIN - 1 + j. Shortest period first,
// so a cry is not read an octave low.
static void cryPickPitch(const float* r, float& f0, float& voicing) {
  float best = r[1];
  for (int j = 2; j < CRY_N_LAGS - 1; j++) best = fmaxf(best, r[j]);
  if (best <= 0) {
    f0 = 0;
    voicing = 0;
    return;
  }
  int pick = -1;
  for (int j = 1; j < CRY_N_LAGS - 1; j++) {
    if (r[j] >= CRY_OCTAVE_TOL * best && r[j] >= r[j - 1] && r[j] >= r[j + 1]) {
      pick = j;
      break;
    }
  }
  if (pick < 0) {
    pick = 1;
    for (int j = 2; j < CRY_N_LAGS - 1; j++) if (r[j] > r[pick]) pick = j;
  }
  float y0 = r[pick - 1], y1 = r[pick], y2 = r[pick + 1];
  float denom = y0 - 2 * y1 + y2;
  float delta = denom < 0 ? 0.5f * (y0 - y2) / denom : 0.0f;
  delta = fminf(0.5f, fmaxf(-0.5f, delta));
  f0 = CRY_FS / (CRY_LAG_MIN - 1 + pick + delta);
  voicing = y1;
}

// s: CRY_FRAME samples at 8 kHz, int16 scale.
static void cryFrameFeatures(const float* s, CryFrame& f) {
  static float x[CRY_FRAME], re[CRY_FRAME], im[CRY_FRAME], p[CRY_FRAME / 2 + 1];
  cryInitTables();

  float sum = 0, sq = 0;
  for (int i = 0; i < CRY_FRAME; i++) {
    x[i] = s[i] / 32768.0f;
    sum += x[i];
    sq += x[i] * x[i];
  }
  f.db = 10 * log10f(sq / CRY_FRAME + CRY_EPS);

  for (int i = 0; i < CRY_FRAME; i++) {
    re[i] = x[i] * cryHann[i];
    im[i] = 0;
  }
  cryFft(re, im);
  for (int k = 0; k <= CRY_FRAME / 2; k++) p[k] = re[k] * re[k] + im[k] * im[k];

  float bp[CRY_N_BANDS], total = 0;
  for (int b = 0; b < CRY_N_BANDS; b++) {
    bp[b] = 0;
    for (int k = CRY_BAND_START[b]; k < CRY_BAND_END[b]; k++) bp[b] += p[k];
    total += bp[b];
  }
  float totalDb = 10 * log10f(total + CRY_EPS);
  for (int b = 0; b < CRY_N_BANDS; b++) f.bands[b] = 10 * log10f(bp[b] + CRY_EPS) - totalDb;

  int lo = CRY_BAND_START[0], hi = CRY_BAND_END[CRY_N_BANDS - 1];
  float ps = 0, weighted = 0, logs = 0;
  for (int k = lo; k < hi; k++) {
    ps += p[k];
    weighted += p[k] * ((float)k * CRY_FS / CRY_FRAME);
    logs += logf(p[k] + CRY_EPS);
  }
  f.centroid = weighted / (ps + CRY_EPS);
  f.flatness = expf(logs / (hi - lo)) / (ps / (hi - lo) + CRY_EPS);

  float mean = sum / CRY_FRAME;
  for (int i = 0; i < CRY_FRAME; i++) x[i] -= mean;
  float r[CRY_N_LAGS];
  for (int j = 0; j < CRY_N_LAGS; j++) {
    int lag = CRY_LAG_MIN - 1 + j;
    float dot = 0, aa = 0, bb = 0;
    for (int i = 0; i < CRY_FRAME - lag; i++) {
      dot += x[i] * x[i + lag];
      aa += x[i] * x[i];
      bb += x[i + lag] * x[i + lag];
    }
    r[j] = dot / sqrtf(aa * bb + CRY_EPS);
  }
  cryPickPitch(r, f.f0, f.voicing);
}

// ---------- detector ----------

static inline bool cryIsCryLike(const CryFrame& f, float floorDb) {
  return f.db >= floorDb + CRY_LOUD_DB && f.voicing >= CRY_VOICING_MIN && f.f0 >= CRY_F0_MIN_HZ &&
         f.f0 <= CRY_F0_MAX_HZ && f.flatness <= CRY_FLATNESS_MAX;
}

// The noise floor falls at once and rises slowly.
static inline float cryFloorStep(float floorDb, float db) { return fminf(db, floorDb + CRY_FLOOR_RISE_DB); }

// Number of cry bursts in flags[0..n); *totalFrames gets their summed length.
static int cryBursts(const bool* flags, int n, int* totalFrames) {
  int count = 0, total = 0, start = -1, last = -1, on = 0;
  for (int i = 0; i < n; i++) {
    if (flags[i]) {
      if (start < 0) {
        start = i;
        on = 0;
      }
      last = i;
      on++;
    } else if (start >= 0 && i - last > CRY_BURST_GAP_FRAMES) {
      if (on >= CRY_BURST_MIN_FRAMES) {
        count++;
        total += last - start + 1;
      }
      start = -1;
    }
  }
  if (start >= 0 && on >= CRY_BURST_MIN_FRAMES) {
    count++;
    total += last - start + 1;
  }
  if (totalFrames) *totalFrames = total;
  return count;
}

static int cryCountOn(const bool* flags, int n) {
  int on = 0;
  for (int i = 0; i < n; i++) on += flags[i];
  return on;
}

// One cry with its pauses, fed one frame at a time (features.py Episode).
// step() and finish() return true when the episode gives a guess, with the
// frames to fingerprint in [first, last].
struct CryEpisode {
  int32_t start = -1, last = -1;
  int cry = 0;

  void reset() {
    start = last = -1;
    cry = 0;
  }
  bool open() const { return start >= 0; }

  bool step(int32_t i, bool flag, int32_t& first, int32_t& lastOut) {
    if (start < 0) {
      if (flag) {
        start = last = i;
        cry = 1;
      }
      return false;
    }
    if (flag) {
      last = i;
      cry++;
      if (cry >= CRY_EPISODE_CRY_FRAMES) return close(true, first, lastOut);
    } else if (i - last >= CRY_EPISODE_GAP_FRAMES) {
      return close(cry >= CRY_EPISODE_MIN_CRY_FRAMES, first, lastOut);
    }
    return false;
  }

  // The recording ended: like the baby falling silent.
  bool finish(int32_t& first, int32_t& lastOut) {
    return close(start >= 0 && cry >= CRY_EPISODE_MIN_CRY_FRAMES, first, lastOut);
  }

 private:
  bool close(bool give, int32_t& first, int32_t& lastOut) {
    if (give) {
      first = last - CRY_EPISODE_MAX_FRAMES + 1 > start ? last - CRY_EPISODE_MAX_FRAMES + 1 : start;
      lastOut = last;
    }
    reset();
    return give;
  }
};

// numpy's default (linear) percentile. Sorts v in place.
static float cryPercentile(float* v, int n, float q) {
  for (int i = 1; i < n; i++) {
    float key = v[i];
    int j = i - 1;
    for (; j >= 0 && v[j] > key; j--) v[j + 1] = v[j];
    v[j + 1] = key;
  }
  float pos = q / 100.0f * (n - 1);
  int lo = (int)floorf(pos);
  int hi = lo + 1 < n ? lo + 1 : n - 1;
  return v[lo] + (pos - lo) * (v[hi] - v[lo]);
}

// ---------- fingerprint ----------

// Fingerprint of a run of frames (an episode), oldest first. An episode has
// cry-like frames; with fewer than 2 this uses every frame (features.py uses the
// louder half there, for clips the detector missed).
static void cryFingerprint(const CryFrame* const* fr, const bool* flags, int n, float* fp) {
  bool all = cryCountOn(flags, n) < 2;
  int sel = 0;
  float bandSum[CRY_N_BANDS] = {0}, centroid = 0, f0Sum = 0;
  int voiced = 0;
  for (int i = 0; i < n; i++) {
    if (!all && !flags[i]) continue;
    sel++;
    for (int b = 0; b < CRY_N_BANDS; b++) bandSum[b] += fr[i]->bands[b];
    centroid += fr[i]->centroid;
    if (fr[i]->f0 > 0) {
      f0Sum += log2f(fr[i]->f0);
      voiced++;
    }
  }
  float f0Mean = voiced ? f0Sum / voiced : log2f(450.0f);
  float bandVar[CRY_N_BANDS] = {0}, f0Var = 0;
  for (int i = 0; i < n; i++) {
    if (!all && !flags[i]) continue;
    for (int b = 0; b < CRY_N_BANDS; b++) {
      float d = fr[i]->bands[b] - bandSum[b] / sel;
      bandVar[b] += d * d;
    }
    if (fr[i]->f0 > 0) {
      float d = log2f(fr[i]->f0) - f0Mean;
      f0Var += d * d;
    }
  }
  for (int b = 0; b < CRY_N_BANDS; b++) {
    fp[b] = bandSum[b] / sel;
    fp[CRY_N_BANDS + b] = sqrtf(bandVar[b] / sel);
  }
  int burstFrames = 0;
  int bursts = cryBursts(flags, n, &burstFrames);
  float* rest = fp + 2 * CRY_N_BANDS;
  rest[0] = f0Mean;
  rest[1] = voiced ? sqrtf(f0Var / voiced) : 0.0f;
  rest[2] = (float)cryCountOn(flags, n) / n;
  rest[3] = bursts / ((float)n * CRY_FRAME / CRY_FS);
  rest[4] = bursts ? (float)burstFrames / bursts * CRY_FRAME / CRY_FS : 0.0f;
  rest[5] = centroid / sel / 1000.0f;
}

// A recorded clip, as features.py clip_fingerprint() takes it: the floor starts
// at a low percentile of the clip, the first episode that gives a guess is used
// (the end of the clip counts as silence), and without one the
// CRY_EPISODE_MAX_FRAMES run with the most cry-like frames. Returns 1 when the
// detector gave a guess, 0 for the fallback, -1 when the clip is empty or memory
// ran out. `frames` are the clip's frames, oldest first.
static int cryClipFingerprintFrames(const CryFrame* frames, int nf, float* fp, float* cryFraction) {
  if (nf < 1) return -1;
  bool* flags = (bool*)malloc(nf);
  float* dbs = (float*)malloc(nf * sizeof(float));
  if (!flags || !dbs) {
    free(flags);
    free(dbs);
    return -1;
  }
  for (int i = 0; i < nf; i++) dbs[i] = frames[i].db;
  float floorDb = cryPercentile(dbs, nf, CRY_FLOOR_INIT_PERCENTILE);
  int on = 0;
  for (int i = 0; i < nf; i++) {
    flags[i] = cryIsCryLike(frames[i], floorDb);
    on += flags[i];
    floorDb = cryFloorStep(floorDb, frames[i].db);
  }
  CryEpisode episode;
  int32_t first = -1, last = -1;
  bool given = false;
  for (int i = 0; i < nf && !given; i++) given = episode.step(i, flags[i], first, last);
  if (!given) given = episode.finish(first, last);
  if (!given) {
    int w = nf < CRY_EPISODE_MAX_FRAMES ? nf : CRY_EPISODE_MAX_FRAMES;
    int best = -1;
    for (int s = 0; s + w <= nf; s++) {
      int c = cryCountOn(flags + s, w);
      if (c > best) {
        best = c;
        first = s;
      }
    }
    last = first + w - 1;
  }
  static const CryFrame* seq[CRY_EPISODE_MAX_FRAMES];
  int n = last - first + 1;
  for (int i = 0; i < n; i++) seq[i] = &frames[first + i];
  cryFingerprint(seq, flags + first, n, fp);
  if (cryFraction) *cryFraction = (float)on / nf;
  free(flags);
  free(dbs);
  return given ? 1 : 0;
}

// ---------- self-test signal ----------

// Sample i of the synthetic cry at 16 kHz (features.py selftest_signal()).
static float crySelftestSample(uint32_t i, uint32_t& state) {
  double t = (double)i / CRY_FS_IN;
  double u = fmod(t, CRY_SELFTEST_PERIOD_S);
  double env = u < CRY_SELFTEST_ON_S ? sin(CRY_PI * u / CRY_SELFTEST_ON_S) : 0.0;
  double depth = CRY_SELFTEST_VIBRATO_HZ / (2 * CRY_PI * CRY_SELFTEST_VIBRATO_RATE_HZ);
  double phase = 2 * CRY_PI * (CRY_SELFTEST_F0_HZ * t + depth * (1 - cos(2 * CRY_PI * CRY_SELFTEST_VIBRATO_RATE_HZ * t)));
  float ph = (float)fmod(phase, 2 * CRY_PI);
  float tone = 0;
  for (int h = 1; h <= CRY_SELFTEST_HARMONICS; h++) tone += sinf(h * ph) / h;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  float noise = ((state & 0xFFFF) / 65535.0f - 0.5f) * 2 * CRY_SELFTEST_NOISE;
  return CRY_SELFTEST_AMP * (float)env * tone + noise;
}

// Fingerprints the synthetic cry through the decimator, like live audio.
// Returns as cryClipFingerprintFrames(). Takes about 10 KB of heap for a moment.
static int crySelfTest(float* fp) {
  const int nIn = CRY_SELFTEST_SECONDS * CRY_FS_IN;
  const int nf = nIn / 2 / CRY_FRAME;
  CryFrame* frames = (CryFrame*)malloc(nf * sizeof(CryFrame));
  if (!frames) return -1;
  static float buf[CRY_FRAME];
  CryDecimator dec;
  dec.reset();
  uint32_t state = CRY_SELFTEST_SEED;
  int fill = 0, k = 0;
  for (int i = 0; i < nIn; i++) {
    float y;
    if (dec.push(crySelftestSample(i, state), y)) {
      buf[fill++] = y;
      if (fill == CRY_FRAME) {
        cryFrameFeatures(buf, frames[k++]);
        fill = 0;
      }
    }
  }
  int r = cryClipFingerprintFrames(frames, k, fp, nullptr);
  free(frames);
  return r;
}

// ---------- classifier ----------

static inline float cryKernel(const float* z, const float* p) {
  float d2 = 0;
  for (int i = 0; i < CRY_N_FEATURES; i++) {
    float d = z[i] - p[i];
    d2 += d * d;
  }
  return expf(-d2 / (2 * CRY_KERNEL_SIGMA * CRY_KERNEL_SIGMA * CRY_N_FEATURES));
}

static inline void cryZ(const float* fp, float* z) {
  for (int i = 0; i < CRY_N_FEATURES; i++) z[i] = (fp[i] - CRY_FEAT_MEAN[i]) / CRY_FEAT_STD[i];
}

// p(need | cry) for a fingerprint, given the cries this baby's parent confirmed.
// For each need the Donate-a-Cry prototypes count as CRY_BASELINE_WEIGHT cries
// and every confirmed cry as one, so the baby's own cries soon outweigh them.
// hungryBoost multiplies the prior of Hungry (1 = none).
static void cryPosterior(const float* fp, const CrySavedCry* saved, int nSaved, float hungryBoost, float* post) {
  float z[CRY_N_FEATURES], zs[CRY_N_FEATURES];
  cryZ(fp, z);
  float num[CRY_N_NEEDS], den[CRY_N_NEEDS];
  for (int c = 0; c < CRY_N_NEEDS; c++) {
    float sum = 0;
    int n = 0;
    for (int k = 0; k < CRY_N_PROTOS; k++) {
      if (CRY_PROTO_NEED[k] != c) continue;
      sum += cryKernel(z, CRY_PROTOS[k]);
      n++;
    }
    num[c] = CRY_BASELINE_WEIGHT * (n ? sum / n : CRY_FLAT_LIKELIHOOD);
    den[c] = CRY_BASELINE_WEIGHT;
  }
  for (int i = 0; i < nSaved; i++) {
    cryZ(saved[i].fp, zs);
    num[saved[i].need] += cryKernel(z, zs);
    den[saved[i].need] += 1;
  }
  float total = 0;
  for (int c = 0; c < CRY_N_NEEDS; c++) {
    post[c] = CRY_PRIOR[c] * (c == NEED_HUNGRY ? hungryBoost : 1.0f) * num[c] / den[c];
    total += post[c];
  }
  for (int c = 0; c < CRY_N_NEEDS; c++) post[c] /= total;
}
