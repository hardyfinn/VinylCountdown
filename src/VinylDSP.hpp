// VinylCountdown sound engines. No Rack code in here, so test/render.cpp can
// run everything offline.
//
// House rule for this plugin: every module processes LIVE audio and never
// adds more than 100 ms of latency. Nothing here speeds audio up or slows it
// down for longer than a few milliseconds.
//
//   Platter      off-centre pressing wow (fixed 5 ms latency)
//   InnerGroove  stylus tracing loss, tracing distortion and mistracking as
//                the needle moves from the outer edge to the label (0 latency)
//   Lathe        cutting-room chain: hot cut, elliptic (mono bass) filter,
//                HF limiter and groove echo from neighbouring grooves (0 latency)
//   RIAA         the record EQ curves as a send/return sandwich (0 latency)
//   Trainwreck   two live decks that can't quite beatmatch (<= 100 ms)
//   Stylus       cartridge physics: load resonance, crosstalk, azimuth,
//                vertical mistracking (< 0.2 ms)
//   Pressing     GENERATION (copy of a copy) and WEAR (dubplate wear) (0 latency)
//   Scratch      scratch-gesture sequencer: live audio passes straight through;
//                a gesture step scratches the audio that just went past
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace vinyl {

static const float PI = 3.14159265358979f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float coefFor(float seconds, float sr) {
	return 1.f - std::exp(-1.f / std::max(seconds * sr, 1.f));
}

// Zero-delay state-variable filter (Cytomic / Simper)
struct SVF {
	float ic1 = 0.f, ic2 = 0.f;
	void reset() { ic1 = ic2 = 0.f; }
	// g = tan(pi fc / sr), k = 1 / Q
	inline void process(float v0, float g, float k, float& lp, float& bp, float& hp) {
		float a1 = 1.f / (1.f + g * (g + k));
		float a2 = g * a1;
		float a3 = g * a2;
		float v3 = v0 - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		lp = v2; bp = v1; hp = v0 - k * v1 - v2;
	}
	inline float lowpass(float v0, float g, float k) { float l, b, h; process(v0, g, k, l, b, h); return l; }
	inline float bandpass(float v0, float g, float k) { float l, b, h; process(v0, g, k, l, b, h); return b; }
	inline float highpass(float v0, float g, float k) { float l, b, h; process(v0, g, k, l, b, h); return h; }
};
inline float svfG(float fc, float sr) {
	fc = clampf(fc, 5.f, sr * 0.45f);
	return std::tan(PI * fc / sr);
}

// one-pole high shelf (used for the cutting-head pre/de-emphasis)
struct OnePole {
	float z = 0.f;
	void reset() { z = 0.f; }
	inline float lp(float x, float a) { z += a * (x - z); return z; }
};

// Circular buffer. read(d) = the sample d samples ago (d = 0 is the newest),
// 4-point Hermite interpolation for fractional delays.
struct Ring {
	std::vector<float> b;
	int n = 4, w = 0;
	void init(int size) { n = std::max(size, 8); b.assign(n, 0.f); w = 0; }
	void clear() { std::fill(b.begin(), b.end(), 0.f); }
	inline void push(float v) { w++; if (w >= n) w = 0; b[w] = v; }
	inline float at(int d) const { int i = w - d; while (i < 0) i += n; return b[i]; }
	inline float read(float d) const {
		d = clampf(d, 1.f, (float) (n - 4));
		int i = (int) d;
		float f = d - (float) i;
		// samples at delays i-1, i, i+1, i+2 (newer to older)
		float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
		float c0 = x0;
		float c1 = 0.5f * (x1 - xm1);
		float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * f + c2) * f + c1) * f + c0;
	}
};

struct Rng {
	uint32_t s = 0x9E3779B9u;
	inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	inline float uni() { return (next() >> 8) * (1.f / 16777216.f); }        // 0..1
	inline float bi() { return uni() * 2.f - 1.f; }                          // -1..1
};

// Speeds
static const float RPM_VALUES[3] = {100.f / 3.f, 45.f, 78.f};
static const char* const RPM_NAMES[3] = {"33", "45", "78"};

// Groove radii on a 12" LP (mm). Outer edge of the music to the run-out.
static const float R_OUTER = 146.f;
static const float R_INNER = 60.f;

// ============================================================================
// PLATTER: off-centre pressing.
// The hole is punched OFFSET mm away from the true centre of the spiral, so
// the needle sees the groove speed up and slow down once per revolution. The
// speed ratio is 1 + (offset / radius) * sin(angle). Live audio can do this
// with a short delay line whose length swings by +-(offset/radius)/omega.
// That is at most 4.8 ms (1 mm, label area, 33 rpm), so the latency is fixed
// at 5 ms and never moves.
// ============================================================================
struct Platter {
	static constexpr float LATENCY = 0.005f;   // seconds, fixed
	float sr = 48000.f;
	Ring buf[2];
	double phase = 0.0;        // 0..1, one revolution
	float sOffset = 0.f, sRadius = R_OUTER;  // smoothed controls
	// outputs for CV
	float speed = 1.f;         // instantaneous speed ratio
	bool revTick = false;      // true on the sample a revolution starts

