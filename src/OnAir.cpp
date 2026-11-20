// On Air: a record played on FM radio, simulated from the station's
// processing through the air to a receiver. See RadioDSP.hpp.
#include "Assign.hpp"
#include "RadioDSP.hpp"
namespace LA = layout::OnAir;

static const float STATION_MHZ = 97.6f;

struct OnAirModule : Module {
	enum ParamIds {
		STATION_ON_PARAM, AIR_ON_PARAM, NEXT_ON_PARAM,
		PROC_PARAM, ERA_PARAM, EMPH_PARAM,
		SIGNAL_PARAM, MULTIPATH_PARAM, MOTION_PARAM,
		LEVEL_PARAM, SPACING_PARAM,
		TUNE_PARAM, SET_PARAM, MONO_PARAM,
		BYPASS_PARAM, A_DEST_PARAM, A_AMT_PARAM, B_DEST_PARAM, B_AMT_PARAM,
		NUM_PARAMS
	};
	enum InputIds { IN_L_INPUT, IN_R_INPUT, NEXT_L_INPUT, NEXT_R_INPUT, CVA_INPUT, CVB_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { STATION_ON_LIGHT, AIR_ON_LIGHT, NEXT_ON_LIGHT, MONO_LIGHT, BYPASS_LIGHT, NUM_LIGHTS };

	enum { NUM_DESTS = 9 };
	static const chain::Dest* dests() {
		static const chain::Dest t[NUM_DESTS] = {
			{"Off", "--", chain::NONE, -1, 0.f},
			{"Station: processing", "PROCESSING", chain::KNOB, PROC_PARAM, 0.1f},
			{"Air: signal", "SIGNAL", chain::KNOB, SIGNAL_PARAM, 0.1f},
			{"Air: multipath", "MULTIPATH", chain::KNOB, MULTIPATH_PARAM, 0.1f},
			{"Air: motion", "MOTION", chain::KNOB, MOTION_PARAM, 0.1f},
			{"Next: level", "NEXT LEVEL", chain::KNOB, LEVEL_PARAM, 0.1f},
			{"Radio: tune (70 kHz/V)", "TUNE", chain::KNOB, TUNE_PARAM, 0.07f},
			{"Radio: mono (gate)", "MONO", chain::GATE, MONO_PARAM, 0.f},
			{"Bypass (gate)", "BYPASS", chain::GATE, BYPASS_PARAM, 0.f},
		};
		return t;
	}

	vinyl::FmLink link;
	chain::Assign cv;
	float bypassFade = 0.f;
	bool eco = false, ecoApplied = false;
	float tuneMhz = STATION_MHZ;
	dsp::ClockDivider lightDiv;

	OnAirModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		const char* secs[3] = {"Station processing", "Air (noise, multipath)", "Next station"};
		for (int i = 0; i < 3; i++)
			configSwitch(STATION_ON_PARAM + i, 0.f, 1.f, 1.f, string::f("%s", secs[i]), {"Off", "On"});
		configParam(PROC_PARAM, 0.f, 1.f, 0.5f, "Processing (how hard the station leans on it)", "%", 0.f, 100.f);
		configSwitch(ERA_PARAM, 0.f, 2.f, 1.f, "Era", {"70s: gentle AGC + limiter", "80s: 3-band", "90s: loud, hard clipped"});
		configSwitch(EMPH_PARAM, 0.f, 1.f, 0.f, "Pre-emphasis", {"EU (50 us)", "US (75 us)"});
		configParam(SIGNAL_PARAM, 0.f, 1.f, 0.85f, "Signal strength (carrier to noise)", " dB", 0.f, 65.f, -5.f);
		configParam(MULTIPATH_PARAM, 0.f, 1.f, 0.1f, "Multipath (reflections off hills and buildings)", "%", 0.f, 100.f);
		configParam(MOTION_PARAM, 0.f, 1.f, 0.f, "Motion (the receiver moving: picket fencing)", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.3f, "Next station level (relative to ours)", " dB", 0.f, 46.f, -40.f);
		configSwitch(SPACING_PARAM, 0.f, 1.f, 1.f, "Next station is", {"100 kHz up", "200 kHz up"});
		configParam(TUNE_PARAM, STATION_MHZ - 0.25f, STATION_MHZ + 0.45f, STATION_MHZ, "Tuning", " MHz");
		configSwitch(SET_PARAM, 0.f, 2.f, 2.f, "Radio", {"Cheap portable (narrow, mono-ish)", "Car radio (blends, high cut, soft mute)", "Hi-fi tuner (wide, clean)"});
		configSwitch(MONO_PARAM, 0.f, 1.f, 0.f, "Mono", {"Auto stereo", "Mono"});
		configSwitch(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass (dry input straight to the outputs, no delay)", {"Off", "Bypassed"});
		paramQuantities[BYPASS_PARAM]->randomizeEnabled = false;
		std::vector<std::string> dn = chain::destNames(dests(), NUM_DESTS);
		configSwitch(A_DEST_PARAM, 0.f, NUM_DESTS - 1, 0.f, "CV A destination", dn);
		configParam(A_AMT_PARAM, -1.f, 1.f, 0.5f, "CV A amount", "%", 0.f, 100.f);
		configSwitch(B_DEST_PARAM, 0.f, NUM_DESTS - 1, 0.f, "CV B destination", dn);
		configParam(B_AMT_PARAM, -1.f, 1.f, 0.5f, "CV B amount", "%", 0.f, 100.f);
		configInput(IN_L_INPUT, "Our station, left (or mono)");
		configInput(IN_R_INPUT, "Our station, right (normalled to left)");
		configInput(NEXT_L_INPUT, "Next station, left (unpatched: a silent carrier)");
		configInput(NEXT_R_INPUT, "Next station, right (normalled to left)");
		configInput(CVA_INPUT, "CV A (10 V = full knob range at 100%; gates above 1 V)");
		configInput(CVB_INPUT, "CV B (10 V = full knob range at 100%; gates above 1 V)");
		configOutput(OUT_L_OUTPUT, "Radio left");
		configOutput(OUT_R_OUTPUT, "Radio right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		cv.table = dests(); cv.n = NUM_DESTS;
		lightDiv.setDivision(64);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { ecoApplied = eco; link.setSampleRate(e.sampleRate, eco ? 384000.f : 768000.f); }
	void onAdd(const AddEvent& e) override { ecoApplied = eco; link.setSampleRate(APP->engine->getSampleRate(), eco ? 384000.f : 768000.f); }
	void onReset(const ResetEvent& e) override { Module::onReset(e); eco = false; bypassFade = 0.f; }
	float pv(int p, float lo = 0.f, float hi = 1.f) { return clamp(params[p].getValue() + cv.mod(p), lo, hi); }
	bool on(int i) { return params[STATION_ON_PARAM + i].getValue() > 0.5f; }
	int sw(int p, int n) { return clamp((int) std::round(params[p].getValue()), 0, n - 1); }

	void process(const ProcessArgs& args) override {
		if (eco != ecoApplied) { ecoApplied = eco; link.setSampleRate(args.sampleRate, eco ? 384000.f : 768000.f); }
		cv.update(this, A_DEST_PARAM, A_AMT_PARAM, CVA_INPUT, B_DEST_PARAM, B_AMT_PARAM, CVB_INPUT);
		float dryL = inputs[IN_L_INPUT].getVoltage();
		float dryR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : dryL;
		float nL = inputs[NEXT_L_INPUT].getVoltage();
		float nR = inputs[NEXT_R_INPUT].isConnected() ? inputs[NEXT_R_INPUT].getVoltage() : nL;
		bool air = on(1), next = on(2);
		tuneMhz = pv(TUNE_PARAM, STATION_MHZ - 0.25f, STATION_MHZ + 0.45f);
		bool mono = (params[MONO_PARAM].getValue() > 0.5f) != cv.gate(MONO_PARAM);
		float l, r;
		link.process(dryL, dryR, nL, nR, next,
		             on(0) ? pv(PROC_PARAM) : 0.f, sw(ERA_PARAM, 3), params[EMPH_PARAM].getValue() > 0.5f,
		             air ? pv(SIGNAL_PARAM) : 1.f, air ? pv(MULTIPATH_PARAM) : 0.f, air ? pv(MOTION_PARAM) : 0.f,
		             next ? pv(LEVEL_PARAM) : 0.f, params[SPACING_PARAM].getValue() > 0.5f,
		             (tuneMhz - STATION_MHZ) * 1e6f, 2 - sw(SET_PARAM, 3), mono, l, r);

		bool byp = (params[BYPASS_PARAM].getValue() > 0.5f) != cv.gate(BYPASS_PARAM);
		bypassFade = clamp(bypassFade + (byp ? 1.f : -1.f) * args.sampleTime / 0.01f, 0.f, 1.f);
		l += (dryL - l) * bypassFade;
		r += (dryR - r) * bypassFade;
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(l));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(r));
		if (lightDiv.process()) {
			for (int i = 0; i < 3; i++) lights[STATION_ON_LIGHT + i].setBrightness(on(i) ? 1.f : 0.f);
			lights[MONO_LIGHT].setBrightness(mono ? 1.f : 0.f);
			lights[BYPASS_LIGHT].setBrightness(bypassFade);
		}
	}
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "eco", json_boolean(eco));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "eco");
		if (j) eco = json_boolean_value(j);
	}
};

