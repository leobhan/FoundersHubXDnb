// Runs the board's cry DSP (cry/cry_dsp.h) on the laptop, for check_parity.py.
//
//   parity_native selftest           fingerprint of the synthetic cry
//   parity_native clips <list.txt>   one JSON line per "<wav path>\t<need>" line: the
//                                    fingerprint, the guess with no saved cries, and the
//                                    guess with the previous 3 clips saved as confirmed
//
// check_parity.py builds and runs this; you do not need to call it yourself.
#define _CRT_SECURE_NO_WARNINGS   // fopen on the MSVC runtime
#include <stdio.h>
#include <vector>

#include "../../cry/cry_dsp.h"

static void printArray(const float* v, int n) {
  printf("[");
  for (int i = 0; i < n; i++) printf(i ? ",%.9g" : "%.9g", v[i]);
  printf("]");
}

static uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const unsigned char* p) { return p[0] | p[1] << 8; }

// 8 kHz mono 16-bit PCM only, like the corpus.
static bool readWav(const char* path, std::vector<float>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  std::vector<unsigned char> d;
  unsigned char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
  fclose(f);
  if (d.size() < 12 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) return false;
  bool fmtOk = false;
  for (size_t i = 12; i + 8 <= d.size();) {
    uint32_t size = le32(&d[i + 4]);
    if (!memcmp(&d[i], "fmt ", 4) && i + 24 <= d.size()) {
      fmtOk = le16(&d[i + 8]) == 1 && le16(&d[i + 10]) == 1 && le32(&d[i + 12]) == CRY_FS && le16(&d[i + 22]) == 16;
    } else if (!memcmp(&d[i], "data", 4)) {
      if (!fmtOk) return false;
      size_t end = i + 8 + size < d.size() ? i + 8 + size : d.size();
      for (size_t k = i + 8; k + 1 < end; k += 2) out.push_back((float)(int16_t)le16(&d[k]));
      return true;
    }
    i += 8 + size + (size & 1);
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc >= 2 && !strcmp(argv[1], "selftest")) {
    float fp[CRY_N_FEATURES];
    int r = crySelfTest(fp);
    printf("{\"result\":%d,\"fp\":", r);
    printArray(fp, CRY_N_FEATURES);
    printf("}\n");
    return r < 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "clips")) {
    FILE* list = fopen(argv[2], "r");
    if (!list) return 1;
    char path[4096];
    std::vector<CrySavedCry> saved;
    while (fgets(path, sizeof(path), list)) {
      path[strcspn(path, "\r\n")] = 0;
      char* tab = strchr(path, '\t');
      if (!tab) continue;
      *tab = 0;
      uint8_t need = (uint8_t)atoi(tab + 1);
      std::vector<float> x;
      if (!readWav(path, x)) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
      }
      int nf = (int)(x.size() / CRY_FRAME);
      std::vector<CryFrame> frames(nf);
      for (int i = 0; i < nf; i++) cryFrameFeatures(&x[i * CRY_FRAME], frames[i]);
      float fp[CRY_N_FEATURES], post[CRY_N_NEEDS], learned[CRY_N_NEEDS], frac = 0;
      int r = cryClipFingerprintFrames(frames.data(), nf, fp, &frac);
      cryPosterior(fp, nullptr, 0, 1.0f, post);
      size_t from = saved.size() > 3 ? saved.size() - 3 : 0;
      cryPosterior(fp, saved.data() + from, (int)(saved.size() - from), 1.0f, learned);
      printf("{\"result\":%d,\"frac\":%.9g,\"fp\":", r, frac);
      printArray(fp, CRY_N_FEATURES);
      printf(",\"post\":");
      printArray(post, CRY_N_NEEDS);
      printf(",\"learned\":");
      printArray(learned, CRY_N_NEEDS);
      printf("}\n");
      CrySavedCry s = {};
      s.need = need;
      memcpy(s.fp, fp, sizeof(fp));
      saved.push_back(s);
    }
    fclose(list);
    return 0;
  }
  fprintf(stderr, "usage: parity_native selftest | clips <list.txt>\n");
  return 2;
}
