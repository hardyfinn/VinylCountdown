// ON AIR: a record played on FM radio, simulated end to end.
//
//   STATION   broadcast processing (slow AGC, 3-band compressor), FM
//             pre-emphasis and the final clipper that crunches bright sounds
//   TRANSMIT  stereo multiplex: (L+R) + 19 kHz pilot + (L-R) on a 38 kHz
//             subcarrier, frequency-modulated (+-75 kHz deviation)
//   AIR       the radio path at complex baseband: a reflected copy
//             (multipath) that rotates when the receiver moves (picket
//             fencing), thermal noise set by SIGNAL, and the next station
//             100 or 200 kHz away
//   RADIO     tuning, IF filter, limiter, FM discriminator, a PLL that locks
//             to the pilot, stereo decoder, and the receiver's own reactions
//             to a weak signal (stereo blend, high cut, soft mute); then
//             de-emphasis
//
// Everything between the multiplex and the decoder runs at about 768 kHz, so
// the clicks near reception threshold, the capture effect between stations,
// multipath distortion and stereo noise all come out of the physics.
// Latency: well under 1 ms. No Rack code (test/render.cpp runs it offline).
#pragma once
#include "ChainDSP.hpp"
#include <complex>

namespace vinyl {

typedef std::complex<float> cf;

// unit phasor rotated by a small angle (|a| < ~1 rad), renormalised
inline cf rotSmall(cf z, float a) {
	float a2 = a * a;
	float c = 1.f - a2 * (0.5f - a2 * (1.f / 24.f - a2 / 720.f));
	float s = a * (1.f - a2 * (1.f / 6.f - a2 * (1.f / 120.f - a2 / 5040.f)));
	cf r(z.real() * c - z.imag() * s, z.real() * s + z.imag() * c);
	float m = r.real() * r.real() + r.imag() * r.imag();
	return r * (1.5f - 0.5f * m);   // one Newton step towards |r| = 1
}
// atan2 with ~1e-5 rad error
inline float fastAtan2(float y, float x) {
	float ax = std::fabs(x), ay = std::fabs(y);
	float mx = std::max(ax, ay), mn = std::min(ax, ay);
	if (mx < 1e-30f) return 0.f;
	float a = mn / mx, s = a * a;
	float r = ((((((-0.0040540580f * s + 0.0218612288f) * s - 0.0559098861f) * s + 0.0964200441f) * s - 0.1390853351f) * s
	          + 0.1994653599f) * s - 0.3332985605f) * s * a + a;
	if (ay > ax) r = 1.57079637f - r;
	if (x < 0) r = 3.14159274f - r;
	return y < 0 ? -r : r;
}

// ---------------------------------------------------------------------------
// Broadcast processing at the audio rate. Output is in modulation units:
// +-1 = 100% (75 kHz deviation), pre-emphasised and clipped.
//  amount  how hard the station leans on it
//  era     0 = 70s (gentle AGC + limiter), 1 = 80s (3-band), 2 = 90s (loud)
// ---------------------------------------------------------------------------
struct BroadcastProc {
	float sr = 48000.f;
	float agcDb = 0.f, agcEnv = 0.f;
	SVF xo[2][2];
	float env[3] = {0, 0, 0};
	float preX[2] = {0, 0}, preY[2] = {0, 0};
	void setSampleRate(float s) { sr = s; }
	void reset() { agcDb = 0.f; agcEnv = 0.f; for (int c = 0; c < 2; c++) { xo[c][0].reset(); xo[c][1].reset(); preX[c] = preY[c] = 0.f; } for (float& e : env) e = 0.f; }
	inline void process(float l, float r, float amount, int era, bool us, float& oL, float& oR) {
		float x[2] = {l * 0.2f, r * 0.2f};
		// slow AGC towards a target, frozen in quiet passages
		float lev = std::max(std::fabs(x[0]), std::fabs(x[1]));
		agcEnv += (lev > agcEnv ? coefFor(0.05f, sr) : coefFor(0.6f, sr)) * (lev - agcEnv);
		float lDb = 20.f * std::log10(std::max(agcEnv, 1e-5f));
		if (lDb > -40.f) {
			float want = clampf(-2.f - lDb, -12.f, 12.f) * amount;
			agcDb += coefFor(era == 0 ? 3.f : 1.5f, sr) * (want - agcDb);
		}
		float g = std::pow(10.f, agcDb / 20.f);
		x[0] *= g; x[1] *= g;
		// 3-band compression (80s/90s)
		if (era > 0 && amount > 0.001f) {
			float b[2][3];
			for (int c = 0; c < 2; c++) {
				b[c][0] = xo[c][0].lowpass(x[c], svfG(200.f, sr), 0.7071f);
				float rest = x[c] - b[c][0];
				b[c][1] = xo[c][1].lowpass(rest, svfG(3500.f, sr), 0.7071f);
				b[c][2] = rest - b[c][1];
			}
			const float rel[3] = {0.25f, 0.12f, 0.06f};
			float ratio = era == 1 ? 3.f : 8.f;
			float thrDb = -10.f - 14.f * amount;
			const float makeup[3] = {1.f, 1.f, era == 2 ? 1.6f : 1.2f};
			for (int k = 0; k < 3; k++) {
				float lv = std::max(std::fabs(b[0][k]), std::fabs(b[1][k]));
				env[k] += (lv > env[k] ? coefFor(0.004f, sr) : coefFor(rel[k], sr)) * (lv - env[k]);
				float eDb = 20.f * std::log10(std::max(env[k], 1e-6f));
				float over = std::max(eDb - thrDb, 0.f);
				float gk = std::pow(10.f, (-over * (1.f - 1.f / ratio) + (-thrDb - 10.f) * 0.5f * amount) / 20.f) * makeup[k];
				b[0][k] *= gk; b[1][k] *= gk;
			}
			for (int c = 0; c < 2; c++) x[c] = b[c][0] + b[c][1] + b[c][2];
		}
		// pre-emphasis: first-order shelf, zero at 1/(2 pi tau)
		float tau = us ? 75e-6f : 50e-6f;
		float fz = 1.f / (2.f * PI * tau), fp = std::min(21000.f, sr * 0.45f);
		// bilinear: H(s) = (1 + s/wz) / (1 + s/wp)
		float K = 2.f * sr;
		float wz = 2.f * PI * fz, wp = 2.f * PI * fp;
		float b0 = (1.f + K / wz), b1 = (1.f - K / wz), a0 = (1.f + K / wp), a1 = (1.f - K / wp);
		float drive = 1.f + (era == 2 ? 2.2f : (era == 1 ? 1.2f : 0.5f)) * amount;
		for (int c = 0; c < 2; c++) {
			float y = (b0 * x[c] + b1 * preX[c] - a1 * preY[c]) / a0;
			preX[c] = x[c]; preY[c] = y;
			// final clipper: 100% modulation, soft just below
			float v = y * drive;
			float a = std::fabs(v);
			float cl = a < 0.85f ? v : (v > 0 ? 1.f : -1.f) * (0.85f + 0.15f * std::tanh((a - 0.85f) / 0.15f));
			if (c == 0) oL = cl; else oR = cl;
		}
	}
};

// One FM transmitter: stereo multiplex at the oversampled rate, then FM.
struct Transmitter {
	float fs = 768000.f;
	Biquad lp[2][2];
	cf pilot{1.f, 0.f};
	cf carrier{1.f, 0.f};
	float pilotStep = 0.f;
	void setRate(float f) {
		fs = f;
		for (int c = 0; c < 2; c++) { bqLP(lp[c][0], 15500.0, 0.541, fs); bqLP(lp[c][1], 15500.0, 1.307, fs); }
		pilotStep = 2.f * PI * 19000.f / fs;
	}
	void reset() { for (int c = 0; c < 2; c++) { lp[c][0].reset(); lp[c][1].reset(); } pilot = cf(1.f, 0.f); carrier = cf(1.f, 0.f); }
	// l, r: modulation units (already processed and band-limited at audio rate)
	inline cf step(float l, float r, float& mpxOut) {
		l = lp[0][1].process(lp[0][0].process(l));
		r = lp[1][1].process(lp[1][0].process(r));
		pilot = rotSmall(pilot, pilotStep);
		float s1 = pilot.imag(), c1 = pilot.real();
		float s2 = 2.f * s1 * c1;
		float mpx = 0.9f * (0.5f * (l + r) + 0.5f * (l - r) * s2) + 0.09f * s1;
		mpxOut = mpx;
		carrier = rotSmall(carrier, 2.f * PI * 75000.f * mpx / fs);
		return carrier;
	}
};

// ---------------------------------------------------------------------------
// The whole link. Controls (all 0..1 unless noted):
//  signal    0 = no station .. 1 = strong local (C/N about -5 .. +60 dB)
//  multipath strength of the reflection; motion = how fast it rotates
//  nextLevel the next station's strength relative to ours (-40 .. +6 dB)
//  spacing   100 or 200 kHz above ours
//  tuneHz    receiver tuning offset from our station
//  set       0 = HIFI tuner, 1 = CAR radio, 2 = CHEAP portable
// ---------------------------------------------------------------------------
struct FmLink {
	float sr = 48000.f, fs = 768000.f;
	int N = 16;
	BroadcastProc procA, procB;
	Transmitter txA, txB;
	// interpolation state (audio rate -> fs)
	float prev[4] = {0, 0, 0, 0};
	// air
	cf mp[64];
	int mpW = 0;
	cf dop1{1.f, 0.f}, dop2{1.f, 0.f}, spaceRot{1.f, 0.f}, tuneRot{1.f, 0.f};
	float fadeT = 0.f, fade = 0.f;
	int fadeN = 0;
	Rng rng;
	// receiver
	Biquad ifI[2], ifQ[2];
	Changed ifCh;
	cf zPrev{1.f, 0.f};
	cf pll{1.f, 0.f};
	float pllInt = 0.f, pilotLev = 0.f;
	Biquad outM[3], outD[3];   // 15 kHz LP (2) + 19 kHz notch (1)
	Biquad nzHp[2];
	float noiseP = 0.f, rssi = 0.f;
	float decM = 0.f, decD = 0.f;
	// audio-rate receiver behaviour
	float blend = 1.f, hiCut = 1.f, mute = 1.f;
	OnePole hc[2], de[2];
	// meters for the panel
	float meterSignal = 0.f;
	bool stereoLit = false;

