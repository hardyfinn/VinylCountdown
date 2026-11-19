// Vinyl Countdown: seven vinyl stages in one module, in record order.
//   LATHE -> RIAA cut -> PRESSING -> GROOVE -> STYLUS -> PLATTER -> RIAA play -> WRECK
// The middle stages sit inside the RIAA "sandwich", like a real record:
// surface noise and groove distortion happen with the treble lifted, then
// the playback curve darkens them again.
// Shared physics: RADIUS drives both the inner-groove loss and the size of
// the off-centre wow; DRIVE (the cut level) drives both the lathe and how
// hard the groove is to trace; RPM sets all three speeds.
// Latency: 0, plus 5 ms with PLATTER on, plus up to 100 ms on deck B in WRECK.
#include "plugin.hpp"
namespace LV = layout::VinylCountdown;

struct VinylModule : Module {
	enum ParamIds {
		LATHE_ON_PARAM, RIAA_ON_PARAM, PRESS_ON_PARAM, GROOVE_ON_PARAM, STYLUS_ON_PARAM, PLATTER_ON_PARAM, WRECK_ON_PARAM,
		DRIVE_PARAM, ELLIPTIC_PARAM, HF_PARAM, ECHO_PARAM,
		CUT_PARAM, PLAY_PARAM,
		GEN_PARAM, WEAR_PARAM,
		RADIUS_PARAM, TRACK_PARAM, SIDE_PARAM, SIDE_PLAY_PARAM,
		TIP_PARAM, LOAD_PARAM, AZIMUTH_PARAM, VERT_PARAM,
		OFFSET_PARAM, RPM_PARAM, BEATS_PARAM,
		DRIFT_PARAM, SLOP_PARAM, WOFFSET_PARAM, XFADE_PARAM, SYNC_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		DRIVE_INPUT, ECHO_INPUT, CUT_INPUT, PLAY_INPUT, GEN_INPUT, WEAR_INPUT, RADIUS_INPUT, SIDE_PLAY_INPUT,
		AZIMUTH_INPUT, VERT_INPUT, OFFSET_INPUT, XFADE_INPUT, SYNC_INPUT,
		IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, B_L_INPUT, B_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds { WOW_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };
	enum LightIds {
		LATHE_ON_LIGHT, RIAA_ON_LIGHT, PRESS_ON_LIGHT, GROOVE_ON_LIGHT, STYLUS_ON_LIGHT, PLATTER_ON_LIGHT, WRECK_ON_LIGHT,
		HF_LIGHT, SIDE_PLAY_LIGHT, REV_LIGHT, NUDGE_LIGHT,
		NUM_LIGHTS
	};

	vinyl::Lathe lathe;
	vinyl::RiaaPair riaa;
	vinyl::Pressing pressing;
	vinyl::InnerGroove groove;
	vinyl::Stylus stylus;
	vinyl::Platter platter;
	vinyl::Trainwreck wreck;

	dsp::SchmittTrigger clockTrig, sidePlayTrig, sidePlayBtn, syncTrig, syncBtn;
	float sinceClock = 0.f, clockPeriod = 0.f;
	bool sidePlaying = false;
	float sideProgress = 0.f;
	float revGlow = 0.f, nudgeGlow = 0.f;
	dsp::ClockDivider lightDiv;
	// right-click options
	bool movingCoil = false;
	bool wreckBoth = false;
	// for the screen
	int dispCut = 0, dispPlay = 0;

	VinylModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		const char* secs[7] = {"Lathe", "RIAA", "Pressing", "Groove", "Stylus", "Platter", "Wreck (deck B drift)"};
		const bool defOn[7] = {true, true, true, true, true, true, false};
		for (int i = 0; i < 7; i++)
			configSwitch(LATHE_ON_PARAM + i, 0.f, 1.f, defOn[i] ? 1.f : 0.f, string::f("%s section", secs[i]), {"Off", "On"});

		configParam(DRIVE_PARAM, 0.f, 1.f, 0.3f, "Drive / cut level (lathe saturation and how hard the groove is to trace)", "%", 0.f, 100.f);
		configParam(ELLIPTIC_PARAM, 0.f, 1.f, 0.f, "Elliptic filter: bass below this goes mono (0 = off)", " Hz", 15.f, 20.f);
		configParam(HF_PARAM, 0.f, 1.f, 0.f, "HF limiter (0 = off)", "%", 0.f, 100.f);
		configParam(ECHO_PARAM, 0.f, 1.f, 0.3f, "Groove echo (a dull ghost one revolution late)", "%", 0.f, 100.f);

