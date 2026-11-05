// Cry engine, board side: the microphone ring buffer, the live cry detector
// and the store of cries the parent confirmed. The signal processing and the
// guess are in cry_dsp.h.
#pragma once
#include <Arduino.h>
#include <LittleFS.h>

#include "cry_dsp.h"

#define CRY_FLOOR_INIT_FRAMES 31   // 1 s of room sound sets the first noise floor
#define CRY_REARM_FRAMES 312       // 10 s without a cry-like frame before the next guess
#define CRY_MAX_SAVED 200
#define CRY_STORE_PATH "/cries.bin"
#define CRY_STORE_MAGIC 0x31595243u   // "CRY1"

// ---------- ring buffer, filled on the microphone's capture task (core 0) ----------

#define CRY_RING 8192   // 0.5 s at 16 kHz, a power of two
static int16_t cryRing[CRY_RING];
static volatile uint32_t cryRingHead = 0;

// mic.onAudio(cryOnMic, nullptr): copies and returns, as the capture task needs.
static void cryOnMic(const int16_t* samples, int count, void*) {
  uint32_t h = cryRingHead;
  for (int i = 0; i < count; i++) cryRing[(h + i) & (CRY_RING - 1)] = samples[i];
  __sync_synchronize();
  cryRingHead = h + count;
}

// ---------- live detector ----------

// 200 Hz high-pass (RBJ biquad, Q 0.707) after the decimator. The board's
// microphone carries a lot of rumble below 100 Hz that hid quieter cries and
// spoiled the pitch; a cry's pitch is 250 Hz and up, so nothing of it is lost.
struct CryHighpass {
  float b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  CryHighpass() {
    float w0 = 2 * (float)M_PI * 200 / CRY_FS, al = sinf(w0) / (2 * 0.7071f), c = cosf(w0);
    b0 = (1 + c) / 2 / (1 + al);
    b1 = -(1 + c) / (1 + al);
    b2 = b0;
    a1 = -2 * c / (1 + al);
    a2 = (1 - al) / (1 + al);
  }
  float push(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

// Frames kept: a whole episode plus the silence that ends it.
#define CRY_HISTORY_FRAMES (CRY_EPISODE_MAX_FRAMES + CRY_EPISODE_GAP_FRAMES + 1)

class CryListener {
 public:
  float fp[CRY_N_FEATURES];   // fingerprint of the last episode
  CryFrame last;              // latest frame, for cry_debug
  bool lastCry = false;
  float floorDb = 0;
  bool floorReady = false;
  int quietFrames = 0;        // frames since the last cry-like one
  uint32_t overruns = 0;      // times the ring filled up before loop read it

  // A recording of what the detector hears (8 kHz, after the decimator), for
  // tuning it on the laptop: tools/baseline/record_board.py.
  int16_t* rec = nullptr;
  int recLen = 0, recPos = 0;

  bool startRecording(int samples) {
    free(rec);
    rec = (int16_t*)malloc(samples * sizeof(int16_t));
    recLen = rec ? samples : 0;
    recPos = 0;
    return rec != nullptr;
  }
  void stopRecording() {
    free(rec);
    rec = nullptr;
    recLen = recPos = 0;
  }

  void begin() {
    dec.reset();
    tail = cryRingHead;
  }

  // Processes up to maxFrames frames of waiting audio. Returns true once when
  // a cry episode gives a guess while `listening` and armed; the fingerprint is then in fp.
  bool pump(bool listening, int maxFrames = 8) {
    uint32_t head = cryRingHead;
    __sync_synchronize();
    if (head - tail > CRY_RING) {
      overruns++;
      tail = head - CRY_RING;
    }
    bool fired = false;
    int frames = 0;
    while (tail != head && frames < maxFrames) {
      float y;
      if (dec.push(cryRing[tail & (CRY_RING - 1)], y)) {
        if (recPos < recLen) rec[recPos++] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, roundf(y)));
        frameBuf[fill++] = hp.push(y);
        if (fill == CRY_FRAME) {
          fill = 0;
          frames++;
          if (onFrame(listening && !fired)) fired = true;
        }
      }
      tail++;
    }
    return fired;
  }

  // After a guess: wait for quiet before the next one, so one long cry gives one guess.
  void disarm() {
    armed = false;
    episode.reset();
  }
  bool isArmed() const { return armed; }

  // A cry is being heard and its guess is still to come; pauses keep it going.
  // A burst's worth of cry-like frames first, so a single odd frame does not show.
  bool hearing() const { return episode.open() && episode.cry >= CRY_EPISODE_MIN_CRY_FRAMES; }
  // Cry-like frames so far, against the CRY_EPISODE_CRY_FRAMES that give a guess at once.
  int heardFrames() const { return episode.cry; }

  // Drops audio that piled up while loop was busy (the self-test).
  void skipBacklog() { tail = cryRingHead; }

 private:
  uint32_t tail = 0;
  CryDecimator dec;
  CryHighpass hp;
  float frameBuf[CRY_FRAME];
  int fill = 0;
  CryFrame hist[CRY_HISTORY_FRAMES];
  bool histCry[CRY_HISTORY_FRAMES];
  int32_t frameNo = 0;
  float initDb[CRY_FLOOR_INIT_FRAMES];
  int initCount = 0;
  bool armed = true;
  CryEpisode episode;

