// Scratch: an 8-step scratch-gesture sequencer. Live audio passes straight
// through with no latency; a step that holds a gesture scratches the audio
// from BACK steps ago for the length of that step.
#include "plugin.hpp"
namespace LSc = layout::Scratch;

struct ScratchModule : Module {
	enum ParamIds { ENUMS(STEP_PARAM, 8), DEPTH_PARAM, CHANCE_PARAM, BACK_PARAM, STEPS_PARAM, NUM_PARAMS };
	enum InputIds { CLOCK_INPUT, RESET_INPUT, DEPTH_INPUT, CHANCE_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { HAND_OUTPUT, FADER_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { ENUMS(STEP_LIGHT, 8), NUM_LIGHTS };

	vinyl::Scratch eng;
	dsp::SchmittTrigger clockTrig, resetTrig;
	float sinceClock = 0.f, period = 0.25f;
	int step = -1;
	bool played = false;   // did this step's gesture actually play (CHANCE)
	random::Xoroshiro128Plus rng;

	ScratchModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::vector<std::string> names(vinyl::GESTURE_NAMES, vinyl::GESTURE_NAMES + vinyl::NUM_GESTURES);
		const int def[8] = {0, 0, 0, 1, 0, 0, 2, 3};
		for (int i = 0; i < 8; i++)
			configSwitch(STEP_PARAM + i, 0.f, vinyl::NUM_GESTURES - 1, def[i], string::f("Step %d gesture", i + 1), names);
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth (how far the hand moves the record)", "%", 0.f, 100.f);
		configParam(CHANCE_PARAM, 0.f, 1.f, 1.f, "Chance (each gesture plays or stays live)", "%", 0.f, 100.f);
		configParam(BACK_PARAM, 1.f, 8.f, 2.f, "Back (scratch the audio from this many steps ago)", " steps");
		paramQuantities[BACK_PARAM]->snapEnabled = true;
		configParam(STEPS_PARAM, 1.f, 8.f, 8.f, "Steps (pattern length)");
		paramQuantities[STEPS_PARAM]->snapEnabled = true;
		configInput(CLOCK_INPUT, "Clock (one step per pulse; step length follows the clock)");
		configInput(RESET_INPUT, "Reset to step 1");
		configInput(DEPTH_INPUT, "Depth CV (+10%/V)");
		configInput(CHANCE_INPUT, "Chance CV (+10%/V)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(HAND_OUTPUT, "Hand position (0-10 V; scratch something else with it)");
		configOutput(FADER_OUTPUT, "Crossfader gate (10 V = open)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		rng.seed(random::u64(), random::u64());
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); step = -1; }

	float uni() { return (rng() >> 40) * (1.f / 16777216.f); }