		std::vector<std::string> curves(vinyl::CURVE_NAMES, vinyl::CURVE_NAMES + vinyl::NUM_CURVES);
		configSwitch(CUT_PARAM, 0.f, vinyl::NUM_CURVES - 1, 0.f, "Cut curve", curves);
		configSwitch(PLAY_PARAM, 0.f, vinyl::NUM_CURVES - 1, 0.f, "Play curve (different from CUT = tone tilt)", curves);

		configParam(GEN_PARAM, 0.f, 1.f, 0.3f, "Generation (lacquer .. late pressing)", "%", 0.f, 100.f);
		configParam(WEAR_PARAM, 0.f, 1.f, 0.f, "Wear (fresh dubplate .. played to death)", "%", 0.f, 100.f);

		configParam(RADIUS_PARAM, 0.f, 1.f, 0.3f, "Radius (0% outer edge, 100% label): darker toward the label, and more wow", "%", 0.f, 100.f);
		configParam(TRACK_PARAM, 0.f, 1.f, 0.5f, "Tracking force (light arm mistracks sooner)", "%", 0.f, 100.f);
		configParam(SIDE_PARAM, 0.f, 1.f, 0.5f, "Side length for PLAY", " min", 30.f, 1.f);
		configButton(SIDE_PLAY_PARAM, "Play the side (the needle travels to the label over SIDE)");

		configSwitch(TIP_PARAM, 0.f, 2.f, 0.f, "Stylus tip", {"Conical (darkest)", "Elliptical", "Shibata (finest)"});
		configParam(LOAD_PARAM, 0.f, 1.f, 0.4f, "Load capacitance (more = lower, peakier treble bump)", " pF", 0.f, 400.f, 100.f);
		configParam(AZIMUTH_PARAM, -1.f, 1.f, 0.f, "Azimuth (needle lean: one side late and duller, more crosstalk)", " deg", 0.f, 3.f);
		configParam(VERT_PARAM, 0.f, 1.f, 0.f, "Vertical mistracking (stereo width distorts first)", "%", 0.f, 100.f);

		configParam(OFFSET_PARAM, 0.f, 1.f, 0.3f, "Off-centre offset", " mm");
		configSwitch(RPM_PARAM, 0.f, 2.f, 0.f, "Speed", {"33 rpm", "45 rpm", "78 rpm"});
		configParam(BEATS_PARAM, 1.f, 8.f, 4.f, "Beats per revolution (when CLOCK is patched)");
		paramQuantities[BEATS_PARAM]->snapEnabled = true;

		configParam(DRIFT_PARAM, 0.f, 1.f, 0.4f, "Drift (how wrong deck B's pitch fader is)", "%", 0.f, 100.f);
		configParam(SLOP_PARAM, 0.f, 1.f, 0.4f, "Slop (how far apart the beats get before the DJ nudges)", " ms", 0.f, 40.f, 3.f);
		configParam(WOFFSET_PARAM, -50.f, 50.f, 0.f, "Offset (where in sync is; + = B late)", " ms");
		configParam(XFADE_PARAM, 0.f, 1.f, 0.5f, "Crossfader (record to deck B)", "%", 0.f, 100.f);
		configButton(SYNC_PARAM, "Nudge deck B back now");

