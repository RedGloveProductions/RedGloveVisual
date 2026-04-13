#include "plugin.hpp"

Plugin* pluginInstance = nullptr;
extern Model* modelGlassBridge;

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelGlassBridge);
}
