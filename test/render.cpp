// Offline checks + demo WAVs for the VinylCountdown engines (no Rack needed).
//   make test     (writes test/*.wav)
// Checks the latency promise of every module, plus basic sanity.
#include "../src/ScratchDSP.hpp"
#include "../src/ChainDSP.hpp"
#include "../src/RadioDSP.hpp"
#include <cstdio>
#include <functional>
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
		check(lag == (int) std::lround(Platter::LATENCY * SR), "fixed 6 ms latency");
		// pitch swing for 0.3 mm near the label at 33
		Platter q; q.setSampleRate(SR);
		float mx = 0, mn = 2;
		for (int i = 0; i < SR * 4; i++) { float o, o2; q.process(0, 0, 0.3f, R_INNER, RPM_VALUES[0], o, o2); if (i > SR) { mx = std::max(mx, q.speed); mn = std::min(mn, q.speed); } }
		float cents = 1200.f * std::log2(mx);
		printf("  0.3 mm at label, 33 rpm: speed %.4f..%.4f = +-%.1f cents\n", mn, mx, cents);
		check(std::fabs(cents - 8.6f) < 0.6f, "0.3 mm at the label gives about 9 cents");
		// 1.2 mm worst case stays inside the 6 ms budget
		Platter w; w.setSampleRate(SR);
		float wmx = 0;
		for (int i = 0; i < SR * 4; i++) { float o, o2; w.process(0, 0, 1.2f, R_INNER, RPM_VALUES[0], o, o2); if (i > SR) wmx = std::max(wmx, w.speed); }
		printf("  1.2 mm at label: +-%.1f cents\n", 1200.f * std::log2(wmx));
		check(wmx > 1.019f && wmx < 1.021f, "1.2 mm at the label reaches about 34 cents, no clipping of the swing");
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

	// ---------------- CHAIN (the Vinyl Countdown module, defaults) ----------------
	printf("Vinyl Countdown chain\n");
	{
		Lathe la; RiaaPair rp; Pressing pr; InnerGroove ig; Stylus sty; Platter pl;
		la.setSampleRate(SR); rp.setSampleRate(SR); pr.setSampleRate(SR); ig.setSampleRate(SR); sty.setSampleRate(SR); pl.setSampleRate(SR);
		rp.set(0, 0);
		Loop L; std::vector<float> wav; double ein = 0, eout = 0; bool nan = false;
		for (int i = 0; i < SR * 16; i++) {
			float t = i / (float) SR, a, b; L.next(t, a, b);
			float l = a, r = b, wl, wr;
			float radius = std::min(1.f, 0.3f + t / 16.f * 0.7f);   // play the side inward
			la.process(l, r, 0.3f, 0.f, 0.f, 0.3f, 1, 60.f / 128.f * 4.f, l, r, wl, wr);
			l = rp.cut[0].process(l) * 0.5f; r = rp.cut[1].process(r) * 0.5f;
			pr.process(l, r, 0.3f, t > 8.f ? (t - 8.f) / 8.f : 0.f, l, r);
			ig.process(l, r, radius, RPM_VALUES[0], 0, 0.5f, 0.3f, l, r);
			sty.process(l, r, false, 0.4f, 0.25f, 0.f, 0.f, l, r);
			pl.process(l, r, 0.3f, R_OUTER - radius * (R_OUTER - R_INNER), RPM_VALUES[0], l, r);
			l = rp.play[0].process(l * 2.f); r = rp.play[1].process(r * 2.f);
			if (!std::isfinite(l)) nan = true;
			if (i > SR && i < SR * 4) { ein += a * a; eout += l * l; }
			wav.push_back(l); wav.push_back(r);
		}
		float db = 10 * std::log10(eout / ein);
		printf("  level through the default chain: %+.1f dB\n", db);
		check(!nan && std::fabs(db) < 4.f, "default chain keeps level within 4 dB");
		writeWav(dir + "/test/vinylcountdown-demo.wav", wav);
	}

	// ---------------- SCRATCH ----------------
	printf("Scratch\n");
	{
		ScratchEngine s; s.setSampleRate(SR);
		Rng r; double err = 0;
		for (int i = 0; i < SR; i++) { float v = r.bi(), o1, o2; s.process(v, v, 0.8f, false, 0.f, false, o1, o2); err += std::fabs(o1 - v); }
		check(err < 1e-6, "no gesture = live audio untouched, no latency");

		// every gesture, LIVE source, finite and sane
		bool ok = true;
		for (int g = 1; g < NUM_SGESTURES; g++) {
			ScratchEngine e; e.setSampleRate(SR); Loop L; float peak = 0;
			for (int i = 0; i < SR * 3; i++) {
				float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b);
				if (i == SR * 2) e.start(g, ScratchEngine::LIVE, 0.47f, 0.6f, 0.5f, 0.5f, 0.47f, 0.f, true);
				e.process(a, b, 0.8f, false, 0.f, false, o1, o2);
				if (!std::isfinite(o1)) ok = false;
				if (i > SR * 2) peak = std::max(peak, std::fabs(o1));
			}
			if (peak > 15.f || peak < 0.05f) { ok = false; printf("  %s peak %.2f\n", SGESTURE_NAMES[g], peak); }
		}
		check(ok, "all 12 gestures play from LIVE, finite and sane");

		// human: the same gesture performed twice is not identical
		ScratchEngine h; h.setSampleRate(SR);
		h.start(S_TRANSFORM, ScratchEngine::LIVE, 0.5f, 0.5f, 1.f, 1.f, 0.5f, 0.f, false);
		auto f1 = h.fader; auto k1 = h.hand;
		h.start(S_TRANSFORM, ScratchEngine::LIVE, 0.5f, 0.5f, 1.f, 1.f, 0.5f, 0.f, false);
		bool differ = f1.size() != h.fader.size();
		for (size_t i = 0; i < std::min(f1.size(), h.fader.size()); i++) differ |= std::fabs(f1[i].t - h.fader[i].t) > 1e-4f;
		check(differ, "HUMAN/SLOPPY re-perform each gesture differently");
		ScratchEngine c; c.setSampleRate(SR);
		c.start(S_TRANSFORM, ScratchEngine::LIVE, 0.5f, 0.5f, 0.f, 0.f, 0.5f, 0.f, false);
		bool exact = c.fader.size() == 16;
		for (size_t i = 1; i < c.fader.size() && exact; i++) exact = std::fabs(c.fader[i].t - (i / 2) / 8.f - (i & 1 ? 0.07f : 0.f)) < 1e-5f;
		check(exact, "HUMAN 0 + SLOPPY 0 = machine-exact");

		// GRAB + onsets
		ScratchEngine gb; gb.setSampleRate(SR); Loop L;
		for (int i = 0; i < SR * 3; i++) { float t = i / (float) SR, a, b, o1, o2; L.next(t, a, b); gb.process(a, b, 0.8f, false, 0.f, false, o1, o2); }
		auto rec = gb.grab(2.f);
		printf("  grabbed %.2f s, %d hits found (2 s at 128 BPM: about 4 kicks + 4 hats)\n", rec ? rec->seconds() : 0.f, rec ? (int) rec->onsets.size() : 0);
		check(rec && rec->onsets.size() >= 6 && rec->onsets.size() <= 14, "GRAB freezes audio and finds the hits");

		// demo: 16 bars at 128 BPM 8ths. Bars 1-8 clean (HUMAN 0), 9-16 human + sloppy
		std::vector<float> wav; ScratchEngine d; d.setSampleRate(SR); Loop L2;
		float stepLen = 60.f / 128.f / 2.f;
		const int pat[8] = {0, 0, S_BABY, 0, 0, S_CHIRP, 0, S_TRANSFORM};
		const int pat2[8] = {0, S_FLARE, 0, S_CRAB, 0, S_ORBIT, S_TEAR, S_STAB};
		int st = -1;
		for (int i = 0; i < SR * 30; i++) {
			float t = i / (float) SR, a, b, o1, o2; L2.next(t, a, b);
			int k = (int) (t / stepLen);
			if (k != st) {
				st = k;
				bool late = t > 15.f;
				int g = (k / 8) % 2 ? pat2[k % 8] : pat[k % 8];
				if (g) d.start(g, ScratchEngine::LIVE, stepLen, 0.55f, late ? 0.6f : 0.f, late ? 0.6f : 0.f, stepLen * 2.f, 0.f, true);
			}
			d.process(a, b, 0.85f, false, 0.f, false, o1, o2);
			wav.push_back(o1); wav.push_back(o2);
		}
		writeWav(dir + "/test/scratch-demo.wav", wav);
	}

	{
		printf("Preamp\n");
		auto rms = [](const std::vector<float>& v) { double a = 0; for (float x : v) a += x * x; return (float) std::sqrt(a / std::max<size_t>(v.size(), 1)); };
		// silence in, no hum: silence out
		Preamp p; p.setSampleRate(SR);
		std::vector<float> o;
		for (int i = 0; i < SR; i++) { float a, b; p.process(0, 0, 0.8f, 0.8f, 0.f, false, false, a, b); if (i > SR / 2) o.push_back(a); }
		check(rms(o) < 1e-3f, "no hum, no input: silent");
		// impulse at zero settings: no latency
		Preamp z; z.setSampleRate(SR);
		int pk = -1; float best = 0;
		for (int i = 0; i < 64; i++) { float a, b; z.process(i == 10 ? 1.f : 0.f, 0, 0.f, 0.f, 0.f, false, false, a, b); if (std::fabs(a) > best) { best = std::fabs(a); pk = i; } }
		check(pk == 10, "0 latency");
		// hum level
		Preamp h; h.setSampleRate(SR); o.clear();
		for (int i = 0; i < SR; i++) { float a, b; h.process(0, 0, 0.f, 0.f, 1.f, false, false, a, b); o.push_back(a); }
		float hr = rms(o);
		printf("  HUM 100%%: %.3f V rms (%.1f dB re 5 V)\n", hr, 20.f * std::log10(hr / 5.f));
		check(hr > 0.15f && hr < 0.4f, "hum at full is clearly audible but well under the music");
		Preamp g; g.setSampleRate(SR); o.clear();
		for (int i = 0; i < SR; i++) { float a, b; g.process(0, 0, 0.f, 0.f, 0.f, false, true, a, b); if (i > SR / 4) o.push_back(a); }
		float gr = rms(o);
		printf("  ground lifted: %.3f V rms\n", gr);
		check(gr > 0.3f && gr < 1.5f, "lifted ground hums and buzzes");
		// valve: small signals stay about the same level, loud ones squash with 2nd harmonic
		for (float lvl : {0.5f, 5.f}) {
			Preamp v; v.setSampleRate(SR);
			std::vector<float> in, out;
			for (int i = 0; i < SR; i++) { float x = lvl * std::sin(2 * PI * 220.f * i / SR), a, b; v.process(x, x, 0.8f, 0.f, 0.f, false, false, a, b); if (i > SR / 2) { in.push_back(x); out.push_back(a); } }
			// 2nd harmonic by correlation
			double c2s = 0, c2c = 0, c1 = 0;
			for (size_t i = 0; i < out.size(); i++) { double ph = 2 * PI * 220.0 * (i + SR / 2 + 1) / SR; c2s += out[i] * std::sin(2 * ph); c2c += out[i] * std::cos(2 * ph); c1 += out[i] * std::sin(ph); }
			float h2 = (float) (std::sqrt(c2s * c2s + c2c * c2c) / std::max(std::fabs(c1), 1e-9));
			printf("  valve 80%%, %.1f V sine: gain %.2f, 2nd harmonic %.1f%%\n", lvl, rms(out) / rms(in), h2 * 100.f);
			if (lvl < 1.f) check(std::fabs(rms(out) / rms(in) - 1.f) < 0.25f, "small signal about unity gain");
			else check(h2 > 0.01f, "loud signal gets even harmonics");
		}
	}

	{
		printf("Studio + Listening Room\n");
		auto rms = [](const std::vector<float>& v) { double a = 0; for (float x : v) a += x * x; return (float) std::sqrt(a / std::max<size_t>(v.size(), 1)); };
		auto finite = [](const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x) || std::fabs(x) > 30.f) return false; return true; };
		// a run of the music loop through f(l, r, &ol, &or)
		auto run = [&](std::function<void(float, float, float&, float&)> f, float secs, std::vector<float>* in = nullptr) {
			Loop L; std::vector<float> o;
			for (int i = 0; i < (int) (SR * secs); i++) { float t = i / (float) SR, a, b, x, y; L.next(t, a, b); f(a, b, x, y); o.push_back(x); o.push_back(y); if (in) { in->push_back(a); in->push_back(b); } }
			return o;
		};
		std::vector<float> dry; run([](float a, float b, float& x, float& y) { x = a; y = b; }, 4.f, &dry);
		float dryR = rms(dry);
		auto db = [&](const std::vector<float>& v) { return 20.f * std::log10(rms(v) / dryR); };
		// console
		{ Console c; c.setSampleRate(SR); auto o = run([&](float a, float b, float& x, float& y) { c.process(a, b, 0.6f, 0.7f, 0.5f, x, y); }, 4.f);
		  printf("  console (drive 60%%, iron 70%%): %+.1f dB\n", db(o)); check(finite(o) && std::fabs(db(o)) < 4.f, "console keeps level within 4 dB"); }
		// tape latency at flutter 0 + level
		{ Tape t; t.setSampleRate(SR); std::vector<float> x, y; Rng r;
		  for (int i = 0; i < SR / 2; i++) { float v = r.bi(), a, b; t.process(v, v, 0.f, 0.f, 2, 0.f, 0.f, a, b); x.push_back(v); y.push_back(a); }
		  int lag = bestLag(x, y, 400); printf("  tape latency: %.2f ms\n", lag * 1000.f / SR); check(std::abs(lag - (int) std::lround(Tape::LATENCY * SR)) <= 1, "tape fixed 3 ms latency");
		  Tape u; u.setSampleRate(SR); auto o = run([&](float a, float b, float& x2, float& y2) { u.process(a, b, 0.8f, 0.8f, 1, 0.8f, 0.5f, x2, y2); }, 4.f);
		  printf("  tape (sat 80%%, flutter 80%%): %+.1f dB\n", db(o)); check(finite(o) && std::fabs(db(o)) < 4.f, "tape keeps level within 4 dB"); }
		// dolby
		{ float lv[3];
		  for (int p = 0; p < 3; p++) { DolbyA d; d.setSampleRate(SR); auto o = run([&](float a, float b, float& x, float& y) { d.process(a * 0.3f, b * 0.3f, 0.f, p, x, y); x /= 0.3f; y /= 0.3f; }, 4.f); lv[p] = db(o); check(finite(o), "dolby finite"); }
		  printf("  dolby OK %+.1f dB, ENC %+.1f dB, DEC %+.1f dB\n", lv[0], lv[1], lv[2]);
		  check(std::fabs(lv[0]) < 1.5f, "dolby encode+decode about transparent");
		  check(lv[1] > lv[0] + 0.3f && lv[2] < lv[0] - 0.3f, "ENC louder/brighter, DEC quieter/duller");
		  DolbyA e; e.setSampleRate(SR); auto o = run([&](float a, float b, float& x, float& y) { e.process(a * 0.3f, b * 0.3f, 6.f, 0, x, y); x /= 0.3f; y /= 0.3f; }, 4.f);
		  printf("  dolby ERROR +6 dB: %+.1f dB\n", db(o));
		  DolbyA e2; e2.setSampleRate(SR); auto o2 = run([&](float a, float b, float& x, float& y) { e2.process(a * 0.3f, b * 0.3f, -6.f, 0, x, y); x /= 0.3f; y /= 0.3f; }, 4.f);
		  printf("  dolby ERROR -6 dB: %+.1f dB\n", db(o2)); }
		// glue
		{ Glue g; g.setSampleRate(SR); float mx = 0; auto o = run([&](float a, float b, float& x, float& y) { g.process(a, b, 0.8f, 1, x, y); mx = std::max(mx, g.grDb); }, 4.f);
		  printf("  glue 80%%: max GR %.1f dB, level %+.1f dB\n", mx, db(o)); check(mx > 3.f && finite(o), "glue squeezes the loud bits"); }
		// amp
		for (int ty = 0; ty < 3; ty++) { PowerAmp p; p.setSampleRate(SR); auto o = run([&](float a, float b, float& x, float& y) { p.process(a, b, 0.6f, ty, true, x, y); }, 3.f);
		  printf("  amp type %d: %+.1f dB\n", ty, db(o)); check(finite(o) && std::fabs(db(o)) < 9.f, "amp level sane"); }
		// speakers
		for (int m = 0; m < NUM_SPEAKERS; m++) { Speaker sp; sp.setSampleRate(SR); auto o = run([&](float a, float b, float& x, float& y) { sp.process(a, b, m, 0.5f, x, y); }, 2.f);
		  printf("  speaker %-24s %+.1f dB\n", SPEAKERS[m].name, db(o)); check(finite(o), "speaker finite"); }
		// room + ears
		{ Room rm; rm.setSampleRate(SR); Ears e; e.setSampleRate(SR);
		  auto o = run([&](float a, float b, float& x, float& y) { float dl, dr, wl, wr; rm.process(a, b, 0.5f, 0.4f, 0.5f, false, dl, dr, wl, wr); e.process(dl, dr, wl, wr, 0.4f, 0.3f, 0.3f, x, y); }, 4.f);
		  printf("  room + ears: %+.1f dB\n", db(o)); check(finite(o) && std::fabs(db(o)) < 8.f, "room + ears level sane");
		  Room w; w.setSampleRate(SR); auto o2 = run([&](float a, float b, float& x, float& y) { float dl, dr, wl, wr; w.process(a, b, 0.5f, 0.4f, 0.f, true, dl, dr, wl, wr); x = dl + wl; y = dr + wr; }, 3.f);
		  check(finite(o2), "next door finite");
		  // impulse: direct sound arrives with no delay at angle 0
		  Room z; z.setSampleRate(SR); Ears ez; ez.setSampleRate(SR); int pk = -1; float best = 0;
		  for (int i = 0; i < 200; i++) { float dl, dr, wl, wr, x, y; z.process(i == 50 ? 1.f : 0.f, i == 50 ? 1.f : 0.f, 0.3f, 0.5f, 0.f, false, dl, dr, wl, wr); ez.process(dl, dr, wl, wr, 0.f, 0.f, 0.f, x, y); if (std::fabs(x) > best) { best = std::fabs(x); pk = i; } }
		  printf("  direct sound peak at %d samples after the input\n", pk - 50); check(pk - 50 <= 2, "listening room: direct sound not delayed"); }
		// demo: studio chain then the room
		{ Console c; Tape t; DolbyA d; Glue g; PowerAmp p; Speaker sp; Room rm; Ears e;
		  c.setSampleRate(SR); t.setSampleRate(SR); d.setSampleRate(SR); g.setSampleRate(SR); p.setSampleRate(SR); sp.setSampleRate(SR); rm.setSampleRate(SR); e.setSampleRate(SR);
		  auto o = run([&](float a, float b, float& x, float& y) {
			  float t1 = 0;
			  c.process(a, b, 0.5f, 0.6f, 0.3f, a, b); t.process(a, b, 0.6f, 0.6f, 1, 0.4f, 0.1f, a, b);
			  d.process(a, b, -3.f, 0, a, b); g.process(a, b, 0.5f, 1, a, b);
			  p.process(a, b, 0.5f, 0, false, a, b); sp.process(a, b, 0, 0.3f, a, b);
			  float dl, dr, wl, wr; rm.process(a, b, 0.4f, 0.5f, 0.4f, false, dl, dr, wl, wr); e.process(dl, dr, wl, wr, 0.35f, 0.f, 0.f, x, y); (void) t1; }, 8.f);
		  writeWav(dir + "/test/studio-room-demo.wav", o); }
	}

	{
		printf("On Air (FM link)\n");
		auto tone = [&](float sig, float mp, float motion, float tune, int set, float& sep, float& lvl, float& blend) {
			FmLink f; f.setSampleRate(SR);
			double a = 0, b = 0, in = 0;
			for (int i = 0; i < SR * 2; i++) { float x = 2.5f * std::sin(2 * PI * 1000.f * i / SR), l, r;
				f.process(x, 0, 0, 0, false, 0.f, 0, false, sig, mp, motion, 0.f, true, tune, set, false, l, r);
				if (i > SR) { a += l * l; b += r * r; in += x * x; } }
			sep = 10.f * (float) std::log10(b / a + 1e-12); lvl = 10.f * (float) std::log10(a / in); blend = f.blend;
		};
		float sep, lvl, bl;
		tone(1.f, 0.f, 0.f, 0.f, 0, sep, lvl, bl);
		printf("  strong signal: level %+.1f dB, separation %.1f dB\n", lvl, sep);
		check(std::fabs(lvl) < 1.f && sep < -30.f, "strong signal: clean stereo");
		tone(0.25f, 0.f, 0.f, 0.f, 1, sep, lvl, bl);
		printf("  weak signal, car radio: blend %.2f, separation %.1f dB\n", bl, sep);
		check(bl < 0.2f && sep > -3.f, "weak signal: the radio blends to mono");
		// latency
		{ FmLink f; f.setSampleRate(SR); std::vector<float> x, y; Rng r; float lp = 0;
		  for (int i = 0; i < SR / 2; i++) { lp += 0.3f * (r.bi() - lp); float v = lp * 3.f, a, b; f.process(v, v, 0, 0, false, 0.f, 0, false, 1.f, 0, 0, 0, true, 0, 2, false, a, b); x.push_back(v); y.push_back(a); }
		  int lag = bestLag(x, y, 200); printf("  latency %.2f ms\n", lag * 1000.f / SR); check(lag * 1000.f / SR < 1.f, "under 1 ms latency"); }
		// demo: strong -> driving through town -> fringe -> drifting to the next station
		{ FmLink f; f.setSampleRate(SR); Loop L; std::vector<float> wav;
		  for (int i = 0; i < SR * 16; i++) {
			  float t = i / (float) SR, a, b, l, r; L.next(t, a, b);
			  float n = 2.f * std::sin(2 * PI * 330.f * t) * (std::fmod(t * 2.f, 1.f) < 0.5f ? 1.f : 0.3f);
			  float sig = t < 4 ? 0.9f : (t < 8 ? 0.6f : (t < 11 ? 0.3f : 0.8f));
			  float mp = (t >= 4 && t < 8) ? 0.8f : 0.1f, mo = (t >= 4 && t < 8) ? 0.6f : 0.f;
			  float tune = t < 11 ? 0.f : std::min((t - 11.f) / 4.f, 1.f) * 200000.f;
			  f.process(a, b, n, n, true, 0.6f, 1, false, sig, mp, mo, 0.8f, true, tune, t < 4 ? 2 : 1, false, l, r);
			  wav.push_back(l); wav.push_back(r);
		  }
		  writeWav(dir + "/test/onair-demo.wav", wav); }
	}

	printf(fails ? "%d FAILED\n" : "all ok\n", fails);
	return fails ? 1 : 0;
}
