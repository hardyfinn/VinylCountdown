// Scratch: an 8-step scratch sequencer and gesture trigger that plays like
// a person. Sources: LIVE (rolling), GRAB (frozen) or a loaded WAV.
// Live audio passes straight through between gestures.
//
// HUMAN  timing that drifts the way a player does (correlated, sometimes
//        early, sometimes late, up to +-28 ms), plus small changes in speed
//        and stroke size every time a gesture is played.
// SLOPPY extra mess on the fiddly parts: fader taps drift and lag behind
//        the hand, and get missed now and then. Scaled by how complex each
//        gesture is, so a baby scratch stays clean while a crab falls apart.
#include "plugin.hpp"
#include "ScratchDSP.hpp"
#include <osdialog.h>
#include <fstream>
namespace LSc = layout::Scratch;

// ---- minimal WAV reader (PCM 8/16/24/32 bit, float 32; mono or stereo) ----
static std::shared_ptr<vinyl::Record> loadWav(const std::string& path, std::string& err) {
	std::ifstream f(path, std::ios::binary);
	if (!f) { err = "can't open file"; return nullptr; }
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) { err = "not a WAV file"; return nullptr; }
	auto u16 = [&](size_t i) { return (uint32_t) d[i] | ((uint32_t) d[i + 1] << 8); };
	auto u32 = [&](size_t i) { return u16(i) | (u16(i + 2) << 16); };
	int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0;
	size_t dataPos = 0, dataLen = 0;
	size_t p = 12;
	while (p + 8 <= d.size()) {
		uint32_t len = u32(p + 4);
		if (!std::memcmp(d.data() + p, "fmt ", 4) && p + 24 <= d.size()) {
			fmt = u16(p + 8); ch = u16(p + 10); rate = u32(p + 12); bits = u16(p + 22);
			if (fmt == 0xFFFE && len >= 40) fmt = u16(p + 32);   // WAVE_FORMAT_EXTENSIBLE
		}
		else if (!std::memcmp(d.data() + p, "data", 4)) {
			dataPos = p + 8;
			dataLen = std::min<size_t>(len, d.size() - dataPos);
			break;
		}
		p += 8 + len + (len & 1);
	}
	if (!dataPos || ch < 1 || rate < 4000) { err = "unsupported WAV"; return nullptr; }
	if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) { err = "unsupported sample format"; return nullptr; }
	int bps = bits / 8;
	size_t frames = dataLen / (bps * ch);
	frames = std::min<size_t>(frames, (size_t) rate * 120);   // two minutes is plenty
	auto rec = std::make_shared<vinyl::Record>();
	rec->sr = (float) rate;
	rec->L.resize(frames); rec->R.resize(frames);
	auto sample = [&](size_t i) -> float {
		const uint8_t* q = d.data() + i;
		if (fmt == 3) { float v; std::memcpy(&v, q, 4); return v; }
		switch (bits) {
			case 8: return (q[0] - 128) / 128.f;
			case 16: return (int16_t) (q[0] | (q[1] << 8)) / 32768.f;
			case 24: { int32_t v = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); return v / 2147483648.f; }
			default: { int32_t v = (int32_t) (q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t) q[3] << 24)); return v / 2147483648.f; }
		}
	};
	for (size_t i = 0; i < frames; i++) {
		size_t base = dataPos + i * bps * ch;
		float l = sample(base);
		float r = ch > 1 ? sample(base + bps) : l;
		rec->L[i] = l * 5.f;
		rec->R[i] = r * 5.f;
	}
	rec->analyse();
	return rec;
}

struct ScratchModule : Module {
	enum ParamIds {
		ENUMS(STEP_PARAM, 8), SOURCE_PARAM, GRAB_PARAM, CUE_PARAM, BACK_PARAM, GESTURE_PARAM, LENGTH_PARAM, DEPTH_PARAM,
		HUMAN_PARAM, SLOPPY_PARAM, CHANCE_PARAM, STEPS_PARAM, CURVE_PARAM, MODE_PARAM, DUCK_PARAM,
		NUM_PARAMS
	};
	enum InputIds { CLOCK_INPUT, RESET_INPUT, TRIG_INPUT, GEST_INPUT, GRAB_INPUT, CUE_INPUT, DEPTH_INPUT, HUMAN_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { END_OUTPUT, HAND_OUTPUT, FADER_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(STEP_LIGHT, 8), GRAB_LIGHT, NUM_LIGHTS };

