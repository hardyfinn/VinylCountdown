// Pressing: GENERATION (lacquer .. late pressing) and WEAR (a dubplate
// played to death). No memory, no latency.
#include "plugin.hpp"
namespace LPr = layout::Pressing;

struct PressingModule : Module {
	enum ParamIds { GEN_PARAM, WEAR_PARAM, NUM_PARAMS };
	enum InputIds { GEN_INPUT, WEAR_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };

	vinyl::Pressing eng;

	PressingModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		configParam(GEN_PARAM, 0.f, 1.f, 0.3f, "Generation (lacquer .. late pressing from a tired stamper)", "%", 0.f, 100.f);
		configParam(WEAR_PARAM, 0.f, 1.f, 0.f, "Wear (fresh dubplate .. played to death)", "%", 0.f, 100.f);
		configInput(GEN_INPUT, "Generation CV (+10%/V)");
		configInput(WEAR_INPUT, "Wear CV (+10%/V)");
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
		float g = clamp(params[GEN_PARAM].getValue() + inputs[GEN_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float w = clamp(params[WEAR_PARAM].getValue() + inputs[WEAR_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr;
		eng.process(l, r, g, w, ol, orr);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
	}
};

struct PressingWidget : ModuleWidget {
	PressingWidget(PressingModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Pressing.svg")));
		addScrews(this);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LPr::GEN_X, LPr::GEN_Y), module, PressingModule::GEN_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LPr::WEAR_X, LPr::WEAR_Y), module, PressingModule::WEAR_PARAM));
		addInput(createInputCentered<PJ301MPort>(P(LPr::GEN_IN_X, LPr::GEN_IN_Y), module, PressingModule::GEN_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LPr::WEAR_IN_X, LPr::WEAR_IN_Y), module, PressingModule::WEAR_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LPr::IN_L_X, LPr::IN_L_Y), module, PressingModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LPr::IN_R_X, LPr::IN_R_Y), module, PressingModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LPr::OUT_L_X, LPr::OUT_L_Y), module, PressingModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LPr::OUT_R_X, LPr::OUT_R_Y), module, PressingModule::OUT_R_OUTPUT));
	}
};

Model* modelPressing = createModel<PressingModule, PressingWidget>("Pressing");