	void setSampleRate(float s) {
		sr = s;
		int n = (int) (LATENCY * 2.f * sr) + 16;
		buf[0].init(n); buf[1].init(n);
	}
	void reset() { phase = 0.0; }

	// offsetMm 0..1, radiusMm R_INNER..R_OUTER, rpm
	inline void process(float inL, float inR, float offsetMm, float radiusMm, float rpm, float& outL, float& outR) {
		float a = coefFor(0.03f, sr);
		sOffset += a * (offsetMm - sOffset);
		sRadius += a * (radiusMm - sRadius);
		float revHz = rpm / 60.f;
		float omega = 2.f * PI * revHz;
		phase += revHz / sr;
		revTick = false;
		if (phase >= 1.0) { phase -= 1.0; revTick = true; }
		float ratio = sOffset / std::max(sRadius, 10.f);
		float swing = std::min(ratio / omega, LATENCY - 4.f / sr);  // seconds
		float ang = 2.f * PI * (float) phase;
		// read position t - d(t), d = L + swing*cos(ang)  ->  speed = 1 + ratio*sin(ang)
		float d = (LATENCY + swing * std::cos(ang)) * sr;
		speed = 1.f + swing * omega * std::sin(ang);
		buf[0].push(inL); buf[1].push(inR);
		outL = buf[0].read(d);
		outR = buf[1].read(d);
	}
	// the wow as 1 V/oct pitch CV (so an oscillator can wobble in sync)
	float wowVolts() const { return std::log2(std::max(speed, 0.5f)); }
};

// ============================================================================
// INNER GROOVE: the needle's position across the side.
// Groove speed past the needle is 2*pi*r*rpm/60, about 0.51 m/s at the outer
// edge of a 33 and 0.21 m/s near the label. Slower groove = shorter recorded
// wavelengths, so the stylus tip can't trace the top end (loss), traces the
// waveform's curves unevenly (even-order "tracing" distortion, worse in the
// vertical = stereo-difference plane) and, on loud fast transients, leaves
// the groove wall (mistracking: slew-limited splatter).
// ============================================================================
struct InnerGroove {
	float sr = 48000.f;
	SVF trM[2], trS[2];        // tracing loss: two cascaded 2-poles, mid and side
	SVF hpM, hpS;              // band that drives the tracing distortion
	SVF spl;                   // splatter colour
	float yM = 0.f, yS = 0.f;  // slew-limited state
	float splEnv = 0.f;
	float dcM = 0.f, dcS = 0.f;
	Rng rng;
	float sV = 1.f, sStylus = 0.f;
	// meters
	float mistrack = 0.f;      // 0..1, how hard it is mistracking now

	// stylus minor radius relative to conical (18 um): elliptical 7 um, Shibata 3 um
	static constexpr float STYLUS_R[3] = {1.f, 0.39f, 0.17f};
	static constexpr float STYLUS_FC[3] = {15000.f, 22000.f, 30000.f}; // -3 dB at outer edge, 33 rpm

	void setSampleRate(float s) { sr = s; }
	void reset() {
		for (int i = 0; i < 2; i++) { trM[i].reset(); trS[i].reset(); }
		hpM.reset(); hpS.reset(); spl.reset();
		yM = yS = 0.f; splEnv = 0.f; dcM = dcS = 0.f;
	}

	// relative groove speed (1 = outer edge of a 33)
	static float grooveSpeed(float radius01, float rpm) {
		float r = R_OUTER - clampf(radius01, 0.f, 1.f) * (R_OUTER - R_INNER);
		return (r / R_OUTER) * (rpm / (100.f / 3.f));
	}

