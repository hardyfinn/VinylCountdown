#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelPlatter);
	p->addModel(modelInnerGroove);
	p->addModel(modelLathe);
	p->addModel(modelRiaa);
	p->addModel(modelTrainwreck);
	p->addModel(modelStylus);
	p->addModel(modelPressing);
	p->addModel(modelScratch);
}
