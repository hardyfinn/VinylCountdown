// Scratch engine: a human hand on a record. No Rack code in here, so
// test/render.cpp can run it offline.
//
// Sources
//   LIVE  a rolling recording of the last 8 s of the input. A gesture grabs
//         the audio from BACK steps ago (snapped to the nearest hit).
//   GRAB  a frozen copy of recent live audio, kept until the next grab.
//   WAV   a loaded file.
// Between gestures the live input passes straight through, untouched.
//
// The hand
//   Each gesture is a list of hand key positions (in "strokes") and fader
//   events. Every time a gesture plays it is re-performed: HUMAN moves the
//   keys, stroke sizes and overall speed a little; SLOPPY adds extra mess to
//   the fiddly parts (fader taps drift, lag behind the hand, sometimes get
//   missed), scaled by how complex the gesture is. The hand moves between
//   keys along minimum-jerk curves (how real arms move) and the record
//   follows the hand through a little platter mass. Gestures that let go of
//   the record hand it back to the motor, which pulls it up to speed.
#pragma once
#include "VinylDSP.hpp"
#include <memory>

namespace vinyl {

// A static record (GRAB or WAV). Samples at its own rate.
struct Record {
	std::vector<float> L, R;
	float sr = 48000.f;
	std::vector<float> peaks;     // |x| max per 256-sample block, for the screen
	std::vector<int> onsets;      // sample indices of hits
	int size() const { return (int) L.size(); }
	float seconds() const { return L.empty() ? 0.f : L.size() / sr; }

	inline float readAt(int i, const std::vector<float>& v) const {
		return (i >= 0 && i < (int) v.size()) ? v[i] : 0.f;
	}
	// Hermite read at fractional index
	inline void read(double idx, float& l, float& r) const {
		int i = (int) std::floor(idx);
		float f = (float) (idx - i);
		auto h = [&](const std::vector<float>& v) {
			float xm1 = readAt(i - 1, v), x0 = readAt(i, v), x1 = readAt(i + 1, v), x2 = readAt(i + 2, v);
			float c1 = 0.5f * (x1 - xm1);
			float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
			float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
			return ((c3 * f + c2) * f + c1) * f + x0;
		};
		l = h(L); r = h(R);
	}

	// peaks + onset detection (energy rises in the upper mids)
	void analyse() {
		int n = size();
		peaks.clear(); onsets.clear();
		for (int b = 0; b < n; b += 256) {
			float m = 0.f;
			for (int j = b; j < std::min(n, b + 256); j++) m = std::max(m, std::max(std::fabs(L[j]), std::fabs(R[j])));
			peaks.push_back(m);
		}
		// onsets: a fast envelope jumping well above a slow one, in a low band
		// (kicks) or a high band (hats, snares), at least 60 ms apart
		SVF lo, hi;
		float gl = svfG(150.f, sr), gh = svfG(3000.f, sr);
		float fl = 0.f, sl = 0.f, fh = 0.f, sh = 0.f;
		float af = coefFor(0.001f, sr), as = coefFor(0.12f, sr);
		int last = -100000;
		int gap = (int) (0.1f * sr);
		for (int j = 0; j < n; j++) {
			float m = 0.5f * (L[j] + R[j]);
			float a = std::fabs(lo.lowpass(m, gl, 0.8f)), b = std::fabs(hi.highpass(m, gh, 0.8f));
			fl += af * (a - fl); sl += as * (a - sl);
			fh += af * (b - fh); sh += as * (b - sh);
			bool hit = (fl > sl * 1.6f + 0.15f) || (fh > sh * 2.0f + 0.12f);
			if (hit && j - last > gap) {
				onsets.push_back(std::max(0, j - (int) (0.004f * sr)));
				last = j;
			}
		}
	}
	// nearest onset to idx within tol samples, else idx
	int snap(int idx, int tol) const {
		int best = idx, bd = tol + 1;
		for (int o : onsets) {
			int d = std::abs(o - idx);
			if (d < bd) { bd = d; best = o; }
		}
		return best;
	}
};

static const int NUM_SGESTURES = 13;
enum SGesture { S_OFF, S_BABY, S_CHIRP, S_FORWARD, S_TRANSFORM, S_FLARE, S_FLARE2, S_CRAB, S_ORBIT, S_TEAR, S_SCRIBBLE, S_STAB, S_DRAG };
static const char* const SGESTURE_NAMES[NUM_SGESTURES] = {
	"Off (live)", "Baby", "Chirp", "Forward", "Transformer", "Flare (1-click)", "Flare (2-click)",
	"Crab", "Orbit", "Tear", "Scribble", "Stab (push and let go)", "Drag"
};
static const char* const SGESTURE_SHORT[NUM_SGESTURES] = {
	"--", "BABY", "CHIRP", "FWD", "TRANS", "FLARE", "FLR2", "CRAB", "ORBIT", "TEAR", "SCRBL", "STAB", "DRAG"
};
// how fiddly each gesture is: SLOPPY is scaled by this
static const float SGESTURE_COMPLEXITY[NUM_SGESTURES] = {0.f, 0.15f, 0.45f, 0.25f, 1.f, 0.7f, 0.9f, 1.f, 0.9f, 0.6f, 0.75f, 0.3f, 0.2f};

struct HandKey { float t, pos; };
struct FaderKey { float t; bool open; };

struct ScratchEngine {
	enum Source { LIVE = 0, GRAB = 1, WAV = 2 };
	static constexpr float LIVE_SECONDS = 8.5f;