	void setSampleRate(float s, float target = 768000.f) {
		sr = s;
		N = std::max(1, (int) std::lround(target / sr));
		fs = sr * N;
		procA.setSampleRate(sr); procB.setSampleRate(sr);
		txA.setRate(fs); txB.setRate(fs);
		for (int k = 0; k < 2; k++) { bqLP(outM[k], 15000.0, k ? 1.307 : 0.541, fs); bqLP(outD[k], 15000.0, k ? 1.307 : 0.541, fs); }
		// 19 kHz notch
		double w = 2 * PI * 19000.0 / fs, al = std::sin(w) / (2 * 4.0);
		bqSet(outM[2], 1, -2 * std::cos(w), 1, 1 + al, -2 * std::cos(w), 1 - al);
		bqSet(outD[2], 1, -2 * std::cos(w), 1, 1 + al, -2 * std::cos(w), 1 - al);
		bqHP(nzHp[0], 75000.0, 0.7, fs); bqHP(nzHp[1], 75000.0, 0.7, fs);
		ifCh.reset();
		reset();
	}
	void reset() {
		procA.reset(); procB.reset(); txA.reset(); txB.reset();
		for (float& p : prev) p = 0.f;
		for (cf& z : mp) z = cf(0.f, 0.f);
		mpW = 0; zPrev = cf(1.f, 0.f); pll = cf(1.f, 0.f); pllInt = 0.f; pilotLev = 0.f;
		for (int k = 0; k < 2; k++) { ifI[k].reset(); ifQ[k].reset(); nzHp[k].reset(); hc[k].reset(); de[k].reset(); }
		for (int k = 0; k < 3; k++) { outM[k].reset(); outD[k].reset(); }
		noiseP = 0.f; rssi = 0.f; blend = 1.f; hiCut = 1.f; mute = 1.f; ifCh.reset();
		// the next station's pilot starts at another phase
		txB.pilot = cf(std::cos(1.3f), std::sin(1.3f));
	}
	inline float gauss() { return (rng.uni() + rng.uni() + rng.uni() + rng.uni() - 2.f) * 1.7320508f; }