typedef OnAirModule OA;
static const chain::Preset ONAIR_PRESETS[] = {
	{"Local station, strong", {}},
	{"70s FM, gentle", {{OA::ERA_PARAM, 0.f}, {OA::PROC_PARAM, 0.4f}}},
	{"90s loudness war", {{OA::ERA_PARAM, 2.f}, {OA::PROC_PARAM, 1.f}}},
	{"Fringe reception", {{OA::SIGNAL_PARAM, 0.35f}}},
	{"Car, driving through town", {{OA::SET_PARAM, 1.f}, {OA::SIGNAL_PARAM, 0.55f}, {OA::MULTIPATH_PARAM, 0.7f}, {OA::MOTION_PARAM, 0.6f}}},
	{"Kitchen portable, weak", {{OA::SET_PARAM, 0.f}, {OA::SIGNAL_PARAM, 0.32f}, {OA::MULTIPATH_PARAM, 0.3f}}},
	{"Hills and reflections", {{OA::MULTIPATH_PARAM, 1.f}, {OA::MOTION_PARAM, 0.1f}, {OA::SIGNAL_PARAM, 0.7f}}},
	{"Next station bleeding in", {{OA::LEVEL_PARAM, 0.9f}, {OA::SPACING_PARAM, 0.f}, {OA::TUNE_PARAM, STATION_MHZ + 0.04f}}},
	{"Between stations", {{OA::TUNE_PARAM, STATION_MHZ + 0.1f}, {OA::LEVEL_PARAM, 0.85f}}},
	{"Tuned slightly off", {{OA::TUNE_PARAM, STATION_MHZ + 0.045f}}},
	{"US mono receiver", {{OA::EMPH_PARAM, 1.f}, {OA::MONO_PARAM, 1.f}, {OA::SET_PARAM, 0.f}}},
};

struct FreqScreen : VinylScreen {
	OnAirModule* module = nullptr;
	void lines(std::vector<std::string>& out) override {
		if (!module) { out.push_back("FM 97.60"); out.push_back("SIG ||||  STEREO"); return; }
		out.push_back(string::f("FM %.2f", module->tuneMhz));
		int bars = (int) std::round(module->link.meterSignal * 5.f);
		std::string b = "SIG ";
		for (int i = 0; i < 5; i++) b += i < bars ? "|" : ".";
		b += module->link.stereoLit ? "  STEREO" : "  MONO  ";
		out.push_back(b);
	}
};