	vinyl::ScratchEngine eng;
	dsp::SchmittTrigger clockTrig, resetTrig, trigTrig, grabTrig, grabBtn;
	dsp::PulseGenerator endPulse;
	float sinceClock = 10.f, period = 0.25f;
	bool clockKnown = false;
	int step = -1;
	bool played = false;
	bool earlyStarted = false;
	float nextOffset = 0.f;      // HUMAN offset for the next step (s, negative = early)
	float pendingStep = -1.f;    // countdown to a late step start
	int pendingStepIdx = 0;
	float pendingTrig = -1.f;    // countdown to a humanised trigger gesture
	float walk = 0.f;
	float grabGlow = 0.f;
	std::string wavPath, wavName, wavError;
	vinyl::Rng rng;

	ScratchModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::vector<std::string> names(vinyl::SGESTURE_NAMES, vinyl::SGESTURE_NAMES + vinyl::NUM_SGESTURES);
		const int def[8] = {0, 0, 1, 0, 0, 2, 0, 7};
		for (int i = 0; i < 8; i++)
			configSwitch(STEP_PARAM + i, 0.f, vinyl::NUM_SGESTURES - 1, def[i], string::f("Step %d gesture", i + 1), names);
		configSwitch(SOURCE_PARAM, 0.f, 2.f, 0.f, "Source", {"Live (rolling, last 8 s)", "Grab (frozen live audio)", "WAV (right-click or drop a file)"});
		configButton(GRAB_PARAM, "Grab: freeze the last bar or two of live audio");
		configParam(CUE_PARAM, 0.f, 1.f, 0.f, "Cue (GRAB/WAV: where on the record; snaps to the nearest hit)", "%", 0.f, 100.f);
		configParam(BACK_PARAM, 1.f, 8.f, 2.f, "Back (LIVE: scratch the audio from this many steps ago)", " steps");
		paramQuantities[BACK_PARAM]->snapEnabled = true;
		configSwitch(GESTURE_PARAM, 1.f, vinyl::NUM_SGESTURES - 1, 1.f, "Gesture for the TRIG input",
		             std::vector<std::string>(vinyl::SGESTURE_NAMES + 1, vinyl::SGESTURE_NAMES + vinyl::NUM_SGESTURES));
		configSwitch(LENGTH_PARAM, 0.f, 3.f, 1.f, "Length of a TRIG gesture", {"1/2 step", "1 step", "2 steps", "4 steps"});
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth (how far the hand moves the record)", "%", 0.f, 100.f);
		configParam(HUMAN_PARAM, 0.f, 1.f, 0.35f, "Human (timing drift, early and late, plus speed and stroke changes)", "%", 0.f, 100.f);
		configParam(SLOPPY_PARAM, 0.f, 1.f, 0.2f, "Sloppy (messier fader work on complex gestures)", "%", 0.f, 100.f);
		configParam(CHANCE_PARAM, 0.f, 1.f, 1.f, "Chance (each sequenced gesture plays or stays live)", "%", 0.f, 100.f);
		configParam(STEPS_PARAM, 1.f, 8.f, 8.f, "Steps (pattern length)");
		paramQuantities[STEPS_PARAM]->snapEnabled = true;
		configParam(CURVE_PARAM, 0.f, 1.f, 0.85f, "Fader curve (smooth .. razor cut)", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Insert (the scratch replaces the signal)", "Over (the scratch plays on top)"});
		configParam(DUCK_PARAM, 0.f, 1.f, 0.5f, "Duck (OVER: how far the live signal dips under a scratch)", "%", 0.f, 100.f);
		configInput(CLOCK_INPUT, "Clock (one step per pulse)");
		configInput(RESET_INPUT, "Reset to step 1");
		configInput(TRIG_INPUT, "Gesture trigger (plays GESTURE for LENGTH)");
		configInput(GEST_INPUT, "Gesture CV for TRIG (+1 gesture per 0.8 V)");
		configInput(GRAB_INPUT, "Grab trigger");
		configInput(CUE_INPUT, "Cue CV (+10%/V)");
		configInput(DEPTH_INPUT, "Depth CV (+10%/V)");
		configInput(HUMAN_INPUT, "Human CV (+10%/V)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(END_OUTPUT, "End of gesture trigger");
		configOutput(HAND_OUTPUT, "Hand position (0-10 V)");
		configOutput(FADER_OUTPUT, "Crossfader (10 V = open)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		rng.s = (uint32_t) random::u32() | 1u;
		eng.rng.s = (uint32_t) random::u32() | 1u;
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		eng.reset(); step = -1; earlyStarted = false; pendingStep = pendingTrig = -1.f;
	}

	float human() { return clamp(params[HUMAN_PARAM].getValue() + inputs[HUMAN_INPUT].getVoltage() * 0.1f, 0.f, 1.f); }
	float depth() { return clamp(params[DEPTH_PARAM].getValue() + inputs[DEPTH_INPUT].getVoltage() * 0.1f, 0.f, 1.f); }
	float cue() { return clamp(params[CUE_PARAM].getValue() + inputs[CUE_INPUT].getVoltage() * 0.1f, 0.f, 1.f); }
	int source() { return clamp((int) std::round(params[SOURCE_PARAM].getValue()), 0, 2); }

	// a player's timing: mostly drifting (ahead for a while, then behind),
	// with a little jitter on top
	float makeOffset() {
		walk = 0.88f * walk + 0.12f * rng.bi() * 2.2f;
		float v = clampf(0.7f * walk + 0.3f * rng.bi(), -1.f, 1.f);
		return human() * 0.028f * v;
	}
	static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

	int advance() {
		int steps = clamp((int) params[STEPS_PARAM].getValue(), 1, 8);
		step = (step + 1) % steps;
		return step;
	}
	void playStep(int k) {
		int g = clamp((int) std::round(params[STEP_PARAM + k].getValue()), 0, vinyl::NUM_SGESTURES - 1);
		played = g != vinyl::S_OFF && rng.uni() < params[CHANCE_PARAM].getValue();
		if (!played) return;
		eng.start(g, source(), period, depth(), human(), params[SLOPPY_PARAM].getValue(),
		          params[BACK_PARAM].getValue() * period, cue(), true);
	}

	void doGrab() {
		float secs = clockKnown ? std::min(8.f * period, 8.f) : 4.f;
		auto r = eng.grab(secs);
		if (r) {
			std::atomic_store(&eng.recGrab, r);
			if (source() == vinyl::ScratchEngine::LIVE) params[SOURCE_PARAM].setValue(1.f);
			grabGlow = 1.f;
		}
	}

	void loadFile(const std::string& path) {
		std::string err;
		auto r = loadWav(path, err);
		if (!r) { wavError = err; return; }
		wavError.clear();
		wavPath = path;
		wavName = system::getStem(path);
		std::atomic_store(&eng.recWav, r);
		params[SOURCE_PARAM].setValue(2.f);
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			step = -1; earlyStarted = false; pendingStep = -1.f;
		}
		if (grabBtn.process(params[GRAB_PARAM].getValue()) || grabTrig.process(inputs[GRAB_INPUT].getVoltage(), 0.1f, 1.f))
			doGrab();

		// ---- sequencer with human timing ----
		sinceClock += dt;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sinceClock > 0.01f && sinceClock < 2.f) { period = sinceClock; clockKnown = true; }
			sinceClock = 0.f;
			if (earlyStarted) {
				earlyStarted = false;           // this step already started a little early
			}
			else {
				int k = advance();
				if (nextOffset > 0.f) { pendingStep = nextOffset; pendingStepIdx = k; }
				else playStep(k);
			}
			nextOffset = makeOffset();          // for the step after this one
		}
		if (sinceClock > 2.f) clockKnown = false;
		if (pendingStep >= 0.f) {
			pendingStep -= dt;
			if (pendingStep < 0.f) playStep(pendingStepIdx);
		}
		// land early: start the next step before its clock arrives
		if (clockKnown && !earlyStarted && nextOffset < 0.f && pendingStep < 0.f && sinceClock >= period + nextOffset) {
			int k = advance();
			playStep(k);
			earlyStarted = true;
		}

