# VinylCountdown

Eight VCV Rack modules about vinyl, by Haer. They go past noise, crackle and
a filter. Each one copies a physical part of making or playing a record.

**House rule: every module works on live audio and adds at most 100 ms of latency.**

Two modules:

**Vinyl Countdown** (32 HP): seven stages in record order, each with an on/off button.
LATHE (hot cut DRIVE, ELLIPTIC mono bass, HF LIMIT, groove ECHO) -> RIAA cut curve ->
PRESSING (GENERATION, WEAR) -> GROOVE (RADIUS, TRACK, SIDE + PLAY sweeps a whole side) ->
STYLUS (TIP, LOAD, AZIMUTH, VERTICAL) -> PLATTER (off-centre OFFSET, RPM, BEATS) ->
RIAA play curve -> WRECK (deck B drifts and gets nudged; B is the dry input unless patched).
RADIUS drives both the inner-groove loss and the wow; DRIVE is the cut level for lathe and groove.
Latency: 0, plus 6 ms with PLATTER on, plus up to 100 ms on deck B in WRECK.

**Scratch** (24 HP): a scratch sequencer that plays like a person. 12 gestures (baby, chirp,
forward, transformer, 1- and 2-click flare, crab, orbit, tear, scribble, stab, drag) on 8 clocked
steps or a TRIG input. HUMAN = drifting timing (early and late), speed and stroke changes;
SLOPPY = messier fader work, scaled by how complex the gesture is. Sources: LIVE (rolling 8 s),
GRAB (freeze recent audio) or a WAV (right-click or drag a file onto the module). Cues snap to hits.
Live audio passes straight through between gestures. INSERT or OVER (with DUCK), fader CURVE,
hamster option, HAND / FADER / END outputs.

## Build
Every push builds an Apple Silicon `.vcvplugin` in GitHub Actions (see the
"preview" or "latest" release). Locally: `make RACK_DIR=path/to/Rack-SDK dist`.

`make test` runs offline engine checks (including every latency promise) and writes demo WAVs.
Panels and `src/Layout.hpp` come from `python3 res/make_panels.py` (needs fonttools).