	// inL..: Rack volts. nextConnected: if false, the next station is silent air (carrier only)
	inline void process(float inL, float inR, float nL, float nR, bool nextOn,
	                    float procAmt, int era, bool us,
	                    float signal, float multipath, float motion,
	                    float nextLevel, bool spacing200,
	                    float tuneHz, int set, bool forceMono,
	                    float& outL, float& outR) {
		// --- station processing at the audio rate
		float aL, aR, bL = 0.f, bR = 0.f;
		procA.process(inL, inR, procAmt, era, us, aL, aR);
		if (nextOn) procB.process(nL, nR, 0.75f, 1, us, bL, bR);

		// --- per-block settings
		const float ifHalf[3] = {115000.f, 95000.f, 70000.f};
		if (ifCh({(float) set})) for (int k = 0; k < 2; k++) {
			bqLP(ifI[k], ifHalf[set], k ? 1.307 : 0.541, fs);
			bqLP(ifQ[k], ifHalf[set], k ? 1.307 : 0.541, fs);
		}
		float cnDb = -5.f + 65.f * signal;
		float sigma = std::sqrt((fs / (2.f * ifHalf[1])) / std::pow(10.f, cnDb / 10.f)) * 0.70710678f;
		float nextAmp = nextOn || nextLevel > 0.f ? std::pow(10.f, (-40.f + 46.f * nextLevel) / 20.f) : 0.f;
		float spaceStep = 2.f * PI * (spacing200 ? 200000.f : 100000.f) / fs;
		float tuneStep = -2.f * PI * tuneHz / fs;
		// multipath: two reflections, 9 and 24 us late, rotating with motion
		int d1 = std::max(1, (int) std::lround(9e-6f * fs)), d2 = std::max(2, (int) std::lround(24e-6f * fs));
		float a1 = 0.92f * multipath, a2 = 0.6f * multipath;
		float fd = 0.2f + 25.f * motion * motion;      // Doppler, Hz
		float dop1Step = 2.f * PI * fd / fs, dop2Step = -2.f * PI * fd * 0.73f / fs;
		// slow shadowing when moving (buildings): up to -12 dB dips
		if (--fadeN <= 0) { fadeN = (int) (sr * (0.4f - 0.3f * motion)); fadeT = rng.uni(); }
		fade += coefFor(0.25f, sr) * (fadeT - fade);
		float shadow = std::pow(10.f, -12.f * motion * fade * fade / 20.f);

		float acc[2] = {0.f, 0.f};
		float nAcc = 0.f, rAcc = 0.f;
		float invN = 1.f / N;
		float ploop = 2.f * PI * 19000.f / fs;
		// PLL gains: ~30 Hz loop, detector gain 0.045
		float wn = 2.f * PI * 30.f, kd = 0.045f;
		float kp = 2.f * 0.707f * wn / (kd * fs), ki = wn * wn / (kd * fs * fs);
		for (int i = 0; i < N; i++) {
			float t = (i + 1) * invN;
			float l = prev[0] + (aL - prev[0]) * t, r = prev[1] + (aR - prev[1]) * t;
			float mpxA, mpxB;
			cf sA = txA.step(l, r, mpxA) * shadow;
			cf sB(0.f, 0.f);
			if (nextAmp > 0.f) {
				float nl = prev[2] + (bL - prev[2]) * t, nr = prev[3] + (bR - prev[3]) * t;
				spaceRot = rotSmall(spaceRot, spaceStep);
				sB = txB.step(nl, nr, mpxB) * spaceRot * nextAmp;
			}
			// multipath on our station
			mp[mpW] = sA;
			cf e1 = mp[(mpW - d1 + 64) & 63], e2 = mp[(mpW - d2 + 64) & 63];
			mpW = (mpW + 1) & 63;
			cf rx = sA;
			if (a1 > 0.f) {
				dop1 = rotSmall(dop1, dop1Step); dop2 = rotSmall(dop2, dop2Step);
				rx += e1 * dop1 * a1 + e2 * dop2 * a2;
			}
			rx += sB;
			rx += cf(gauss() * sigma, gauss() * sigma);
			// tune
			tuneRot = rotSmall(tuneRot, tuneStep);
			rx *= tuneRot;
			// IF filter
			float I = ifI[1].process(ifI[0].process(rx.real()));
			float Q = ifQ[1].process(ifQ[0].process(rx.imag()));
			float mag = std::sqrt(I * I + Q * Q);
			rAcc += mag;
			// limiter + discriminator
			cf z = mag > 1e-12f ? cf(I / mag, Q / mag) : zPrev;
			cf dz = z * std::conj(zPrev);
			zPrev = z;
			float mpx = fastAtan2(dz.imag(), dz.real()) * fs / (2.f * PI * 75000.f);
			// noise above the multiplex band tells the radio how bad it is
			float hn = nzHp[1].process(nzHp[0].process(mpx));
			nAcc += hn * hn;
			// pilot PLL
			float s1 = pll.imag(), c1 = pll.real();
			float err = mpx * c1;
			pllInt += ki * err;
			pllInt = clampf(pllInt, -0.002f, 0.002f);
			pll = rotSmall(pll, ploop + kp * err + pllInt);
			pilotLev += 0.00002f * (mpx * s1 - pilotLev);
			float s2 = 2.f * s1 * c1;
			float m = outM[2].process(outM[1].process(outM[0].process(mpx)));
			float d = outD[2].process(outD[1].process(outD[0].process(mpx * 2.f * s2)));
			decM = m; decD = d;
		}
		prev[0] = aL; prev[1] = aR; prev[2] = bL; prev[3] = bR;
		(void) acc;

		// --- receiver reactions (audio rate)
		noiseP += coefFor(0.08f, sr) * (nAcc * invN - noiseP);
		rssi += coefFor(0.05f, sr) * (rAcc * invN - rssi);
		// quality: 1 = clean, 0 = hopeless (noise power calibrated on C/N)
		float nDb = 10.f * std::log10(std::max(noiseP, 1e-9f));
		float q = clampf((-12.f - nDb) / 30.f, 0.f, 1.f);
		bool pilotOk = pilotLev > 0.025f;
		// how each kind of radio copes
		const float blendLo[3] = {0.45f, 0.55f, 0.6f}, blendHi[3] = {0.6f, 0.85f, 0.8f};
		float bTarget = pilotOk ? clampf((q - blendLo[set]) / (blendHi[set] - blendLo[set]), 0.f, 1.f) : 0.f;
		if (forceMono) bTarget = 0.f;
		blend += coefFor(0.15f, sr) * (bTarget - blend);
		float hcTarget = set == 0 ? 1.f : clampf((q - 0.15f) / 0.45f, 0.f, 1.f);
		hiCut += coefFor(0.1f, sr) * (hcTarget - hiCut);
		float muteTarget = set == 0 ? 1.f : 0.35f + 0.65f * clampf(q / 0.3f, 0.f, 1.f);
		mute += coefFor(0.1f, sr) * (muteTarget - mute);
		meterSignal = q;
		stereoLit = pilotOk && blend > 0.5f;

		float M = decM / 0.9f, D = decD / 0.9f * blend;
		float o[2] = {M + D, M - D};
		float hcHz = 2500.f + 15000.f * hiCut * hiCut;
		float aHc = onePoleA(hcHz, sr);
		float aDe = 1.f - std::exp(-1.f / ((us ? 75e-6f : 50e-6f) * sr));
		for (int c = 0; c < 2; c++) {
			float x = de[c].lp(o[c], aDe);
			if (hiCut < 0.999f) x = hc[c].lp(x, aHc);
			o[c] = x * mute * 5.f;
		}
		outL = o[0]; outR = o[1];
	}
};

} // namespace vinyl
