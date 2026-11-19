// Stylus: cartridge and needle physics. Under 0.2 ms latency (azimuth only).
#include "plugin.hpp"
namespace LS = layout::Stylus;

struct StylusModule : Module {
	enum ParamIds { LOAD_PARAM, AZIMUTH_PARAM, XTALK_PARAM, VERT_PARAM, TYPE_PARAM, NUM_PARAMS };
	enum InputIds { LOAD_INPUT, AZIMUTH_INPUT, VERT_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };

	vinyl::Stylus eng;

	StylusModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(LOAD_PARAM, 0.f, 1.f, 0.4f, "Load capacitance (MM: more = lower, peakier treble bump)", " pF", 0.f, 400.f, 100.f);
		configParam(AZIMUTH_PARAM, -1.f, 1.f, 0.f, "Azimuth (needle lean: one side late and duller)", " deg", 0.f, 3.f);
		configParam(XTALK_PARAM, 0.f, 1.f, 0.3f, "Crosstalk (channel leak, more in the treble)", " dB", 0.f, 33.f, -45.f);
		configParam(VERT_PARAM, 0.f, 1.f, 0.f, "Vertical mistracking (stereo width distorts first)", "%", 0.f, 100.f);
		configSwitch(TYPE_PARAM, 0.f, 1.f, 0.f, "Cartridge", {"MM (moving magnet)", "MC (moving coil)"});
		configInput(LOAD_INPUT, "Load CV (+10%/V)");
		configInput(AZIMUTH_INPUT, "Azimuth CV (+-5 V = full lean)");
		configInput(VERT_INPUT, "Vertical CV (+10%/V)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); }

	void process(const ProcessArgs& args) override {
		float load = clamp(params[LOAD_PARAM].getValue() + inputs[LOAD_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float az = clamp(params[AZIMUTH_PARAM].getValue() + inputs[AZIMUTH_INPUT].getVoltage() * 0.2f, -1.f, 1.f);
		float vert = clamp(params[VERT_PARAM].getValue() + inputs[VERT_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr;
		eng.process(l, r, params[TYPE_PARAM].getValue() > 0.5f, load, params[XTALK_PARAM].getValue(), az, vert, ol, orr);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
	}
};

struct StylusWidget : ModuleWidget {
	StylusWidget(StylusModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Stylus.svg")));
		addScrews(this);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LS::LOAD_X, LS::LOAD_Y), module, StylusModule::LOAD_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LS::AZIMUTH_X, LS::AZIMUTH_Y), module, StylusModule::AZIMUTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LS::XTALK_X, LS::XTALK_Y), module, StylusModule::XTALK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LS::VERT_X, LS::VERT_Y), module, StylusModule::VERT_PARAM));
		addParam(createParamCentered<CKSS>(P(LS::TYPE_X, LS::TYPE_Y), module, StylusModule::TYPE_PARAM));
		addInput(createInputCentered<PJ301MPort>(P(LS::LOAD_IN_X, LS::LOAD_IN_Y), module, StylusModule::LOAD_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LS::AZIMUTH_IN_X, LS::AZIMUTH_IN_Y), module, StylusModule::AZIMUTH_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LS::VERT_IN_X, LS::VERT_IN_Y), module, StylusModule::VERT_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LS::IN_L_X, LS::IN_L_Y), module, StylusModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LS::IN_R_X, LS::IN_R_Y), module, StylusModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LS::OUT_L_X, LS::OUT_L_Y), module, StylusModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LS::OUT_R_X, LS::OUT_R_Y), module, StylusModule::OUT_R_OUTPUT));
	}
};

Model* modelStylus = createModel<StylusModule, StylusWidget>("Stylus");