		configInput(DRIVE_INPUT, "Drive CV (+10%/V)");
		configInput(ECHO_INPUT, "Echo CV (+10%/V)");
		configInput(CUT_INPUT, "Cut curve CV (+1 curve per 2 V)");
		configInput(PLAY_INPUT, "Play curve CV (+1 curve per 2 V)");
		configInput(GEN_INPUT, "Generation CV (+10%/V)");
		configInput(WEAR_INPUT, "Wear CV (+10%/V)");
		configInput(RADIUS_INPUT, "Radius CV (+10%/V)");
		configInput(SIDE_PLAY_INPUT, "Play side / stop (trigger toggles)");
		configInput(AZIMUTH_INPUT, "Azimuth CV (+-5 V = full lean)");
		configInput(VERT_INPUT, "Vertical CV (+10%/V)");
		configInput(OFFSET_INPUT, "Offset CV (+0.1 mm/V)");
		configInput(XFADE_INPUT, "Crossfader CV (+10%/V)");
		configInput(SYNC_INPUT, "Nudge deck B now (trigger)");
		configInput(IN_L_INPUT, "Left (or mono)");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configInput(CLOCK_INPUT, "Clock (one pulse per beat: a revolution = BEATS beats)");
		configInput(B_L_INPUT, "Deck B left (unpatched: the dry input, for flams against itself)");
		configInput(B_R_INPUT, "Deck B right (normalled to B left)");
		configOutput(WOW_OUTPUT, "Wow as pitch CV (1 V/oct)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		lightDiv.setDivision(64);
	}

	void setRates(float sr) {
		lathe.setSampleRate(sr); riaa.setSampleRate(sr); pressing.setSampleRate(sr); groove.setSampleRate(sr);
		stylus.setSampleRate(sr); platter.setSampleRate(sr); wreck.setSampleRate(sr);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { setRates(e.sampleRate); }
	void onAdd(const AddEvent& e) override { setRates(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		lathe.reset(); riaa.reset(); pressing.reset(); groove.reset(); stylus.reset(); platter.reset(); wreck.reset();
		sidePlaying = false; sideProgress = 0.f; movingCoil = false; wreckBoth = false;
	}

	bool on(int sec) { return params[LATHE_ON_PARAM + sec].getValue() > 0.5f; }
	float cv01(int p, int in, float scale = 0.1f) {
		return clamp(params[p].getValue() + inputs[in].getVoltage() * scale, 0.f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;
		int rpmIdx = clamp((int) std::round(params[RPM_PARAM].getValue()), 0, 2);
		float rpm = vinyl::RPM_VALUES[rpmIdx];

		// clock -> revolution length
		sinceClock += args.sampleTime;
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (sinceClock > 0.05f && sinceClock < 4.f) clockPeriod = sinceClock;
			sinceClock = 0.f;
		}
		if (sinceClock > 4.f) clockPeriod = 0.f;
		float revSec = (inputs[CLOCK_INPUT].isConnected() && clockPeriod > 0.f)
		               ? clockPeriod * params[BEATS_PARAM].getValue() : 60.f / rpm;

		// side play
		if (sidePlayBtn.process(params[SIDE_PLAY_PARAM].getValue()) || sidePlayTrig.process(inputs[SIDE_PLAY_INPUT].getVoltage(), 0.1f, 1.f)) {
			sidePlaying = !sidePlaying;
			if (sidePlaying && sideProgress >= 1.f) sideProgress = 0.f;
		}
		if (sidePlaying) {
			sideProgress += args.sampleTime / (60.f * std::pow(30.f, params[SIDE_PARAM].getValue()));
			if (sideProgress >= 1.f) { sideProgress = 1.f; sidePlaying = false; }
		}
		float radius01 = clamp(params[RADIUS_PARAM].getValue() + inputs[RADIUS_INPUT].getVoltage() * 0.1f + sideProgress, 0.f, 1.f);
		float radiusMm = vinyl::R_OUTER - radius01 * (vinyl::R_OUTER - vinyl::R_INNER);
		float drive = cv01(DRIVE_PARAM, DRIVE_INPUT);
		int tip = clamp((int) std::round(params[TIP_PARAM].getValue()), 0, 2);

		float dryL = inputs[IN_L_INPUT].getVoltage();
		float dryR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : dryL;
		float l = dryL, r = dryR;

		// LATHE
		if (on(0)) {
			float ell = clamp(params[ELLIPTIC_PARAM].getValue(), 0.f, 1.f);
			float ellHz = ell < 0.01f ? 0.f : 20.f * std::pow(15.f, ell);
			float wl, wr;
			lathe.process(l, r, drive, ellHz, params[HF_PARAM].getValue(), cv01(ECHO_PARAM, ECHO_INPUT), 1, revSec, l, r, wl, wr);
		}
		// RIAA cut
		int cut = clamp((int) std::round(params[CUT_PARAM].getValue() + inputs[CUT_INPUT].getVoltage() * 0.5f), 0, vinyl::NUM_CURVES - 1);
		int play = clamp((int) std::round(params[PLAY_PARAM].getValue() + inputs[PLAY_INPUT].getVoltage() * 0.5f), 0, vinyl::NUM_CURVES - 1);
		bool riaaOn = on(1);
		if (riaaOn) {
			riaa.set(cut, play);
			l = riaa.cut[0].process(l) * 0.5f;
			r = riaa.cut[1].process(r) * 0.5f;
		}
		// PRESSING
		if (on(2)) pressing.process(l, r, cv01(GEN_PARAM, GEN_INPUT), cv01(WEAR_PARAM, WEAR_INPUT), l, r);
		// GROOVE
		if (on(3)) groove.process(l, r, radius01, rpm, tip, params[TRACK_PARAM].getValue(), drive, l, r);
		// STYLUS
		if (on(4)) {
			float az = clamp(params[AZIMUTH_PARAM].getValue() + inputs[AZIMUTH_INPUT].getVoltage() * 0.2f, -1.f, 1.f);
			float xt = 0.25f + 0.55f * std::fabs(az);   // a leaning needle also leaks more between channels
			stylus.process(l, r, movingCoil, params[LOAD_PARAM].getValue(), xt, az, cv01(VERT_PARAM, VERT_INPUT), l, r);
		}
		// PLATTER
		bool revTick = false;
		if (on(5)) {
			float off = cv01(OFFSET_PARAM, OFFSET_INPUT);
			platter.process(l, r, off, radiusMm, rpm, l, r);
			revTick = platter.revTick;
			outputs[WOW_OUTPUT].setVoltage(platter.wowVolts());
		}
		else outputs[WOW_OUTPUT].setVoltage(0.f);
		// RIAA play
		if (riaaOn) {
			l = riaa.play[0].process(l * 2.f);
			r = riaa.play[1].process(r * 2.f);
		}
		// WRECK
		if (on(6)) {
			bool sync = syncBtn.process(params[SYNC_PARAM].getValue());
			if (syncTrig.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 1.f)) sync = true;
			float a[2] = {l, r}, b[2], m[2], bo[2];
			if (inputs[B_L_INPUT].isConnected()) {
				b[0] = inputs[B_L_INPUT].getVoltage();
				b[1] = inputs[B_R_INPUT].isConnected() ? inputs[B_R_INPUT].getVoltage() : b[0];
			}
			else { b[0] = dryL; b[1] = dryR; }
			float manual = params[WOFFSET_PARAM].getValue();
			if (!wreckBoth) manual = std::max(manual, 0.f);
			wreck.process(a, b, params[DRIFT_PARAM].getValue(), params[SLOP_PARAM].getValue(), manual, wreckBoth,
			              cv01(XFADE_PARAM, XFADE_INPUT), sync, m, bo);
			l = m[0]; r = m[1];
			if (wreck.nudgeFired) nudgeGlow = 1.f;
		}
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(l));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(r));

