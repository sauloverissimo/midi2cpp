# midi2cpp / daisyseed-host-midi2
#
# USB MIDI 2.0 host on the Daisy Seed, built with the libDaisy makefile.
#
# Set LIBDAISY_DIR to a libDaisy checkout on branch feat/usb-midi2-transport, then:
#
#   make LIBDAISY_DIR=/path/to/libDaisy
#
# MIDI2CPP_DIR defaults to this repo root (../..) and rarely needs overriding.

TARGET = daisyseed-host-midi2

# midi2cpp requires C++17; the libDaisy core makefile defaults to gnu++14.
CPP_STANDARD = -std=gnu++17

MIDI2CPP_DIR ?= ../..

ifeq ($(LIBDAISY_DIR),)
$(error LIBDAISY_DIR is not set. Point it at a libDaisy checkout on branch feat/usb-midi2-transport)
endif

# Recipe sources
CPP_SOURCES = src/main.cpp src/daisyseed_host_midi2.cpp

# midi2cpp C++ wrapper
CPP_SOURCES += $(MIDI2CPP_DIR)/src/midi2_device.cpp \
               $(MIDI2CPP_DIR)/src/midi2_ci.cpp \
               $(MIDI2CPP_DIR)/src/midi2_host.cpp \
               $(MIDI2CPP_DIR)/src/midi2_bridge.cpp

# midi2 C99 core (amalgamated single translation unit)
C_SOURCES += $(MIDI2CPP_DIR)/src/midi2.c

C_INCLUDES += -I$(MIDI2CPP_DIR)/src

# Core location and generic libDaisy makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
