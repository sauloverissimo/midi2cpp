# midi2cpp / daisyseed-midi2
#
# USB MIDI 2.0 device on the Daisy Seed, built with the libDaisy makefile.
#
# Set LIBDAISY_DIR to a libDaisy checkout on branch feat/usb-midi2-transport
# and MIDI2_DIR to a midi2 checkout (github.com/sauloverissimo/midi2), then:
#
#   make LIBDAISY_DIR=/path/to/libDaisy MIDI2_DIR=/path/to/midi2
#
# MIDI2CPP_DIR defaults to this repo root (../..) and rarely needs overriding.

TARGET = daisyseed-midi2

# midi2cpp requires C++17; the libDaisy core makefile defaults to gnu++14.
CPP_STANDARD = -std=gnu++17

MIDI2CPP_DIR ?= ../..

ifeq ($(LIBDAISY_DIR),)
$(error LIBDAISY_DIR is not set. Point it at a libDaisy checkout on branch feat/usb-midi2-transport)
endif
ifeq ($(MIDI2_DIR),)
$(error MIDI2_DIR is not set. Point it at a midi2 checkout, e.g. github.com/sauloverissimo/midi2)
endif

# Recipe sources
CPP_SOURCES = src/main.cpp src/daisyseed_midi2.cpp

# midi2cpp C++ wrapper
CPP_SOURCES += $(MIDI2CPP_DIR)/src/midi2_device.cpp \
               $(MIDI2CPP_DIR)/src/midi2_ci.cpp \
               $(MIDI2CPP_DIR)/src/midi2_host.cpp \
               $(MIDI2CPP_DIR)/src/midi2_bridge.cpp

# midi2 C99 core (amalgamated single translation unit)
C_SOURCES += $(MIDI2_DIR)/dist/midi2.c

C_INCLUDES += -I$(MIDI2CPP_DIR)/src -I$(MIDI2_DIR)/dist

# Core location and generic libDaisy makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