	float sr = 48000.f;
	Ring live[2];
	Ring liveEnv;            // decimated (x32) envelope of the live input, for snapping to hits
	int envDiv = 0;
	float envFollow = 0.f;
	SVF envHP;
	uint64_t written = 0;    // live samples written so far
	// GRAB and WAV records: written by other threads with std::atomic_store,
	// read with std::atomic_load. `cur` is the one the current gesture uses
	// (audio thread only).
	std::shared_ptr<Record> recGrab, recWav, cur;
	std::shared_ptr<Record> record(int src) const {
		return src == GRAB ? std::atomic_load(&recGrab) : src == WAV ? std::atomic_load(&recWav) : nullptr;
	}
	Rng rng;

	// performed gesture
	bool active = false;
	int gesture = S_OFF;
	int source = LIVE;
	float dur = 0.25f, t = 0.f;
	double anchor = 0.0;      // LIVE: absolute sample count; static: record index
	float stroke = 0.1f;      // seconds of record per stroke
	std::vector<HandKey> hand;
	std::vector<FaderKey> fader;
	float releaseAt = -1.f;   // fraction of dur where the hand lets go (-1 = never)
	// record motion
	float pos = 0.f, vel = 0.f;  // seconds of record from the anchor, record speed
	bool released = false;
	// fader + output
	float faderPos = 1.f, mixEnv = 0.f;
	SVF dc[2];
	bool endPulse = false;
	// for CV outs and screen
	float handOut = 0.f, faderGain = 1.f;
	double needleIdx = 0.0;   // where the needle is (absolute live count or record index)

	void setSampleRate(float s) {
		sr = s;
		int n = (int) (LIVE_SECONDS * sr) + 16;
		live[0].init(n); live[1].init(n);
		liveEnv.init(n / 32 + 16);
	}
	void reset() { active = false; mixEnv = 0.f; }

	// --- the gesture book --------------------------------------------------
	static void build(int g, std::vector<HandKey>& h, std::vector<FaderKey>& f, float& rel) {
		h.clear(); f.clear(); rel = -1.f;
		auto H = [&](float t, float p) { h.push_back({t, p}); };
		auto F = [&](float t, bool o) { f.push_back({t, o}); };
		switch (g) {
			case S_BABY: H(0, 0); H(0.5f, 1); H(1, 0); F(0, true); break;
			case S_CHIRP: H(0, 0); H(0.45f, 1); H(1, 0); F(0, true); F(0.38f, false); break;
			case S_FORWARD: H(0, 0); H(0.45f, 1); H(1, 0); F(0, true); F(0.47f, false); break;
			case S_TRANSFORM:
				H(0, 0); H(0.5f, 1); H(1, 0);
				for (int i = 0; i < 8; i++) { F(i / 8.f, true); F(i / 8.f + 0.07f, false); }
				break;
			case S_FLARE:
				H(0, 0); H(0.5f, 1); H(1, 0);
				F(0, true); F(0.22f, false); F(0.28f, true); F(0.72f, false); F(0.78f, true);
				break;
			case S_FLARE2:
				H(0, 0); H(0.5f, 1); H(1, 0);
				F(0, true);
				for (float c : {0.15f, 0.32f, 0.65f, 0.82f}) { F(c, false); F(c + 0.05f, true); }
				break;
			case S_CRAB:
				// fader closed, four fingers tap it open on each stroke
				H(0, 0); H(0.5f, 1); H(1, 0);
				F(0, false);
				for (int s = 0; s < 2; s++)
					for (int i = 0; i < 4; i++) { float t = s * 0.5f + 0.04f + i * 0.1f; F(t, true); F(t + 0.04f, false); }
				break;
			case S_ORBIT:
				H(0, 0); H(0.25f, 1); H(0.5f, 0); H(0.75f, 1); H(1, 0);
				F(0, true); F(0.12f, false); F(0.16f, true); F(0.62f, false); F(0.66f, true);
				break;
			case S_TEAR:
				H(0, 0); H(0.2f, 0.5f); H(0.3f, 0.52f); H(0.5f, 1); H(1, 0);
				F(0, true);
				break;
			case S_SCRIBBLE: {
				// a tense forearm: small fast wobble while creeping forward
				H(0, 0);
				int n = 14;
				for (int i = 1; i <= n; i++) H(i / (float) n, 0.35f * i / n + ((i & 1) ? 0.14f : -0.04f));
				F(0, true); F(0.97f, false);
				break;
			}
			case S_STAB: H(0, 0); H(0.12f, 0.6f); F(0, true); F(0.55f, false); rel = 0.12f; break;
			case S_DRAG: H(0, 0); H(1, 0.55f); F(0, true); break;
			default: H(0, 0); H(1, 0); F(0, true); break;
		}
	}

