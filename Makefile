RACK_DIR := /Users/captainstem/Documents/Rack-SDK

SOURCES += src/plugin.cpp
SOURCES += src/GlassBridge.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE*

include $(RACK_DIR)/plugin.mk
