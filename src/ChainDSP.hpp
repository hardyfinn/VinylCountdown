// The rest of the record's journey, before and after Vinyl Countdown.
//   STUDIO          console -> master tape (+ Dolby A) -> mastering glue
//   LISTENING ROOM  power amp -> speaker -> room -> ears
// Same house rule as the rest of the plugin: live audio, at most 100 ms of
// latency (here: 0, plus 3 ms for tape flutter and < 0.7 ms for head angle).
// No Rack code, so test/render.cpp can run it offline. Rack volts (+-5 V).
#pragma once
#include "VinylDSP.hpp"
#include <initializer_list>

namespace vinyl {

// ---- RBJ biquad helpers ----------------------------------------------------
inline void bqSet(Biquad& q, double b0, double b1, double b2, double a0, double a1, double a2) {
	q.b0 = (float) (b0 / a0); q.b1 = (float) (b1 / a0); q.b2 = (float) (b2 / a0);
	q.a1 = (float) (a1 / a0); q.a2 = (float) (a2 / a0);
}
inline void bqPeak(Biquad& q, double f, double Q, double dB, double sr) {
	double A = std::pow(10.0, dB / 40.0), w = 2 * PI * std::min(f, sr * 0.45) / sr, al = std::sin(w) / (2 * Q);
	bqSet(q, 1 + al * A, -2 * std::cos(w), 1 - al * A, 1 + al / A, -2 * std::cos(w), 1 - al / A);
}
inline void bqLP(Biquad& q, double f, double Q, double sr) {
	double w = 2 * PI * std::min(f, sr * 0.45) / sr, al = std::sin(w) / (2 * Q), c = std::cos(w);
	bqSet(q, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}
inline void bqHP(Biquad& q, double f, double Q, double sr) {
	double w = 2 * PI * std::min(f, sr * 0.45) / sr, al = std::sin(w) / (2 * Q), c = std::cos(w);
	bqSet(q, (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
inline void bqShelf(Biquad& q, bool high, double f, double dB, double sr) {
	double A = std::pow(10.0, dB / 40.0), w = 2 * PI * std::min(f, sr * 0.45) / sr, c = std::cos(w);
	double al = std::sin(w) / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
	if (high)
		bqSet(q, A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
		      (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
	else
		bqSet(q, A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
		      (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
}
inline float onePoleA(float hz, float sr) { return 1.f - std::exp(-2.f * PI * std::min(hz, sr * 0.45f) / sr); }

// Re-design filters only when a control has moved (cheap per sample).
struct Changed {
	float last[6] = {-1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f};
	bool operator()(std::initializer_list<float> v) {
		bool ch = false; int i = 0;
		for (float x : v) { if (std::fabs(x - last[i]) > 1e-4f) { ch = true; last[i] = x; } i++; }
		return ch;
	}
	void reset() { for (float& x : last) x = -1e9f; }
};

// ============================================================================
// STUDIO 1: CONSOLE. Mic/line amps into transformers, then the mix bus.
//  DRIVE  how hot the channels are run (soft, slightly asymmetric clipping)
//  IRON   transformer colour: bass thickens and saturates first, top softens
//  XTALK  bus crosstalk: each side leaks a little of the other, mostly treble
// ============================================================================
struct Console {
	float sr = 48000.f;
	OnePole lo[2], hfR[2], xt[2];
	float dc[2] = {0.f, 0.f};
	void setSampleRate(float s) { sr = s; }
	void reset() { for (int c = 0; c < 2; c++) { lo[c].reset(); hfR[c].reset(); xt[c].reset(); dc[c] = 0.f; } }
	inline void process(float inL, float inR, float drive, float iron, float xtalk, float& outL, float& outR) {
		float in[2] = {inL, inR}, y[2];
		float g = std::pow(10.f, 18.f * drive / 20.f);
		float aLo = onePoleA(120.f, sr), aHf = onePoleA(30000.f - 16000.f * iron, sr), aDc = onePoleA(6.f, sr);
		float aXt = onePoleA(1000.f, sr);
		for (int c = 0; c < 2; c++) {
			float x = in[c] * 0.2f;
			// transformer: low band saturates, then a gentle top roll-off
			if (iron > 0.001f) {
				float l = lo[c].lp(x, aLo);
				float gi = 1.f + 5.f * iron;
				x = x - l + std::tanh(gi * l * 1.2f) / (gi * 1.2f) * (1.f + 0.25f * iron);
				x = hfR[c].lp(x, aHf);
			}
			// amp stage
			float v = std::tanh(g * x + 0.08f * drive) - std::tanh(0.08f * drive);
			x = v / std::pow(g, 0.85f);
			dc[c] += aDc * (x - dc[c]);
			y[c] = (x - dc[c]) * 5.f;
		}
		// crosstalk: the other channel's treble, up to about -14 dB
		float k = 0.2f * xtalk * xtalk;
		float hL = y[1] - xt[1].lp(y[1], aXt), hR = y[0] - xt[0].lp(y[0], aXt);
		outL = y[0] + k * (hL + 0.3f * y[1]);
		outR = y[1] + k * (hR + 0.3f * y[0]);
	}
};

// ============================================================================
// STUDIO 2: MASTER TAPE. The tape the lathe cuts from.
//  SAT      level onto tape: treble is boosted going on and cut coming off,
//           so loud highs squash first (tape's soft, dense saturation)
//  BUMP     head bump: a low resonance whose pitch follows SPEED
//  SPEED    7.5 / 15 / 30 ips: slower = lower bump and less top end
//  FLUTTER  scrape flutter + a little wow (fixed 3 ms delay line)
//  AZIMUTH  a copy master with the head slightly crooked: one channel late,
//           top end lost in mono
// ============================================================================
struct Tape {
	static constexpr float LATENCY = 0.003f;
	float sr = 48000.f;
	Ring buf[2];
	Biquad pre[2], de[2], bump[2], dip[2], lp[2], gap[2];
	Changed ch;
	double ph1 = 0, ph2 = 0, ph3 = 0;
	float nz = 0.f, nzT = 0.f;
	int nzN = 0;
	Rng rng;
	float hy[2] = {0.f, 0.f};
	void setSampleRate(float s) {
		sr = s; buf[0].init((int) (sr * 0.006f) + 16); buf[1].init((int) (sr * 0.006f) + 16); ch.reset();
	}
	void reset() {
		buf[0].clear(); buf[1].clear(); ch.reset();
		for (int c = 0; c < 2; c++) { pre[c].reset(); de[c].reset(); bump[c].reset(); dip[c].reset(); lp[c].reset(); gap[c].reset(); hy[c] = 0.f; }
	}
	inline void process(float inL, float inR, float sat, float bumpAmt, int speed, float flutter, float azimuth,
	                    float& outL, float& outR) {
		if (ch({bumpAmt, (float) speed, std::fabs(azimuth)})) {
			const float bumpHz[3] = {32.f, 55.f, 100.f}, topHz[3] = {12000.f, 17000.f, 21000.f};
			for (int c = 0; c < 2; c++) {
				bqShelf(pre[c], true, 3000.0, 6.0, sr);
				bqShelf(de[c], true, 3000.0, -6.0, sr);
				bqPeak(bump[c], bumpHz[speed], 1.4, 5.0 * bumpAmt, sr);
				bqPeak(dip[c], bumpHz[speed] * 2.2, 1.6, -2.5 * bumpAmt, sr);
				bqLP(lp[c], topHz[speed], 0.6, sr);
				bqShelf(gap[c], true, 6000.0, -9.0 * std::fabs(azimuth), sr);
			}
		}
		float in[2] = {inL, inR};
		float g = std::pow(10.f, (-3.f + 15.f * sat) / 20.f);
		for (int c = 0; c < 2; c++) {
			float x = in[c] * 0.2f;
			if (sat > 0.001f) {
				float p = pre[c].process(x) * g;
				// soft saturation with a little memory (hysteresis-ish)
				float s = std::tanh(p + 0.12f * (p - hy[c]));
				hy[c] = p;
				x = de[c].process(s) / g;
			}
			x = bump[c].process(x);
			x = dip[c].process(x);
			x = lp[c].process(x);
			buf[c].push(x * 5.f);
		}
		// flutter: scrape (~9 Hz), wow (~0.8 Hz), and a random wander; swing <= 1 ms
		ph1 += 9.3 / sr; ph2 += 0.8 / sr; ph3 += 3.1 / sr;
		if (ph1 > 1) ph1 -= 1;
		if (ph2 > 1) ph2 -= 1;
		if (ph3 > 1) ph3 -= 1;
		if (--nzN <= 0) { nzN = (int) (sr * 0.03f); nzT = rng.bi(); }
		nz += onePoleA(4.f, sr) * (nzT - nz);
		float f = flutter * flutter;
		float swing = f * (0.00006f * std::sin(2 * PI * (float) ph1) + 0.0004f * std::sin(2 * PI * (float) ph2)
		                   + 0.00008f * std::sin(2 * PI * (float) ph3) + 0.00015f * nz);
		float d = (LATENCY + swing) * sr;
		// azimuth: one channel up to 0.25 ms late
		float az = azimuth * 0.00025f * sr;
		float dl = d + std::max(-az, 0.f), dr = d + std::max(az, 0.f);
		outL = buf[0].read(dl);
		outR = buf[1].read(dr);
		if (std::fabs(azimuth) > 0.001f) { outL = gap[0].process(outL); outR = gap[1].process(outR); }
	}
};

// ============================================================================
// STUDIO 3: DOLBY A. Four bands; quiet detail in each band is lifted on the
// way onto tape (encode) and pushed back down on the way off (decode).
//  ERROR  the tape is played back this many dB off Dolby level, so the decoder
//         undoes the wrong amount: + (tape hot) = too bright and hissy-breathy,
//         - (tape quiet) = dull, with quiet bits sinking away
//  PATH   OK = encode + decode; ENC = never decoded (the classic bright,
//         squashed mistake); DEC = decoded but never encoded (dull, gated)
// ============================================================================
struct DolbyA {
	float sr = 48000.f;
	SVF fe[2][4], fd[2][4];
	float envE[4] = {0, 0, 0, 0}, envD[4] = {0, 0, 0, 0};
	void setSampleRate(float s) { sr = s; }
	void reset() { for (int c = 0; c < 2; c++) for (int b = 0; b < 4; b++) { fe[c][b].reset(); fd[c][b].reset(); } for (int b = 0; b < 4; b++) envE[b] = envD[b] = 0.f; }
	// split into the four Dolby A bands (they add back up to exactly x, so
	// each band can be lifted and undone on its own)
	inline void split(SVF* f, float x, float* b) {
		b[0] = f[0].lowpass(x, svfG(80.f, sr), 0.7071f);
		float rest = x - b[0];
		float mid = f[1].lowpass(rest, svfG(3000.f, sr), 0.7071f);
		float hi = rest - mid;
		b[1] = mid;
		float lo9 = f[2].lowpass(hi, svfG(9000.f, sr), 0.7071f);
		b[2] = lo9;
		b[3] = hi - lo9;
	}
	// extra gain (linear, above 1) for a band at this envelope level
	static inline float lift(int band, float env) {
		const float maxX[4] = {2.16f, 2.16f, 2.16f, 4.62f};   // +10 dB, +10, +10, +15 (minus 1)
		float t = env / 0.12f;                                  // threshold about -18 dB re 5 V
		return (maxX[band] - 1.f) / (1.f + t * std::sqrt(t));
	}
	// onto tape: lift the quiet detail in each band
	inline void encode(float& l, float& r) {
		float in[2] = {l * 0.2f, r * 0.2f}, bE[2][4];
		float aA = coefFor(0.002f, sr), aR = coefFor(0.06f, sr);
		float e[2] = {in[0], in[1]};
		split(fe[0], in[0], bE[0]); split(fe[1], in[1], bE[1]);
		for (int b = 0; b < 4; b++) {
			float lev = std::max(std::fabs(bE[0][b]), std::fabs(bE[1][b]));
			envE[b] += (lev > envE[b] ? aA : aR) * (lev - envE[b]);
			float k = lift(b, envE[b]);
			e[0] += bE[0][b] * k; e[1] += bE[1][b] * k;
		}
		l = e[0] * 5.f; r = e[1] * 5.f;
	}
	// off tape: push it back down, judged by a level that is errorDb off
	inline void decode(float& l, float& r, float errorDb) {
		float e[2] = {l * 0.2f, r * 0.2f}, bD[2][4];
		float aA = coefFor(0.002f, sr), aR = coefFor(0.06f, sr);
		float errG = std::pow(10.f, errorDb / 20.f);
		float out[2] = {e[0], e[1]};
		split(fd[0], e[0], bD[0]); split(fd[1], e[1], bD[1]);
		for (int b = 0; b < 4; b++) {
			float lev = std::max(std::fabs(bD[0][b]), std::fabs(bD[1][b])) * errG;
			envD[b] += (lev > envD[b] ? aA : aR) * (lev - envD[b]);
			// the decoder sees the lifted level: solve for the original level
			// (env_x = env_e / (1 + k(env_x))) in a few fixed-point steps
			float ex = envD[b];
			for (int it = 0; it < 4; it++) ex = envD[b] / (1.f + lift(b, ex));
			float k = lift(b, ex);
			float cut = k / (1.f + k);
			out[0] -= bD[0][b] * cut; out[1] -= bD[1][b] * cut;
		}
		l = out[0] * 5.f; r = out[1] * 5.f;
	}
	// path: 0 = encode + decode, 1 = encode only, 2 = decode only
	inline void process(float inL, float inR, float errorDb, int path, float& outL, float& outR) {
		float l = inL, r = inR;
		if (path != 2) encode(l, r);
		if (path != 1) decode(l, r, errorDb);
		outL = l; outR = r;
	}
};

// ============================================================================
// STUDIO 4: GLUE. A variable-mu valve compressor on the mix: the harder it is
// hit, the harder it squeezes (soft knee, ratio rises with level).
//  AMOUNT  threshold down + automatic make-up
//  TIME    FAST / MID / SLOW attack and release
// ============================================================================
struct Glue {
	float sr = 48000.f;
	float env = 0.f, grDb = 0.f;
	void setSampleRate(float s) { sr = s; }
	void reset() { env = 0.f; grDb = 0.f; }
	inline void process(float inL, float inR, float amount, int time, float& outL, float& outR) {
		const float att[3] = {0.001f, 0.006f, 0.025f}, rel[3] = {0.12f, 0.45f, 1.6f};
		float lev = std::sqrt(0.5f * (inL * inL + inR * inR)) * 0.2f;
		env += (lev > env ? coefFor(att[time], sr) : coefFor(rel[time], sr)) * (lev - env);
		float lDb = 20.f * std::log10(std::max(env, 1e-6f));
		float thr = -4.f - 22.f * amount;
		float over = lDb - thr;
		float target = 0.f;
		if (over > -6.f) {
			// soft knee over 6 dB, ratio grows from 1.5:1 towards 8:1
			float o = over + 6.f;
			float soft = o * o / 24.f;
			float x = over > 6.f ? over : soft;
			float ratio = std::min(1.5f + std::max(x, 0.f) / 4.f, 8.f);
			target = x * (1.f - 1.f / ratio);
		}
		grDb = target;
		float g = std::pow(10.f, (-grDb + 14.f * amount) / 20.f);
		// a whiff of valve on the way out
		outL = std::tanh(inL * g * 0.18f) / 0.18f;
		outR = std::tanh(inR * g * 0.18f) / 0.18f;
	}
};

// ============================================================================
// LISTENING 1: POWER AMP.
//  VOLUME   how hard the amp is driven
//  TYPE     VALVE (push-pull, soft, sags), SOLID (clean then hard), CHEAP
//           (crossover distortion: worst when quiet, a classic of cheap amps)
//  LOUDNESS the old hi-fi button: bass and treble lift, more at low volume
// ============================================================================
struct PowerAmp {
	float sr = 48000.f;
	Biquad ldB[2], ldT[2];
	Changed ch;
	OnePole sag, xlo[2];
	void setSampleRate(float s) { sr = s; ch.reset(); }
	void reset() { for (int c = 0; c < 2; c++) { ldB[c].reset(); ldT[c].reset(); xlo[c].reset(); } sag.reset(); ch.reset(); }
	inline void process(float inL, float inR, float volume, int type, bool loudness, float& outL, float& outR) {
		float q = loudness ? (1.f - volume) : 0.f;
		if (ch({q})) for (int c = 0; c < 2; c++) {
			bqShelf(ldB[c], false, 120.0, 12.0 * q, sr);
			bqShelf(ldT[c], true, 7000.0, 6.0 * q, sr);
		}
		float g = 0.25f * std::pow(16.f, volume);            // -12 dB .. +12 dB into the amp
		float in[2] = {inL, inR}, out[2];
		float lev = std::max(std::fabs(inL), std::fabs(inR)) * g * 0.2f;
		float s = sag.lp(lev, lev > sag.z ? coefFor(0.03f, sr) : coefFor(0.3f, sr));
		for (int c = 0; c < 2; c++) {
			float x = in[c];
			if (q > 0.001f) x = ldT[c].process(ldB[c].process(x));
			x *= g * 0.2f;
			float y;
			if (type == 0) {
				// valve push-pull: sag lowers the rails a little when pushed
				float rail = 1.f / (1.f + 0.35f * s);
				float l = xlo[c].lp(x, onePoleA(90.f, sr));
				x = x + 0.25f * (std::tanh(l * 2.f) * 0.5f - l);   // output transformer bass
				y = rail * std::tanh(x / rail);
			} else if (type == 1) {
				// solid state: clean, then a hard-ish knee near the rails
				float a = std::fabs(x);
				y = a < 0.9f ? x : (x > 0 ? 1.f : -1.f) * (0.9f + 0.1f * std::tanh((a - 0.9f) * 10.f));
			} else {
				// cheap class B: a dead zone near zero (crossover) and low rails
				float dz = 0.012f;
				float xc = x - dz * std::tanh(x / dz);
				y = 0.8f * std::tanh(xc / 0.8f * 1.1f);
				y *= 1.f + dz * 8.f;
			}
			out[c] = y * 5.f / std::sqrt(g);   // louder with VOLUME, but not by the full amount
		}
		outL = out[0]; outR = out[1];
	}
};

// ============================================================================
// LISTENING 2: SPEAKER. Eight boxes, each a band-pass with its own peaks and
// dips; the small ones are mono. BREAKUP = cone excursion and breakup.
// ============================================================================
struct SpeakerModel { const char* name; const char* line1; const char* line2; bool mono; float hp, lp; float pk[3][3]; };
static const int NUM_SPEAKERS = 8;
static const SpeakerModel SPEAKERS[NUM_SPEAKERS] = {
	{"Portable record player", "PORTABLE", "PLAYER", true, 180.f, 7000.f, {{900.f, 1.5f, 5.f}, {2500.f, 3.f, 4.f}, {4500.f, 4.f, -6.f}}},
	{"Transistor radio", "TRANSISTOR", "RADIO", true, 320.f, 5000.f, {{1200.f, 1.2f, 6.f}, {3000.f, 3.f, 3.f}, {600.f, 2.f, -3.f}}},
	{"Car door", "CAR", "DOOR", false, 70.f, 9000.f, {{140.f, 1.5f, 6.f}, {2500.f, 2.f, -4.f}, {6000.f, 3.f, 3.f}}},
	{"Phone speaker", "PHONE", "SPEAKER", true, 650.f, 9000.f, {{1500.f, 1.f, 4.f}, {4000.f, 3.f, 5.f}, {900.f, 3.f, -3.f}}},
	{"Bookshelf hi-fi", "BOOKSHELF", "HI-FI", false, 65.f, 18000.f, {{100.f, 1.2f, 3.f}, {2000.f, 1.5f, -2.f}, {10000.f, 2.f, 2.f}}},
	{"Studio nearfield", "STUDIO", "NEARFIELD", false, 55.f, 19000.f, {{1500.f, 1.3f, 3.f}, {90.f, 1.f, -3.f}, {9000.f, 2.f, -2.f}}},
	{"Floorstander", "FLOOR-", "STANDER", false, 32.f, 20000.f, {{55.f, 1.f, 2.f}, {3000.f, 1.2f, -1.5f}, {12000.f, 2.f, 1.f}}},
	{"Club stack", "CLUB", "STACK", false, 30.f, 16000.f, {{55.f, 1.f, 6.f}, {250.f, 1.f, -3.f}, {3000.f, 2.f, 3.f}}},
};
struct Speaker {
	float sr = 48000.f;
	Biquad hp[2], lp[2], pk[2][3];
	OnePole exc[2], hq[2];
	Changed ch;
	// level matching between boxes (dB measured on a music loop)
	float makeup[NUM_SPEAKERS] = {1.07f, 1.48f, 0.86f, 3.43f, 0.93f, 1.05f, 0.87f, 0.79f};
	void setSampleRate(float s) { sr = s; ch.reset(); }
	void reset() { for (int c = 0; c < 2; c++) { hp[c].reset(); lp[c].reset(); exc[c].reset(); hq[c].reset(); for (int k = 0; k < 3; k++) pk[c][k].reset(); } ch.reset(); }
	inline void process(float inL, float inR, int model, float breakup, float& outL, float& outR) {
		model = std::max(0, std::min(model, NUM_SPEAKERS - 1));
		const SpeakerModel& m = SPEAKERS[model];
		if (ch({(float) model})) for (int c = 0; c < 2; c++) {
			bqHP(hp[c], m.hp, 0.8, sr);
			bqLP(lp[c], m.lp, 0.7, sr);
			for (int k = 0; k < 3; k++) bqPeak(pk[c][k], m.pk[k][0], m.pk[k][1], m.pk[k][2], sr);
		}
		float in[2] = {inL, inR};
		if (m.mono) in[0] = in[1] = 0.5f * (inL + inR);
		float aE = onePoleA(m.hp * 2.5f, sr), aH = onePoleA(1500.f, sr);
		float out[2];
		for (int c = 0; c < 2; c++) {
			float x = in[c] * 0.2f;
			if (breakup > 0.001f) {
				// excursion: the bass the cone can't follow flattens (and farts)
				float e = exc[c].lp(x, aE);
				float gE = 1.f + 10.f * breakup;
				x = x - e + std::tanh(e * gE) / gE * (1.f + 2.f * breakup);
				// breakup: a little rough 2nd/3rd harmonic in the upper mids
				float h = x - hq[c].lp(x, aH);
				x += breakup * 0.6f * (h * std::fabs(h) * 4.f - h * h * h * 6.f);
			}
			x = hp[c].process(x);
			x = lp[c].process(x);
			for (int k = 0; k < 3; k++) x = pk[c][k].process(x);
			out[c] = x * 5.f * makeup[model];
		}
		outL = out[0]; outR = out[1];
	}
};

// ============================================================================
// LISTENING 3: ROOM. Early reflections and a short diffuse tail (these are
// echoes of the sound, the direct sound itself is not delayed), plus bass
// modes. WALL = you're next door: only the bass and the boom get through.
// ============================================================================
struct Room {
	float sr = 48000.f;
	Ring er[2];
	Ring fdn[4];
	OnePole fdnLp[4], erLp[2];
	Biquad mode[2][2], wallLp[2][2], wallPk[2];
	Changed ch;
	float sWall = 0.f;
	void setSampleRate(float s) {
		sr = s; ch.reset();
		for (int c = 0; c < 2; c++) er[c].init((int) (sr * 0.09f) + 16);
		for (int i = 0; i < 4; i++) fdn[i].init((int) (sr * 0.12f) + 16);
	}
	void reset() {
		for (int c = 0; c < 2; c++) { er[c].clear(); erLp[c].reset(); for (int k = 0; k < 2; k++) { mode[c][k].reset(); wallLp[c][k].reset(); } wallPk[c].reset(); }
		for (int i = 0; i < 4; i++) { fdn[i].clear(); fdnLp[i].reset(); }
		ch.reset(); sWall = 0.f;
	}
	// direct* = the speaker with bass modes; wet* = reflections + tail
	inline void process(float inL, float inR, float size, float damp, float modes, bool wall,
	                    float& dirL, float& dirR, float& wetL, float& wetR) {
		float metres = 2.5f + 14.f * size;
		if (ch({size, modes})) for (int c = 0; c < 2; c++) {
			float f1 = 343.f / (2.f * metres), f2 = 343.f / (2.f * metres * 0.62f);
			bqPeak(mode[c][0], f1 * (c ? 1.07f : 1.f), 6.0, 10.0 * modes, sr);
			bqPeak(mode[c][1], f2 * (c ? 0.95f : 1.f), 5.0, 7.0 * modes, sr);
			bqLP(wallLp[c][0], 260.0, 0.7, sr);
			bqLP(wallLp[c][1], 260.0, 0.7, sr);
			bqPeak(wallPk[c], 110.0, 2.0, 6.0, sr);
		}
		float in[2] = {inL, inR}, d[2];
		for (int c = 0; c < 2; c++) {
			d[c] = mode[c][1].process(mode[c][0].process(in[c]));
			er[c].push(d[c]);
		}
		// early reflections: 6 taps per side, spread by room size
		const float tap[6] = {0.11f, 0.17f, 0.23f, 0.31f, 0.43f, 0.58f};
		const float tg[6] = {0.55f, 0.45f, 0.38f, 0.3f, 0.24f, 0.18f};
		float span = (0.008f + 0.07f * size) * sr;
		float aD = onePoleA(9000.f - 7500.f * damp, sr);
		float e[2] = {0.f, 0.f};
		for (int k = 0; k < 6; k++) {
			int from = (k & 1);
			e[0] += tg[k] * er[from].read(tap[k] * span + 1.f);
			e[1] += tg[k] * er[1 - from].read(tap[(k + 3) % 6] * span + 1.f);
		}
		e[0] = erLp[0].lp(e[0], aD); e[1] = erLp[1].lp(e[1], aD);
		// tail: 4-line feedback network (Householder), RT from SIZE and DAMP
		const float len[4] = {0.0297f, 0.0371f, 0.0411f, 0.0437f};
		float rt = (0.25f + 1.2f * size) * (1.2f - 0.9f * damp);
		float o[4], sum = 0.f;
		for (int i = 0; i < 4; i++) {
			float L = len[i] * (0.5f + 1.6f * size) * sr;
			o[i] = fdnLp[i].lp(fdn[i].read(L), aD);
			sum += o[i];
		}
		for (int i = 0; i < 4; i++) {
			float L = len[i] * (0.5f + 1.6f * size);
			float fb = std::pow(10.f, -3.f * L / std::max(rt, 0.05f));
			float x = (o[i] - 0.5f * sum) * fb + ((i & 1) ? e[1] : e[0]) * 0.35f;
			fdn[i].push(x);
		}
		float tl = 0.6f * (o[0] + o[2]), tr = 0.6f * (o[1] + o[3]);
		float w[2] = {e[0] * 0.7f + tl, e[1] * 0.7f + tr};
		// next door: low-passed, boomy, mono, mostly room
		sWall += coefFor(0.05f, sr) * ((wall ? 1.f : 0.f) - sWall);
		if (sWall > 0.001f) {
			float m = 0.5f * (d[0] + d[1]) * 0.6f + 0.5f * (w[0] + w[1]);
			float t = wallPk[0].process(wallLp[0][1].process(wallLp[0][0].process(m))) * 0.5f;
			for (int c = 0; c < 2; c++) { d[c] += (t - d[c]) * sWall; w[c] *= (1.f - sWall); }
		}
		dirL = d[0]; dirR = d[1]; wetL = w[0]; wetR = w[1];
	}
};

// ============================================================================
// LISTENING 4: EARS.
//  DISTANCE  near: mostly speaker; far: quieter, duller, mostly room
//  ANGLE     head turned: the far ear is later (up to 0.65 ms) and duller
//  PARTY     ears after a loud night: top end gone, everything squashed flat,
//            and a faint ringing
// ============================================================================
struct Ears {
	float sr = 48000.f;
	Ring itd[2];
	Biquad air[2], shade[2], tts[2], tts2[2];
	Changed ch;
	float env = 0.f;
	double ring = 0.0;
	void setSampleRate(float s) { sr = s; ch.reset(); itd[0].init((int) (sr * 0.002f) + 16); itd[1].init((int) (sr * 0.002f) + 16); }
	void reset() { itd[0].clear(); itd[1].clear(); for (int c = 0; c < 2; c++) { air[c].reset(); shade[c].reset(); tts[c].reset(); tts2[c].reset(); } env = 0.f; ch.reset(); }
	inline void process(float dirL, float dirR, float wetL, float wetR, float distance, float angle, float party,
	                    float& outL, float& outR) {
		if (ch({distance, angle, party})) for (int c = 0; c < 2; c++) {
			bqLP(air[c], 20000.0 - 13000.0 * distance, 0.6, sr);
			// left ear is far when angle > 0 (head turned to the right)
			float far = c == 0 ? std::max(angle, 0.f) : std::max(-angle, 0.f);
			float nearA = c == 0 ? std::max(-angle, 0.f) : std::max(angle, 0.f);
			bqShelf(shade[c], true, 1500.0, -9.0 * far + 2.0 * nearA, sr);
			bqShelf(tts[c], true, 3500.0, -16.0 * party, sr);
			bqPeak(tts2[c], 4000.0, 1.0, -6.0 * party, sr);
		}
		float m = 0.5f + 19.5f * distance * distance;      // metres
		float dGain = 1.f / std::max(m, 0.5f) * 0.5f + 0.0f;
		dGain = std::min(dGain * 1.f, 1.f);
		float wGain = std::min(0.15f + 0.9f * distance, 1.f);
		float x[2] = {dirL * dGain + wetL * wGain, dirR * dGain + wetR * wGain};
		// level is normalised so DISTANCE mostly changes the balance, not volume
		float norm = 1.f / std::sqrt(dGain * dGain + 0.5f * wGain * wGain);
		norm = std::min(norm, 4.f);
		for (int c = 0; c < 2; c++) {
			x[c] = air[c].process(x[c] * norm);
			itd[c].push(x[c]);
		}
		float lagL = std::max(angle, 0.f) * 0.00065f * sr, lagR = std::max(-angle, 0.f) * 0.00065f * sr;
		float y[2] = {itd[0].read(lagL + 1.f), itd[1].read(lagR + 1.f)};
		for (int c = 0; c < 2; c++) y[c] = shade[c].process(y[c]);
		if (party > 0.001f) {
			for (int c = 0; c < 2; c++) y[c] = tts2[c].process(tts[c].process(y[c]));
			// squash: fast compression, and less stereo
			float lev = std::max(std::fabs(y[0]), std::fabs(y[1])) * 0.2f;
			env += (lev > env ? coefFor(0.003f, sr) : coefFor(0.15f, sr)) * (lev - env);
			float gr = 1.f / (1.f + party * 6.f * env);
			float mid = 0.5f * (y[0] + y[1]), side = 0.5f * (y[0] - y[1]) * (1.f - 0.6f * party);
			y[0] = (mid + side) * gr * (1.f + 1.5f * party);
			y[1] = (mid - side) * gr * (1.f + 1.5f * party);
			// ringing: a faint high whistle that comes and goes
			ring += 6200.0 / sr; if (ring > 1) ring -= 1;
			float rv = 0.03f * party * party * std::sin(2 * PI * (float) ring);
			y[0] += rv; y[1] += rv * 0.8f;
		}
		outL = y[0]; outR = y[1];
	}
};

} // namespace vinyl
