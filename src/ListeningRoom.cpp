// Listening Room: the record after the preamp.
//   AMP -> SPEAKER -> ROOM -> EARS
// Live audio; the direct sound is never delayed (only the far ear, by up to
// 0.65 ms, when the head is turned). Room echoes come after the direct sound.
#include "Assign.hpp"
#include "ChainDSP.hpp"
namespace LR = layout::ListeningRoom;

struct RoomModule : Module {
	enum ParamIds {
		AMP_ON_PARAM, SPEAKER_ON_PARAM, ROOM_ON_PARAM, EARS_ON_PARAM,
		VOLUME_PARAM, TYPE_PARAM, LOUD_PARAM,
		MODEL_PARAM, BREAKUP_PARAM,
		SIZE_PARAM, DAMP_PARAM, MODES_PARAM, WALL_PARAM,
		DISTANCE_PARAM, ANGLE_PARAM, PARTY_PARAM,
		BYPASS_PARAM, A_DEST_PARAM, A_AMT_PARAM, B_DEST_PARAM, B_AMT_PARAM,
		NUM_PARAMS
	};
	enum InputIds { IN_L_INPUT, IN_R_INPUT, CVA_INPUT, CVB_INPUT, NUM_INPUTS };
	enum OutputIds { OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds { AMP_ON_LIGHT, SPEAKER_ON_LIGHT, ROOM_ON_LIGHT, EARS_ON_LIGHT, LOUD_LIGHT, WALL_LIGHT, BYPASS_LIGHT, NUM_LIGHTS };

	enum { NUM_DESTS = 13 };
	static const chain::Dest* dests() {
		static const chain::Dest t[NUM_DESTS] = {
			{"Off", "--", chain::NONE, -1, 0.f},
			{"Amp: volume", "VOLUME", chain::KNOB, VOLUME_PARAM, 0.1f},
			{"Speaker: model", "SPEAKER", chain::KNOB, MODEL_PARAM, 0.7f},
			{"Speaker: breakup", "BREAKUP", chain::KNOB, BREAKUP_PARAM, 0.1f},
			{"Room: size", "SIZE", chain::KNOB, SIZE_PARAM, 0.1f},
			{"Room: damping", "DAMP", chain::KNOB, DAMP_PARAM, 0.1f},
			{"Room: bass modes", "MODES", chain::KNOB, MODES_PARAM, 0.1f},
			{"Room: next door (gate)", "WALL", chain::GATE, WALL_PARAM, 0.f},
			{"Ears: distance", "DISTANCE", chain::KNOB, DISTANCE_PARAM, 0.1f},
			{"Ears: head angle", "ANGLE", chain::KNOB, ANGLE_PARAM, 0.2f},
			{"Ears: party", "PARTY", chain::KNOB, PARTY_PARAM, 0.1f},
			{"Amp: loudness (gate)", "LOUDNESS", chain::GATE, LOUD_PARAM, 0.f},
			{"Bypass (gate)", "BYPASS", chain::GATE, BYPASS_PARAM, 0.f},
		};
		return t;
	}

	vinyl::PowerAmp amp;
	vinyl::Speaker speaker;
	vinyl::Room room;
	vinyl::Ears ears;
	chain::Assign cv;
	float bypassFade = 0.f;
	int model = 4;
	dsp::ClockDivider lightDiv;

	RoomModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		const char* secs[4] = {"Amp", "Speaker", "Room", "Ears"};
		for (int i = 0; i < 4; i++)
			configSwitch(AMP_ON_PARAM + i, 0.f, 1.f, 1.f, string::f("%s section", secs[i]), {"Off", "On"});
		configParam(VOLUME_PARAM, 0.f, 1.f, 0.5f, "Volume (how hard the amp is driven)", "%", 0.f, 100.f);
		configSwitch(TYPE_PARAM, 0.f, 2.f, 2.f, "Amp", {"Cheap (crossover distortion)", "Solid state", "Valve"});
		configSwitch(LOUD_PARAM, 0.f, 1.f, 0.f, "Loudness (bass + treble lift, more at low volume)", {"Off", "On"});
		std::vector<std::string> models;
		for (int i = 0; i < vinyl::NUM_SPEAKERS; i++) models.push_back(vinyl::SPEAKERS[i].name);
		configSwitch(MODEL_PARAM, 0.f, vinyl::NUM_SPEAKERS - 1, 4.f, "Speaker", models);
		configParam(BREAKUP_PARAM, 0.f, 1.f, 0.1f, "Breakup (cone excursion and rough mids)", "%", 0.f, 100.f);
		configParam(SIZE_PARAM, 0.f, 1.f, 0.35f, "Room size", " m", 0.f, 14.f, 2.5f);
		configParam(DAMP_PARAM, 0.f, 1.f, 0.5f, "Damping (soft furnishings)", "%", 0.f, 100.f);
		configParam(MODES_PARAM, 0.f, 1.f, 0.3f, "Bass modes (boomy notes)", "%", 0.f, 100.f);
		configSwitch(WALL_PARAM, 0.f, 1.f, 0.f, "Next door (through the wall)", {"Off", "On"});
		configParam(DISTANCE_PARAM, 0.f, 1.f, 0.25f, "Distance from the speakers", "%", 0.f, 100.f);
		configParam(ANGLE_PARAM, -1.f, 1.f, 0.f, "Head angle (+ = turned right)", " deg", 0.f, 90.f);
		configParam(PARTY_PARAM, 0.f, 1.f, 0.f, "Party (ears after a loud night)", "%", 0.f, 100.f);
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
		configOutput(OUT_L_OUTPUT, "Left ear");
		configOutput(OUT_R_OUTPUT, "Right ear");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		cv.table = dests(); cv.n = NUM_DESTS;
		lightDiv.setDivision(64);
	}
	void setRates(float sr) { amp.setSampleRate(sr); speaker.setSampleRate(sr); room.setSampleRate(sr); ears.setSampleRate(sr); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override { setRates(e.sampleRate); }
	void onAdd(const AddEvent& e) override { setRates(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		amp.reset(); speaker.reset(); room.reset(); ears.reset(); bypassFade = 0.f;
	}
	bool on(int i) { return params[AMP_ON_PARAM + i].getValue() > 0.5f; }
	float pv(int p, float lo = 0.f, float hi = 1.f) { return clamp(params[p].getValue() + cv.mod(p), lo, hi); }
	bool latch(int p) { return (params[p].getValue() > 0.5f) != cv.gate(p); }

	void process(const ProcessArgs& args) override {
		cv.update(this, A_DEST_PARAM, A_AMT_PARAM, CVA_INPUT, B_DEST_PARAM, B_AMT_PARAM, CVB_INPUT);
		float dryL = inputs[IN_L_INPUT].getVoltage();
		float dryR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : dryL;
		float l = dryL, r = dryR;
		if (on(0)) amp.process(l, r, pv(VOLUME_PARAM), clamp((int) std::round(params[TYPE_PARAM].getValue()), 0, 2), latch(LOUD_PARAM), l, r);
		model = (int) std::round(pv(MODEL_PARAM, 0.f, vinyl::NUM_SPEAKERS - 1));
		if (on(1)) speaker.process(l, r, model, pv(BREAKUP_PARAM), l, r);
		float dl = l, dr = r, wl = 0.f, wr = 0.f;
		if (on(2)) room.process(l, r, pv(SIZE_PARAM), pv(DAMP_PARAM), pv(MODES_PARAM), latch(WALL_PARAM), dl, dr, wl, wr);
		if (on(3)) ears.process(dl, dr, wl, wr, pv(DISTANCE_PARAM), pv(ANGLE_PARAM, -1.f, 1.f), pv(PARTY_PARAM), l, r);
		else { l = dl + 0.5f * wl; r = dr + 0.5f * wr; }

		bool byp = latch(BYPASS_PARAM);
		bypassFade = clamp(bypassFade + (byp ? 1.f : -1.f) * args.sampleTime / 0.01f, 0.f, 1.f);
		l += (dryL - l) * bypassFade;
		r += (dryR - r) * bypassFade;
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(l));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(r));

		if (lightDiv.process()) {
			for (int i = 0; i < 4; i++) lights[AMP_ON_LIGHT + i].setBrightness(on(i) ? 1.f : 0.f);
			lights[LOUD_LIGHT].setBrightness(latch(LOUD_PARAM) ? 1.f : 0.f);
			lights[WALL_LIGHT].setBrightness(latch(WALL_PARAM) ? 1.f : 0.f);
			lights[BYPASS_LIGHT].setBrightness(bypassFade);
		}
	}
};

