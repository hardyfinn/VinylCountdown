// Inner Groove: the needle's journey from the outer edge to the label.
// RADIUS (or PLAY, which sweeps a whole side over SIDE minutes) darkens and
// distorts the sound the way the end of a side does. No latency.
#include "plugin.hpp"
namespace LI = layout::InnerGroove;

struct InnerGrooveModule : Module {
	enum ParamIds { RADIUS_PARAM, STYLUS_PARAM, TRACK_PARAM, CUT_PARAM, RPM_PARAM, SIDE_PARAM, PLAY_PARAM, NUM_PARAMS };
	enum InputIds { RADIUS_INPUT, PLAY_INPUT, RESET_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { POS_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { MIST_LIGHT, PLAY_LIGHT, NUM_LIGHTS };

	vinyl::InnerGroove eng;
	dsp::SchmittTrigger playTrig, playBtn, resetTrig;
	bool playing = false;
	float progress = 0.f;   // 0..1 across the side
	dsp::ClockDivider lightDiv;

	InnerGrooveModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(RADIUS_PARAM, 0.f, 1.f, 0.f, "Radius (0% outer edge, 100% label)", "%", 0.f, 100.f);
		configSwitch(STYLUS_PARAM, 0.f, 2.f, 0.f, "Stylus tip", {"Conical (darkest)", "Elliptical", "Shibata (finest)"});
		configParam(TRACK_PARAM, 0.f, 1.f, 0.5f, "Tracking (light arm mistracks sooner)", "%", 0.f, 100.f);
		configParam(CUT_PARAM, 0.f, 1.f, 0.35f, "Cut level (how hot the record was cut)", "%", 0.f, 100.f);
		configSwitch(RPM_PARAM, 0.f, 1.f, 0.f, "Speed (45 has more groove speed: brighter, cleaner)", {"33 rpm", "45 rpm"});
		configParam(SIDE_PARAM, 0.f, 1.f, 0.5f, "Side length for PLAY", " min", 30.f, 1.f);
		configButton(PLAY_PARAM, "Play the side (needle travels edge to label over SIDE)");
		configInput(RADIUS_INPUT, "Radius CV (+10%/V)");
		configInput(PLAY_INPUT, "Play/stop (trigger toggles)");
		configInput(RESET_INPUT, "Back to the outer edge (trigger)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(POS_OUTPUT, "Needle position (0 V edge .. 10 V label)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configLight(MIST_LIGHT, "Mistracking");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		lightDiv.setDivision(64);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); playing = false; progress = 0.f; }

	void process(const ProcessArgs& args) override {
		if (playBtn.process(params[PLAY_PARAM].getValue()) || playTrig.process(inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f)) {
			playing = !playing;
			if (playing && progress >= 1.f) progress = 0.f;
		}
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) progress = 0.f;
		if (playing) {
			float sideSec = 60.f * std::pow(30.f, params[SIDE_PARAM].getValue());
			progress += args.sampleTime / sideSec;
			if (progress >= 1.f) { progress = 1.f; playing = false; }
		}
		float r01 = clamp(params[RADIUS_PARAM].getValue() + inputs[RADIUS_INPUT].getVoltage() * 0.1f + progress, 0.f, 1.f);
		int stylus = clamp((int) std::round(params[STYLUS_PARAM].getValue()), 0, 2);
		float rpm = params[RPM_PARAM].getValue() > 0.5f ? 45.f : 100.f / 3.f;
		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr;
		eng.process(l, r, r01, rpm, stylus, params[TRACK_PARAM].getValue(), params[CUT_PARAM].getValue(), ol, orr);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
		outputs[POS_OUTPUT].setVoltage(r01 * 10.f);
		if (lightDiv.process()) {
			lights[MIST_LIGHT].setBrightness(eng.mistrack);
			lights[PLAY_LIGHT].setBrightness(playing ? 1.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "playing", json_boolean(playing));
		json_object_set_new(root, "progress", json_real(progress));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "playing"))) playing = json_boolean_value(j);
		if ((j = json_object_get(root, "progress"))) progress = json_number_value(j);
	}
};

struct InnerGrooveWidget : ModuleWidget {
	InnerGrooveWidget(InnerGrooveModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/InnerGroove.svg")));
		addScrews(this);
		addParam(createParamCentered<RoundHugeBlackKnob>(P(LI::RADIUS_X, LI::RADIUS_Y), module, InnerGrooveModule::RADIUS_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LI::STYLUS_X, LI::STYLUS_Y), module, InnerGrooveModule::STYLUS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LI::TRACK_X, LI::TRACK_Y), module, InnerGrooveModule::TRACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LI::CUT_X, LI::CUT_Y), module, InnerGrooveModule::CUT_PARAM));
		addParam(createParamCentered<CKSS>(P(LI::RPM_X, LI::RPM_Y), module, InnerGrooveModule::RPM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LI::SIDE_X, LI::SIDE_Y), module, InnerGrooveModule::SIDE_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(P(LI::PLAY_X, LI::PLAY_Y), module, InnerGrooveModule::PLAY_PARAM, InnerGrooveModule::PLAY_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(P(LI::MIST_X, LI::MIST_Y), module, InnerGrooveModule::MIST_LIGHT));
		addInput(createInputCentered<PJ301MPort>(P(LI::RADIUS_IN_X, LI::RADIUS_IN_Y), module, InnerGrooveModule::RADIUS_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LI::PLAY_IN_X, LI::PLAY_IN_Y), module, InnerGrooveModule::PLAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LI::RESET_IN_X, LI::RESET_IN_Y), module, InnerGrooveModule::RESET_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LI::POS_OUT_X, LI::POS_OUT_Y), module, InnerGrooveModule::POS_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LI::IN_L_X, LI::IN_L_Y), module, InnerGrooveModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LI::IN_R_X, LI::IN_R_Y), module, InnerGrooveModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LI::OUT_L_X, LI::OUT_L_Y), module, InnerGrooveModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LI::OUT_R_X, LI::OUT_R_Y), module, InnerGrooveModule::OUT_R_OUTPUT));
	}
};

Model* modelInnerGroove = createModel<InnerGrooveModule, InnerGrooveWidget>("InnerGroove");
