// RIAA Sandwich: the record EQ curves wrapped around a send/return (or the
// built-in drive). No latency apart from Rack's one-sample cable delay on
// the return.
#include "plugin.hpp"
namespace LR = layout::Riaa;

struct RiaaModule : Module {
	enum ParamIds { CUT_PARAM, PLAY_PARAM, DRIVE_PARAM, MIX_PARAM, NUM_PARAMS };
	enum InputIds { RTN_L_INPUT, RTN_R_INPUT, DRIVE_INPUT, MIX_INPUT, CUT_INPUT, PLAY_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { SEND_L_OUTPUT, SEND_R_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };

	vinyl::Riaa eng;
	int dispCut = 0, dispPlay = 0;
	bool dispReturn = false;

	RiaaModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
		std::vector<std::string> names(vinyl::CURVE_NAMES, vinyl::CURVE_NAMES + vinyl::NUM_CURVES);
		configSwitch(CUT_PARAM, 0.f, vinyl::NUM_CURVES - 1, 0.f, "Cut curve (EQ on the way into the groove)", names);
		configSwitch(PLAY_PARAM, 0.f, vinyl::NUM_CURVES - 1, 0.f, "Play curve (EQ on the way out; different from CUT = tone tilt)", names);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.5f, "Built-in drive (used when RETURN is not patched)", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix (dry to sandwich)", "%", 0.f, 100.f);
		configInput(RTN_L_INPUT, "Return left (from your effect)");
		configInput(RTN_R_INPUT, "Return right (normalled to return left)");
		configInput(DRIVE_INPUT, "Drive CV (+10%/V)");
		configInput(MIX_INPUT, "Mix CV (+10%/V)");
		configInput(CUT_INPUT, "Cut curve CV (+1 curve per 2 V)");
		configInput(PLAY_INPUT, "Play curve CV (+1 curve per 2 V)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(SEND_L_OUTPUT, "Send left (treble lifted, bass cut, -6 dB)");
		configOutput(SEND_R_OUTPUT, "Send right");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); }

	void process(const ProcessArgs& args) override {
		int cut = clamp((int) std::round(params[CUT_PARAM].getValue() + inputs[CUT_INPUT].getVoltage() * 0.5f), 0, vinyl::NUM_CURVES - 1);
		int play = clamp((int) std::round(params[PLAY_PARAM].getValue() + inputs[PLAY_INPUT].getVoltage() * 0.5f), 0, vinyl::NUM_CURVES - 1);
		float drive = clamp(params[DRIVE_PARAM].getValue() + inputs[DRIVE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float sl, sr;
		eng.toSend(l, r, cut, play, sl, sr);
		outputs[SEND_L_OUTPUT].setVoltage(sl);
		outputs[SEND_R_OUTPUT].setVoltage(sr);
		float rl, rr;
		bool ret = inputs[RTN_L_INPUT].isConnected();
		if (ret) {
			rl = inputs[RTN_L_INPUT].getVoltage();
			rr = inputs[RTN_R_INPUT].isConnected() ? inputs[RTN_R_INPUT].getVoltage() : rl;
		}
		else {
			rl = vinyl::Riaa::drive(sl, drive);
			rr = vinyl::Riaa::drive(sr, drive);
		}
		float wl, wr;
		eng.fromReturn(rl, rr, wl, wr);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(l + (wl - l) * mix));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(r + (wr - r) * mix));
		dispCut = cut; dispPlay = play; dispReturn = ret;
	}
};

struct RiaaScreen : VinylScreen {
	RiaaModule* module = nullptr;
	void lines(std::vector<std::string>& out) override {
		int c = module ? module->dispCut : 0, p = module ? module->dispPlay : 1;
		out.push_back(string::f("CUT  %s", vinyl::CURVE_NAMES[c]));
		out.push_back(string::f("PLAY %s", vinyl::CURVE_NAMES[p]));
	}
};

struct RiaaWidget : ModuleWidget {
	RiaaWidget(RiaaModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Riaa.svg")));
		addScrews(this);
		RiaaScreen* s = createWidget<RiaaScreen>(P(LR::DISP_X, LR::DISP_Y));
		s->box.size = mm2px(Vec(LR::DISP_W, LR::DISP_H));
		s->module = module;
		s->accent = nvgRGB(127, 184, 164);
		s->fontMm = 3.2f;
		addChild(s);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LR::CUT_X, LR::CUT_Y), module, RiaaModule::CUT_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LR::PLAY_X, LR::PLAY_Y), module, RiaaModule::PLAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LR::DRIVE_X, LR::DRIVE_Y), module, RiaaModule::DRIVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LR::MIX_X, LR::MIX_Y), module, RiaaModule::MIX_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(P(LR::SEND_L_X, LR::SEND_L_Y), module, RiaaModule::SEND_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LR::SEND_R_X, LR::SEND_R_Y), module, RiaaModule::SEND_R_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::RTN_L_X, LR::RTN_L_Y), module, RiaaModule::RTN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::RTN_R_X, LR::RTN_R_Y), module, RiaaModule::RTN_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::DRIVE_IN_X, LR::DRIVE_IN_Y), module, RiaaModule::DRIVE_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::MIX_IN_X, LR::MIX_IN_Y), module, RiaaModule::MIX_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::CUT_IN_X, LR::CUT_IN_Y), module, RiaaModule::CUT_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::PLAY_IN_X, LR::PLAY_IN_Y), module, RiaaModule::PLAY_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::IN_L_X, LR::IN_L_Y), module, RiaaModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::IN_R_X, LR::IN_R_Y), module, RiaaModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LR::OUT_L_X, LR::OUT_L_Y), module, RiaaModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LR::OUT_R_X, LR::OUT_R_Y), module, RiaaModule::OUT_R_OUTPUT));
	}
};

Model* modelRiaa = createModel<RiaaModule, RiaaWidget>("RiaaSandwich");
