// Studio: the record before it reaches the lathe.
//   CONSOLE -> TAPE (master tape, with optional DOLBY A around it) -> GLUE
// Live audio; latency 0, plus 3 ms with TAPE on.
#include "Assign.hpp"
#include "ChainDSP.hpp"
namespace LS = layout::Studio;

struct StudioModule : Module {
	enum ParamIds {
		CONSOLE_ON_PARAM, TAPE_ON_PARAM, DOLBY_ON_PARAM, GLUE_ON_PARAM,
		DRIVE_PARAM, IRON_PARAM, XTALK_PARAM,
		SAT_PARAM, BUMP_PARAM, FLUTTER_PARAM, AZIMUTH_PARAM, SPEED_PARAM,
		ERROR_PARAM, PATH_PARAM,
		AMOUNT_PARAM, TIME_PARAM,
		BYPASS_PARAM, A_DEST_PARAM, A_AMT_PARAM, B_DEST_PARAM, B_AMT_PARAM,
		NUM_PARAMS
	};
	enum InputIds { IN_L_INPUT, IN_R_INPUT, CVA_INPUT, CVB_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { CONSOLE_ON_LIGHT, TAPE_ON_LIGHT, DOLBY_ON_LIGHT, GLUE_ON_LIGHT, GR_LIGHT, BYPASS_LIGHT, NUM_LIGHTS };

	enum { NUM_DESTS = 11 };
	static const chain::Dest* dests() {
		static const chain::Dest t[NUM_DESTS] = {
			{"Off", "--", chain::NONE, -1, 0.f},
			{"Console: drive", "DRIVE", chain::KNOB, DRIVE_PARAM, 0.1f},
			{"Console: iron", "IRON", chain::KNOB, IRON_PARAM, 0.1f},
			{"Console: crosstalk", "XTALK", chain::KNOB, XTALK_PARAM, 0.1f},
			{"Tape: saturation", "TAPE SAT", chain::KNOB, SAT_PARAM, 0.1f},
			{"Tape: head bump", "TAPE BUMP", chain::KNOB, BUMP_PARAM, 0.1f},
			{"Tape: flutter", "FLUTTER", chain::KNOB, FLUTTER_PARAM, 0.1f},
			{"Tape: azimuth", "AZIMUTH", chain::KNOB, AZIMUTH_PARAM, 0.2f},
			{"Dolby: level error", "DOLBY ERR", chain::KNOB, ERROR_PARAM, 0.2f},
			{"Glue: amount", "GLUE", chain::KNOB, AMOUNT_PARAM, 0.1f},
			{"Bypass (gate)", "BYPASS", chain::GATE, BYPASS_PARAM, 0.f},
		};
		return t;
	}

	vinyl::Console console;
	vinyl::Tape tape;
	vinyl::DolbyA dolby;
	vinyl::Glue glue;
	chain::Assign cv;
	float bypassFade = 0.f;
	dsp::ClockDivider lightDiv;

	StudioModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		const char* secs[4] = {"Console", "Tape", "Dolby A", "Glue"};
		const bool defOn[4] = {true, true, false, true};
		for (int i = 0; i < 4; i++)
			configSwitch(CONSOLE_ON_PARAM + i, 0.f, 1.f, defOn[i] ? 1.f : 0.f, string::f("%s section", secs[i]), {"Off", "On"});
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.3f, "Drive (how hot the channels run)", "%", 0.f, 100.f);
		configParam(IRON_PARAM, 0.f, 1.f, 0.3f, "Iron (transformer colour: thick bass, softer top)", "%", 0.f, 100.f);
		configParam(XTALK_PARAM, 0.f, 1.f, 0.2f, "Crosstalk (each side leaks into the other)", "%", 0.f, 100.f);
		configParam(SAT_PARAM, 0.f, 1.f, 0.4f, "Tape saturation (level onto tape)", "%", 0.f, 100.f);
		configParam(BUMP_PARAM, 0.f, 1.f, 0.4f, "Head bump (low resonance; pitch follows SPEED)", "%", 0.f, 100.f);
		configParam(FLUTTER_PARAM, 0.f, 1.f, 0.2f, "Flutter and wow", "%", 0.f, 100.f);
		configParam(AZIMUTH_PARAM, -1.f, 1.f, 0.f, "Azimuth error (crooked head on a copy master)", "%", 0.f, 100.f);
		configSwitch(SPEED_PARAM, 0.f, 2.f, 1.f, "Tape speed", {"7.5 ips", "15 ips", "30 ips"});
		configParam(ERROR_PARAM, -1.f, 1.f, 0.f, "Dolby level error (+ tape hot: bright; - tape quiet: dull)", " dB", 0.f, 10.f);
		configSwitch(PATH_PARAM, 0.f, 2.f, 2.f, "Dolby path", {"DEC: decoded, never encoded (dull)", "ENC: encoded, never decoded (bright, squashed)", "OK: encode + decode"});
		configParam(AMOUNT_PARAM, 0.f, 1.f, 0.3f, "Glue amount (vari-mu valve compressor)", "%", 0.f, 100.f);
		configSwitch(TIME_PARAM, 0.f, 2.f, 1.f, "Glue time", {"Slow", "Mid", "Fast"});
		configSwitch(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass (dry input straight to the outputs, no delay)", {"Off", "Bypassed"});
		paramQuantities[BYPASS_PARAM]->randomizeEnabled = false;
		std::vector<std::string> dn = chain::destNames(dests(), NUM_DESTS);
		configSwitch(A_DEST_PARAM, 0.f, NUM_DESTS - 1, 0.f, "CV A destination", dn);
		configParam(A_AMT_PARAM, -1.f, 1.f, 0.5f, "CV A amount", "%", 0.f, 100.f);
		configSwitch(B_DEST_PARAM, 0.f, NUM_DESTS - 1, 0.f, "CV B destination", dn);
		configParam(B_AMT_PARAM, -1.f, 1.f, 0.5f, "CV B amount", "%", 0.f, 100.f);
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configInput(CVA_INPUT, "CV A (10 V = full knob range at 100%; gates above 1 V)");
		configInput(CVB_INPUT, "CV B (10 V = full knob range at 100%; gates above 1 V)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		cv.table = dests(); cv.n = NUM_DESTS;
		lightDiv.setDivision(64);
	}
	void setRates(float sr) { console.setSampleRate(sr); tape.setSampleRate(sr); dolby.setSampleRate(sr); glue.setSampleRate(sr); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { setRates(e.sampleRate); }
	void onAdd(const AddEvent& e) override { setRates(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		console.reset(); tape.reset(); dolby.reset(); glue.reset(); bypassFade = 0.f;
	}
	bool on(int i) { return params[CONSOLE_ON_PARAM + i].getValue() > 0.5f; }
	float pv(int p, float lo = 0.f, float hi = 1.f) { return clamp(params[p].getValue() + cv.mod(p), lo, hi); }
	int sw(int p, int n) { return clamp((int) std::round(params[p].getValue()), 0, n - 1); }

	void process(const ProcessArgs& args) override {
		cv.update(this, A_DEST_PARAM, A_AMT_PARAM, CVA_INPUT, B_DEST_PARAM, B_AMT_PARAM, CVB_INPUT);
		float dryL = inputs[IN_L_INPUT].getVoltage();
		float dryR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : dryL;
		float l = dryL, r = dryR;
		if (on(0)) console.process(l, r, pv(DRIVE_PARAM), pv(IRON_PARAM), pv(XTALK_PARAM), l, r);
		// Dolby A goes around the tape: encode -> tape -> decode
		// PATH: 0 DEC (never encoded), 1 ENC (never decoded), 2 OK
		int path = sw(PATH_PARAM, 3);
		bool dolbyOn = on(2);
		if (dolbyOn && path != 0) dolby.encode(l, r);
		if (on(1)) tape.process(l, r, pv(SAT_PARAM), pv(BUMP_PARAM), sw(SPEED_PARAM, 3), pv(FLUTTER_PARAM), pv(AZIMUTH_PARAM, -1.f, 1.f), l, r);
		if (dolbyOn && path != 1) dolby.decode(l, r, pv(ERROR_PARAM, -1.f, 1.f) * 10.f);
		if (on(3)) glue.process(l, r, pv(AMOUNT_PARAM), 2 - sw(TIME_PARAM, 3), l, r);

		bool byp = (params[BYPASS_PARAM].getValue() > 0.5f) != cv.gate(BYPASS_PARAM);
		bypassFade = clamp(bypassFade + (byp ? 1.f : -1.f) * args.sampleTime / 0.01f, 0.f, 1.f);
		l += (dryL - l) * bypassFade;
		r += (dryR - r) * bypassFade;
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(l));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(r));

		if (lightDiv.process()) {
			for (int i = 0; i < 4; i++) lights[CONSOLE_ON_LIGHT + i].setBrightness(on(i) ? 1.f : 0.f);
			lights[GR_LIGHT].setBrightness(on(3) ? clamp(glue.grDb / 10.f, 0.f, 1.f) : 0.f);
			lights[BYPASS_LIGHT].setBrightness(bypassFade);
		}
	}
};

static const chain::Preset STUDIO_PRESETS[] = {
	{"Clean tracking", {}},
	{"Hot desk", {{StudioModule::DRIVE_PARAM, 0.8f}, {StudioModule::IRON_PARAM, 0.7f}, {StudioModule::XTALK_PARAM, 0.35f}}},
	{"Tape, 15 ips, warm", {{StudioModule::SAT_PARAM, 0.6f}, {StudioModule::BUMP_PARAM, 0.6f}, {StudioModule::SPEED_PARAM, 1.f}}},
	{"Tape, 7.5 ips, lo-fi", {{StudioModule::SAT_PARAM, 0.75f}, {StudioModule::BUMP_PARAM, 0.8f}, {StudioModule::SPEED_PARAM, 0.f}, {StudioModule::FLUTTER_PARAM, 0.6f}}},
	{"Tape, 30 ips, mastering", {{StudioModule::SAT_PARAM, 0.35f}, {StudioModule::BUMP_PARAM, 0.5f}, {StudioModule::SPEED_PARAM, 2.f}, {StudioModule::FLUTTER_PARAM, 0.08f}}},
	{"Wobbly copy master", {{StudioModule::FLUTTER_PARAM, 0.9f}, {StudioModule::AZIMUTH_PARAM, 0.6f}, {StudioModule::SAT_PARAM, 0.5f}}},
	{"Dolby left on, never decoded", {{StudioModule::DOLBY_ON_PARAM, 1.f}, {StudioModule::PATH_PARAM, 1.f}}},
	{"Dolby, tape played too quiet", {{StudioModule::DOLBY_ON_PARAM, 1.f}, {StudioModule::ERROR_PARAM, -0.6f}}},
	{"Dolby, tape played too hot", {{StudioModule::DOLBY_ON_PARAM, 1.f}, {StudioModule::ERROR_PARAM, 0.6f}}},
	{"Sixties glue", {{StudioModule::AMOUNT_PARAM, 0.6f}, {StudioModule::TIME_PARAM, 0.f}, {StudioModule::IRON_PARAM, 0.5f}, {StudioModule::SAT_PARAM, 0.45f}}},
	{"Squashed master", {{StudioModule::AMOUNT_PARAM, 0.9f}, {StudioModule::TIME_PARAM, 2.f}, {StudioModule::DRIVE_PARAM, 0.6f}}},
};

struct StudioWidget : ModuleWidget {
	StudioWidget(StudioModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Studio.svg")));
		addScrews(this);
		typedef StudioModule M;
		const float onX[4] = {LS::CONSOLE_ON_X, LS::TAPE_ON_X, LS::DOLBY_ON_X, LS::GLUE_ON_X};
		for (int i = 0; i < 4; i++)
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(onX[i], LS::CONSOLE_ON_Y), module, M::CONSOLE_ON_PARAM + i, M::CONSOLE_ON_LIGHT + i));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LS::DRIVE_X, LS::DRIVE_Y), module, M::DRIVE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LS::IRON_X, LS::IRON_Y), module, M::IRON_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LS::XTALK_X, LS::XTALK_Y), module, M::XTALK_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LS::SAT_X, LS::SAT_Y), module, M::SAT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LS::BUMP_X, LS::BUMP_Y), module, M::BUMP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LS::FLUTTER_X, LS::FLUTTER_Y), module, M::FLUTTER_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LS::AZIMUTH_X, LS::AZIMUTH_Y), module, M::AZIMUTH_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LS::SPEED_X, LS::SPEED_Y), module, M::SPEED_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LS::ERROR_X, LS::ERROR_Y), module, M::ERROR_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LS::PATH_X, LS::PATH_Y), module, M::PATH_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LS::AMOUNT_X, LS::AMOUNT_Y), module, M::AMOUNT_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LS::TIME_X, LS::TIME_Y), module, M::TIME_PARAM));
		addChild(createLightCentered<MediumLight<YellowLight>>(P(LS::GR_LIGHT_X, LS::GR_LIGHT_Y), module, M::GR_LIGHT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(P(LS::BYPASS_X, LS::BYPASS_Y), module, M::BYPASS_PARAM, M::BYPASS_LIGHT));
		const float la[10] = {LS::CVA_IN_X, LS::CVA_IN_Y, LS::A_DEST_X, LS::A_DEST_Y, LS::A_AMT_X, LS::A_AMT_Y, LS::A_DISP_X, LS::A_DISP_Y, LS::A_DISP_W, LS::A_DISP_H};
		const float lb[10] = {LS::CVB_IN_X, LS::CVB_IN_Y, LS::B_DEST_X, LS::B_DEST_Y, LS::B_AMT_X, LS::B_AMT_Y, LS::B_DISP_X, LS::B_DISP_Y, LS::B_DISP_W, LS::B_DISP_H};
		chain::addAssignWidgets(this, module, M::dests(), M::NUM_DESTS, M::CVA_INPUT, M::CVB_INPUT,
		                        M::A_DEST_PARAM, M::A_AMT_PARAM, M::B_DEST_PARAM, M::B_AMT_PARAM, la, lb, "A > TAPE SAT +50%", "B > FLUTTER +50%");
		addInput(createInputCentered<PJ301MPort>(P(LS::IN_L_X, LS::IN_L_Y), module, M::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LS::IN_R_X, LS::IN_R_Y), module, M::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LS::OUT_L_X, LS::OUT_L_Y), module, M::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LS::OUT_R_X, LS::OUT_R_Y), module, M::OUT_R_OUTPUT));
		chain::Rings* rg = createWidget<chain::Rings>(Vec(0, 0));
		rg->box.size = box.size;
		rg->module = module; rg->table = M::dests(); rg->n = M::NUM_DESTS; rg->destA = M::A_DEST_PARAM; rg->destB = M::B_DEST_PARAM;
		const float L = 7.4f, S = 4.6f, B = 3.4f;
		rg->spots = {
			{M::DRIVE_PARAM, {LS::DRIVE_X, LS::DRIVE_Y, L}}, {M::IRON_PARAM, {LS::IRON_X, LS::IRON_Y, S}},
			{M::XTALK_PARAM, {LS::XTALK_X, LS::XTALK_Y, S}}, {M::SAT_PARAM, {LS::SAT_X, LS::SAT_Y, S}},
			{M::BUMP_PARAM, {LS::BUMP_X, LS::BUMP_Y, S}}, {M::FLUTTER_PARAM, {LS::FLUTTER_X, LS::FLUTTER_Y, S}},
			{M::AZIMUTH_PARAM, {LS::AZIMUTH_X, LS::AZIMUTH_Y, S}}, {M::ERROR_PARAM, {LS::ERROR_X, LS::ERROR_Y, L}},
			{M::AMOUNT_PARAM, {LS::AMOUNT_X, LS::AMOUNT_Y, L}}, {M::BYPASS_PARAM, {LS::BYPASS_X, LS::BYPASS_Y, B}},
		};
		addChild(rg);
	}
	void appendContextMenu(Menu* menu) override {
		StudioModule* m = dynamic_cast<StudioModule*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Presets", "", [=](Menu* sub) {
			int n = sizeof(STUDIO_PRESETS) / sizeof(STUDIO_PRESETS[0]);
			for (int i = 0; i < n; i++)
				sub->addChild(createMenuItem(string::f("%02d  %s", i + 1, STUDIO_PRESETS[i].name), "", [=]() {
					chain::applyPreset(m, STUDIO_PRESETS[i], {StudioModule::BYPASS_PARAM, StudioModule::A_DEST_PARAM, StudioModule::A_AMT_PARAM,
					                                          StudioModule::B_DEST_PARAM, StudioModule::B_AMT_PARAM});
				}));
		}));
	}
};

Model* modelStudio = createModel<StudioModule, StudioWidget>("Studio");