	// Re-perform the gesture with human error.
	void humanise(int g, float human, float sloppy) {
		float c = SGESTURE_COMPLEXITY[g];
		float jt = 0.012f * human + 0.05f * sloppy * c;          // key timing wobble (fraction of gesture)
		float js = 0.14f * human + 0.1f * sloppy * c;            // stroke size wobble
		for (size_t i = 1; i + 1 < hand.size(); i++) {
			float lo = hand[i - 1].t + 0.02f, hi = hand[i + 1].t - 0.02f;
			hand[i].t = clampf(hand[i].t + rng.bi() * jt, lo, std::max(lo, hi));
			hand[i].pos *= 1.f + rng.bi() * js;
		}
		if (hand.size() == 2) hand[1].pos *= 1.f + rng.bi() * js;
		if (releaseAt > 0.f && hand.size() >= 2) releaseAt = hand[1].t;
		// fader: drift, lag behind the hand, occasionally a missed tap
		float jf = 0.008f * human + 0.045f * sloppy * c;
		float lag = 0.02f * sloppy * c * rng.uni();
		std::vector<FaderKey> out;
		for (size_t i = 0; i < fader.size(); i++) {
			FaderKey k = fader[i];
			if (i > 0) k.t = clampf(k.t + lag + rng.bi() * jf, 0.f, 0.995f);
			// a missed tap: skip an open/close pair (only on multi-tap moves)
			if (i > 0 && fader.size() > 4 && !k.open && rng.uni() < 0.18f * sloppy * c) {
				if (i + 1 < fader.size()) { i++; continue; }
			}
			out.push_back(k);
		}
		std::sort(out.begin(), out.end(), [](const FaderKey& a, const FaderKey& b) { return a.t < b.t; });
		fader = out;
	}

	// --- starting a gesture ------------------------------------------------
	// g: gesture. len: seconds. depth 0..1. human, sloppy 0..1.
	// LIVE: back = seconds ago; snapTol = seconds either side to look for a hit.
	// Static: cue01 = 0..1 through the record.
	void start(int g, int src, float len, float depth, float human, float sloppy, float back, float cue01, bool snap) {
		if (g <= S_OFF || g >= NUM_SGESTURES) { active = false; return; }
		std::shared_ptr<Record> r = record(src);
		if (src != LIVE && (!r || r->size() < 64)) src = LIVE;
		cur = r;
		gesture = g;
		source = src;
		dur = std::max(len * (1.f + rng.bi() * 0.05f * human), 0.03f);
		t = 0.f;
		build(g, hand, fader, releaseAt);
		humanise(g, human, sloppy);
		// stroke length: a real baby scratch moves 1/8 to 1/2 a turn
		stroke = (0.03f + 0.5f * depth * depth) * std::min(dur, 0.6f) / 0.25f * 0.5f;
		if (src == LIVE) {
			float maxBack = LIVE_SECONDS - dur - stroke - 0.2f;
			back = clampf(back, 0.02f, maxBack);
			double a = (double) written - back * sr;
			if (snap) a = snapLive(a, 0.12f * std::min(back, 0.5f) + 0.02f);
			anchor = a;
		}
		else {
			double a = clampf(cue01, 0.f, 1.f) * (r->size() - 1);
			if (snap) a = r->snap((int) a, (int) (0.25f * r->sr));
			anchor = a;
		}
		pos = 0.f; vel = 0.f; released = false;
		active = true;
	}

	// strongest rise in the live envelope near idx
	double snapLive(double idx, float tolSec) {
		int tol = std::max(1, (int) (tolSec * sr / 32.f));
		double centre = idx / 32.0;
		double nowEnv = (double) written / 32.0;
		int best = 0; float bv = 0.f;
		for (int k = -tol; k <= tol; k++) {
			double e = centre + k;
			int d = (int) (nowEnv - e);
			if (d < 3 || d >= liveEnv.n - 4) continue;
			float rise = liveEnv.at(d - 1) - liveEnv.at(d + 1);
			if (rise > bv) { bv = rise; best = k; }
		}
		if (bv < 0.02f) return idx;
		return (centre + best) * 32.0 - 0.002 * sr;
	}