	void process(const ProcessArgs& args) override {
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) { step = -1; eng.reset(); }
		sinceClock += args.sampleTime;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sinceClock > 0.01f && sinceClock < 2.f) period = sinceClock;
			sinceClock = 0.f;
			int steps = clamp((int) params[STEPS_PARAM].getValue(), 1, 8);
			step = (step + 1) % steps;
			int g = clamp((int) std::round(params[STEP_PARAM + step].getValue()), 0, vinyl::NUM_GESTURES - 1);
			float chance = clamp(params[CHANCE_PARAM].getValue() + inputs[CHANCE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
			played = g != vinyl::G_OFF && uni() < chance;
			float depth = clamp(params[DEPTH_PARAM].getValue() + inputs[DEPTH_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
			eng.startStep(played ? g : vinyl::G_OFF, period, params[BACK_PARAM].getValue() * period, depth);
		}
		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr;
		eng.process(l, r, ol, orr);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
		outputs[HAND_OUTPUT].setVoltage(eng.handOut * 10.f);
		outputs[FADER_OUTPUT].setVoltage(eng.faderOut * 10.f);
		for (int i = 0; i < 8; i++)
			lights[STEP_LIGHT + i].setBrightnessSmooth(i == step ? (played ? 1.f : 0.25f) : 0.f, args.sampleTime);
	}
};

// 8 cells: the gesture on each step, the current one highlighted
struct ScratchScreen : VinylScreen {
	ScratchModule* module = nullptr;
	void drawExtra(const DrawArgs& args, std::shared_ptr<window::Font> font) override {
		float w = box.size.x, h = box.size.y;
		nvgFontSize(args.vg, mm2px(2.9f));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		int steps = module ? (int) module->params[ScratchModule::STEPS_PARAM].getValue() : 8;
		for (int i = 0; i < 8; i++) {
			static const int DEF[8] = {0, 0, 0, 1, 0, 0, 2, 3};
			int g = module ? (int) std::round(module->params[ScratchModule::STEP_PARAM + i].getValue()) : DEF[i];
			g = clamp(g, 0, vinyl::NUM_GESTURES - 1);
			float cx = w * ((i % 4) + 0.5f) / 4.f, cy = h * ((i / 4) + 0.5f) / 2.f;
			bool cur = module && module->step == i;
			if (cur) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, cx - w / 8.f + 1.f, cy - h / 4.f + 1.f, w / 4.f - 2.f, h / 2.f - 2.f, 2.f);
				nvgFillColor(args.vg, nvgRGBA(230, 200, 79, module->played ? 70 : 25));
				nvgFill(args.vg);
			}
			NVGcolor c = accent;
			if (i >= steps) c = nvgRGB(70, 66, 60);
			else if (g == vinyl::G_OFF) c = nvgRGB(130, 122, 110);
			nvgFillColor(args.vg, c);
			std::string s = string::f("%d %s", i + 1, vinyl::GESTURE_SHORT[g]);
			nvgText(args.vg, cx, cy, s.c_str(), NULL);
		}
	}
};

struct ScratchWidget : ModuleWidget {
	ScratchWidget(ScratchModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Scratch.svg")));
		addScrews(this);
		ScratchScreen* s = createWidget<ScratchScreen>(P(LSc::DISP_X, LSc::DISP_Y));
		s->box.size = mm2px(Vec(LSc::DISP_W, LSc::DISP_H));
		s->module = module;
		s->accent = nvgRGB(230, 200, 79);
		addChild(s);
		const float sx[8] = {LSc::STEP1_X, LSc::STEP2_X, LSc::STEP3_X, LSc::STEP4_X, LSc::STEP5_X, LSc::STEP6_X, LSc::STEP7_X, LSc::STEP8_X};
		for (int i = 0; i < 8; i++) {
			addParam(createParamCentered<RoundSmallBlackKnob>(P(sx[i], LSc::STEP1_Y), module, ScratchModule::STEP_PARAM + i));
			addChild(createLightCentered<SmallLight<YellowLight>>(P(sx[i], LSc::STEP1_LIGHT_Y), module, ScratchModule::STEP_LIGHT + i));
		}
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LSc::DEPTH_X, LSc::DEPTH_Y), module, ScratchModule::DEPTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::CHANCE_X, LSc::CHANCE_Y), module, ScratchModule::CHANCE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::BACK_X, LSc::BACK_Y), module, ScratchModule::BACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LSc::STEPS_X, LSc::STEPS_Y), module, ScratchModule::STEPS_PARAM));
		addInput(createInputCentered<PJ301MPort>(P(LSc::CLOCK_IN_X, LSc::CLOCK_IN_Y), module, ScratchModule::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LSc::RESET_IN_X, LSc::RESET_IN_Y), module, ScratchModule::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LSc::DEPTH_IN_X, LSc::DEPTH_IN_Y), module, ScratchModule::DEPTH_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LSc::CHANCE_IN_X, LSc::CHANCE_IN_Y), module, ScratchModule::CHANCE_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LSc::HAND_OUT_X, LSc::HAND_OUT_Y), module, ScratchModule::HAND_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LSc::FADER_OUT_X, LSc::FADER_OUT_Y), module, ScratchModule::FADER_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LSc::IN_L_X, LSc::IN_L_Y), module, ScratchModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LSc::IN_R_X, LSc::IN_R_Y), module, ScratchModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LSc::OUT_L_X, LSc::OUT_L_Y), module, ScratchModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LSc::OUT_R_X, LSc::OUT_R_Y), module, ScratchModule::OUT_R_OUTPUT));
	}
};

Model* modelScratch = createModel<ScratchModule, ScratchWidget>("Scratch");
