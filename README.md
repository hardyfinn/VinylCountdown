# VinylCountdown

Eight VCV Rack modules about vinyl, by Haer. They go past noise, crackle and
a filter. Each one copies a physical part of making or playing a record.

**House rule: every module works on live audio and adds at most 100 ms of latency.**

Five modules, in the order a record travels:

**Vinyl Countdown** (36 HP): eight stages in record order, each with an on/off button.
LATHE (hot cut DRIVE, ELLIPTIC mono bass, HF LIMIT, groove ECHO) -> RIAA cut curve ->
PRESSING (GENERATION, WEAR) -> GROOVE (RADIUS, TRACK, SIDE + PLAY sweeps a whole side) ->
STYLUS (TIP, LOAD, AZIMUTH, VERTICAL) -> PLATTER (off-centre OFFSET, RPM, BEATS) ->
RIAA play curve -> PREAMP (VALVE drive, STEP-UP transformer, mains HUM 50/60 Hz, GND lift buzz) -> WRECK (deck B drifts and gets nudged; B is the dry input unless patched).
RADIUS drives both the inner-groove loss and the wow; DRIVE is the cut level for lathe and groove.
Two assignable CV inputs: CV A and CV B each have a DEST knob (any knob, the PLAY and nudge
triggers, or a BYPASS gate), an AMOUNT attenuverter and a screen; a dashed ring shows the target.
BYPASS (top right) passes the dry input straight through, with no delay; Randomize leaves it alone.
Right-click > Presets has 20 starting points.
Latency: 0, plus 6 ms with PLATTER on, plus up to 100 ms on deck B in WRECK.

**Studio** (20 HP): before the lathe. CONSOLE (DRIVE, IRON transformer, XTALK) -> TAPE master
(SAT, BUMP, FLUTTER, AZIMUTH, 7.5/15/30 ips) with DOLBY A around it (ERROR = played back off level;
PATH OK / ENC never decoded / DEC never encoded) -> GLUE (vari-mu AMOUNT, TIME). Latency 0, +3 ms with TAPE.

**Listening Room** (20 HP): after the preamp. AMP (VOLUME, VALVE/SOLID/CHEAP, LOUDNESS) -> SPEAKER
(8 boxes from phone to club stack, BREAKUP) -> ROOM (SIZE, DAMP, bass MODES, WALL = next door) ->
EARS (DISTANCE, head ANGLE, PARTY = the morning after). The direct sound is never delayed.

**On Air** (24 HP): the record played on FM, simulated end to end. STATION (PROCESSING, ERA 70s/80s/90s,
EU/US pre-emphasis + final clipper) -> stereo multiplex with 19 kHz pilot -> FM at ~768 kHz -> AIR (SIGNAL =
carrier to noise, MULTIPATH, MOTION = picket fencing) + NEXT station (its own input, LEVEL, 100/200 kHz up)
-> RADIO (TUNE, HIFI/CAR/CHEAP sets with stereo blend, high cut and soft mute; MONO). Latency under 1 ms.
Right-click Eco mode halves the radio sample rate for less CPU.

Studio, Listening Room and On Air also have CV A/B, BYPASS and presets, like Vinyl Countdown.

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

## Licence

MIT (see `LICENSE.txt`): you're free to use, change, port (VST/CLAP, other platforms, hardware) and share this code, as long as the copyright notice and licence text come along. Fonts on the panel (Inter, Climate Crisis) are under the SIL Open Font Licence.
