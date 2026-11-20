// Shared bits for the chain modules (Studio, Listening Room, On Air):
// two assignable CV inputs with a screen and target rings, plus presets.
#pragma once
#include "plugin.hpp"
#include <map>

namespace chain {

enum Kind { NONE, KNOB, TRIG, GATE };
struct Dest { const char* menu; const char* screen; int kind; int param; float perVolt; };

// per-sample state of CV A and CV B
struct Assign {
	const Dest* table = nullptr;
	int n = 1;
	int dA = 0, dB = 0;
	float vA = 0.f, vB = 0.f;
	bool fA = false, fB = false;
	dsp::SchmittTrigger tA, tB;
	void update(Module* m, int destA, int amtA, int inA, int destB, int amtB, int inB) {
		dA = clamp((int) std::round(m->params[destA].getValue()), 0, n - 1);
		dB = clamp((int) std::round(m->params[destB].getValue()), 0, n - 1);
		vA = m->inputs[inA].getVoltage() * m->params[amtA].getValue();
		vB = m->inputs[inB].getVoltage() * m->params[amtB].getValue();
		fA = tA.process(vA, 0.1f, 1.f);
		fB = tB.process(vB, 0.1f, 1.f);
	}
	float mod(int p) const {
		float s = 0.f;
		if (table[dA].kind == KNOB && table[dA].param == p) s += vA * table[dA].perVolt;
		if (table[dB].kind == KNOB && table[dB].param == p) s += vB * table[dB].perVolt;
		return s;
	}
	bool fired(int p) const { return (fA && table[dA].kind == TRIG && table[dA].param == p) || (fB && table[dB].kind == TRIG && table[dB].param == p); }
	bool gate(int p) const { return (table[dA].kind == GATE && table[dA].param == p && vA > 1.f) || (table[dB].kind == GATE && table[dB].param == p && vB > 1.f); }
};

inline std::vector<std::string> destNames(const Dest* t, int n) {
	std::vector<std::string> v;
	for (int i = 0; i < n; i++) v.push_back(t[i].menu);
	return v;
}

struct Screen : VinylScreen {
	Module* module = nullptr;
	const Dest* table = nullptr;
	int n = 1, destParam = 0, amtParam = 0;
	const char* tag = "A";
	const char* demo = "";
	void lines(std::vector<std::string>& out) override {
		if (!module) { out.push_back(demo); return; }
		int d = clamp((int) std::round(module->params[destParam].getValue()), 0, n - 1);
		float amt = module->params[amtParam].getValue();
		const Dest& D = table[d];
		if (D.kind == NONE) out.push_back(string::f("%s > OFF", tag));
		else if (D.kind == KNOB) out.push_back(string::f("%s > %s %+d%%", tag, D.screen, (int) std::round(amt * 100.f)));
		else out.push_back(string::f("%s > %s%s", tag, D.screen, amt < 0.f ? " INV" : ""));
	}
};

struct Spot { float x, y, r; };
struct Rings : TransparentWidget {
	Module* module = nullptr;
	const Dest* table = nullptr;
	int n = 1, destA = 0, destB = 0;
	std::map<int, Spot> spots;
	void ring(NVGcontext* vg, Spot s, NVGcolor c, float extra) {
		Vec p = P(s.x, s.y);
		float rr = mm2px(s.r + extra);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, mm2px(0.55f));
		const int N = 18;
		for (int i = 0; i < N; i++) {
			float a0 = 2.f * M_PI * i / N, a1 = a0 + 2.f * M_PI / N * 0.6f;
			nvgBeginPath(vg);
			nvgArc(vg, p.x, p.y, rr, a0, a1, NVG_CW);
			nvgStroke(vg);
		}
	}
	void draw(const DrawArgs& args) override {
		if (!module) return;
		int d[2] = {clamp((int) std::round(module->params[destA].getValue()), 0, n - 1),
		            clamp((int) std::round(module->params[destB].getValue()), 0, n - 1)};
		NVGcolor cols[2] = {nvgRGB(232, 176, 74), nvgRGB(127, 184, 164)};
		for (int k = 0; k < 2; k++) {
			if (table[d[k]].kind == NONE) continue;
			auto it = spots.find(table[d[k]].param);
			if (it == spots.end()) continue;
			ring(args.vg, it->second, cols[k], (k == 1 && d[0] == d[1]) ? 1.f : 0.f);
		}
	}
};

// add the CV A/B block (jacks, knobs, screens) and the rings overlay
template <class LAYOUT_A, class LAYOUT_B>
void addAssignWidgets(ModuleWidget* w, Module* m, const Dest* table, int n,
                      int inA, int inB, int destA, int amtA, int destB, int amtB,
                      const LAYOUT_A& la, const LAYOUT_B& lb, const char* demoA, const char* demoB) {
	const float* L[2] = {la, lb};
	int ins[2] = {inA, inB}, dests[2] = {destA, destB}, amts[2] = {amtA, amtB};
	const char* demos[2] = {demoA, demoB};
	for (int k = 0; k < 2; k++) {
		// L[k] = {in x, in y, dest x, dest y, amt x, amt y, disp x, disp y, disp w, disp h}
		w->addInput(createInputCentered<PJ301MPort>(P(L[k][0], L[k][1]), m, ins[k]));
		w->addParam(createParamCentered<RoundSmallBlackKnob>(P(L[k][2], L[k][3]), m, dests[k]));
		w->addParam(createParamCentered<Trimpot>(P(L[k][4], L[k][5]), m, amts[k]));
		Screen* s = createWidget<Screen>(P(L[k][6], L[k][7]));
		s->box.size = mm2px(Vec(L[k][8], L[k][9]));
		s->module = m; s->table = table; s->n = n; s->destParam = dests[k]; s->amtParam = amts[k];
		s->tag = k ? "B" : "A"; s->demo = demos[k];
		s->accent = k ? nvgRGB(127, 184, 164) : nvgRGB(232, 176, 74);
		s->fontMm = 2.3f;
		w->addChild(s);
	}
}

// presets: start from defaults, set the listed values; CV routing and
// bypass are left alone
struct PV { int param; float value; };
struct Preset { const char* name; std::vector<PV> values; };
inline void applyPreset(Module* m, const Preset& p, std::vector<int> keep) {
	history::ModuleChange* h = new history::ModuleChange;
	h->name = string::f("load preset %s", p.name);
	h->moduleId = m->id;
	h->oldModuleJ = m->toJson();
	for (int i = 0; i < (int) m->params.size(); i++) {
		if (std::find(keep.begin(), keep.end(), i) != keep.end()) continue;
		m->paramQuantities[i]->reset();
	}
	for (const PV& v : p.values) m->params[v.param].setValue(v.value);
	h->newModuleJ = m->toJson();
	APP->history->push(h);
}

} // namespace chain
