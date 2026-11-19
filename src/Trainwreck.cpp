// Trainwreck: two live decks that never quite stay beatmatched. Deck B
// drifts, a DJ nudges it back. Everything stays inside a 100 ms window.
#include "plugin.hpp"
namespace LT = layout::Trainwreck;

struct TrainwreckModule : Module {
	enum ParamIds { DRIFT_PARAM, SLOP_PARAM, OFFSET_PARAM, XFADE_PARAM, SYNC_PARAM, RANGE_PARAM, NUM_PARAMS };
	enum InputIds { DRIFT_INPUT, XFADE_INPUT, SYNC_INPUT, A_L_INPUT, A_R_INPUT, B_L_INPUT, B_R_INPUT, NUM_INPUTS };
	enum OutputIds { NUDGE_OUTPUT, OFFSET_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, BOUT_L_OUTPUT, BOUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { NUDGE_LIGHT, NUM_LIGHTS };

	vinyl::Trainwreck eng;
	dsp::SchmittTrigger syncTrig, syncBtn;
	dsp::PulseGenerator nudgePulse;
	float nudgeGlow = 0.f;
	float dispOff = 0.f;
	bool dispBoth = false;

	TrainwreckModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.4f, "Drift (how wrong deck B's pitch fader is)", "%", 0.f, 100.f);
		configParam(SLOP_PARAM, 0.f, 1.f, 0.4f, "Slop (how far apart the beats get before the DJ nudges)", " ms", 0.f, 40.f, 3.f);
		configParam(OFFSET_PARAM, -50.f, 50.f, 0.f, "Offset (where the DJ thinks in sync is; + = B late)", " ms");
		configParam(XFADE_PARAM, 0.f, 1.f, 0.5f, "Crossfader (A to B)", "%", 0.f, 100.f);
		configButton(SYNC_PARAM, "Nudge B back now");
		configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "Range", {"Late: A live, B 0-100 ms behind", "Both: both decks 50 ms back, B +-50 ms"});
		configInput(DRIFT_INPUT, "Drift CV (+10%/V)");
		configInput(XFADE_INPUT, "Crossfader CV (+10%/V)");
		configInput(SYNC_INPUT, "Nudge now (trigger)");
		configInput(A_L_INPUT, "Deck A left (or mono)");
		configInput(A_R_INPUT, "Deck A right (normalled)");
		configInput(B_L_INPUT, "Deck B left (or mono)");
		configInput(B_R_INPUT, "Deck B right (normalled)");
		configOutput(NUDGE_OUTPUT, "Nudge trigger (fires when the DJ corrects)");
		configOutput(OFFSET_OUTPUT, "B offset (1 V per 10 ms, + = late)");
		configOutput(OUT_L_OUTPUT, "Mix left");
		configOutput(OUT_R_OUTPUT, "Mix right");
		configOutput(BOUT_L_OUTPUT, "Deck B only (drifted), left");
		configOutput(BOUT_R_OUTPUT, "Deck B only (drifted), right");
		configLight(NUDGE_LIGHT, "Nudge");
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); }

	void process(const ProcessArgs& args) override {
		bool sync = syncBtn.process(params[SYNC_PARAM].getValue());
		if (syncTrig.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 1.f)) sync = true;
		bool both = params[RANGE_PARAM].getValue() > 0.5f;
		float manual = params[OFFSET_PARAM].getValue();
		// in LATE range B can't run ahead, so a negative offset means 0
		if (!both) manual = std::max(manual, 0.f);
		float drift = clamp(params[DRIFT_PARAM].getValue() + inputs[DRIFT_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float xf = clamp(params[XFADE_PARAM].getValue() + inputs[XFADE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float a[2], b[2], m[2], bo[2];
		a[0] = inputs[A_L_INPUT].getVoltage();
		a[1] = inputs[A_R_INPUT].isConnected() ? inputs[A_R_INPUT].getVoltage() : a[0];
		b[0] = inputs[B_L_INPUT].getVoltage();
		b[1] = inputs[B_R_INPUT].isConnected() ? inputs[B_R_INPUT].getVoltage() : b[0];
		eng.process(a, b, drift, params[SLOP_PARAM].getValue(), manual, both, xf, sync, m, bo);
		if (eng.nudgeFired) { nudgePulse.trigger(1e-3f); nudgeGlow = 1.f; }
		nudgeGlow = std::max(0.f, nudgeGlow - args.sampleTime * 4.f);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(m[0]));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(m[1]));
		outputs[BOUT_L_OUTPUT].setVoltage(bo[0]);
		outputs[BOUT_R_OUTPUT].setVoltage(bo[1]);
		outputs[NUDGE_OUTPUT].setVoltage(nudgePulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[OFFSET_OUTPUT].setVoltage(eng.offMs * 0.1f);
		lights[NUDGE_LIGHT].setBrightnessSmooth(std::max(nudgeGlow, eng.nudgeLeft > 0.f ? 1.f : 0.f), args.sampleTime);
		dispOff = eng.offMs;
		dispBoth = both;
	}
};

struct TrainwreckScreen : VinylScreen {
	TrainwreckModule* module = nullptr;
	void lines(std::vector<std::string>& out) override {
		float o = module ? module->dispOff : 12.f;
		out.push_back(string::f("%+.0f ms", o));
		out.push_back("");
	}
	void drawExtra(const DrawArgs& args, std::shared_ptr<window::Font> font) override {
		// bar: centre = in sync, left = B early, right = B late
		float o = module ? module->dispOff : 12.f;
		bool both = module ? module->dispBoth : true;
		float w = box.size.x, h = box.size.y;
		float x0 = mm2px(1.5f), x1 = w - mm2px(1.5f), y = h * 0.72f;
		float zero = both ? (x0 + x1) / 2 : x0;
		float span = both ? (x1 - x0) / 2 / 50.f : (x1 - x0) / 100.f;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x0, y - 1.f, x1 - x0, 2.f);
		nvgFillColor(args.vg, nvgRGBA(224, 96, 90, 60));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, zero - 0.5f, y - mm2px(1.2f), 1.f, mm2px(2.4f));
		nvgFillColor(args.vg, nvgRGB(160, 150, 140));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, clamp(zero + o * span, x0, x1), y, mm2px(0.9f));
		nvgFillColor(args.vg, accent);
		nvgFill(args.vg);
	}
};