	// radius01: 0 outer edge .. 1 label. stylus 0/1/2. track 0..1 (light..solid).
	// cut 0..1: how hot the record was cut (input gain into the groove).
	inline void process(float inL, float inR, float radius01, float rpm, int stylus, float track, float cut,
	                    float& outL, float& outR) {
		stylus = std::max(0, std::min(2, stylus));
		float a = coefFor(0.02f, sr);
		sV += a * (grooveSpeed(radius01, rpm) - sV);
		float v = std::max(sV, 0.2f);
		float rs = STYLUS_R[stylus];

		float gain = 0.5f + 1.5f * cut * cut + 0.5f * cut;   // 0.5x .. 2.5x
		float m = 0.5f * (inL + inR) * gain;
		float s = 0.5f * (inL - inR) * gain;

		// 1) tracing loss (top end falls with groove speed); vertical a bit worse
		float fc = STYLUS_FC[stylus] * v;
		float gM = svfG(fc, sr), gS = svfG(fc * 0.8f, sr);
		m = trM[1].lowpass(trM[0].lowpass(m, gM, 1.6f), gM, 1.6f);
		s = trS[1].lowpass(trS[0].lowpass(s, gS, 1.6f), gS, 1.6f);

		// 2) tracing distortion: second harmonic of the upper band, grows as 1/v^2
		//    and with the stylus radius; twice as strong in the vertical plane
		float inv = 1.f / (v * v);
		float k = 0.01f * rs * inv;
		float hm = hpM.highpass(m, svfG(1500.f, sr), 0.9f);
		float hs = hpS.highpass(s, svfG(1500.f, sr), 0.9f);
		float dM = k * hm * std::fabs(hm);
		float dS = 2.f * k * hs * std::fabs(hs);
		dcM += coefFor(0.05f, sr) * (dM - dcM);
		dcS += coefFor(0.05f, sr) * (dS - dcS);
		m += dM - dcM;
		s += dS - dcS;

		// 3) mistracking: the needle can only follow so steep a slope.
		//    Ability falls with groove speed, rises with tracking force.
		float smax = (60000.f + 340000.f * track) * v * (1.f + 0.5f * (1.f - rs));  // V/s, finer tips track better
		float maxStep = smax / sr;
		float dm = m - yM, ds = s - yS;
		float overM = std::fabs(dm) / maxStep, overS = std::fabs(ds) / (maxStep * 0.7f);
		yM += maxStep * std::tanh(dm / maxStep);
		yS += maxStep * 0.7f * std::tanh(ds / (maxStep * 0.7f));
		float over = std::max(overM, overS);
		float hit = clampf((over - 0.8f) * 0.8f, 0.f, 1.f);
		splEnv += (hit > splEnv ? coefFor(0.0005f, sr) : coefFor(0.03f, sr)) * (hit - splEnv);
		mistrack = splEnv;
		// splatter: gritty band of noise riding the mistracked signal
		float nz = spl.bandpass(rng.bi(), svfG(3200.f, sr), 1.2f) * splEnv * (0.4f + 0.3f * std::fabs(yM));

		float mo = yM + nz, so = yS + nz * 0.6f * rng.bi();
		float g = 1.f / gain;
		outL = (mo + so) * g;
		outR = (mo - so) * g;
	}
};
constexpr float InnerGroove::STYLUS_R[3];
constexpr float InnerGroove::STYLUS_FC[3];

// ============================================================================
// LATHE: what the cutting room does to the music on the way to the record.
// ============================================================================
struct Lathe {
	float sr = 48000.f;
	// cutting head drive: pre-emphasis -> saturation -> de-emphasis (per channel)
	OnePole pre[2], de[2];
	// elliptic filter (side high-pass, 2 cascaded 2-poles = 4th order)
	SVF ell[2];
	// HF limiter
	SVF hfBand[2];
	float hfEnv = 0.f;
	float hfGain = 1.f;        // for the light
	// groove echo
	Ring echo[2];
	SVF echoTone[2];
	float sRev = 1.8f;         // smoothed revolution length (s)
	float maxSeconds = 8.5f;

	void setSampleRate(float s) {
		sr = s;
		int n = (int) (maxSeconds * sr) + 16;
		echo[0].init(n); echo[1].init(n);
	}
	void reset() {
		for (int i = 0; i < 2; i++) { pre[i].reset(); de[i].reset(); ell[i].reset(); hfBand[i].reset(); echoTone[i].reset(); echo[i].clear(); }
		ell[1].reset();
		hfEnv = 0.f; hfGain = 1.f;
	}

	static float drive1(float x) {
		// cutting amp + head: soft, slightly asymmetric
		float y = std::tanh(x * 0.2f + 0.02f) - std::tanh(0.02f);
		return y * 5.f;
	}

	// drive 0..1, ellHz 0 = off else 20..300, hfAmt 0..1 (0 = off),
	// echoAmt 0..1, revs 1 or 2, revSeconds = one revolution
	inline void process(float inL, float inR, float drive, float ellHz, float hfAmt, float echoAmt, int revs,
	                    float revSeconds, float& outL, float& outR, float& wetL, float& wetR) {
		float x[2] = {inL, inR};

		// 1) hot cut: pre-emphasis lifts the top ~ +12 dB, saturate, undo
		float pg = 1.f + 5.f * drive * drive;
		float aPre = coefFor(1.f / (2.f * PI * 2100.f), sr);
		for (int c = 0; c < 2; c++) {
			float lo = pre[c].lp(x[c], aPre);
			float emph = lo + 4.f * (x[c] - lo);           // high shelf +12 dB
			float sat = drive1(emph * pg) / pg;
			float lo2 = de[c].lp(sat, aPre);
			float dem = lo2 + 0.25f * (sat - lo2);          // inverse shelf
			x[c] = drive > 0.001f ? dem : x[c];
		}

		// 2) elliptic filter: below ellHz the bass is folded to mono
		if (ellHz > 1.f) {
			float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]);
			float g = svfG(ellHz, sr);
			s = ell[1].highpass(ell[0].highpass(s, g, 1.84776f), g, 0.76537f);
			x[0] = m + s; x[1] = m - s;
		}

		// 3) HF limiter: holds down loud highs (cymbals, sibilance) the way a
		//    cutting engineer protects the stylus; stereo linked
		if (hfAmt > 0.001f) {
			float g = svfG(6000.f, sr);
			float b0 = hfBand[0].highpass(x[0], g, 1.2f);
			float b1 = hfBand[1].highpass(x[1], g, 1.2f);
			float e = std::max(std::fabs(b0), std::fabs(b1));
			hfEnv += (e > hfEnv ? coefFor(0.0003f, sr) : coefFor(0.06f, sr)) * (e - hfEnv);
			float thr = 4.f * std::pow(0.04f, hfAmt);   // 4 V .. 0.16 V
			float gain = hfEnv > thr ? thr / hfEnv : 1.f;
			hfGain = gain;
			x[0] -= b0 * (1.f - gain);
			x[1] -= b1 * (1.f - gain);
		}
		else hfGain = 1.f;

		// 4) groove echo: neighbouring grooves print into this one, so a dull
		//    ghost of the music arrives one (or two) revolutions late
		sRev += coefFor(0.2f, sr) * (clampf(revSeconds, 0.3f, maxSeconds / 2.f - 0.05f) - sRev);
		float lvl = echoAmt <= 0.001f ? 0.f : std::pow(10.f, (-48.f + 34.f * echoAmt) / 20.f);  // -48 .. -14 dB
		for (int c = 0; c < 2; c++) {
			echo[c].push(x[c]);
			float e1 = echo[c].read(sRev * sr);
			float e = e1;
			if (revs >= 2) e = 0.6f * e1 + 0.8f * echo[c].read(2.f * sRev * sr);
			e = echoTone[c].bandpass(e, svfG(1800.f, sr), 0.6f) * 1.3f;
			float w = e * lvl;
			if (c == 0) { wetL = w; outL = x[0] + w; }
			else { wetR = w; outR = x[1] + w; }
		}
	}
};