typedef RoomModule RM;
static const chain::Preset ROOM_PRESETS[] = {
	{"Living room hi-fi", {}},
	{"Bedroom record player", {{RM::MODEL_PARAM, 0.f}, {RM::VOLUME_PARAM, 0.7f}, {RM::TYPE_PARAM, 0.f}, {RM::SIZE_PARAM, 0.15f}, {RM::DISTANCE_PARAM, 0.15f}}},
	{"Kitchen radio", {{RM::MODEL_PARAM, 1.f}, {RM::TYPE_PARAM, 0.f}, {RM::SIZE_PARAM, 0.25f}, {RM::DAMP_PARAM, 0.2f}, {RM::DISTANCE_PARAM, 0.35f}}},
	{"Car stereo, loudness on", {{RM::MODEL_PARAM, 2.f}, {RM::TYPE_PARAM, 1.f}, {RM::LOUD_PARAM, 1.f}, {RM::SIZE_PARAM, 0.f}, {RM::DAMP_PARAM, 0.9f}, {RM::MODES_PARAM, 0.6f}, {RM::DISTANCE_PARAM, 0.05f}}},
	{"Phone on the table", {{RM::MODEL_PARAM, 3.f}, {RM::TYPE_PARAM, 1.f}, {RM::VOLUME_PARAM, 0.8f}, {RM::SIZE_PARAM, 0.3f}, {RM::DISTANCE_PARAM, 0.3f}}},
	{"Studio control room", {{RM::MODEL_PARAM, 5.f}, {RM::TYPE_PARAM, 1.f}, {RM::SIZE_PARAM, 0.2f}, {RM::DAMP_PARAM, 0.85f}, {RM::MODES_PARAM, 0.1f}, {RM::DISTANCE_PARAM, 0.1f}}},
	{"Club, front of the stack", {{RM::MODEL_PARAM, 7.f}, {RM::VOLUME_PARAM, 0.85f}, {RM::TYPE_PARAM, 1.f}, {RM::BREAKUP_PARAM, 0.4f}, {RM::SIZE_PARAM, 0.9f}, {RM::DAMP_PARAM, 0.3f}, {RM::MODES_PARAM, 0.5f}, {RM::DISTANCE_PARAM, 0.3f}}},
	{"Party next door", {{RM::MODEL_PARAM, 7.f}, {RM::VOLUME_PARAM, 0.8f}, {RM::WALL_PARAM, 1.f}, {RM::SIZE_PARAM, 0.5f}, {RM::MODES_PARAM, 0.7f}}},
	{"Back of the hall", {{RM::MODEL_PARAM, 6.f}, {RM::SIZE_PARAM, 1.f}, {RM::DAMP_PARAM, 0.2f}, {RM::DISTANCE_PARAM, 1.f}}},
	{"The morning after", {{RM::PARTY_PARAM, 0.85f}, {RM::DISTANCE_PARAM, 0.2f}}},
	{"Head turned to the window", {{RM::ANGLE_PARAM, 0.8f}, {RM::MODEL_PARAM, 6.f}, {RM::DISTANCE_PARAM, 0.35f}}},
};

struct SpeakerScreen : VinylScreen {
	RoomModule* module = nullptr;
	void lines(std::vector<std::string>& out) override {
		int m = module ? module->model : 4;
		m = clamp(m, 0, vinyl::NUM_SPEAKERS - 1);
		out.push_back(vinyl::SPEAKERS[m].line1);
		out.push_back(vinyl::SPEAKERS[m].line2);
	}
};