		if (revTick) revGlow = 1.f;
		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int i = 0; i < 7; i++) lights[LATHE_ON_LIGHT + i].setBrightness(on(i) ? 1.f : 0.f);
			lights[HF_LIGHT].setBrightness(on(0) ? clamp((1.f - lathe.hfGain) * 3.f, 0.f, 1.f) : 0.f);
			lights[SIDE_PLAY_LIGHT].setBrightness(sidePlaying ? 1.f : 0.f);
			lights[REV_LIGHT].setBrightness(revGlow);
			lights[NUDGE_LIGHT].setBrightness(std::max(nudgeGlow, on(6) && wreck.nudgeLeft > 0.f ? 0.7f : 0.f));
			revGlow = std::max(0.f, revGlow - dt * 6.f);
			nudgeGlow = std::max(0.f, nudgeGlow - dt * 4.f);
		}
		dispCut = cut; dispPlay = play;
		(void) sr;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "movingCoil", json_boolean(movingCoil));
		json_object_set_new(root, "wreckBoth", json_boolean(wreckBoth));
		json_object_set_new(root, "sidePlaying", json_boolean(sidePlaying));
		json_object_set_new(root, "sideProgress", json_real(sideProgress));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "movingCoil"))) movingCoil = json_boolean_value(j);
		if ((j = json_object_get(root, "wreckBoth"))) wreckBoth = json_boolean_value(j);
		if ((j = json_object_get(root, "sidePlaying"))) sidePlaying = json_boolean_value(j);
		if ((j = json_object_get(root, "sideProgress"))) sideProgress = json_number_value(j);
	}
};