	// GRAB: freeze the last `seconds` of live input into a record
	std::shared_ptr<Record> grab(float seconds) {
		auto r = std::make_shared<Record>();
		r->sr = sr;
		int n = (int) (clampf(seconds, 0.2f, LIVE_SECONDS - 0.2f) * sr);
		n = std::min<int>(n, (int) std::min<uint64_t>(written, (uint64_t) live[0].n - 8));
		if (n < 64) return nullptr;
		r->L.resize(n); r->R.resize(n);
		for (int i = 0; i < n; i++) { r->L[i] = live[0].at(n - 1 - i); r->R[i] = live[1].at(n - 1 - i); }
		r->analyse();
		return r;
	}

	// --- per sample --------------------------------------------------------
	inline float handAt(float u) const {
		if (hand.empty()) return 0.f;
		if (u <= hand.front().t) return hand.front().pos;
		for (size_t i = 0; i + 1 < hand.size(); i++) {
			if (u <= hand[i + 1].t) {
				float span = std::max(hand[i + 1].t - hand[i].t, 1e-4f);
				float s = (u - hand[i].t) / span;
				float m = s * s * s * (10.f + s * (-15.f + 6.f * s));   // minimum jerk
				return hand[i].pos + (hand[i + 1].pos - hand[i].pos) * m;
			}
		}
		return hand.back().pos;
	}
	inline bool faderAt(float u) const {
		bool o = true;
		for (const auto& k : fader) { if (k.t <= u) o = k.open; else break; }
		return o;
	}

	// curve 0..1 (smooth .. razor cut). mode: false = INSERT, true = OVER.
	inline void process(float inL, float inR, float curve, bool over, float duck, bool hamster,
	                    float& outL, float& outR) {
		endPulse = false;
		// record the live input + its envelope
		live[0].push(inL); live[1].push(inR);
		float x = envHP.highpass(0.5f * (inL + inR), svfG(300.f, sr), 1.f);
		envFollow += coefFor(0.003f, sr) * (std::fabs(x) - envFollow);
		if (++envDiv >= 32) { envDiv = 0; liveEnv.push(envFollow); }
		written++;

		float target = 0.f;
		bool open = true;
		if (active) {
			float u = t / dur;
			t += 1.f / sr;
			if (u >= 1.f) { active = false; endPulse = true; }
			else {
				open = faderAt(u);
				if (releaseAt >= 0.f && u >= releaseAt) released = true;
				target = handAt(u) * stroke;
			}
		}
		if (hamster) open = !open;
		// the record: follows the hand through a little platter mass, or the
		// motor once let go (catches up to normal speed in ~0.12 s)
		float dt = 1.f / sr;
		if (released) {
			vel += (1.f - vel) * coefFor(0.12f, sr);
			pos += vel * dt;
		}
		else {
			const float w = 2.f * PI * 45.f;
			float acc = w * w * (target - pos) - 2.f * w * vel;
			vel += acc * dt;
			pos += vel * dt;
		}
		handOut = active ? clampf(target / std::max(stroke, 1e-4f), 0.f, 1.2f) : 0.f;

		// fader travel: razor = 0.4 ms and cuts in at the first millimetre,
		// smooth = 6 ms and a gradual curve
		float ramp = 0.0004f + 0.0056f * (1.f - curve);
		faderPos += clampf((open ? 1.f : 0.f) - faderPos, -dt / ramp, dt / ramp);
		float k = 1.f + 30.f * curve * curve;
		faderGain = std::min(1.f, faderPos * k);
		if (curve < 0.3f) faderGain = faderGain * faderGain * (3.f - 2.f * faderGain);

		mixEnv += coefFor(0.003f, sr) * ((active ? 1.f : 0.f) - mixEnv);
		if (mixEnv < 0.0005f && !active) { outL = inL; outR = inR; return; }

		float sL = 0.f, sR = 0.f;
		if (source == LIVE) {
			double idx = anchor + pos * sr;
			needleIdx = idx;
			double d = (double) written - 1.0 - idx;
			sL = live[0].read((float) d);
			sR = live[1].read((float) d);
		}
		else {
			const std::shared_ptr<Record>& r = cur;
			if (r) {
				double idx = anchor + pos * r->sr;
				needleIdx = idx;
				r->read(idx, sL, sR);
			}
		}
		float gdc = svfG(22.f, sr);
		sL = dc[0].highpass(sL, gdc, 1.4f) * faderGain;
		sR = dc[1].highpass(sR, gdc, 1.4f) * faderGain;
		if (over) {
			float d = 1.f - duck * mixEnv;
			outL = inL * d + sL * mixEnv;
			outR = inR * d + sR * mixEnv;
		}
		else {
			outL = inL + (sL - inL) * mixEnv;
			outR = inR + (sR - inR) * mixEnv;
		}
	}
};

} // namespace vinyl
