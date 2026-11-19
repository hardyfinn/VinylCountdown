// Platter: off-centre pressing wow on live audio (fixed 5 ms latency).
#include "plugin.hpp"
namespace LP = layout::Platter;

struct PlatterModule : Module {
	enum ParamIds { OFFSET_PARAM, RADIUS_PARAM, RPM_PARAM, NUM_PARAMS };
	enum InputIds { OFFSET_INPUT, RADIUS_INPUT, RESET_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { WOW_OUTPUT, REV_OUTPUT, PHASE_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };

	vinyl::Platter eng;
	dsp::SchmittTrigger resetTrig;
	dsp::PulseGenerator revPulse;

	PlatterModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(OFFSET_PARAM, 0.f, 1.f, 0.3f, "Off-centre offset", " mm");
		configParam(RADIUS_PARAM, 0.f, 1.f, 0.5f, "Needle radius (edge to label; the wow grows toward the label)", " mm", 0.f, -(vinyl::R_OUTER - vinyl::R_INNER), vinyl::R_OUTER);
		configSwitch(RPM_PARAM, 0.f, 2.f, 0.f, "Speed", {"33 rpm", "45 rpm", "78 rpm"});
		configInput(OFFSET_INPUT, "Offset CV (+0.1 mm/V)");
		configInput(RADIUS_INPUT, "Radius CV (+10%/V toward the label)");
		configInput(RESET_INPUT, "Reset rotation (trigger)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(WOW_OUTPUT, "Wow as pitch CV (1 V/oct, add to an oscillator to wobble in sync)");
		configOutput(REV_OUTPUT, "Revolution trigger");
		configOutput(PHASE_OUTPUT, "Rotation phase (0-10 V saw)");
		configOutput(OUT_L_OUTPUT, "Left (5 ms latency)");
		configOutput(OUT_R_OUTPUT, "Right (5 ms latency)");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); }

	void process(const ProcessArgs& args) override {
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) eng.reset();
		float off = clamp(params[OFFSET_PARAM].getValue() + inputs[OFFSET_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float r01 = clamp(params[RADIUS_PARAM].getValue() + inputs[RADIUS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float radius = vinyl::R_OUTER - r01 * (vinyl::R_OUTER - vinyl::R_INNER);
		int rpm = clamp((int) std::round(params[RPM_PARAM].getValue()), 0, 2);
		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr;
		eng.process(l, r, off, radius, vinyl::RPM_VALUES[rpm], ol, orr);
		if (eng.revTick) revPulse.trigger(1e-3f);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
		outputs[WOW_OUTPUT].setVoltage(eng.wowVolts());
		outputs[REV_OUTPUT].setVoltage(revPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[PHASE_OUTPUT].setVoltage((float) eng.phase * 10.f);
	}
};

struct PlatterWidget : ModuleWidget {
	PlatterWidget(PlatterModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Platter.svg")));
		addScrews(this);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LP::OFFSET_X, LP::OFFSET_Y), module, PlatterModule::OFFSET_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LP::RADIUS_X, LP::RADIUS_Y), module, PlatterModule::RADIUS_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LP::RPM_X, LP::RPM_Y), module, PlatterModule::RPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(P(LP::OFFSET_IN_X, LP::OFFSET_IN_Y), module, PlatterModule::OFFSET_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LP::RADIUS_IN_X, LP::RADIUS_IN_Y), module, PlatterModule::RADIUS_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LP::RESET_IN_X, LP::RESET_IN_Y), module, PlatterModule::RESET_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LP::WOW_OUT_X, LP::WOW_OUT_Y), module, PlatterModule::WOW_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LP::REV_OUT_X, LP::REV_OUT_Y), module, PlatterModule::REV_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LP::PHASE_OUT_X, LP::PHASE_OUT_Y), module, PlatterModule::PHASE_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LP::IN_L_X, LP::IN_L_Y), module, PlatterModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LP::IN_R_X, LP::IN_R_Y), module, PlatterModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LP::OUT_L_X, LP::OUT_L_Y), module, PlatterModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LP::OUT_R_X, LP::OUT_R_Y), module, PlatterModule::OUT_R_OUTPUT));
	}
};

Model* modelPlatter = createModel<PlatterModule, PlatterWidget>("Platter");
