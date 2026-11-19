# VinylCountdown

Eight VCV Rack modules about vinyl, by Haer. They go past noise, crackle and
a filter. Each one copies a physical part of making or playing a record.

**House rule: every module works on live audio and adds at most 100 ms of latency.**

| Module | What it does | Latency |
|---|---|---|
| **Platter** | Off-centre pressing wow: a pitch sway locked to the rotation. OFFSET (0-1 mm), RADIUS, 33/45/78. WOW out is the same wobble as 1 V/oct CV, plus a revolution trigger and phase. | fixed 5 ms |
| **Inner Groove** | Needle position from the outer edge to the label: top-end loss, tracing distortion, mistracking splatter. STYLUS tip, TRACK, CUT level, 33/45. PLAY sweeps a whole side over SIDE minutes. | 0 |
| **Lathe** | The cutting room: DRIVE (hot cut, treble saturates first), ELLIPTIC (bass to mono), HF LIMIT, ECHO (groove echo one or two revolutions late, from RPM or a CLOCK). | 0 |
| **RIAA Sandwich** | Record EQ curves around a SEND/RETURN (or built-in DRIVE). CUT and PLAY curves: RIAA, Columbia, NAB, Decca FFRR, AES. Mismatch them to tilt the tone. | 0 |
| **Trainwreck** | Two live decks that never quite beatmatch: B drifts, a DJ nudges it back, flams and phasing. LATE (A live) or BOTH (both 50 ms back). | ≤ 100 ms |
| **Stylus** | Cartridge: MM load resonance or MC, crosstalk, azimuth lean, vertical (stereo-width) mistracking. | < 0.2 ms |
| **Pressing** | GENERATION (lacquer to late pressing) and WEAR (dubplate played to death). Plain knobs, no memory. | 0 |
| **Scratch** | 8-step scratch-gesture sequencer (baby, chirp, transformer, flare, crab, orbit, tear, scribble). Live audio passes straight through; a gesture step scratches the audio from BACK steps ago. HAND and FADER CV outs. | 0 (live path) |

## Build
Every push builds an Apple Silicon `.vcvplugin` in GitHub Actions (see the
"preview" or "latest" release). Locally: `make RACK_DIR=path/to/Rack-SDK dist`.

`make test` runs offline engine checks (including every latency promise) and writes demo WAVs.
Panels and `src/Layout.hpp` come from `python3 res/make_panels.py` (needs fonttools).
