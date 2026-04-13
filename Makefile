RACK_DIR := /Users/joe/Documents/Rack-SDK-2

SOURCES += src/plugin.cpp
SOURCES += src/GlassBridge.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE*

include $(RACK_DIR)/plugin.mk