struct TrainwreckWidget : ModuleWidget {
	TrainwreckWidget(TrainwreckModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Trainwreck.svg")));
		addScrews(this);
		TrainwreckScreen* s = createWidget<TrainwreckScreen>(P(LT::DISP_X, LT::DISP_Y));
		s->box.size = mm2px(Vec(LT::DISP_W, LT::DISP_H));
		s->module = module;
		s->accent = nvgRGB(224, 96, 90);
		addChild(s);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LT::DRIFT_X, LT::DRIFT_Y), module, TrainwreckModule::DRIFT_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LT::SLOP_X, LT::SLOP_Y), module, TrainwreckModule::SLOP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LT::OFFSET_X, LT::OFFSET_Y), module, TrainwreckModule::OFFSET_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LT::XFADE_X, LT::XFADE_Y), module, TrainwreckModule::XFADE_PARAM));
		addParam(createParamCentered<VCVButton>(P(LT::SYNC_X, LT::SYNC_Y), module, TrainwreckModule::SYNC_PARAM));
		addParam(createParamCentered<CKSS>(P(LT::RANGE_X, LT::RANGE_Y), module, TrainwreckModule::RANGE_PARAM));
		addChild(createLightCentered<MediumLight<RedLight>>(P(LT::NUDGE_LIGHT_X, LT::NUDGE_LIGHT_Y), module, TrainwreckModule::NUDGE_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(P(LT::NUDGE_OUT_X, LT::NUDGE_OUT_Y), module, TrainwreckModule::NUDGE_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::DRIFT_IN_X, LT::DRIFT_IN_Y), module, TrainwreckModule::DRIFT_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::XFADE_IN_X, LT::XFADE_IN_Y), module, TrainwreckModule::XFADE_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::SYNC_IN_X, LT::SYNC_IN_Y), module, TrainwreckModule::SYNC_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LT::OFFSET_OUT_X, LT::OFFSET_OUT_Y), module, TrainwreckModule::OFFSET_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::A_L_X, LT::A_L_Y), module, TrainwreckModule::A_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::A_R_X, LT::A_R_Y), module, TrainwreckModule::A_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::B_L_X, LT::B_L_Y), module, TrainwreckModule::B_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LT::B_R_X, LT::B_R_Y), module, TrainwreckModule::B_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LT::OUT_L_X, LT::OUT_L_Y), module, TrainwreckModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LT::OUT_R_X, LT::OUT_R_Y), module, TrainwreckModule::OUT_R_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LT::BOUT_L_X, LT::BOUT_L_Y), module, TrainwreckModule::BOUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LT::BOUT_R_X, LT::BOUT_R_Y), module, TrainwreckModule::BOUT_R_OUTPUT));
	}
};

Model* modelTrainwreck = createModel<TrainwreckModule, TrainwreckWidget>("Trainwreck");