		// ---- gesture trigger (a person can only react late) ----
		if (trigTrig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) pendingTrig = human() * 0.02f * rng.uni();
		if (pendingTrig >= 0.f) {
			pendingTrig -= dt;
			if (pendingTrig < 0.f) {
				int g = clamp((int) std::round(params[GESTURE_PARAM].getValue() + inputs[GEST_INPUT].getVoltage() * 1.25f), 1, vinyl::NUM_SGESTURES - 1);
				const float mult[4] = {0.5f, 1.f, 2.f, 4.f};
				float len = period * mult[clamp((int) params[LENGTH_PARAM].getValue(), 0, 3)];
				eng.start(g, source(), len, depth(), human(), params[SLOPPY_PARAM].getValue(),
				          params[BACK_PARAM].getValue() * period, cue(), true);
			}
		}

		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr;
		eng.process(l, r, params[CURVE_PARAM].getValue(), params[MODE_PARAM].getValue() > 0.5f, params[DUCK_PARAM].getValue(),
		            hamster, ol, orr);
		if (eng.endPulse) endPulse.trigger(1e-3f);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
		outputs[HAND_OUTPUT].setVoltage(eng.handOut * 10.f);
		outputs[FADER_OUTPUT].setVoltage(eng.faderGain * 10.f);
		outputs[END_OUTPUT].setVoltage(endPulse.process(dt) ? 10.f : 0.f);
		for (int i = 0; i < 8; i++)
			lights[STEP_LIGHT + i].setBrightnessSmooth(i == step ? (played ? 1.f : 0.25f) : 0.f, dt);
		grabGlow = std::max(0.f, grabGlow - dt * 3.f);
	}

