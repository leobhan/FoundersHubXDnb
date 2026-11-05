# baby-cry-mvp

Atech project targeting the **14port** board.

Edit `project.yaml` to place modules, then:

```bash
atech describe .   # what is placed, each module's API, events and actions
atech validate .
atech build .
atech upload .
atech monitor      # events from the board
```

Working with a coding agent? `AGENTS.md` / `CLAUDE.md` in this folder tell it how
to use the SDK (same text as `atech guide`).

## What it does

When the baby has cried for about 1.5 s (pauses do not count), or stops after at least 0.5 s, the screen shows a guess (Hungry, Sleepy,
Diaper or Discomfort) with how sure it is.

- **Green button:** the guess was right. The cry is saved in flash for this baby,
  and from then on its own cries outweigh the Donate-a-Cry baseline.
- **Red button:** shows the next most likely need. After four reds, nothing is saved.
- **Both buttons held 3 s while listening:** forget every saved cry.

After a guess, the next one waits until the baby has been quiet for 10 s.
`tools/baseline/README.md` covers the detector, the baseline and how good it is.
