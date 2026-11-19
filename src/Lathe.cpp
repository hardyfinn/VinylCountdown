// Lathe: the cutting room. Hot cut, elliptic (mono bass) filter, HF limiter
// and groove echo from neighbouring grooves. No latency on the dry signal.
#include "plugin.hpp"
namespace LL = layout::Lathe;

struct LatheModule : Module {
	enum ParamIds { DRIVE_PARAM, ELLIPTIC_PARAM, HF_PARAM, ECHO_PARAM, REVS_PARAM, RPM_PARAM, BEATS_PARAM, NUM_PARAMS };
	enum InputIds { DRIVE_INPUT, ELLIPTIC_INPUT, HF_INPUT, ECHO_INPUT, CLOCK_INPUT, IN_L_INPUT, IN_R_INPUT, NUM_INPUTS };
	enum OutputIds { WET_L_OUTPUT, WET_R_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { HF_LIGHT, NUM_LIGHTS };

	vinyl::Lathe eng;
	dsp::SchmittTrigger clockTrig;
	float sinceClock = 0.f, clockPeriod = 0.f;
	dsp::ClockDivider lightDiv;

	LatheModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.3f, "Drive (hot cut: saturates the treble first)", "%", 0.f, 100.f);
		configParam(ELLIPTIC_PARAM, 0.f, 1.f, 0.f, "Elliptic filter (bass below this goes mono; 0 = off)", " Hz", 15.f, 20.f);
		configParam(HF_PARAM, 0.f, 1.f, 0.f, "HF limiter (holds down loud highs; 0 = off)", "%", 0.f, 100.f);
		configParam(ECHO_PARAM, 0.f, 1.f, 0.4f, "Groove echo (a dull ghost one revolution late)", "%", 0.f, 100.f);
		configSwitch(REVS_PARAM, 0.f, 1.f, 0.f, "Echo from", {"1 groove away", "1 and 2 grooves away"});
		configSwitch(RPM_PARAM, 0.f, 2.f, 0.f, "Speed (sets the echo time when CLOCK is not patched)", {"33 rpm (1.8 s)", "45 rpm (1.33 s)", "78 rpm (0.77 s)"});
		configParam(BEATS_PARAM, 1.f, 8.f, 4.f, "Beats per revolution (when CLOCK is patched)");
		paramQuantities[BEATS_PARAM]->snapEnabled = true;
		configInput(DRIVE_INPUT, "Drive CV (+10%/V)");
		configInput(ELLIPTIC_INPUT, "Elliptic CV (+10%/V)");
		configInput(HF_INPUT, "HF limiter CV (+10%/V)");
		configInput(ECHO_INPUT, "Echo CV (+10%/V)");
		configInput(CLOCK_INPUT, "Clock (one pulse per beat: echo time = BEATS beats)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(WET_L_OUTPUT, "Groove echo only, left");
		configOutput(WET_R_OUTPUT, "Groove echo only, right");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configLight(HF_LIGHT, "HF limiting");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		lightDiv.setDivision(64);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { eng.setSampleRate(e.sampleRate); }
	void onAdd(const AddEvent& e) override { eng.setSampleRate(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eng.reset(); }

	void process(const ProcessArgs& args) override {
		sinceClock += args.sampleTime;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sinceClock > 0.05f && sinceClock < 4.f) clockPeriod = sinceClock;
			sinceClock = 0.f;
		}
		if (sinceClock > 4.f) clockPeriod = 0.f;
		float revSec;
		if (inputs[CLOCK_INPUT].isConnected() && clockPeriod > 0.f)
			revSec = clockPeriod * params[BEATS_PARAM].getValue();
		else
			revSec = 60.f / vinyl::RPM_VALUES[clamp((int) std::round(params[RPM_PARAM].getValue()), 0, 2)];

		float drive = clamp(params[DRIVE_PARAM].getValue() + inputs[DRIVE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float ell = clamp(params[ELLIPTIC_PARAM].getValue() + inputs[ELLIPTIC_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float ellHz = ell < 0.01f ? 0.f : 20.f * std::pow(15.f, ell);
		float hf = clamp(params[HF_PARAM].getValue() + inputs[HF_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float echo = clamp(params[ECHO_PARAM].getValue() + inputs[ECHO_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		int revs = params[REVS_PARAM].getValue() > 0.5f ? 2 : 1;

		float l = inputs[IN_L_INPUT].getVoltage();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : l;
		float ol, orr, wl, wr;
		eng.process(l, r, drive, ellHz, hf, echo, revs, revSec, ol, orr, wl, wr);
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(ol));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(orr));
		outputs[WET_L_OUTPUT].setVoltage(wl);
		outputs[WET_R_OUTPUT].setVoltage(wr);
		if (lightDiv.process()) lights[HF_LIGHT].setBrightness(clamp((1.f - eng.hfGain) * 3.f, 0.f, 1.f));
	}
};

struct LatheWidget : ModuleWidget {
	LatheWidget(LatheModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lathe.svg")));
		addScrews(this);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LL::DRIVE_X, LL::DRIVE_Y), module, LatheModule::DRIVE_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LL::ELLIPTIC_X, LL::ELLIPTIC_Y), module, LatheModule::ELLIPTIC_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LL::HF_X, LL::HF_Y), module, LatheModule::HF_PARAM));
		addChild(createLightCentered<SmallLight<YellowLight>>(P(LL::HF_LIGHT_X, LL::HF_LIGHT_Y), module, LatheModule::HF_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(P(LL::ECHO_X, LL::ECHO_Y), module, LatheModule::ECHO_PARAM));
		addParam(createParamCentered<CKSS>(P(LL::REVS_X, LL::REVS_Y), module, LatheModule::REVS_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LL::RPM_X, LL::RPM_Y), module, LatheModule::RPM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(P(LL::BEATS_X, LL::BEATS_Y), module, LatheModule::BEATS_PARAM));
		addInput(createInputCentered<PJ301MPort>(P(LL::DRIVE_IN_X, LL::DRIVE_IN_Y), module, LatheModule::DRIVE_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LL::ELLIPTIC_IN_X, LL::ELLIPTIC_IN_Y), module, LatheModule::ELLIPTIC_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LL::HF_IN_X, LL::HF_IN_Y), module, LatheModule::HF_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LL::ECHO_IN_X, LL::ECHO_IN_Y), module, LatheModule::ECHO_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LL::CLOCK_IN_X, LL::CLOCK_IN_Y), module, LatheModule::CLOCK_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LL::WET_L_X, LL::WET_L_Y), module, LatheModule::WET_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LL::WET_R_X, LL::WET_R_Y), module, LatheModule::WET_R_OUTPUT));
		addInput(createInputCentered<PJ301MPort>(P(LL::IN_L_X, LL::IN_L_Y), module, LatheModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LL::IN_R_X, LL::IN_R_Y), module, LatheModule::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LL::OUT_L_X, LL::OUT_L_Y), module, LatheModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LL::OUT_R_X, LL::OUT_R_Y), module, LatheModule::OUT_R_OUTPUT));
	}
};

Model* modelLathe = createModel<LatheModule, LatheWidget>("Lathe");
