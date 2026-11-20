#pragma once
#include <rack.hpp>
#include "VinylDSP.hpp"
#include "Layout.hpp"

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelVinylCountdown;
extern Model* modelScratch;
extern Model* modelStudio;
extern Model* modelListeningRoom;

// mm -> px
inline Vec P(float x, float y) { return mm2px(Vec(x, y)); }

inline float softLimit(float v) {
	float a = std::fabs(v);
	if (a < 8.f) return v;
	float y = 8.f + 3.f * std::tanh((a - 8.f) / 3.f);
	return v < 0.f ? -y : y;
}

inline void addScrews(ModuleWidget* w) {
	w->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	if (w->box.size.x > 8 * RACK_GRID_WIDTH + 1) {
		w->addChild(createWidget<ScrewSilver>(Vec(w->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		w->addChild(createWidget<ScrewSilver>(Vec(w->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
}

// Small text screen used by RIAA, Trainwreck and Scratch. Subclasses fill
// lines() and may draw extra things in drawExtra().
struct VinylScreen : Widget {
	NVGcolor accent = nvgRGB(232, 176, 74);
	float fontMm = 3.0f;
	virtual void lines(std::vector<std::string>& out) {}
	virtual void drawExtra(const DrawArgs& args, std::shared_ptr<window::Font> font) {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				nvgFontFaceId(args.vg, font->handle);
				std::vector<std::string> ls;
				lines(ls);
				nvgFontSize(args.vg, mm2px(fontMm));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, accent);
				float n = (float) ls.size();
				for (size_t i = 0; i < ls.size(); i++) {
					float y = box.size.y * (i + 0.5f) / std::max(n, 1.f);
					nvgText(args.vg, box.size.x / 2, y, ls[i].c_str(), NULL);
				}
				drawExtra(args, font);
			}
		}
		Widget::drawLayer(args, layer);
	}
};