struct OnAirWidget : ModuleWidget {
	OnAirWidget(OnAirModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/OnAir.svg")));
		addScrews(this);
		typedef OnAirModule M;
		const float onX[3] = {LA::STATION_ON_X, LA::AIR_ON_X, LA::NEXT_ON_X};
		for (int i = 0; i < 3; i++)
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(onX[i], LA::STATION_ON_Y), module, M::STATION_ON_PARAM + i, M::STATION_ON_LIGHT + i));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LA::PROC_X, LA::PROC_Y), module, M::PROC_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LA::ERA_X, LA::ERA_Y), module, M::ERA_PARAM));
		addParam(createParamCentered<CKSS>(P(LA::EMPH_X, LA::EMPH_Y), module, M::EMPH_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LA::SIGNAL_X, LA::SIGNAL_Y), module, M::SIGNAL_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LA::MULTIPATH_X, LA::MULTIPATH_Y), module, M::MULTIPATH_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LA::MOTION_X, LA::MOTION_Y), module, M::MOTION_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LA::LEVEL_X, LA::LEVEL_Y), module, M::LEVEL_PARAM));
		addParam(createParamCentered<CKSS>(P(LA::SPACING_X, LA::SPACING_Y), module, M::SPACING_PARAM));
		FreqScreen* s = createWidget<FreqScreen>(P(LA::FREQ_DISP_X, LA::FREQ_DISP_Y));
		s->box.size = mm2px(Vec(LA::FREQ_DISP_W, LA::FREQ_DISP_H));
		s->module = module;
		s->accent = nvgRGB(224, 96, 90);
		s->fontMm = 2.8f;
		addChild(s);
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LA::TUNE_X, LA::TUNE_Y), module, M::TUNE_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LA::SET_X, LA::SET_Y), module, M::SET_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(LA::MONO_X, LA::MONO_Y), module, M::MONO_PARAM, M::MONO_LIGHT));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(P(LA::BYPASS_X, LA::BYPASS_Y), module, M::BYPASS_PARAM, M::BYPASS_LIGHT));
		const float la[10] = {LA::CVA_IN_X, LA::CVA_IN_Y, LA::A_DEST_X, LA::A_DEST_Y, LA::A_AMT_X, LA::A_AMT_Y, LA::A_DISP_X, LA::A_DISP_Y, LA::A_DISP_W, LA::A_DISP_H};
		const float lb[10] = {LA::CVB_IN_X, LA::CVB_IN_Y, LA::B_DEST_X, LA::B_DEST_Y, LA::B_AMT_X, LA::B_AMT_Y, LA::B_DISP_X, LA::B_DISP_Y, LA::B_DISP_W, LA::B_DISP_H};
		chain::addAssignWidgets(this, module, M::dests(), M::NUM_DESTS, M::CVA_INPUT, M::CVB_INPUT,
		                        M::A_DEST_PARAM, M::A_AMT_PARAM, M::B_DEST_PARAM, M::B_AMT_PARAM, la, lb, "A > SIGNAL +50%", "B > TUNE +50%");
		addInput(createInputCentered<PJ301MPort>(P(LA::IN_L_X, LA::IN_L_Y), module, M::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LA::IN_R_X, LA::IN_R_Y), module, M::IN_R_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LA::NEXT_L_X, LA::NEXT_L_Y), module, M::NEXT_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LA::NEXT_R_X, LA::NEXT_R_Y), module, M::NEXT_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LA::OUT_L_X, LA::OUT_L_Y), module, M::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LA::OUT_R_X, LA::OUT_R_Y), module, M::OUT_R_OUTPUT));
		chain::Rings* rg = createWidget<chain::Rings>(Vec(0, 0));
		rg->box.size = box.size;
		rg->module = module; rg->table = M::dests(); rg->n = M::NUM_DESTS; rg->destA = M::A_DEST_PARAM; rg->destB = M::B_DEST_PARAM;
		const float L = 7.4f, S = 4.6f, B = 3.4f;
		rg->spots = {
			{M::PROC_PARAM, {LA::PROC_X, LA::PROC_Y, L}}, {M::SIGNAL_PARAM, {LA::SIGNAL_X, LA::SIGNAL_Y, L}},
			{M::MULTIPATH_PARAM, {LA::MULTIPATH_X, LA::MULTIPATH_Y, S}}, {M::MOTION_PARAM, {LA::MOTION_X, LA::MOTION_Y, S}},
			{M::LEVEL_PARAM, {LA::LEVEL_X, LA::LEVEL_Y, L}}, {M::TUNE_PARAM, {LA::TUNE_X, LA::TUNE_Y, L}},
			{M::MONO_PARAM, {LA::MONO_X, LA::MONO_Y, B}}, {M::BYPASS_PARAM, {LA::BYPASS_X, LA::BYPASS_Y, B}},
		};
		addChild(rg);
	}
	void appendContextMenu(Menu* menu) override {
		OnAirModule* m = dynamic_cast<OnAirModule*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Presets", "", [=](Menu* sub) {
			int n = sizeof(ONAIR_PRESETS) / sizeof(ONAIR_PRESETS[0]);
			for (int i = 0; i < n; i++)
				sub->addChild(createMenuItem(string::f("%02d  %s", i + 1, ONAIR_PRESETS[i].name), "", [=]() {
					chain::applyPreset(m, ONAIR_PRESETS[i], {OA::BYPASS_PARAM, OA::A_DEST_PARAM, OA::A_AMT_PARAM, OA::B_DEST_PARAM, OA::B_AMT_PARAM});
				}));
		}));
		menu->addChild(createBoolMenuItem("Eco mode (half the radio sample rate, less CPU)", "",
			[=]() { return m->eco; }, [=](bool v) { m->eco = v; }));
	}
};

Model* modelOnAir = createModel<OnAirModule, OnAirWidget>("OnAir");