struct RoomWidget : ModuleWidget {
	RoomWidget(RoomModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ListeningRoom.svg")));
		addScrews(this);
		typedef RoomModule M;
		const float onX[4] = {LR::AMP_ON_X, LR::SPEAKER_ON_X, LR::ROOM_ON_X, LR::EARS_ON_X};
		for (int i = 0; i < 4; i++)
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(onX[i], LR::AMP_ON_Y), module, M::AMP_ON_PARAM + i, M::AMP_ON_LIGHT + i));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LR::VOLUME_X, LR::VOLUME_Y), module, M::VOLUME_PARAM));
		addParam(createParamCentered<CKSSThree>(P(LR::TYPE_X, LR::TYPE_Y), module, M::TYPE_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(LR::LOUD_X, LR::LOUD_Y), module, M::LOUD_PARAM, M::LOUD_LIGHT));
		SpeakerScreen* s = createWidget<SpeakerScreen>(P(LR::SPK_DISP_X, LR::SPK_DISP_Y));
		s->box.size = mm2px(Vec(LR::SPK_DISP_W, LR::SPK_DISP_H));
		s->module = module;
		s->accent = nvgRGB(232, 176, 74);
		s->fontMm = 2.7f;
		addChild(s);
		RoundLargeBlackKnob* mk = createParamCentered<RoundLargeBlackKnob>(P(LR::MODEL_X, LR::MODEL_Y), module, M::MODEL_PARAM);
		mk->snap = true;
		addParam(mk);
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LR::BREAKUP_X, LR::BREAKUP_Y), module, M::BREAKUP_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LR::SIZE_X, LR::SIZE_Y), module, M::SIZE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LR::DAMP_X, LR::DAMP_Y), module, M::DAMP_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LR::MODES_X, LR::MODES_Y), module, M::MODES_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(LR::WALL_X, LR::WALL_Y), module, M::WALL_PARAM, M::WALL_LIGHT));
		addParam(createParamCentered<RoundLargeBlackKnob>(P(LR::DISTANCE_X, LR::DISTANCE_Y), module, M::DISTANCE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LR::ANGLE_X, LR::ANGLE_Y), module, M::ANGLE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(P(LR::PARTY_X, LR::PARTY_Y), module, M::PARTY_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(P(LR::BYPASS_X, LR::BYPASS_Y), module, M::BYPASS_PARAM, M::BYPASS_LIGHT));
		const float la[10] = {LR::CVA_IN_X, LR::CVA_IN_Y, LR::A_DEST_X, LR::A_DEST_Y, LR::A_AMT_X, LR::A_AMT_Y, LR::A_DISP_X, LR::A_DISP_Y, LR::A_DISP_W, LR::A_DISP_H};
		const float lb[10] = {LR::CVB_IN_X, LR::CVB_IN_Y, LR::B_DEST_X, LR::B_DEST_Y, LR::B_AMT_X, LR::B_AMT_Y, LR::B_DISP_X, LR::B_DISP_Y, LR::B_DISP_W, LR::B_DISP_H};
		chain::addAssignWidgets(this, module, M::dests(), M::NUM_DESTS, M::CVA_INPUT, M::CVB_INPUT,
		                        M::A_DEST_PARAM, M::A_AMT_PARAM, M::B_DEST_PARAM, M::B_AMT_PARAM, la, lb, "A > DISTANCE +50%", "B > SPEAKER +50%");
		addInput(createInputCentered<PJ301MPort>(P(LR::IN_L_X, LR::IN_L_Y), module, M::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LR::IN_R_X, LR::IN_R_Y), module, M::IN_R_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LR::OUT_L_X, LR::OUT_L_Y), module, M::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(P(LR::OUT_R_X, LR::OUT_R_Y), module, M::OUT_R_OUTPUT));
		chain::Rings* rg = createWidget<chain::Rings>(Vec(0, 0));
		rg->box.size = box.size;
		rg->module = module; rg->table = M::dests(); rg->n = M::NUM_DESTS; rg->destA = M::A_DEST_PARAM; rg->destB = M::B_DEST_PARAM;
		const float L = 7.4f, S = 4.6f, B = 3.4f;
		rg->spots = {
			{M::VOLUME_PARAM, {LR::VOLUME_X, LR::VOLUME_Y, L}}, {M::LOUD_PARAM, {LR::LOUD_X, LR::LOUD_Y, B}},
			{M::MODEL_PARAM, {LR::MODEL_X, LR::MODEL_Y, L}}, {M::BREAKUP_PARAM, {LR::BREAKUP_X, LR::BREAKUP_Y, S}},
			{M::SIZE_PARAM, {LR::SIZE_X, LR::SIZE_Y, L}}, {M::DAMP_PARAM, {LR::DAMP_X, LR::DAMP_Y, S}},
			{M::MODES_PARAM, {LR::MODES_X, LR::MODES_Y, S}}, {M::WALL_PARAM, {LR::WALL_X, LR::WALL_Y, B}},
			{M::DISTANCE_PARAM, {LR::DISTANCE_X, LR::DISTANCE_Y, L}}, {M::ANGLE_PARAM, {LR::ANGLE_X, LR::ANGLE_Y, S}},
			{M::PARTY_PARAM, {LR::PARTY_X, LR::PARTY_Y, S}}, {M::BYPASS_PARAM, {LR::BYPASS_X, LR::BYPASS_Y, B}},
		};
		addChild(rg);
	}
	void appendContextMenu(Menu* menu) override {
		RoomModule* m = dynamic_cast<RoomModule*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Presets", "", [=](Menu* sub) {
			int n = sizeof(ROOM_PRESETS) / sizeof(ROOM_PRESETS[0]);
			for (int i = 0; i < n; i++)
				sub->addChild(createMenuItem(string::f("%02d  %s", i + 1, ROOM_PRESETS[i].name), "", [=]() {
					chain::applyPreset(m, ROOM_PRESETS[i], {RM::BYPASS_PARAM, RM::A_DEST_PARAM, RM::A_AMT_PARAM, RM::B_DEST_PARAM, RM::B_AMT_PARAM});
				}));
		}));
	}
};

Model* modelListeningRoom = createModel<RoomModule, RoomWidget>("ListeningRoom");