// Biquad from an analogue prototype via the bilinear transform.
// H(s) = (b0 + b1 s + b2 s^2) / (a0 + a1 s + a2 s^2)
struct Biquad {
	float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	float z1 = 0, z2 = 0;
	void reset() { z1 = z2 = 0.f; }
	void fromAnalog(double B0, double B1, double B2, double A0, double A1, double A2, double sr) {
		double K = 2.0 * sr;
		double nb0 = B0 + B1 * K + B2 * K * K, nb1 = 2 * B0 - 2 * B2 * K * K, nb2 = B0 - B1 * K + B2 * K * K;
		double na0 = A0 + A1 * K + A2 * K * K, na1 = 2 * A0 - 2 * A2 * K * K, na2 = A0 - A1 * K + A2 * K * K;
		b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
		a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
	}
	void scale(float g) { b0 *= g; b1 *= g; b2 *= g; }
	// magnitude at f (Hz)
	float mag(float f, float sr) const {
		double w = 2.0 * PI * f / sr;
		double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
		double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
		double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
		return (float) std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
	}
	inline float process(float x) {
		float y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
};

// ============================================================================
// RIAA SANDWICH: records are cut with the treble boosted and the bass cut
// (the "cut" curve) and played back through the inverse (the "play" curve).
// Anything that happens in between - here: SEND/RETURN, or the built-in
// drive - hits the hats hard and leaves the kick alone. Pick different
// curves for cut and play and the overall tone tilts, the way old records
// sound on the wrong preamp setting.
// Time constants (bass turnover, shelf, treble) in microseconds:
// ============================================================================
static const int NUM_CURVES = 5;
static const char* const CURVE_NAMES[NUM_CURVES] = {"RIAA", "Columbia LP", "NAB", "Decca FFRR", "AES"};
static const char* const CURVE_SHORT[NUM_CURVES] = {"RIAA", "COL", "NAB", "FFRR", "AES"};
static const double CURVE_T[NUM_CURVES][3] = {
	{3180e-6, 318e-6, 75e-6},    // RIAA (1954)
	{1590e-6, 318e-6, 100e-6},   // Columbia LP (1948)
	{3180e-6, 318e-6, 100e-6},   // NAB (1949)
	{3180e-6, 450e-6, 50e-6},    // Decca FFRR (LP, approx.)
	{3180e-6, 398e-6, 63.6e-6},  // AES (1951, approx.)
};

struct RiaaPair {
	Biquad cut[2], play[2];
	int cutCurve = -1, playCurve = -1;
	float sr = 48000.f;
	void setSampleRate(float s) { sr = s; cutCurve = playCurve = -1; }
	void reset() { for (int c = 0; c < 2; c++) { cut[c].reset(); play[c].reset(); } }
	// playback: (1 + s t2) / ((1 + s t1)(1 + s t3)), 0 dB at 1 kHz
	static void makePlay(Biquad& b, int k, float sr) {
		double t1 = CURVE_T[k][0], t2 = CURVE_T[k][1], t3 = CURVE_T[k][2];
		b.fromAnalog(1.0, t2, 0.0, 1.0, t1 + t3, t1 * t3, sr);
		b.scale(1.f / b.mag(1000.f, sr));
	}
	// cutting: the inverse, plus a pole at 50 kHz (3.18 us) so it stays finite
	static void makeCut(Biquad& b, int k, float sr) {
		double t1 = CURVE_T[k][0], t2 = CURVE_T[k][1], t3 = CURVE_T[k][2], t4 = 3.18e-6;
		b.fromAnalog(1.0, t1 + t3, t1 * t3, 1.0, t2 + t4, t2 * t4, sr);
		b.scale(1.f / b.mag(1000.f, sr));
	}
	void set(int c, int p) {
		if (c != cutCurve) { cutCurve = c; makeCut(cut[0], c, sr); makeCut(cut[1], c, sr); }
		if (p != playCurve) { playCurve = p; makePlay(play[0], p, sr); makePlay(play[1], p, sr); }
	}
};

struct Riaa {
	RiaaPair eq;
	float sr = 48000.f;
	void setSampleRate(float s) { sr = s; eq.setSampleRate(s); }
	void reset() { eq.reset(); }
	inline void toSend(float inL, float inR, int cutCurve, int playCurve, float& sL, float& sR) {
		eq.set(cutCurve, playCurve);
		// the cut curve is about +20 dB at 20 kHz; scale the send so the
		// return path sees sensible levels
		sL = eq.cut[0].process(inL) * 0.5f;
		sR = eq.cut[1].process(inR) * 0.5f;
	}
	// built-in drive (used when RETURN is not patched)
	static inline float drive(float x, float d) {
		if (d <= 0.001f) return x;
		float g = 1.f + 9.f * d * d;
		return 5.f * std::tanh(x * g / 5.f) / std::sqrt(g) * (1.f + 0.6f * d);
	}
	inline void fromReturn(float rL, float rR, float& oL, float& oR) {
		oL = eq.play[0].process(rL * 2.f);
		oR = eq.play[1].process(rR * 2.f);
	}
};

// ============================================================================
// TRAINWRECK: two live decks that never quite stay beatmatched.
// Deck B drifts against deck A (its pitch fader is never exactly right),
// a DJ notices once the beats flam far enough apart and nudges the platter
// back, usually overshooting a little. All of it lives inside a 100 ms delay
// window: in LATE range A is live and B can only fall behind (0..100 ms);
// in BOTH range both decks sit 50 ms back so B can also run ahead.
// ============================================================================
struct Trainwreck {
	static constexpr float MAX_MS = 100.f;
	float sr = 48000.f;
	Ring a[2], b[2];
	Rng rng;
	float err = 0.f;          // B's speed error (fraction, e.g. 0.004 = +0.4 %)
	float errTarget = 0.f;
	float wander = 0.f;       // time to next change of errTarget
	float offMs = 0.f;        // B offset relative to A (ms), + = B late
	float nudge = 0.f;        // active correction speed (fraction)
	float nudgeLeft = 0.f;    // seconds of correction left
	bool nudgeFired = false;  // true on the sample a nudge starts
	float sManual = 0.f;

