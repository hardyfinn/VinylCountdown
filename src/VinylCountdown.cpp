// Vinyl Countdown: eight vinyl stages in one module, in record order.
//   LATHE -> RIAA cut -> PRESSING -> GROOVE -> STYLUS -> PLATTER -> RIAA play -> PREAMP -> WRECK
// Two assignable CV inputs (A, B) can drive any knob, button or the bypass.
// The middle stages sit inside the RIAA "sandwich", like a real record:
// surface noise and groove distortion happen with the treble lifted, then
// the playback curve darkens them again.
// Shared physics: RADIUS drives both the inner-groove loss and the size of
// the off-centre wow; DRIVE (the cut level) drives both the lathe and how
// hard the groove is to trace; RPM sets all three speeds.
// Latency: 0, plus 6 ms with PLATTER on, plus up to 100 ms on deck B in WRECK.
#include "plugin.hpp"
namespace LV = layout::VinylCountdown;

// ---- assignable CV destinations -------------------------------------------
enum DestKind { DK_NONE, DK_KNOB, DK_TRIG, DK_GATE };
struct Dest { const char* menu; const char* screen; int kind; int param; float perVolt; };
enum DestId {
	D_OFF, D_DRIVE, D_ELLIPTIC, D_HF, D_ECHO, D_CUT, D_PLAY, D_GEN, D_WEAR, D_RADIUS, D_TRACK, D_SIDE, D_SIDE_PLAY,
	D_LOAD, D_AZIMUTH, D_VERT, D_OFFSET, D_VALVE, D_XFMR, D_HUM, D_DRIFT, D_SLOP, D_WOFFSET, D_XFADE, D_SYNC, D_BYPASS,
	NUM_DESTS
};

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
		// 2.2.0 (appended so older patches keep their knob settings)
		PREAMP_ON_PARAM, VALVE_PARAM, XFMR_PARAM, HUM_PARAM, HZ_PARAM, GND_PARAM,
		BYPASS_PARAM, A_DEST_PARAM, A_AMT_PARAM, B_DEST_PARAM, B_AMT_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		// the first 13 are the 2.1 per-section CV jacks; no longer on the panel
		DRIVE_INPUT, ECHO_INPUT, CUT_INPUT, PLAY_INPUT, GEN_INPUT, WEAR_INPUT, RADIUS_INPUT, SIDE_PLAY_INPUT,
		AZIMUTH_INPUT, VERT_INPUT, OFFSET_INPUT, XFADE_INPUT, SYNC_INPUT,
		IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, B_L_INPUT, B_R_INPUT,
		CVA_INPUT, CVB_INPUT,
		NUM_INPUTS
	};
	enum OutputIds { WOW_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, NUM_OUTPUTS };

	// perVolt: knob units added per volt at AMOUNT 100% (10 V = the whole knob)
	static const Dest& DEST(int i) {
		static const Dest t[NUM_DESTS] = {
			{"Off", "--", DK_NONE, -1, 0.f},
			{"Lathe: drive", "LATHE DRIVE", DK_KNOB, DRIVE_PARAM, 0.1f},
			{"Lathe: elliptic", "LATHE ELLIPTIC", DK_KNOB, ELLIPTIC_PARAM, 0.1f},
			{"Lathe: HF limit", "LATHE HF LIMIT", DK_KNOB, HF_PARAM, 0.1f},
			{"Lathe: echo", "LATHE ECHO", DK_KNOB, ECHO_PARAM, 0.1f},
			{"RIAA: cut curve", "RIAA CUT", DK_KNOB, CUT_PARAM, 0.4f},
			{"RIAA: play curve", "RIAA PLAY", DK_KNOB, PLAY_PARAM, 0.4f},
			{"Pressing: generation", "PRESS GENERATION", DK_KNOB, GEN_PARAM, 0.1f},
			{"Pressing: wear", "PRESS WEAR", DK_KNOB, WEAR_PARAM, 0.1f},
			{"Groove: radius", "GROOVE RADIUS", DK_KNOB, RADIUS_PARAM, 0.1f},
			{"Groove: track", "GROOVE TRACK", DK_KNOB, TRACK_PARAM, 0.1f},
			{"Groove: side length", "GROOVE SIDE", DK_KNOB, SIDE_PARAM, 0.1f},
			{"Groove: play side (trigger)", "GROOVE PLAY TRIG", DK_TRIG, SIDE_PLAY_PARAM, 0.f},
			{"Stylus: load", "STYLUS LOAD", DK_KNOB, LOAD_PARAM, 0.1f},
			{"Stylus: azimuth", "STYLUS AZIMUTH", DK_KNOB, AZIMUTH_PARAM, 0.2f},
			{"Stylus: vertical", "STYLUS VERTICAL", DK_KNOB, VERT_PARAM, 0.1f},
			{"Platter: offset", "PLATTER OFFSET", DK_KNOB, OFFSET_PARAM, 0.1f},
			{"Preamp: valve", "PREAMP VALVE", DK_KNOB, VALVE_PARAM, 0.1f},
			{"Preamp: step-up", "PREAMP STEP-UP", DK_KNOB, XFMR_PARAM, 0.1f},
			{"Preamp: hum", "PREAMP HUM", DK_KNOB, HUM_PARAM, 0.1f},
			{"Wreck: drift", "WRECK DRIFT", DK_KNOB, DRIFT_PARAM, 0.1f},
			{"Wreck: slop", "WRECK SLOP", DK_KNOB, SLOP_PARAM, 0.1f},
			{"Wreck: offset", "WRECK OFFSET", DK_KNOB, WOFFSET_PARAM, 10.f},
			{"Wreck: crossfader", "WRECK XFADE", DK_KNOB, XFADE_PARAM, 0.1f},
			{"Wreck: nudge (trigger)", "WRECK SYNC TRIG", DK_TRIG, SYNC_PARAM, 0.f},
			{"Bypass (gate)", "BYPASS GATE", DK_GATE, BYPASS_PARAM, 0.f},
		};
		return t[clamp(i, 0, NUM_DESTS - 1)];
	}
	enum LightIds {
		LATHE_ON_LIGHT, RIAA_ON_LIGHT, PRESS_ON_LIGHT, GROOVE_ON_LIGHT, STYLUS_ON_LIGHT, PLATTER_ON_LIGHT, WRECK_ON_LIGHT,
		HF_LIGHT, SIDE_PLAY_LIGHT, REV_LIGHT, NUDGE_LIGHT,
		PREAMP_ON_LIGHT, GND_LIGHT, BYPASS_LIGHT,
		NUM_LIGHTS
	};

	vinyl::Lathe lathe;
	vinyl::RiaaPair riaa;
	vinyl::Pressing pressing;
	vinyl::InnerGroove groove;
	vinyl::Stylus stylus;
	vinyl::Platter platter;
	vinyl::Trainwreck wreck;
	vinyl::Preamp preamp;

	// sections in chain order -> their on/off switch and light
	static constexpr int NSEC = 8;
	int secParam(int i) const { return i == 6 ? PREAMP_ON_PARAM : (i == 7 ? WRECK_ON_PARAM : LATHE_ON_PARAM + i); }
	int secLight(int i) const { return i == 6 ? PREAMP_ON_LIGHT : (i == 7 ? WRECK_ON_LIGHT : LATHE_ON_LIGHT + i); }

	// assignable CV
	int destA = 0, destB = 0;
	float cvA = 0.f, cvB = 0.f;      // volts x amount
	dsp::SchmittTrigger trigA, trigB;
	float bypassFade = 0.f;

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
		const char* secs[NSEC] = {"Lathe", "RIAA", "Pressing", "Groove", "Stylus", "Platter", "Preamp", "Wreck (deck B drift)"};
		const bool defOn[NSEC] = {true, true, true, true, true, true, true, false};
		for (int i = 0; i < NSEC; i++)
			configSwitch(secParam(i), 0.f, 1.f, defOn[i] ? 1.f : 0.f, string::f("%s section", secs[i]), {"Off", "On"});

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

		configParam(OFFSET_PARAM, 0.f, 1.f, 0.3f, "Off-centre offset", " mm", 0.f, 1.2f);  // knob 0..1 = 0..1.2 mm (2.1.1: +20%)
		configSwitch(RPM_PARAM, 0.f, 2.f, 0.f, "Speed", {"33 rpm", "45 rpm", "78 rpm"});
		configParam(BEATS_PARAM, 1.f, 8.f, 4.f, "Beats per revolution (when CLOCK is patched)");
		paramQuantities[BEATS_PARAM]->snapEnabled = true;

		configParam(DRIFT_PARAM, 0.f, 1.f, 0.4f, "Drift (how wrong deck B's pitch fader is)", "%", 0.f, 100.f);
		configParam(SLOP_PARAM, 0.f, 1.f, 0.4f, "Slop (how far apart the beats get before the DJ nudges)", " ms", 0.f, 40.f, 3.f);
		configParam(WOFFSET_PARAM, -50.f, 50.f, 0.f, "Offset (where in sync is; + = B late)", " ms");
		configParam(XFADE_PARAM, 0.f, 1.f, 0.5f, "Crossfader (record to deck B)", "%", 0.f, 100.f);
		configButton(SYNC_PARAM, "Nudge deck B back now");

		configParam(VALVE_PARAM, 0.f, 1.f, 0.25f, "Valve drive (12AX7-style, even harmonics)", "%", 0.f, 100.f);
		configParam(XFMR_PARAM, 0.f, 1.f, 0.2f, "Step-up transformer (bass saturates, top rolls off)", "%", 0.f, 100.f);
		configParam(HUM_PARAM, 0.f, 1.f, 0.f, "Mains hum", "%", 0.f, 100.f);
		configSwitch(HZ_PARAM, 0.f, 1.f, 0.f, "Mains frequency", {"50 Hz (UK/Europe)", "60 Hz (US/Japan)"});
		configSwitch(GND_PARAM, 0.f, 1.f, 0.f, "Ground lead", {"Connected", "Lifted (hum and buzz)"});
		configSwitch(BYPASS_PARAM, 0.f, 1.f, 0.f, "Bypass (dry input straight to the outputs, no delay)", {"Off", "Bypassed"});
		paramQuantities[BYPASS_PARAM]->randomizeEnabled = false;
		std::vector<std::string> dn;
		for (int i = 0; i < NUM_DESTS; i++) dn.push_back(DEST(i).menu);
		configSwitch(A_DEST_PARAM, 0.f, NUM_DESTS - 1, 0.f, "CV A destination", dn);
		configParam(A_AMT_PARAM, -1.f, 1.f, 0.5f, "CV A amount", "%", 0.f, 100.f);
		configSwitch(B_DEST_PARAM, 0.f, NUM_DESTS - 1, 0.f, "CV B destination", dn);
		configParam(B_AMT_PARAM, -1.f, 1.f, 0.5f, "CV B amount", "%", 0.f, 100.f);
		configInput(CVA_INPUT, "CV A (10 V = full knob range at 100%; triggers and gates above 1 V)");
		configInput(CVB_INPUT, "CV B (10 V = full knob range at 100%; triggers and gates above 1 V)");
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
		stylus.setSampleRate(sr); platter.setSampleRate(sr); wreck.setSampleRate(sr); preamp.setSampleRate(sr);
	}
	void onSampleRateChange(const SampleRateChangeEvent& e) override { setRates(e.sampleRate); }
	void onAdd(const AddEvent& e) override { setRates(APP->engine->getSampleRate()); }
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		lathe.reset(); riaa.reset(); pressing.reset(); groove.reset(); stylus.reset(); platter.reset(); wreck.reset(); preamp.reset();
		bypassFade = 0.f;
		sidePlaying = false; sideProgress = 0.f; movingCoil = false; wreckBoth = false;
	}

	bool on(int sec) { return params[secParam(sec)].getValue() > 0.5f; }
	// knob value plus whatever CV A / CV B add to it
	float mod(int p) {
		float m = 0.f;
		if (DEST(destA).kind == DK_KNOB && DEST(destA).param == p) m += cvA * DEST(destA).perVolt;
		if (DEST(destB).kind == DK_KNOB && DEST(destB).param == p) m += cvB * DEST(destB).perVolt;
		return m;
	}
	float pv(int p, float lo = 0.f, float hi = 1.f) { return clamp(params[p].getValue() + mod(p), lo, hi); }

	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;
		// assignable CV
		destA = clamp((int) std::round(params[A_DEST_PARAM].getValue()), 0, NUM_DESTS - 1);
		destB = clamp((int) std::round(params[B_DEST_PARAM].getValue()), 0, NUM_DESTS - 1);
		cvA = inputs[CVA_INPUT].getVoltage() * params[A_AMT_PARAM].getValue();
		cvB = inputs[CVB_INPUT].getVoltage() * params[B_AMT_PARAM].getValue();
		bool firedA = trigA.process(cvA, 0.1f, 1.f), firedB = trigB.process(cvB, 0.1f, 1.f);
		auto fired = [&](int dk) { return (firedA && destA == dk) || (firedB && destB == dk); };
		bool gateBypass = (destA == D_BYPASS && cvA > 1.f) || (destB == D_BYPASS && cvB > 1.f);
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
		if (sidePlayBtn.process(params[SIDE_PLAY_PARAM].getValue()) || fired(D_SIDE_PLAY)) {
			sidePlaying = !sidePlaying;
			if (sidePlaying && sideProgress >= 1.f) sideProgress = 0.f;
		}
		if (sidePlaying) {
			sideProgress += args.sampleTime / (60.f * std::pow(30.f, pv(SIDE_PARAM)));
			if (sideProgress >= 1.f) { sideProgress = 1.f; sidePlaying = false; }
		}
		float radius01 = clamp(pv(RADIUS_PARAM) + sideProgress, 0.f, 1.f);
		float radiusMm = vinyl::R_OUTER - radius01 * (vinyl::R_OUTER - vinyl::R_INNER);
		float drive = pv(DRIVE_PARAM);
		int tip = clamp((int) std::round(params[TIP_PARAM].getValue()), 0, 2);

		float dryL = inputs[IN_L_INPUT].getVoltage();
		float dryR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : dryL;
		float l = dryL, r = dryR;

		// LATHE
		if (on(0)) {
			float ell = pv(ELLIPTIC_PARAM);
			float ellHz = ell < 0.01f ? 0.f : 20.f * std::pow(15.f, ell);
			float wl, wr;
			lathe.process(l, r, drive, ellHz, pv(HF_PARAM), pv(ECHO_PARAM), 1, revSec, l, r, wl, wr);
		}
		// RIAA cut
		int cut = (int) std::round(pv(CUT_PARAM, 0.f, vinyl::NUM_CURVES - 1));
		int play = (int) std::round(pv(PLAY_PARAM, 0.f, vinyl::NUM_CURVES - 1));
		bool riaaOn = on(1);
		if (riaaOn) {
			riaa.set(cut, play);
			l = riaa.cut[0].process(l) * 0.5f;
			r = riaa.cut[1].process(r) * 0.5f;
		}
		// PRESSING
		if (on(2)) pressing.process(l, r, pv(GEN_PARAM), pv(WEAR_PARAM), l, r);
		// GROOVE
		if (on(3)) groove.process(l, r, radius01, rpm, tip, pv(TRACK_PARAM), drive, l, r);
		// STYLUS
		if (on(4)) {
			float az = pv(AZIMUTH_PARAM, -1.f, 1.f);
			float xt = 0.25f + 0.55f * std::fabs(az);   // a leaning needle also leaks more between channels
			stylus.process(l, r, movingCoil, pv(LOAD_PARAM), xt, az, pv(VERT_PARAM), l, r);
		}
		// PLATTER
		bool revTick = false;
		if (on(5)) {
			float off = 1.2f * pv(OFFSET_PARAM);
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
		// PREAMP
		if (on(6))
			preamp.process(l, r, pv(VALVE_PARAM), pv(XFMR_PARAM), pv(HUM_PARAM), params[HZ_PARAM].getValue() > 0.5f,
			               params[GND_PARAM].getValue() > 0.5f, l, r);
		// WRECK
		if (on(7)) {
			bool sync = syncBtn.process(params[SYNC_PARAM].getValue());
			if (fired(D_SYNC)) sync = true;
			float a[2] = {l, r}, b[2], m[2], bo[2];
			if (inputs[B_L_INPUT].isConnected()) {
				b[0] = inputs[B_L_INPUT].getVoltage();
				b[1] = inputs[B_R_INPUT].isConnected() ? inputs[B_R_INPUT].getVoltage() : b[0];
			}
			else { b[0] = dryL; b[1] = dryR; }
			float manual = pv(WOFFSET_PARAM, -50.f, 50.f);
			if (!wreckBoth) manual = std::max(manual, 0.f);
			wreck.process(a, b, pv(DRIFT_PARAM), pv(SLOP_PARAM), manual, wreckBoth,
			              pv(XFADE_PARAM), sync, m, bo);
			l = m[0]; r = m[1];
			if (wreck.nudgeFired) nudgeGlow = 1.f;
		}
		// BYPASS: button XOR a gate on CV A/B; 10 ms crossfade to the dry input
		bool byp = (params[BYPASS_PARAM].getValue() > 0.5f) != gateBypass;
		bypassFade += (byp ? 1.f : -1.f) * args.sampleTime / 0.01f;
		bypassFade = clamp(bypassFade, 0.f, 1.f);
		l = l + (dryL - l) * bypassFade;
		r = r + (dryR - r) * bypassFade;
		outputs[OUT_L_OUTPUT].setVoltage(softLimit(l));
		outputs[OUT_R_OUTPUT].setVoltage(softLimit(r));

		if (revTick) revGlow = 1.f;
		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int i = 0; i < NSEC; i++) lights[secLight(i)].setBrightness(on(i) ? 1.f : 0.f);
			lights[GND_LIGHT].setBrightness(params[GND_PARAM].getValue() > 0.5f ? 1.f : 0.f);
			lights[BYPASS_LIGHT].setBrightness(bypassFade);
			lights[HF_LIGHT].setBrightness(on(0) ? clamp((1.f - lathe.hfGain) * 3.f, 0.f, 1.f) : 0.f);
			lights[SIDE_PLAY_LIGHT].setBrightness(sidePlaying ? 1.f : 0.f);
			lights[REV_LIGHT].setBrightness(revGlow);
			lights[NUDGE_LIGHT].setBrightness(std::max(nudgeGlow, on(7) && wreck.nudgeLeft > 0.f ? 0.7f : 0.f));
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

