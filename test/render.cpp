// Offline checks + demo WAVs for the VinylCountdown engines (no Rack needed).
//   make test     (writes test/*.wav)
// Checks the latency promise of every module, plus basic sanity.
#include "../src/VinylDSP.hpp"
#include <cstdio>
#include <string>
using namespace vinyl;

static const int SR = 48000;

static void writeWav(const std::string& path, const std::vector<float>& lr) {
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return;
	int bytes = (int) lr.size() * 2;
	auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
	fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
	w32(16); w16(1); w16(2); w32(SR); w32(SR * 4); w16(4); w16(16);
	fwrite("data", 1, 4, f); w32(bytes);
	for (float v : lr) { float s = std::max(-1.f, std::min(1.f, v / 10.f)); int16_t q = (int16_t) (s * 32767); fwrite(&q, 2, 1, f); }
	fclose(f);
}

// A 128 BPM test loop: kick, offbeat hats, a held chord (so wow is audible)
struct Loop {
	Rng r; float kph = 0, p1 = 0, p2 = 0, p3 = 0, hp = 0;
	void next(float t, float& L, float& R) {
		float beat = std::fmod(t * 128.f / 60.f, 1.f);
		kph += (48.f + 140.f * std::exp(-beat * 40.f)) / SR; kph -= std::floor(kph);
		float kick = 5.f * std::exp(-beat * 9.f) * std::sin(2 * PI * kph);
		float n = r.bi(); hp += (n - hp) * 0.35f;
		float off = std::fmod(beat + 0.5f, 1.f);
		float hat = 3.f * (n - hp) * std::exp(-off * 28.f);
		p1 += 261.6f / SR; p2 += 311.1f / SR; p3 += 392.f / SR;
		p1 -= std::floor(p1); p2 -= std::floor(p2); p3 -= std::floor(p3);
		float chord = 0.9f * (std::sin(2 * PI * p1) + std::sin(2 * PI * p2) + std::sin(2 * PI * p3));
		L = kick + hat * 0.8f + chord;
		R = kick + hat * 1.1f + chord * 0.9f;
	}
};

static int fails = 0;
static void check(bool ok, const char* what) {
	printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) fails++;
}

// delay (samples) at which y best matches x
static int bestLag(const std::vector<float>& x, const std::vector<float>& y, int maxLag) {
	int best = 0; double bv = -1e30;
	for (int l = 0; l <= maxLag; l++) {
		double s = 0;
		for (size_t i = maxLag; i < x.size(); i++) s += x[i - l] * y[i];
		if (s > bv) { bv = s; best = l; }
	}
	return best;
}

