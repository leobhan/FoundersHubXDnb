# Cry baseline

How the board turns sound into a guess, and where the starting knowledge comes from.

```
mic 16 kHz -> decimate to 8 kHz -> 32 ms frames -> cry detector -> episode fingerprint (38 numbers, pauses allowed)
           -> prior x [Donate-a-Cry prototypes + this baby's confirmed cries] -> Hungry / Sleepy / Diaper / Discomfort
```

| File | What it is |
|---|---|
| `features.py` | The feature pipeline and cry detector. The source of truth. |
| `build_baseline.py` | Downloads Donate-a-Cry, measures the baseline, writes `cry/baseline_cries.h`. |
| `check_parity.py` | Checks that the C++ computes what `features.py` computes. |
| `parity_native.cpp` | Runs `cry/cry_dsp.h` on the laptop for `check_parity.py`. |
| `../../cry/cry_dsp.h` | The same pipeline in C++, plus the guess (`cryPosterior`). |
| `../../cry/cry_engine.h` | The board side: microphone ring buffer, live detector, saved cries in flash. |

## Run it

From `baby-cry-mvp/` (Python 3 with numpy):

```bash
python -I tools/baseline/build_baseline.py    # about 1 min; downloads 50 MB into data/ the first time
python -I tools/baseline/check_parity.py      # needs clang++ or g++
python -I tools/baseline/check_parity.py --device   # the board on USB, firmware uploaded
```

Change a constant in `features.py` or `build_baseline.py`, then run `build_baseline.py` again. It
rewrites the header, and the board reads every constant from there. If the change alters the
fingerprint, bump `FEATURE_VERSION`, so the board drops cries saved with the old one.

## How good the baseline is

The full report is at the top of `cry/baseline_cries.h`. In short:

- **Detector:** it fires on 96 % of the Donate-a-Cry clips, and every clip is a cry. On the board
  the noise floor comes from the room, so this is only a rough guide. Tune it with
  `atech monitor --key cry_debug`.
- **Need, unseen babies:** 33 % balanced accuracy over Hungry, Sleepy and Discomfort, which is
  chance and the same as always guessing Hungry. The grouped cross-validation keeps each app
  instance (a stand-in for one baby) out of its own training data.
  - Other models (nearest centroid, logistic regression, MFCC) did no better.
  - When the same baby is in both training and test data, a model scores 47 %. That is the model
    recognising the baby, not the need.
- **So the baby's own cries decide.** For each need, the whole Donate-a-Cry baseline counts as
  half of one confirmed cry (`BASELINE_WEIGHT = 0.5`), so a single green press outweighs it. Diaper
  has no Donate-a-Cry data and starts from the prior alone.

Donate-a-Cry has 457 clips at 8 kHz, labelled by the parents themselves:

| Donate-a-Cry | Our need | Clips |
|---|---|---|
| hungry | Hungry | 382 |
| tired | Sleepy | 24 |
| discomfort, belly_pain, burping | Discomfort | 51 |
| (none) | Diaper | 0 |

Source: <https://github.com/gveres/donateacry-corpus>, cleaned set. Licensed under ODbL 1.0, with
the contents under DbCL 1.0. The prototypes in `cry/baseline_cries.h` are derived from it, so keep
the attribution there.

## On the board

| Event / action | What it carries |
|---|---|
| `cry_debug` (event, 4 Hz) | Frame level, noise floor, pitch, voicing, whether the frame is cry-like, how much of the current cry was heard. |
| `cry_guess` (event) | The four probabilities when a cry is heard. |
| `cry_saved` (event) | The confirmed need and the fingerprint. Collect these: they are this baby's real labelled data. |
| `cry_store` (event, at boot) | Whether flash is mounted, and how many cries are saved. |
| `cry_selftest` (action) | Fingerprint of a built-in synthetic cry, for `check_parity.py --device`. |
| `cries_reset` (action) | Forgets every saved cry. Holding both buttons for 3 s while listening does the same. |