	bool hamster = false;

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "wavPath", json_string(wavPath.c_str()));
		json_object_set_new(root, "hamster", json_boolean(hamster));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "hamster"))) hamster = json_boolean_value(j);
		if ((j = json_object_get(root, "wavPath"))) {
			std::string p = json_string_value(j);
			if (!p.empty()) {
				float src = params[SOURCE_PARAM].getValue();
				loadFile(p);
				params[SOURCE_PARAM].setValue(src);
			}
		}
	}
};

// Screen: source line, the record's waveform with cue and needle, and the
// eight steps.
struct ScratchScreen : VinylScreen {
	ScratchModule* module = nullptr;
	void drawExtra(const DrawArgs& args, std::shared_ptr<window::Font> font) override {
		float w = box.size.x, h = box.size.y;
		float pad = mm2px(2.f);
		NVGcolor dim = nvgRGB(120, 112, 100), ink = accent;
		nvgFontSize(args.vg, mm2px(2.9f));

		// line 1: source
		int src = module ? module->source() : 0;
		std::string s1;
		std::shared_ptr<vinyl::Record> rec;
		if (module) rec = module->eng.record(src);
		if (src == 0) s1 = "LIVE  rolling 8 s";
		else if (src == 1) s1 = rec ? string::f("GRAB  %.1f s  %d hits", rec->seconds(), (int) rec->onsets.size()) : "GRAB  (press GRAB)";
		else s1 = rec ? string::f("WAV  %s  %.1f s", module->wavName.c_str(), rec->seconds())
		                : (module && !module->wavError.empty() ? "WAV  " + module->wavError : "WAV  right-click or drop a file");
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, ink);
		nvgText(args.vg, pad, mm2px(2.6f), s1.c_str(), NULL);
		if (module && module->hamster) {
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, dim);
			nvgText(args.vg, w - pad, mm2px(2.6f), "HAMSTER", NULL);
		}

		// waveform band
		float y0 = mm2px(5.0f), y1 = mm2px(12.4f), ym = (y0 + y1) / 2, hh = (y1 - y0) / 2;
		float x0 = pad, x1 = w - pad;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x0, ym - 0.25f, x1 - x0, 0.5f);
		nvgFillColor(args.vg, nvgRGB(60, 56, 50));
		nvgFill(args.vg);
		int cols = (int) (x1 - x0);
		if (module && src == 0) {
			// last 4 s of the live envelope, newest at the right
			const vinyl::Ring& env = module->eng.liveEnv;
			float sr = module->eng.sr;
			int span = (int) (4.f * sr / 32.f);
			nvgBeginPath(args.vg);
			for (int c = 0; c < cols; c++) {
				int d = (int) ((1.f - c / (float) cols) * span);
				float v = clamp(env.at(std::min(d, env.n - 1)) * 0.45f, 0.f, 1.f);
				nvgRect(args.vg, x0 + c, ym - v * hh, 1.f, std::max(v * 2.f * hh, 0.5f));
			}
			nvgFillColor(args.vg, nvgRGBA(230, 200, 79, 140));
			nvgFill(args.vg);
			// where BACK grabs from, and the needle
			float back = module->params[ScratchModule::BACK_PARAM].getValue() * module->period;
			float bx = x1 - (x1 - x0) * clamp(back / 4.f, 0.f, 1.f);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, bx - 0.5f, y0, 1.f, y1 - y0);
			nvgFillColor(args.vg, dim);
			nvgFill(args.vg);
			if (module->eng.active) {
				float ago = (float) (((double) module->eng.written - module->eng.needleIdx) / sr);
				float nx = x1 - (x1 - x0) * clamp(ago / 4.f, 0.f, 1.f);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, nx - 1.f, y0 - 1.f, 2.f, y1 - y0 + 2.f);
				nvgFillColor(args.vg, nvgRGB(255, 245, 220));
				nvgFill(args.vg);
			}
		}
		else if (rec && !rec->peaks.empty()) {
			int nb = (int) rec->peaks.size();
			nvgBeginPath(args.vg);
			for (int c = 0; c < cols; c++) {
				int b0 = c * nb / cols, b1 = std::max(b0 + 1, (c + 1) * nb / cols);
				float m = 0.f;
				for (int b = b0; b < b1 && b < nb; b++) m = std::max(m, rec->peaks[b]);
				float v = clamp(m / 5.f, 0.f, 1.f);
				nvgRect(args.vg, x0 + c, ym - v * hh, 1.f, std::max(v * 2.f * hh, 0.5f));
			}
			nvgFillColor(args.vg, nvgRGBA(230, 200, 79, 140));
			nvgFill(args.vg);
			// hits
			nvgBeginPath(args.vg);
			for (int o : rec->onsets) {
				float ox = x0 + (x1 - x0) * o / (float) rec->size();
				nvgRect(args.vg, ox, y1 + 0.5f, 0.8f, mm2px(0.6f));
			}
			nvgFillColor(args.vg, dim);
			nvgFill(args.vg);
			// cue
			int ci = rec->snap((int) (module->cue() * (rec->size() - 1)), (int) (0.25f * rec->sr));
			float cx = x0 + (x1 - x0) * ci / (float) rec->size();
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx - mm2px(0.8f), y0 - mm2px(0.9f));
			nvgLineTo(args.vg, cx + mm2px(0.8f), y0 - mm2px(0.9f));
			nvgLineTo(args.vg, cx, y0);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, ink);
			nvgFill(args.vg);
			if (module->eng.active && module->eng.source == src) {
				float nx = x0 + (x1 - x0) * clamp((float) (module->eng.needleIdx / rec->size()), 0.f, 1.f);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, nx - 1.f, y0 - 1.f, 2.f, y1 - y0 + 2.f);
				nvgFillColor(args.vg, nvgRGB(255, 245, 220));
				nvgFill(args.vg);
			}
		}

		// steps
		static const int DEF[8] = {0, 0, 1, 0, 0, 2, 0, 7};
		int steps = module ? (int) module->params[ScratchModule::STEPS_PARAM].getValue() : 8;
		nvgFontSize(args.vg, mm2px(2.6f));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < 8; i++) {
			int g = module ? (int) std::round(module->params[ScratchModule::STEP_PARAM + i].getValue()) : DEF[i];
			g = clamp(g, 0, vinyl::NUM_SGESTURES - 1);
			float cx = x0 + (x1 - x0) * (i + 0.5f) / 8.f, cy = h - mm2px(2.4f);
			if (module && module->step == i) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - (x1 - x0) / 16.f + 1.f, cy - mm2px(1.7f), (x1 - x0) / 8.f - 2.f, mm2px(3.4f), 2.f);
				nvgFillColor(args.vg, nvgRGBA(230, 200, 79, module->played ? 70 : 25));
				nvgFill(args.vg);
			}
			nvgFillColor(args.vg, i >= steps ? nvgRGB(70, 66, 60) : (g == 0 ? dim : ink));
			std::string t = string::f("%d %s", i + 1, vinyl::SGESTURE_SHORT[g]);
			nvgText(args.vg, cx, cy, t.c_str(), NULL);
		}
	}
};