	void setSampleRate(float s) {
		sr = s;
		int n = (int) (0.11f * sr) + 16;
		for (int c = 0; c < 2; c++) { a[c].init(n); b[c].init(n); }
	}
	void reset() { err = errTarget = offMs = nudge = nudgeLeft = 0.f; }
	void startNudge(float toMs) {
		float dist = offMs - toMs;
		if (std::fabs(dist) < 0.5f) return;
		// a hand on the platter: 2..6 % for as long as it takes, plus a
		// small random overshoot
		float spd = 0.02f + 0.04f * rng.uni();
		float over = 1.f + 0.25f * rng.uni();
		nudge = dist > 0.f ? spd : -spd;   // B late -> push it faster
		nudgeLeft = std::fabs(dist) * over / 1000.f / spd;
		nudgeFired = true;
		// and the DJ touches the pitch fader too: error shrinks, rarely to 0
		errTarget *= 0.3f + 0.4f * rng.uni();
		err *= 0.5f;
	}

	// drift 0..1, slop 0..1 (how far it gets before a nudge), manualMs -50..50,
	// both: B can run ahead, xfade 0..1 (A..B), syncNow: trigger a correction
	inline void process(const float inA[2], const float inB[2], float drift, float slop, float manualMs, bool both,
	                    float xfade, bool syncNow, float outMix[2], float outB[2]) {
		nudgeFired = false;
		sManual += coefFor(0.05f, sr) * (manualMs - sManual);
		// drift: the pitch error wanders (up to +-0.8 %)
		wander -= 1.f / sr;
		if (wander <= 0.f) {
			wander = 0.8f + 3.f * rng.uni();
			errTarget = rng.bi() * 0.008f * drift * drift;
		}
		err += coefFor(1.5f, sr) * (errTarget - err);

		float lo = both ? -MAX_MS * 0.5f : 0.f, hi = both ? MAX_MS * 0.5f : MAX_MS;
		float threshold = 3.f + 40.f * slop;     // ms off before the DJ reacts
		if (syncNow) startNudge(sManual);
		else if (nudgeLeft <= 0.f && std::fabs(offMs - sManual) > threshold) startNudge(sManual);

		float speedDiff = err;            // + = B faster = B moves ahead (offset falls)
		if (nudgeLeft > 0.f) { speedDiff += nudge; nudgeLeft -= 1.f / sr; }
		offMs -= speedDiff * 1000.f / sr;
		// the window edges act like a hard stop (never more than 100 ms)
		// at the window edges the deck bumps and drifts back the other way
		if (offMs < lo) { offMs = lo; if (err > 0.f) { err = -0.3f * err; errTarget = -std::fabs(errTarget); } }
		if (offMs > hi) { offMs = hi; if (err < 0.f) { err = -0.3f * err; errTarget = std::fabs(errTarget); } }

		float baseA = both ? MAX_MS * 0.5f : 0.f;
		float dA = baseA * 0.001f * sr;
		float dB = (baseA + offMs) * 0.001f * sr;
		float gA = std::cos(clampf(xfade, 0.f, 1.f) * PI * 0.5f);
		float gB = std::sin(clampf(xfade, 0.f, 1.f) * PI * 0.5f);
		for (int c = 0; c < 2; c++) {
			a[c].push(inA[c]); b[c].push(inB[c]);
			float va = dA < 0.5f ? inA[c] : a[c].read(dA);
			float vb = dB <= 0.001f ? inB[c] : b[c].read(std::max(dB, 1.f));
			outB[c] = vb;
			outMix[c] = va * gA + vb * gB;
		}
	}
};

// ============================================================================
// STYLUS: the cartridge and needle.
//  LOAD      moving-magnet cartridges resonate with the cable/preamp
//            capacitance: more pF = lower, peakier treble bump then a fall.
//            MC (moving coil) is flat to the top with a far-off peak.
//  CROSSTALK some of each channel leaks into the other (more in the treble)
//  AZIMUTH   the needle leans: one channel arrives a hair late (<0.2 ms)
//            and duller - wide in stereo, phasey in mono
//  VERTICAL  the stereo difference is cut as up-down groove movement, which
//            a needle tracks worse than side-to-side: width distorts first
// ============================================================================
struct Stylus {
	float sr = 48000.f;
	SVF resL[2], resR[2];
	SVF xtHP[2];
	Ring az;
	SVF azLP;
	SVF vertHP;
	float sLoad = 0.4f, sAz = 0.f;