// ---- presets (right-click > Presets) ---------------------------------------
// Each starts from the default settings, then changes only what is listed.
// CV A/B routing and BYPASS are left as they are.
typedef VinylModule VM;
struct PresetValue { int param; float value; };
struct Preset { const char* name; bool movingCoil; std::vector<PresetValue> values; };
static const Preset PRESETS[] = {
	{"Fresh pressing", false, {{VM::GEN_PARAM, 0.1f}, {VM::WEAR_PARAM, 0.0f}, {VM::RADIUS_PARAM, 0.15f}, {VM::TIP_PARAM, 1.0f}, {VM::VALVE_PARAM, 0.15f}, {VM::XFMR_PARAM, 0.1f}, {VM::ECHO_PARAM, 0.15f}}},
	{"Dusty crate find", false, {{VM::GEN_PARAM, 0.65f}, {VM::WEAR_PARAM, 0.45f}, {VM::RADIUS_PARAM, 0.5f}, {VM::TIP_PARAM, 0.0f}, {VM::TRACK_PARAM, 0.4f}, {VM::OFFSET_PARAM, 0.4f}, {VM::VALVE_PARAM, 0.3f}}},
	{"Dubplate, 50 plays", false, {{VM::GEN_PARAM, 0.0f}, {VM::WEAR_PARAM, 0.75f}, {VM::DRIVE_PARAM, 0.6f}, {VM::ECHO_PARAM, 0.4f}, {VM::RADIUS_PARAM, 0.3f}, {VM::TIP_PARAM, 1.0f}}},
	{"Inner groove blues", false, {{VM::RADIUS_PARAM, 0.95f}, {VM::TIP_PARAM, 0.0f}, {VM::TRACK_PARAM, 0.3f}, {VM::DRIVE_PARAM, 0.6f}, {VM::VERT_PARAM, 0.3f}}},
	{"Hot 12-inch club cut", false, {{VM::RPM_PARAM, 1.0f}, {VM::RADIUS_PARAM, 0.05f}, {VM::TIP_PARAM, 2.0f}, {VM::DRIVE_PARAM, 0.55f}, {VM::ELLIPTIC_PARAM, 0.5f}, {VM::HF_PARAM, 0.35f}, {VM::GEN_PARAM, 0.15f}, {VM::VALVE_PARAM, 0.2f}}},
	{"78 shellac", false, {{VM::RPM_PARAM, 2.0f}, {VM::CUT_PARAM, 3.0f}, {VM::PLAY_PARAM, 0.0f}, {VM::GEN_PARAM, 0.8f}, {VM::WEAR_PARAM, 0.7f}, {VM::TIP_PARAM, 0.0f}, {VM::TRACK_PARAM, 0.7f}, {VM::LOAD_PARAM, 0.8f}, {VM::VALVE_PARAM, 0.5f}, {VM::XFMR_PARAM, 0.3f}, {VM::HUM_PARAM, 0.15f}, {VM::ELLIPTIC_PARAM, 1.0f}}},
	{"Wrong EQ: Columbia LP on RIAA", false, {{VM::CUT_PARAM, 1.0f}, {VM::PLAY_PARAM, 0.0f}, {VM::GEN_PARAM, 0.3f}}},
	{"Seasick warp", false, {{VM::OFFSET_PARAM, 1.0f}, {VM::RADIUS_PARAM, 0.9f}, {VM::GEN_PARAM, 0.4f}}},
	{"Bedroom DJ trainwreck", false, {{VM::WRECK_ON_PARAM, 1.0f}, {VM::DRIFT_PARAM, 0.75f}, {VM::SLOP_PARAM, 0.8f}, {VM::XFADE_PARAM, 0.5f}}},
	{"Tight mix, slight flam", false, {{VM::WRECK_ON_PARAM, 1.0f}, {VM::DRIFT_PARAM, 0.15f}, {VM::SLOP_PARAM, 0.1f}, {VM::XFADE_PARAM, 0.5f}, {VM::WOFFSET_PARAM, 8.0f}}},
	{"Valve warmth", false, {{VM::VALVE_PARAM, 0.7f}, {VM::XFMR_PARAM, 0.5f}, {VM::GEN_PARAM, 0.2f}, {VM::WEAR_PARAM, 0.0f}, {VM::RADIUS_PARAM, 0.2f}, {VM::TIP_PARAM, 1.0f}}},
	{"Cutting room, too hot", false, {{VM::DRIVE_PARAM, 0.9f}, {VM::HF_PARAM, 0.7f}, {VM::ECHO_PARAM, 0.5f}, {VM::ELLIPTIC_PARAM, 0.6f}}},
	{"Bootleg, 10th generation", false, {{VM::GEN_PARAM, 1.0f}, {VM::WEAR_PARAM, 0.35f}, {VM::RADIUS_PARAM, 0.6f}, {VM::CUT_PARAM, 2.0f}, {VM::PLAY_PARAM, 0.0f}, {VM::HUM_PARAM, 0.1f}}},
	{"Cheap record player", false, {{VM::TIP_PARAM, 0.0f}, {VM::LOAD_PARAM, 1.0f}, {VM::AZIMUTH_PARAM, 0.5f}, {VM::VERT_PARAM, 0.3f}, {VM::TRACK_PARAM, 0.2f}, {VM::VALVE_PARAM, 0.35f}, {VM::HUM_PARAM, 0.25f}, {VM::OFFSET_PARAM, 0.5f}}},
	{"Audiophile, moving coil", true, {{VM::TIP_PARAM, 2.0f}, {VM::TRACK_PARAM, 0.6f}, {VM::GEN_PARAM, 0.05f}, {VM::WEAR_PARAM, 0.0f}, {VM::OFFSET_PARAM, 0.08f}, {VM::ECHO_PARAM, 0.08f}, {VM::VALVE_PARAM, 0.1f}, {VM::XFMR_PARAM, 0.45f}, {VM::RADIUS_PARAM, 0.1f}}},
	{"Ground loop", false, {{VM::HUM_PARAM, 0.55f}, {VM::GND_PARAM, 1.0f}, {VM::VALVE_PARAM, 0.4f}}},
	{"Leaning needle", false, {{VM::AZIMUTH_PARAM, 0.85f}, {VM::VERT_PARAM, 0.45f}, {VM::LOAD_PARAM, 0.6f}}},
	{"Groove echo ghost", false, {{VM::ECHO_PARAM, 1.0f}, {VM::DRIVE_PARAM, 0.5f}, {VM::RADIUS_PARAM, 0.2f}}},
	{"Played to death", false, {{VM::WEAR_PARAM, 1.0f}, {VM::GEN_PARAM, 0.6f}, {VM::RADIUS_PARAM, 0.85f}, {VM::TRACK_PARAM, 0.15f}, {VM::VERT_PARAM, 0.6f}, {VM::TIP_PARAM, 0.0f}}},
	{"Mono reissue, US pressing", false, {{VM::ELLIPTIC_PARAM, 1.0f}, {VM::GEN_PARAM, 0.55f}, {VM::CUT_PARAM, 2.0f}, {VM::PLAY_PARAM, 0.0f}, {VM::HZ_PARAM, 1.0f}, {VM::HUM_PARAM, 0.2f}, {VM::VALVE_PARAM, 0.45f}}},
};
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