  bool onFrame(bool listening) {
    int32_t i = frameNo++;
    CryFrame& f = hist[i % CRY_HISTORY_FRAMES];
    cryFrameFeatures(frameBuf, f);
    bool c = false;
    if (!floorReady) {
      initDb[initCount++] = f.db;
      if (initCount == CRY_FLOOR_INIT_FRAMES) {
        floorDb = cryPercentile(initDb, CRY_FLOOR_INIT_FRAMES, CRY_FLOOR_INIT_PERCENTILE);
        floorReady = true;
      }
    } else {
      c = cryIsCryLike(f, floorDb);
      floorDb = cryFloorStep(floorDb, f.db);
    }
    histCry[i % CRY_HISTORY_FRAMES] = c;
    last = f;
    lastCry = c;
    quietFrames = c ? 0 : quietFrames + 1;
    if (!armed && quietFrames >= CRY_REARM_FRAMES) armed = true;

    if (!listening || !armed) {
      episode.reset();
      return false;
    }
    int32_t first, end;
    if (!episode.step(i, c, first, end)) return false;
    static const CryFrame* seq[CRY_EPISODE_MAX_FRAMES];
    static bool fl[CRY_EPISODE_MAX_FRAMES];
    int n = end - first + 1;
    for (int k = 0; k < n; k++) {
      seq[k] = &hist[(first + k) % CRY_HISTORY_FRAMES];
      fl[k] = histCry[(first + k) % CRY_HISTORY_FRAMES];
    }
    cryFingerprint(seq, fl, n, fp);
    return true;
  }
};

// ---------- confirmed cries, kept in flash ----------

struct CryStoreHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t nFeatures;
};

class CryStore {
 public:
  CrySavedCry cries[CRY_MAX_SAVED];
  int count = 0;
  bool ok = false;   // flash is mounted; without it cries last until power-off

  // Mounts LittleFS (formatting it the first time) and loads the saved cries.
  // A file made with other features is dropped: its fingerprints no longer compare.
  bool begin() {
    ok = LittleFS.begin(true);
    if (!ok) return false;
    File f = LittleFS.open(CRY_STORE_PATH, "r");
    if (!f) return true;
    CryStoreHeader h;
    bool valid = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == CRY_STORE_MAGIC &&
                 h.version == CRY_FEATURE_VERSION && h.nFeatures == CRY_N_FEATURES;
    while (valid && count < CRY_MAX_SAVED &&
           f.read((uint8_t*)&cries[count], sizeof(CrySavedCry)) == sizeof(CrySavedCry)) {
      if (cries[count].need < CRY_N_NEEDS) count++;
    }
    size_t size = f.size();
    f.close();
    if (!valid) {
      LittleFS.remove(CRY_STORE_PATH);
    } else if (size != sizeof(CryStoreHeader) + count * sizeof(CrySavedCry)) {
      writeAll();   // a torn last record, or more than fit: rewrite cleanly
    }
    return true;
  }

  // Saves a confirmed cry. When full, the oldest cry of the need with the most
  // cries makes room. Returns false when it could not be written to flash.
  bool add(uint8_t need, const float* fp) {
    bool full = count == CRY_MAX_SAVED;
    if (full) {
      int most = 0;
      for (int c = 1; c < CRY_N_NEEDS; c++) if (countOf(c) > countOf(most)) most = c;
      for (int i = 0; i < count; i++) {
        if (cries[i].need != most) continue;
        memmove(&cries[i], &cries[i + 1], (count - i - 1) * sizeof(CrySavedCry));
        count--;
        break;
      }
    }
    CrySavedCry& r = cries[count++];
    memset(&r, 0, sizeof(r));
    r.need = need;
    memcpy(r.fp, fp, sizeof(r.fp));
    if (!ok) return false;
    if (full) return writeAll();
    bool fresh = !LittleFS.exists(CRY_STORE_PATH);
    File f = LittleFS.open(CRY_STORE_PATH, "a");
    if (!f) return false;
    bool written = (!fresh || writeHeader(f)) && f.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
    f.close();
    return written;
  }

  void clear() {
    count = 0;
    if (ok) LittleFS.remove(CRY_STORE_PATH);
  }

  int countOf(int need) const {
    int n = 0;
    for (int i = 0; i < count; i++) n += cries[i].need == need;
    return n;
  }

 private:
  static bool writeHeader(File& f) {
    CryStoreHeader h = {CRY_STORE_MAGIC, CRY_FEATURE_VERSION, CRY_N_FEATURES};
    return f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
  }

  bool writeAll() {
    File f = LittleFS.open(CRY_STORE_PATH, "w");
    if (!f) return false;
    bool written = writeHeader(f);
    for (int i = 0; written && i < count; i++)
      written = f.write((const uint8_t*)&cries[i], sizeof(CrySavedCry)) == sizeof(CrySavedCry);
    f.close();
    return written;
  }
};