	void setSampleRate(float s) { sr = s; az.init((int) (0.002f * sr) + 16); }
	void reset() { for (int i = 0; i < 2; i++) { resL[i].reset(); resR[i].reset(); xtHP[i].reset(); } azLP.reset(); vertHP.reset(); }

	// mc: false = moving magnet. load 0..1 (100..500 pF). crosstalk 0..1 (-45..-12 dB).
	// azimuth -1..1 (+-3 degrees). vertical 0..1.
	inline void process(float inL, float inR, bool mc, float load, float crosstalk, float azimuth, float vertical,
	                    float& outL, float& outR) {
		float a = coefFor(0.03f, sr);
		sLoad += a * (load - sLoad);
		sAz += a * (azimuth - sAz);

		// vertical mistracking on the side signal only
		float m = 0.5f * (inL + inR), s = 0.5f * (inL - inR);
		if (vertical > 0.001f) {
			float g = 1.f + 6.f * vertical * vertical;
			float hs = vertHP.highpass(s, svfG(800.f, sr), 0.8f);
			float dist = 5.f * std::tanh(hs * g / 5.f) / g;
			// even-order wobble: vertical (pinch) distortion
			s = s - hs + dist + 0.03f * vertical * hs * std::fabs(hs);
		}
		float x[2] = {m + s, m - s};

		// crosstalk (frequency dependent: grows ~6 dB/oct above 1 kHz)
		float xt = std::pow(10.f, (-45.f + 33.f * crosstalk) / 20.f);
		float leak[2];
		for (int c = 0; c < 2; c++) {
			float l, b, h; xtHP[c].process(x[c], svfG(1000.f, sr), 0.7f, l, b, h);
			leak[c] = (x[c] * 0.4f + h * 1.6f) * xt;
		}
		float y[2] = {x[0] + leak[1], x[1] + leak[0]};

		// cartridge response
		float fc, k;
		if (mc) { fc = 40000.f; k = 0.9f; }
		else {
			float pf = 100.f + 400.f * sLoad;
			fc = 21000.f * std::sqrt(150.f / pf);  // ~21 kHz at 150 pF, ~11.5 kHz at 500 pF
			k = 1.25f - 0.85f * sLoad;              // peak grows with load
		}
		float gR = svfG(fc, sr);
		for (int c = 0; c < 2; c++) {
			SVF* r = c == 0 ? resL : resR;
			y[c] = r[1].lowpass(r[0].lowpass(y[c], gR, k), svfG(fc * 1.8f, sr), 1.4f);
		}

		// azimuth: one channel up to 0.15 ms late and duller
		float azd = std::fabs(sAz) * 0.00015f * sr;
		int late = sAz >= 0.f ? 1 : 0;
		az.push(y[late]);
		if (azd > 0.01f) {
			float v = az.read(std::max(azd, 1.f));
			if (azd < 1.f) v = y[late] + (az.read(1.f) - y[late]) * azd;
			y[late] = azLP.lowpass(v, svfG(16000.f - 7000.f * std::fabs(sAz), sr), 0.75f);
		}
		outL = y[0]; outR = y[1];
	}
};

// ============================================================================
// PRESSING: two macros.
//  GENERATION  lacquer (0) .. a late pressing from a tired stamper (1):
//              top end falls, noise floor and low rumble rise, a fine
//              "stitching" tick appears once per revolution, a little
//              compression from the vinyl compound.
//  WEAR        a dubplate (acetate) played to death: highs go first, then
//              the walls fizz on loud transients, crackle builds, and at the
//              far end the bass thins out.
// No memory: the knobs (and CV) are the whole story.
// ============================================================================
struct Pressing {
	float sr = 48000.f;
	SVF lp[2][2], fizzHP[2], lowCut[2], rumble, hiss[2], tickRes[2];
	float comp = 0.f;
	float phase = 0.f;
	Rng rng;
	float sGen = 0.f, sWear = 0.f;
	float tickExc[2] = {0.f, 0.f};