struct RiaaMini : VinylScreen {
	VinylModule* module = nullptr;
	void lines(std::vector<std::string>& out) override {
		int c = module ? module->dispCut : 0, p = module ? module->dispPlay : 1;
		out.push_back(vinyl::CURVE_SHORT[c]);
		out.push_back(std::string("> ") + vinyl::CURVE_SHORT[p]);
	}
};

template <class T>
static void knobAt(ModuleWidget* w, Module* m, float x, float y, int id) {
	w->addParam(createParamCentered<T>(P(x, y), m, id));
}

struct VinylWidget : ModuleWidget {
	VinylWidget(VinylModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/VinylCountdown.svg")));
		addScrews(this);
		typedef VinylModule M;
		const float onX[7] = {LV::LATHE_ON_X, LV::RIAA_ON_X, LV::PRESS_ON_X, LV::GROOVE_ON_X, LV::STYLUS_ON_X, LV::PLATTER_ON_X, LV::WRECK_ON_X};
		for (int i = 0; i < 7; i++)
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(onX[i], LV::LATHE_ON_Y), module, M::LATHE_ON_PARAM + i, M::LATHE_ON_LIGHT + i));

		knobAt<RoundSmallBlackKnob>(this, module, LV::DRIVE_X, LV::DRIVE_Y, M::DRIVE_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::ELLIPTIC_X, LV::ELLIPTIC_Y, M::ELLIPTIC_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::HF_X, LV::HF_Y, M::HF_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::ECHO_X, LV::ECHO_Y, M::ECHO_PARAM);
		addChild(createLightCentered<SmallLight<YellowLight>>(P(LV::HF_LIGHT_X, LV::HF_LIGHT_Y), module, M::HF_LIGHT));

		knobAt<RoundLargeBlackKnob>(this, module, LV::CUT_X, LV::CUT_Y, M::CUT_PARAM);
		knobAt<RoundLargeBlackKnob>(this, module, LV::PLAY_X, LV::PLAY_Y, M::PLAY_PARAM);
		RiaaMini* s = createWidget<RiaaMini>(P(LV::RIAA_DISP_X, LV::RIAA_DISP_Y));
		s->box.size = mm2px(Vec(LV::RIAA_DISP_W, LV::RIAA_DISP_H));
		s->module = module;
		s->accent = nvgRGB(127, 184, 164);
		s->fontMm = 2.9f;
		addChild(s);

		knobAt<RoundLargeBlackKnob>(this, module, LV::GEN_X, LV::GEN_Y, M::GEN_PARAM);
		knobAt<RoundLargeBlackKnob>(this, module, LV::WEAR_X, LV::WEAR_Y, M::WEAR_PARAM);

		knobAt<RoundLargeBlackKnob>(this, module, LV::RADIUS_X, LV::RADIUS_Y, M::RADIUS_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::TRACK_X, LV::TRACK_Y, M::TRACK_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::SIDE_X, LV::SIDE_Y, M::SIDE_PARAM);
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(P(LV::SIDE_PLAY_X, LV::SIDE_PLAY_Y), module, M::SIDE_PLAY_PARAM, M::SIDE_PLAY_LIGHT));

		addParam(createParamCentered<CKSSThree>(P(LV::TIP_X, LV::TIP_Y), module, M::TIP_PARAM));
		knobAt<RoundSmallBlackKnob>(this, module, LV::LOAD_X, LV::LOAD_Y, M::LOAD_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::AZIMUTH_X, LV::AZIMUTH_Y, M::AZIMUTH_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::VERT_X, LV::VERT_Y, M::VERT_PARAM);

		knobAt<RoundLargeBlackKnob>(this, module, LV::OFFSET_X, LV::OFFSET_Y, M::OFFSET_PARAM);
		addParam(createParamCentered<CKSSThree>(P(LV::RPM_X, LV::RPM_Y), module, M::RPM_PARAM));
		knobAt<RoundSmallBlackKnob>(this, module, LV::BEATS_X, LV::BEATS_Y, M::BEATS_PARAM);
		addChild(createLightCentered<SmallLight<YellowLight>>(P(LV::REV_LIGHT_X, LV::REV_LIGHT_Y), module, M::REV_LIGHT));

		knobAt<RoundSmallBlackKnob>(this, module, LV::DRIFT_X, LV::DRIFT_Y, M::DRIFT_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::SLOP_X, LV::SLOP_Y, M::SLOP_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::WOFFSET_X, LV::WOFFSET_Y, M::WOFFSET_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::XFADE_X, LV::XFADE_Y, M::XFADE_PARAM);
		addParam(createParamCentered<VCVButton>(P(LV::SYNC_X, LV::SYNC_Y), module, M::SYNC_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(P(LV::NUDGE_LIGHT_X, LV::NUDGE_LIGHT_Y), module, M::NUDGE_LIGHT));

		struct J { float x, y; int id; bool out; };
		const J jacks[] = {
			{LV::DRIVE_IN_X, LV::DRIVE_IN_Y, M::DRIVE_INPUT, false}, {LV::ECHO_IN_X, LV::ECHO_IN_Y, M::ECHO_INPUT, false},
			{LV::CUT_IN_X, LV::CUT_IN_Y, M::CUT_INPUT, false}, {LV::PLAY_IN_X, LV::PLAY_IN_Y, M::PLAY_INPUT, false},
			{LV::GEN_IN_X, LV::GEN_IN_Y, M::GEN_INPUT, false}, {LV::WEAR_IN_X, LV::WEAR_IN_Y, M::WEAR_INPUT, false},
			{LV::RADIUS_IN_X, LV::RADIUS_IN_Y, M::RADIUS_INPUT, false}, {LV::SIDE_PLAY_IN_X, LV::SIDE_PLAY_IN_Y, M::SIDE_PLAY_INPUT, false},
			{LV::AZIMUTH_IN_X, LV::AZIMUTH_IN_Y, M::AZIMUTH_INPUT, false}, {LV::VERT_IN_X, LV::VERT_IN_Y, M::VERT_INPUT, false},
			{LV::OFFSET_IN_X, LV::OFFSET_IN_Y, M::OFFSET_INPUT, false}, {LV::WOW_OUT_X, LV::WOW_OUT_Y, M::WOW_OUTPUT, true},
			{LV::XFADE_IN_X, LV::XFADE_IN_Y, M::XFADE_INPUT, false}, {LV::SYNC_IN_X, LV::SYNC_IN_Y, M::SYNC_INPUT, false},
			{LV::IN_L_X, LV::IN_L_Y, M::IN_L_INPUT, false}, {LV::IN_R_X, LV::IN_R_Y, M::IN_R_INPUT, false},
			{LV::CLOCK_IN_X, LV::CLOCK_IN_Y, M::CLOCK_INPUT, false},
			{LV::B_L_X, LV::B_L_Y, M::B_L_INPUT, false}, {LV::B_R_X, LV::B_R_Y, M::B_R_INPUT, false},
			{LV::OUT_L_X, LV::OUT_L_Y, M::OUT_L_OUTPUT, true}, {LV::OUT_R_X, LV::OUT_R_Y, M::OUT_R_OUTPUT, true},
		};
		for (const J& j : jacks) {
			if (j.out) addOutput(createOutputCentered<PJ301MPort>(P(j.x, j.y), module, j.id));
			else addInput(createInputCentered<PJ301MPort>(P(j.x, j.y), module, j.id));
		}
	}

	void appendContextMenu(Menu* menu) override {
		VinylModule* m = dynamic_cast<VinylModule*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Stylus: moving-coil cartridge (flat, bright; LOAD has no effect)", "", &m->movingCoil));
		menu->addChild(createBoolPtrMenuItem("Wreck: deck B can also run ahead (both decks 50 ms back)", "", &m->wreckBoth));
		menu->addChild(createMenuItem("Groove: back to the outer edge", "", [=]() { m->sideProgress = 0.f; }));
	}
};

Model* modelVinylCountdown = createModel<VinylModule, VinylWidget>("VinylCountdown");