static void applyPreset(VinylModule* m, int i) {
	history::ModuleChange* h = new history::ModuleChange;
	h->name = string::f("load preset %s", PRESETS[i].name);
	h->moduleId = m->id;
	h->oldModuleJ = m->toJson();
	for (int p = 0; p < VM::NUM_PARAMS; p++) {
		if (p == VM::BYPASS_PARAM || p == VM::A_DEST_PARAM || p == VM::A_AMT_PARAM || p == VM::B_DEST_PARAM || p == VM::B_AMT_PARAM)
			continue;
		m->paramQuantities[p]->reset();
	}
	for (const PresetValue& v : PRESETS[i].values) m->params[v.param].setValue(v.value);
	m->movingCoil = PRESETS[i].movingCoil;
	m->wreckBoth = false;
	m->sidePlaying = false;
	m->sideProgress = 0.f;
	h->newModuleJ = m->toJson();
	APP->history->push(h);
}
struct RiaaMini : VinylScreen {
	VinylModule* module = nullptr;
	void lines(std::vector<std::string>& out) override {
		int c = module ? module->dispCut : 0, p = module ? module->dispPlay : 1;
		out.push_back(vinyl::CURVE_SHORT[c]);
		out.push_back(std::string("> ") + vinyl::CURVE_SHORT[p]);
	}
};