	void setSampleRate(float s) { sr = s; }
	void reset() {
		for (int c = 0; c < 2; c++) { lp[c][0].reset(); lp[c][1].reset(); fizzHP[c].reset(); lowCut[c].reset(); hiss[c].reset(); tickRes[c].reset(); }
		rumble.reset(); comp = 0.f;
	}

	inline void process(float inL, float inR, float gen, float wear, float& outL, float& outR) {
		float a = coefFor(0.03f, sr);
		sGen += a * (clampf(gen, 0.f, 1.f) - sGen);
		sWear += a * (clampf(wear, 0.f, 1.f) - sWear);
		float G = sGen, W = sWear;

		// top end: generation 20 kHz -> 8 kHz, wear takes it further to 3.5 kHz
		float fc = 20000.f * std::pow(0.4f, G) * std::pow(0.45f, W * 1.3f);
		float g = svfG(fc, sr);
		// fizz: worn groove walls distort loud highs
		float fizz = W * W * 0.06f;
		// bass thinning only near the end of the wear range
		float lowHz = 20.f + 160.f * std::pow(W, 3.f);
		// compound compression
		float x[2] = {inL, inR};
		float e = std::max(std::fabs(x[0]), std::fabs(x[1]));
		comp += (e > comp ? coefFor(0.005f, sr) : coefFor(0.2f, sr)) * (e - comp);
		float cg = 1.f / (1.f + G * 0.12f * std::max(comp - 2.f, 0.f));

		// once-per-revolution stitching tick (33 rpm) and crackle
		phase += (100.f / 3.f / 60.f) / sr;
		bool tick = false;
		if (phase >= 1.f) { phase -= 1.f; tick = true; }
		float crackleRate = W * W * 30.f + G * 2.f;         // per second
		bool crackle = rng.uni() < crackleRate / sr;
		if (tick && G > 0.15f) { float t = (G - 0.15f) * 0.6f; tickExc[0] += t; tickExc[1] += t * 0.8f; }
		if (crackle) { float amp = (0.2f + 0.8f * rng.uni()) * (0.4f + 1.4f * W) * (rng.uni() < 0.5f ? -1.f : 1.f); int c = rng.uni() < 0.5f ? 0 : 1; tickExc[c] += amp; tickExc[1 - c] += amp * 0.3f; }

		float hissAmt = 0.004f * std::pow(10.f, (G * 18.f + W * 14.f) / 20.f) * (G + W > 0.001f ? 1.f : 0.f);
		float rum = rumble.lowpass(rng.bi(), svfG(28.f, sr), 0.9f) * 0.25f * G * G;

		float y[2];
		for (int c = 0; c < 2; c++) {
			float v = x[c] * cg;
			if (fizz > 0.f) {
				float h = fizzHP[c].highpass(v, svfG(2500.f, sr), 0.8f);
				v += fizz * h * std::fabs(h) - fizz * 0.15f * h;
			}
			v = lp[c][1].lowpass(lp[c][0].lowpass(v, g, 1.3f), g, 1.0f);
			v = W > 0.05f ? lowCut[c].highpass(v, svfG(lowHz, sr), 0.75f) : v;
			float tk = tickRes[c].bandpass(tickExc[c], svfG(3000.f, sr), 1.0f);
			tickExc[c] = 0.f;
			float hs = hiss[c].bandpass(rng.bi(), svfG(5000.f, sr), 1.4f) * hissAmt;
			y[c] = v + tk * 3.f + hs + rum;
		}
		outL = y[0]; outR = y[1];
	}
};

// ============================================================================
// SCRATCH: a gesture sequencer. Live audio passes straight through (no
// latency). On a step that holds a gesture, the module grabs the audio from
// LOOKBACK steps ago and scratches it for the length of the step: a hand
// moves the record (HAND CV out) while a crossfader cuts it (FADER gate out).
// Then it drops straight back to the live signal.
// ============================================================================
static const int NUM_GESTURES = 9;
enum Gesture { G_OFF, G_BABY, G_CHIRP, G_TRANSFORM, G_FLARE, G_CRAB, G_ORBIT, G_TEAR, G_SCRIBBLE };
static const char* const GESTURE_NAMES[NUM_GESTURES] = {
	"Off (live)", "Baby", "Chirp", "Transformer", "Flare", "Crab", "Orbit", "Tear", "Scribble"
};
static const char* const GESTURE_SHORT[NUM_GESTURES] = {"--", "BABY", "CHIRP", "TRANS", "FLARE", "CRAB", "ORBIT", "TEAR", "SCRBL"};

// hand position (0..1 of the stroke) and fader (0/1) at step time u (0..1)
inline void gestureShape(int g, float u, float& hand, float& fader) {
	u = clampf(u, 0.f, 1.f);
	auto stroke = [](float t) { return 0.5f - 0.5f * std::cos(2.f * PI * t); };  // 0 -> 1 -> 0
	auto gate = [](float t, int n, float duty) { float f = t * n; return (f - std::floor(f)) < duty ? 1.f : 0.f; };
	switch (g) {
		default:
		case G_OFF: hand = 0.f; fader = 1.f; break;
		case G_BABY: hand = stroke(u); fader = 1.f; break;
		case G_CHIRP: hand = stroke(u); fader = (u < 0.32f || u > 0.9f) ? 1.f : 0.f; break;
		case G_TRANSFORM: hand = stroke(u); fader = gate(u, 8, 0.45f); break;
		case G_FLARE: hand = stroke(u); fader = (std::fabs(u - 0.25f) < 0.05f || std::fabs(u - 0.75f) < 0.05f) ? 0.f : 1.f; break;
		case G_CRAB: hand = stroke(u); fader = gate(u, 16, 0.5f); break;
		case G_ORBIT: hand = stroke(std::fmod(u * 2.f, 1.f)); fader = gate(u, 4, 0.8f); break;
		case G_TEAR: {
			// forward in two pushes with a stop between, then back in one
			if (u < 0.2f) hand = 0.5f * stroke(u / 0.4f);          // up to 0.5
			else if (u < 0.3f) hand = 0.5f;
			else if (u < 0.5f) hand = 0.5f + 0.5f * stroke((u - 0.3f) / 0.4f);
			else hand = stroke(u);
			fader = 1.f;
			break;
		}
		case G_SCRIBBLE: hand = (0.35f + 0.15f * std::sin(2.f * PI * u * 12.f)) * std::min(u * 10.f, 1.f); fader = 1.f; break;
	}
}

struct Scratch {
	float sr = 48000.f;
	float maxSeconds = 8.f;
	Ring buf[2];
	// current step
	int gesture = G_OFF;
	float stepLen = 0.25f;     // seconds (measured from the clock)
	float elapsed = 0.f;
	float lookback = 0.25f;    // seconds back where the grabbed audio starts
	float depth = 0.5f;        // stroke length as a fraction of the step
	float sHand = 0.f, sFader = 1.f, sScr = 0.f;
	// outputs
	float handOut = 0.f, faderOut = 1.f;
	SVF dc[2];   // a still record holds one sample value: block that DC