int main(int argc, char** argv) {
	std::string dir = argc > 1 ? argv[1] : ".";

	// ---------------- PLATTER ----------------
	printf("Platter\n");
	{
		Platter p; p.setSampleRate(SR);
		// latency at zero offset
		std::vector<float> x, y;
		Rng r;
		for (int i = 0; i < SR / 2; i++) { float v = r.bi(); float o, o2; p.process(v, v, 0.f, R_OUTER, RPM_VALUES[0], o, o2); x.push_back(v); y.push_back(o); }
		int lag = bestLag(x, y, 600);
		printf("  latency at OFFSET 0: %d samples (%.2f ms)\n", lag, lag * 1000.f / SR);
		check(lag == (int) std::lround(Platter::LATENCY * SR), "fixed 5 ms latency");
		// pitch swing for 0.3 mm near the label at 33
		Platter q; q.setSampleRate(SR);
		float mx = 0, mn = 2;
		for (int i = 0; i < SR * 4; i++) { float o, o2; q.process(0, 0, 0.3f, R_INNER, RPM_VALUES[0], o, o2); if (i > SR) { mx = std::max(mx, q.speed); mn = std::min(mn, q.speed); } }
		float cents = 1200.f * std::log2(mx);
		printf("  0.3 mm at label, 33 rpm: speed %.4f..%.4f = +-%.1f cents\n", mn, mx, cents);
		check(std::fabs(cents - 8.6f) < 0.6f, "0.3 mm at the label gives about 9 cents");
		// 1 mm worst case stays inside the 5 ms budget
		Platter w; w.setSampleRate(SR);
		float wmx = 0;
		for (int i = 0; i < SR * 4; i++) { float o, o2; w.process(0, 0, 1.f, R_INNER, RPM_VALUES[0], o, o2); if (i > SR) wmx = std::max(wmx, w.speed); }
		printf("  1 mm at label: +-%.1f cents\n", 1200.f * std::log2(wmx));
		check(wmx > 1.015f && wmx < 1.02f, "1 mm at the label reaches about 29 cents, no clipping of the swing");
		// demo: loop through 0.5 mm, label area, 33 rpm
		Platter d; d.setSampleRate(SR); Loop L; std::vector<float> wav;
		for (int i = 0; i < SR * 10; i++) {
			float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
			float off = t < 3.f ? 0.f : 0.6f;
			d.process(a, b, off, R_INNER + 10.f, RPM_VALUES[0], o1, o2);
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/platter-demo.wav", wav);
	}

	// ---------------- INNER GROOVE ----------------
	printf("Inner Groove\n");
	{
		auto hfRatio = [](float radius, int stylus, float cut, std::vector<float>* wav) {
			InnerGroove g; g.setSampleRate(SR);
			Loop L; SVF hpIn, hpOut;
			double ein = 0, eout = 0; bool nan = false; float peak = 0;
			for (int i = 0; i < SR * 3; i++) {
				float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
				g.process(a, b, radius, RPM_VALUES[0], stylus, 0.5f, cut, o1, o2);
				if (!std::isfinite(o1) || !std::isfinite(o2)) nan = true;
				peak = std::max(peak, std::fabs(o1));
				float hi = hpIn.highpass(a, svfG(8000.f, SR), 0.7f), ho = hpOut.highpass(o1, svfG(8000.f, SR), 0.7f);
				if (i > SR / 2) { ein += hi * hi; eout += ho * ho; }
				if (wav) { wav->push_back(o1); wav->push_back(o2); }
			}
			if (nan || peak > 20.f) return -999.f;
			return (float) (10.0 * std::log10(eout / ein));
		};
		float outer = hfRatio(0.f, 0, 0.3f, nullptr), inner = hfRatio(1.f, 0, 0.3f, nullptr);
		float innerShibata = hfRatio(1.f, 2, 0.3f, nullptr);
		printf("  >8 kHz level change: outer %.1f dB, label (conical) %.1f dB, label (Shibata) %.1f dB\n", outer, inner, innerShibata);
		check(outer > -4.f && outer < 1.5f, "outer edge keeps the top end");
		check(inner < outer - 5.f, "the label side is clearly darker");
		check(innerShibata > inner + 2.f, "Shibata tip loses less than conical");
		// zero latency
		InnerGroove g; g.setSampleRate(SR);
		std::vector<float> x, y; Rng r;
		for (int i = 0; i < SR / 2; i++) { float v = r.bi(); float o, o2; g.process(v, v, 0.f, RPM_VALUES[0], 2, 1.f, 0.f, o, o2); x.push_back(v); y.push_back(o); }
		check(bestLag(x, y, 200) == 0, "no latency");
		// demo: needle travels outer -> label over 10 s, hot cut
		std::vector<float> wav;
		InnerGroove d; d.setSampleRate(SR); Loop L;
		for (int i = 0; i < SR * 10; i++) {
			float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
			d.process(a * 1.4f, b * 1.4f, t / 10.f, RPM_VALUES[0], 0, 0.35f, 0.75f, o1, o2);
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/innergroove-demo.wav", wav);
	}

	// ---------------- LATHE ----------------
	printf("Lathe\n");
	{
		// everything off -> bit-exact passthrough
		Lathe l; l.setSampleRate(SR);
		Rng r; double err = 0;
		for (int i = 0; i < SR; i++) { float v = r.bi() * 5.f, o1, o2, w1, w2; l.process(v, -v, 0.f, 0.f, 0.f, 0.f, 1, 1.8f, o1, o2, w1, w2); err += std::fabs(o1 - v) + std::fabs(o2 + v); }
		check(err < 1e-3, "all off = untouched, no latency");
		// elliptic: side bass removed, side highs kept
		Lathe e; e.setSampleRate(SR);
		double sLo = 0, sHi = 0, ph = 0, ph2 = 0;
		for (int i = 0; i < SR; i++) {
			ph += 50.0 / SR; ph2 += 3000.0 / SR;
			float lo = std::sin(2 * PI * ph), hi = std::sin(2 * PI * ph2);
			float o1, o2, w1, w2;
			e.process(lo + hi, -(lo + hi), 0.f, 150.f, 0.f, 0.f, 1, 1.8f, o1, o2, w1, w2);
			if (i > SR / 2) { sLo += std::fabs(o1 - o2) * 0.5; }
		}
		Lathe e2; e2.setSampleRate(SR); ph = 0;
		for (int i = 0; i < SR; i++) {
			ph += 3000.0 / SR; float hi = std::sin(2 * PI * ph), o1, o2, w1, w2;
			e2.process(hi, -hi, 0.f, 150.f, 0.f, 0.f, 1, 1.8f, o1, o2, w1, w2);
			if (i > SR / 2) sHi += std::fabs(o1 - o2) * 0.5;
		}
		printf("  elliptic 150 Hz: side 50 Hz + 3 kHz -> %.3f, 3 kHz alone -> %.3f (ratio %.3f)\n", sLo, sHi, sLo / sHi);
		check(std::fabs(sLo / sHi - 1.0) < 0.1, "side bass folded to mono, side highs untouched");
		// groove echo arrives exactly one revolution later (33 rpm = 1.8 s)
		Lathe g; g.setSampleRate(SR);
		int peakAt = -1; float pk = 0;
		for (int i = 0; i < SR * 3; i++) {
			float v = (i < 48) ? 8.f : 0.f, o1, o2, w1, w2;
			g.process(v, v, 0.f, 0.f, 0.f, 1.f, 1, 1.8f, o1, o2, w1, w2);
			if (i > SR && std::fabs(w1) > pk) { pk = std::fabs(w1); peakAt = i; }
		}
		printf("  echo peak at %.3f s, level %.4f\n", peakAt / (float) SR, pk);
		check(std::fabs(peakAt / (float) SR - 1.8f) < 0.01f, "groove echo one revolution late");
		// HF limiter pulls down loud highs
		Lathe h; h.setSampleRate(SR); Loop L; SVF a1, a2; double ei = 0, eo = 0;
		for (int i = 0; i < SR * 2; i++) {
			float t = i / (float) SR, a, b, o1, o2, w1, w2; L.next(t, a, b);
			h.process(a * 2.f, b * 2.f, 0.f, 0.f, 0.8f, 0.f, 1, 1.8f, o1, o2, w1, w2);
			float hi = a1.highpass(a * 2.f, svfG(7000.f, SR), 0.7f), ho = a2.highpass(o1, svfG(7000.f, SR), 0.7f);
			ei += hi * hi; eo += ho * ho;
		}
		printf("  HF limit 80%%: highs %.1f dB\n", 10 * std::log10(eo / ei));
		check(eo < ei * 0.5, "HF limiter reduces loud highs");
		// demo: drive up, elliptic on, echo on
		std::vector<float> wav;
		Lathe d; d.setSampleRate(SR); Loop L2;
		for (int i = 0; i < SR * 12; i++) {
			float t = i / (float) SR, a, b, o1, o2, w1, w2; L2.next(t, a, b);
			float sec = t < 4.f ? 0.f : 1.f;
			d.process(a, b, 0.7f * sec, 120.f * sec, 0.5f * sec, 0.85f, t < 8.f ? 1 : 2, 60.f / 128.f * 4.f, o1, o2, w1, w2);
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/lathe-demo.wav", wav);
	}

	// ---------------- RIAA ----------------
	printf("RIAA Sandwich\n");
	{
		// same cut and play curve with nothing in between = flat
		for (int k = 0; k < NUM_CURVES; k++) {
			RiaaPair e; e.setSampleRate(SR); e.set(k, k);
			float worst = 0;
			for (float f : {30.f, 100.f, 1000.f, 5000.f, 15000.f}) {
				float db = 20 * std::log10(e.cut[0].mag(f, SR) * e.play[0].mag(f, SR));
				worst = std::max(worst, std::fabs(db));
			}
			char b[96]; snprintf(b, sizeof b, "%-12s cut x play flat within %.2f dB (30 Hz-15 kHz)", CURVE_NAMES[k], worst);
			check(worst < 1.0f, b);
		}
		RiaaPair e; e.setSampleRate(SR); e.set(0, 0);
		printf("  RIAA cut curve: 50 Hz %+.1f dB, 10 kHz %+.1f dB\n", 20 * std::log10(e.cut[0].mag(50, SR)), 20 * std::log10(e.cut[0].mag(10000, SR)));
		RiaaPair mm; mm.setSampleRate(SR); mm.set(1, 0);
		printf("  Columbia cut, RIAA play: 50 Hz %+.1f dB, 10 kHz %+.1f dB\n",
		       20 * std::log10(mm.cut[0].mag(50, SR) * mm.play[0].mag(50, SR)), 20 * std::log10(mm.cut[0].mag(10000, SR) * mm.play[0].mag(10000, SR)));
		// demo: drive inside the sandwich vs. the same drive outside
		std::vector<float> wav; Loop L; Riaa r; r.setSampleRate(SR);
		for (int i = 0; i < SR * 8; i++) {
			float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
			if (t < 4.f) { o1 = Riaa::drive(a, 0.7f); o2 = Riaa::drive(b, 0.7f); }
			else { float s1, s2; r.toSend(a, b, 0, 0, s1, s2); r.fromReturn(Riaa::drive(s1, 0.7f), Riaa::drive(s2, 0.7f), o1, o2); }
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/riaa-demo.wav", wav);
	}

	// ---------------- TRAINWRECK ----------------
	printf("Trainwreck\n");
	{
		for (int both = 0; both < 2; both++) {
			Trainwreck tw; tw.setSampleRate(SR);
			float lo = 1e9, hi = -1e9; int nudges = 0; bool nan = false;
			Loop L;
			for (int i = 0; i < SR * 60; i++) {
				float t = i / (float) SR, a, b; L.next(t, a, b);
				float A[2] = {a, b}, B[2] = {a, b}, M[2], O[2];
				tw.process(A, B, 1.f, 0.6f, 0.f, both, 0.5f, false, M, O);
				if (!std::isfinite(M[0])) nan = true;
				float total = (both ? 50.f : 0.f) + tw.offMs;
				lo = std::min(lo, total); hi = std::max(hi, total);
				nudges += tw.nudgeFired;
			}
			printf("  %s: B delay %.1f..%.1f ms over 60 s, %d nudges\n", both ? "BOTH" : "LATE", lo, hi, nudges);
			check(!nan && lo >= 0.f && hi <= 100.f && nudges >= 1, "B stays inside 0-100 ms and gets nudged back");
		}
		Trainwreck tw; tw.setSampleRate(SR);
		std::vector<float> x, y; Rng r;
		for (int i = 0; i < SR / 2; i++) { float v = r.bi(); float A[2] = {v, v}, B[2] = {0, 0}, M[2], O[2]; tw.process(A, B, 0.f, 0.f, 0.f, false, 0.f, false, M, O); x.push_back(v); y.push_back(M[0]); }
		check(bestLag(x, y, 200) == 0, "LATE range: deck A has no latency");
		std::vector<float> wav; Loop L1, L2; Trainwreck d; d.setSampleRate(SR);
		for (int i = 0; i < SR * 20; i++) {
			float t = i / (float) SR, a, b, a2, b2; L1.next(t, a, b); L2.next(t + 0.003f, a2, b2);
			float A[2] = {a, b}, B[2] = {a2 * 0.9f, b2 * 0.9f}, M[2], O[2];
			d.process(A, B, 1.f, 0.7f, 0.f, true, 0.5f, false, M, O);
			wav.push_back(M[0] * 0.7f); wav.push_back(M[1] * 0.7f);
		}
		writeWav(dir + "/test/trainwreck-demo.wav", wav);
	}

	// ---------------- STYLUS ----------------
	printf("Stylus\n");
	{
		Stylus st; st.setSampleRate(SR);
		std::vector<float> x, y; Rng r;
		for (int i = 0; i < SR / 2; i++) { float v = r.bi(); float o1, o2; st.process(v, v, false, 0.5f, 0.3f, 1.f, 0.5f, o1, o2); x.push_back(v); y.push_back(o2); }
		int lag = bestLag(x, y, 100);
		printf("  late channel at full azimuth: %d samples\n", lag);
		check(lag <= 10, "azimuth delay under 0.2 ms");
		std::vector<float> wav; Loop L; Stylus d; d.setSampleRate(SR);
		for (int i = 0; i < SR * 8; i++) {
			float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
			d.process(a, b, false, t / 8.f, 0.5f, t < 4 ? 0.f : 1.f, t / 8.f, o1, o2);
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/stylus-demo.wav", wav);
	}

	// ---------------- PRESSING ----------------
	printf("Pressing\n");
	{
		auto hf = [](float gen, float wear) {
			Pressing p; p.setSampleRate(SR); Loop L; SVF h1, h2; double ei = 0, eo = 0; bool nan = false;
			for (int i = 0; i < SR * 2; i++) {
				float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
				p.process(a, b, gen, wear, o1, o2);
				if (!std::isfinite(o1)) nan = true;
				float x1 = h1.highpass(a, svfG(6000.f, SR), 0.7f), x2 = h2.highpass(o1, svfG(6000.f, SR), 0.7f);
				if (i > SR / 4) { ei += x1 * x1; eo += x2 * x2; }
			}
			return nan ? -999.f : (float) (10 * std::log10(eo / ei));
		};
		float h00 = hf(0, 0), h10 = hf(1, 0), h01 = hf(0, 1), h11 = hf(1, 1);
		printf("  >6 kHz change: lacquer %.1f, late pressing %.1f, worn dub %.1f, both %.1f dB\n", h00, h10, h01, h11);
		check(h00 > -1.5f && h10 < -4.f && h01 < h10 && h11 < h01 + 1.f, "generation and wear darken in order");
		std::vector<float> wav; Loop L; Pressing d; d.setSampleRate(SR);
		for (int i = 0; i < SR * 12; i++) {
			float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
			d.process(a, b, std::min(t / 6.f, 1.f), std::max((t - 6.f) / 6.f, 0.f), o1, o2);
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/pressing-demo.wav", wav);
	}

	// ---------------- SCRATCH ----------------
	printf("Scratch\n");
	{
		Scratch s; s.setSampleRate(SR);
		Rng r; double err = 0;
		for (int i = 0; i < SR; i++) { float v = r.bi(), o1, o2; s.process(v, v, o1, o2); err += std::fabs(o1 - v); }
		check(err < 1e-6, "no gesture = live audio untouched, no latency");
		// 8 steps of 16ths at 128 BPM, gestures cycle through all types
		std::vector<float> wav; Loop L; Scratch d; d.setSampleRate(SR);
		float step = 60.f / 128.f / 2.f;   // 8ths
		int st = -1; bool nan = false; float peak = 0;
		for (int i = 0; i < SR * 16; i++) {
			float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
			int k = (int) (t / step);
			if (k != st) {
				st = k;
				int g = (k % 4 == 3) ? 1 + (k / 4) % (NUM_GESTURES - 1) : G_OFF;
				d.startStep(g, step, step * 2.f, 0.6f);
			}
			d.process(a, b, o1, o2);
			if (!std::isfinite(o1)) nan = true;
			peak = std::max(peak, std::fabs(o1));
			wav.push_back(o1); wav.push_back(o2);
		}
		printf("  16 s of gestures: peak %.2f V\n", peak);
		check(!nan && peak < 15.f, "gestures stay finite and sane");
		writeWav(dir + "/test/scratch-demo.wav", wav);
	}

	printf(fails ? "%d FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