// "A > GROOVE RADIUS  +60%"
struct CvScreen : VinylScreen {
	VinylModule* module = nullptr;
	int which = 0;
	void lines(std::vector<std::string>& out) override {
		const char* tag = which ? "B" : "A";
		if (!module) { out.push_back(which ? "B > WRECK XFADE  -40%" : "A > GROOVE RADIUS  +60%"); return; }
		int d = clamp((int) std::round(module->params[which ? VinylModule::B_DEST_PARAM : VinylModule::A_DEST_PARAM].getValue()), 0, NUM_DESTS - 1);
		float amt = module->params[which ? VinylModule::B_AMT_PARAM : VinylModule::A_AMT_PARAM].getValue();
		const Dest& D = VinylModule::DEST(d);
		if (D.kind == DK_NONE) out.push_back(string::f("%s > OFF", tag));
		else if (D.kind == DK_KNOB) out.push_back(string::f("%s > %s  %+d%%", tag, D.screen, (int) std::round(amt * 100.f)));
		else out.push_back(string::f("%s > %s%s", tag, D.screen, amt < 0.f ? " (INV)" : ""));
	}
};

// Dashed rings round whatever CV A (amber) and CV B (teal) are driving.
struct CvRings : TransparentWidget {
	VinylModule* module = nullptr;
	struct Spot { float x, y, r; };
	std::map<int, Spot> spots;
	void ring(NVGcontext* vg, Spot s, NVGcolor c, float extra) {
		Vec p = P(s.x, s.y);
		float rr = mm2px(s.r + extra);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, mm2px(0.55f));
		const int N = 18;
		for (int i = 0; i < N; i++) {
			float a0 = 2.f * M_PI * i / N, a1 = a0 + 2.f * M_PI / N * 0.6f;
			nvgBeginPath(vg);
			nvgArc(vg, p.x, p.y, rr, a0, a1, NVG_CW);
			nvgStroke(vg);
		}
	}
	void draw(const DrawArgs& args) override {
		if (!module) return;
		int dests[2] = {(int) std::round(module->params[VinylModule::A_DEST_PARAM].getValue()),
		                (int) std::round(module->params[VinylModule::B_DEST_PARAM].getValue())};
		NVGcolor cols[2] = {nvgRGB(232, 176, 74), nvgRGB(127, 184, 164)};
		for (int k = 0; k < 2; k++) {
			const Dest& D = VinylModule::DEST(dests[k]);
			if (D.kind == DK_NONE) continue;
			auto it = spots.find(D.param);
			if (it == spots.end()) continue;
			ring(args.vg, it->second, cols[k], (k == 1 && dests[0] == dests[1]) ? 1.0f : 0.f);
		}
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
		const float onX[M::NSEC] = {LV::LATHE_ON_X, LV::RIAA_ON_X, LV::PRESS_ON_X, LV::GROOVE_ON_X, LV::STYLUS_ON_X,
		                            LV::PLATTER_ON_X, LV::PREAMP_ON_X, LV::WRECK_ON_X};
		for (int i = 0; i < M::NSEC; i++) {
			int p = i == 6 ? (int) M::PREAMP_ON_PARAM : (i == 7 ? (int) M::WRECK_ON_PARAM : (int) M::LATHE_ON_PARAM + i);
			int l = i == 6 ? (int) M::PREAMP_ON_LIGHT : (i == 7 ? (int) M::WRECK_ON_LIGHT : (int) M::LATHE_ON_LIGHT + i);
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(P(onX[i], LV::LATHE_ON_Y), module, p, l));
		}

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

		knobAt<RoundLargeBlackKnob>(this, module, LV::VALVE_X, LV::VALVE_Y, M::VALVE_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::XFMR_X, LV::XFMR_Y, M::XFMR_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::HUM_X, LV::HUM_Y, M::HUM_PARAM);
		addParam(createParamCentered<CKSS>(P(LV::HZ_X, LV::HZ_Y), module, M::HZ_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(P(LV::GND_X, LV::GND_Y), module, M::GND_PARAM, M::GND_LIGHT));

		knobAt<RoundSmallBlackKnob>(this, module, LV::DRIFT_X, LV::DRIFT_Y, M::DRIFT_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::SLOP_X, LV::SLOP_Y, M::SLOP_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::WOFFSET_X, LV::WOFFSET_Y, M::WOFFSET_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::XFADE_X, LV::XFADE_Y, M::XFADE_PARAM);
		addParam(createParamCentered<VCVButton>(P(LV::SYNC_X, LV::SYNC_Y), module, M::SYNC_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(P(LV::NUDGE_LIGHT_X, LV::NUDGE_LIGHT_Y), module, M::NUDGE_LIGHT));

		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>>(P(LV::BYPASS_X, LV::BYPASS_Y), module, M::BYPASS_PARAM, M::BYPASS_LIGHT));

		// assignable CV
		addInput(createInputCentered<PJ301MPort>(P(LV::CVA_IN_X, LV::CVA_IN_Y), module, M::CVA_INPUT));
		addInput(createInputCentered<PJ301MPort>(P(LV::CVB_IN_X, LV::CVB_IN_Y), module, M::CVB_INPUT));
		knobAt<RoundSmallBlackKnob>(this, module, LV::A_DEST_X, LV::A_DEST_Y, M::A_DEST_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::A_AMT_X, LV::A_AMT_Y, M::A_AMT_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::B_DEST_X, LV::B_DEST_Y, M::B_DEST_PARAM);
		knobAt<RoundSmallBlackKnob>(this, module, LV::B_AMT_X, LV::B_AMT_Y, M::B_AMT_PARAM);
		for (int k = 0; k < 2; k++) {
			CvScreen* c = createWidget<CvScreen>(k ? P(LV::B_DISP_X, LV::B_DISP_Y) : P(LV::A_DISP_X, LV::A_DISP_Y));
			c->box.size = mm2px(k ? Vec(LV::B_DISP_W, LV::B_DISP_H) : Vec(LV::A_DISP_W, LV::A_DISP_H));
			c->module = module;
			c->which = k;
			c->accent = k ? nvgRGB(127, 184, 164) : nvgRGB(232, 176, 74);
			c->fontMm = 2.6f;
			addChild(c);
		}

		struct J { float x, y; int id; bool out; };
		const J jacks[] = {
			{LV::IN_L_X, LV::IN_L_Y, M::IN_L_INPUT, false}, {LV::IN_R_X, LV::IN_R_Y, M::IN_R_INPUT, false},
			{LV::B_L_X, LV::B_L_Y, M::B_L_INPUT, false}, {LV::B_R_X, LV::B_R_Y, M::B_R_INPUT, false},
			{LV::CLOCK_IN_X, LV::CLOCK_IN_Y, M::CLOCK_INPUT, false},
			{LV::WOW_OUT_X, LV::WOW_OUT_Y, M::WOW_OUTPUT, true},
			{LV::OUT_L_X, LV::OUT_L_Y, M::OUT_L_OUTPUT, true}, {LV::OUT_R_X, LV::OUT_R_Y, M::OUT_R_OUTPUT, true},
		};
		for (const J& j : jacks) {
			if (j.out) addOutput(createOutputCentered<PJ301MPort>(P(j.x, j.y), module, j.id));
			else addInput(createInputCentered<PJ301MPort>(P(j.x, j.y), module, j.id));
		}

		// rings go on top of everything
		CvRings* rg = createWidget<CvRings>(Vec(0, 0));
		rg->box.size = box.size;
		rg->module = module;
		const float L = 7.4f, S = 4.6f, B = 3.4f;
		rg->spots = {
			{M::DRIVE_PARAM, {LV::DRIVE_X, LV::DRIVE_Y, S}}, {M::ELLIPTIC_PARAM, {LV::ELLIPTIC_X, LV::ELLIPTIC_Y, S}},
			{M::HF_PARAM, {LV::HF_X, LV::HF_Y, S}}, {M::ECHO_PARAM, {LV::ECHO_X, LV::ECHO_Y, S}},
			{M::CUT_PARAM, {LV::CUT_X, LV::CUT_Y, L}}, {M::PLAY_PARAM, {LV::PLAY_X, LV::PLAY_Y, L}},
			{M::GEN_PARAM, {LV::GEN_X, LV::GEN_Y, L}}, {M::WEAR_PARAM, {LV::WEAR_X, LV::WEAR_Y, L}},
			{M::RADIUS_PARAM, {LV::RADIUS_X, LV::RADIUS_Y, L}}, {M::TRACK_PARAM, {LV::TRACK_X, LV::TRACK_Y, S}},
			{M::SIDE_PARAM, {LV::SIDE_X, LV::SIDE_Y, S}}, {M::SIDE_PLAY_PARAM, {LV::SIDE_PLAY_X, LV::SIDE_PLAY_Y, B}},
			{M::LOAD_PARAM, {LV::LOAD_X, LV::LOAD_Y, S}}, {M::AZIMUTH_PARAM, {LV::AZIMUTH_X, LV::AZIMUTH_Y, S}},
			{M::VERT_PARAM, {LV::VERT_X, LV::VERT_Y, S}}, {M::OFFSET_PARAM, {LV::OFFSET_X, LV::OFFSET_Y, L}},
			{M::VALVE_PARAM, {LV::VALVE_X, LV::VALVE_Y, L}}, {M::XFMR_PARAM, {LV::XFMR_X, LV::XFMR_Y, S}},
			{M::HUM_PARAM, {LV::HUM_X, LV::HUM_Y, S}}, {M::DRIFT_PARAM, {LV::DRIFT_X, LV::DRIFT_Y, S}},
			{M::SLOP_PARAM, {LV::SLOP_X, LV::SLOP_Y, S}}, {M::WOFFSET_PARAM, {LV::WOFFSET_X, LV::WOFFSET_Y, S}},
			{M::XFADE_PARAM, {LV::XFADE_X, LV::XFADE_Y, S}}, {M::SYNC_PARAM, {LV::SYNC_X, LV::SYNC_Y, B}},
			{M::BYPASS_PARAM, {LV::BYPASS_X, LV::BYPASS_Y, B}},
		};
		addChild(rg);
	}

	void appendContextMenu(Menu* menu) override {
		VinylModule* m = dynamic_cast<VinylModule*>(this->module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Presets", "", [=](Menu* sub) {
			for (int i = 0; i < NUM_PRESETS; i++) {
				const Preset& p = PRESETS[i];
				sub->addChild(createMenuItem(string::f("%02d  %s", i + 1, p.name), "", [=]() { applyPreset(m, i); }));
			}
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Stylus: moving-coil cartridge (flat, bright; LOAD has no effect)", "", &m->movingCoil));
		menu->addChild(createBoolPtrMenuItem("Wreck: deck B can also run ahead (both decks 50 ms back)", "", &m->wreckBoth));
		menu->addChild(createMenuItem("Groove: back to the outer edge", "", [=]() { m->sideProgress = 0.f; }));
	}
};

Model* modelVinylCountdown = createModel<VinylModule, VinylWidget>("VinylCountdown");