	void setSampleRate(float s) {
		sr = s;
		int n = (int) (maxSeconds * sr) + 16;
		buf[0].init(n); buf[1].init(n);
	}
	void reset() { gesture = G_OFF; elapsed = 0.f; sScr = 0.f; }

	// start a new step: g = gesture (G_OFF = live), len = step length in s,
	// back = how many seconds back the grab starts, d = depth 0..1
	void startStep(int g, float len, float back, float d) {
		gesture = g;
		stepLen = std::max(len, 0.02f);
		elapsed = 0.f;
		lookback = clampf(back, stepLen * 0.5f, maxSeconds - stepLen * 2.f - 0.05f);
		depth = clampf(d, 0.05f, 1.f);
	}

	inline void process(float inL, float inR, float& outL, float& outR) {
		buf[0].push(inL); buf[1].push(inR);
		float u = elapsed / stepLen;
		elapsed += 1.f / sr;
		bool active = gesture != G_OFF && u < 1.f;
		float hand = 0.f, fader = 1.f;
		if (active) gestureShape(gesture, u, hand, fader);
		// 1 ms hand smoothing, 1 ms fader ramp, 3 ms in/out of scratch mode
		sHand += coefFor(0.001f, sr) * (hand - sHand);
		sFader += coefFor(0.0008f, sr) * (fader - sFader);
		sScr += coefFor(0.003f, sr) * ((active ? 1.f : 0.f) - sScr);
		handOut = active ? sHand : 0.f;
		faderOut = fader;
		if (sScr < 0.0005f) { outL = inL; outR = inR; return; }
		// record position: grabbed point + hand stroke (seconds of record)
		float recPos = sHand * depth * stepLen;            // seconds ahead of the grab point
		float delay = (elapsed + lookback - recPos) * sr;  // how far back that is now
		float gdc = svfG(25.f, sr);
		float sL = dc[0].highpass(buf[0].read(delay), gdc, 1.4f) * sFader;
		float sR = dc[1].highpass(buf[1].read(delay), gdc, 1.4f) * sFader;
		outL = inL + (sL - inL) * sScr;
		outR = inR + (sR - inR) * sScr;
	}
};

} // namespace vinyl