struct ScratchWidget : ModuleWidget {
	ScratchWidget(ScratchModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Scratch.svg")));
		addScrews(this);
		typedef ScratchModule M;
		ScratchScreen* s = createWidget<ScratchScreen>(P(LSc::DISP_X, LSc::DISP_Y));
		s->box.size = mm2px(Vec(LSc::DISP_W, LSc::DISP_H));
		s->module = module;
		s->accent = nvgRGB(230, 200, 79);
		addChild(s);
		const float sx[8] = {LSc::STEP1_X, LSc::STEP2_X, LSc::STEP3_X, LSc::STEP4_X, LSc::STEP5_X, LSc::STEP6_X, LSc::STEP7_X, LSc::STEP8_X};
		for (int i = 0; i < 8; i++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(P(sx[i], LSc::STEP1_Y), module, M::STEP_PARAM + i));
			addChild(createLightCentered<SmallLight<YellowLight>>(P(sx[i], LSc::STEP1_LIGHT_Y), module, M::STEP_LIGHT + i));
		}
		addParam(createParamCentered<CKSSThree>(P(LSc::SOURCE_X, LSc::SOURCE_Y), module, M::SOURCE_PARAM));
		addParam(createParamCentered<VCVButton>(P(LSc::GRAB_X, LSc::GRAB_Y), module, M::GRAB_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::CUE_X, LSc::CUE_Y), module, M::CUE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::BACK_X, LSc::BACK_Y), module, M::BACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::GESTURE_X, LSc::GESTURE_Y), module, M::GESTURE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::LENGTH_X, LSc::LENGTH_Y), module, M::LENGTH_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LSc::DEPTH_X, LSc::DEPTH_Y), module, M::DEPTH_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LSc::HUMAN_X, LSc::HUMAN_Y), module, M::HUMAN_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LSc::SLOPPY_X, LSc::SLOPPY_Y), module, M::SLOPPY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::CHANCE_X, LSc::CHANCE_Y), module, M::CHANCE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::STEPS_X, LSc::STEPS_Y), module, M::STEPS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::CURVE_X, LSc::CURVE_Y), module, M::CURVE_PARAM));
		addParam(createParamCentered<CKSS>(P(LSc::MODE_X, LSc::MODE_Y), module, M::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::DUCK_X, LSc::DUCK_Y), module, M::DUCK_PARAM));

		struct J { float x, y; int id; bool out; };
		const J jacks[] = {
			{LSc::CLOCK_IN_X, LSc::CLOCK_IN_Y, M::CLOCK_INPUT, false}, {LSc::RESET_IN_X, LSc::RESET_IN_Y, M::RESET_INPUT, false},
			{LSc::TRIG_IN_X, LSc::TRIG_IN_Y, M::TRIG_INPUT, false}, {LSc::GEST_IN_X, LSc::GEST_IN_Y, M::GEST_INPUT, false},
			{LSc::GRAB_IN_X, LSc::GRAB_IN_Y, M::GRAB_INPUT, false}, {LSc::CUE_IN_X, LSc::CUE_IN_Y, M::CUE_INPUT, false},
			{LSc::DEPTH_IN_X, LSc::DEPTH_IN_Y, M::DEPTH_INPUT, false}, {LSc::HUMAN_IN_X, LSc::HUMAN_IN_Y, M::HUMAN_INPUT, false},
			{LSc::IN_L_X, LSc::IN_L_Y, M::IN_L_INPUT, false}, {LSc::IN_R_X, LSc::IN_R_Y, M::IN_R_INPUT, false},
			{LSc::END_OUT_X, LSc::END_OUT_Y, M::END_OUTPUT, true}, {LSc::HAND_OUT_X, LSc::HAND_OUT_Y, M::HAND_OUTPUT, true},
			{LSc::FADER_OUT_X, LSc::FADER_OUT_Y, M::FADER_OUTPUT, true},
			{LSc::OUT_L_X, LSc::OUT_L_Y, M::OUT_L_OUTPUT, true}, {LSc::OUT_R_X, LSc::OUT_R_Y, M::OUT_R_OUTPUT, true},
		};
		for (const J& j : jacks) {
			if (j.out) addOutput(createOutputCentered<PJ301MPort>(P(j.x, j.y), module, j.id));
			else addInput(createInputCentered<PJ301MPort>(P(j.x, j.y), module, j.id));
		}
	}

	void onPathDrop(const PathDropEvent& e) override {
		ScratchModule* m = dynamic_cast<ScratchModule*>(this->module);
		if (m && !e.paths.empty()) { m->loadFile(e.paths[0]); e.consume(this); return; }
		ModuleWidget::onPathDrop(e);
	}

	void appendContextMenu(Menu* menu) override {
		ScratchModule* m = dynamic_cast<ScratchModule*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load WAV...", m->wavName, [=]() {
			osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
			std::string dir = m->wavPath.empty() ? "" : system::getDirectory(m->wavPath);
			char* path = osdialog_file(OSDIALOG_OPEN, dir.empty() ? NULL : dir.c_str(), NULL, filters);
			osdialog_filters_free(filters);
			if (path) { m->loadFile(path); std::free(path); }
		}));
		menu->addChild(createMenuItem("Grab now", "", [=]() { m->doGrab(); }));
		menu->addChild(createBoolPtrMenuItem("Hamster (reverse the crossfader)", "", &m->hamster));
	}
};

Model* modelScratch = createModel<ScratchModule, ScratchWidget>("Scratch");
