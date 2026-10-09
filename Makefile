CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror -std=c99
CPPFLAGS ?= -Iinclude
M68K_CC ?= m68k-aros-gcc
M68K_CFLAGS ?= -O2 -Wall -Wextra -Werror -Wno-volatile-register-var -std=gnu99
FLUIDSYNTH_CFLAGS ?= $(shell pkg-config --cflags fluidsynth 2>/dev/null)
FLUIDSYNTH_LIBS ?= $(shell pkg-config --libs fluidsynth 2>/dev/null)

SOURCES = src/applemidi.c src/rtpmidi.c src/session.c src/peers.c src/config.c src/timing.c src/sender.c network/mdns/embedded.c src/routes.c
PROGRAM_SOURCES = ports/aros/midihub/main.c ports/aros/midihub/camd.c \
                  ports/aros/midihub/network.c \
                  ports/aros/camd_bridge.c
HEADERS = include/midihub/applemidi.h include/midihub/rtpmidi.h \
          include/midihub/session.h include/midihub/peers.h include/midihub/config.h include/midihub/timing.h \
          include/midihub/sender.h include/midihub/mdns.h include/midihub/routes.h \
          ports/aros/midihub/camd.h ports/aros/midihub/network.h \
          ports/aros/camd_bridge.h

.PHONY: test test-network test-synth test-synth-fluid demo m68k synth-m68k clean

test: build/protocol-test build/ble-midi-test build/routes-test build/ump-test build/netmidi2-test build/camd-endpoint-core-test
	./build/protocol-test
	./build/ble-midi-test
	./build/routes-test
	./build/ump-test
	./build/netmidi2-test
	./build/camd-endpoint-core-test

build/camd-endpoint-core-test: tests/camd_endpoint_core.c \
                               prototypes/camd/endpoint_registry.c \
                               prototypes/camd/endpoint_registry.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_endpoint_core.c prototypes/camd/endpoint_registry.c

build/ump-test: tests/ump.c src/ump.c include/midihub/ump.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/ump.c src/ump.c

build/netmidi2-test: tests/netmidi2.c src/netmidi2.c src/ump.c \
                     include/midihub/netmidi2.h include/midihub/ump.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/netmidi2.c src/netmidi2.c src/ump.c

build/routes-test: tests/routes.c src/routes.c include/midihub/routes.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/routes.c src/routes.c

build/ble-midi-test: tests/ble_midi.c src/ble_midi.c src/ble_peripheral.c \
                     include/midihub/ble_midi.h include/midihub/ble_peripheral.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/ble_midi.c src/ble_midi.c src/ble_peripheral.c

demo: build/MIDIHub

m68k: build/MIDIHub-m68k

test-network: build/MIDIHub
	python3 tests/network_loopback.py build/MIDIHub

test-synth: build/synth-render
	./build/synth-render soundfonts/GeneralUser-GS/GeneralUser-GS.sf2 build/generaluser-test.wav

test-synth-fluid: build/synth-render-fluid
	./build/synth-render-fluid soundfonts/GeneralUser-GS/GeneralUser-GS.sf2 build/generaluser-fluid-test.wav fluid

synth-m68k: build/synth-render-m68k

build/synth-render: tests/synth_render.c src/synth.c include/midihub/synth.h third_party/TinySoundFont/tsf.h
	mkdir -p build
	$(CC) $(CPPFLAGS) -Ithird_party/TinySoundFont $(CFLAGS) -o $@ tests/synth_render.c src/synth.c -lm

# Opt-in: FluidSynth is an external LGPL library, never part of the AROS package.
build/synth-render-fluid: tests/synth_render.c src/synth.c include/midihub/synth.h third_party/TinySoundFont/tsf.h
	@test -n "$(FLUIDSYNTH_LIBS)" || { echo "Set FLUIDSYNTH_CFLAGS and FLUIDSYNTH_LIBS, or install an external FluidSynth development package"; exit 1; }
	mkdir -p build
	$(CC) $(CPPFLAGS) -Ithird_party/TinySoundFont $(CFLAGS) \
	    -DMIDIHUB_ENABLE_FLUIDSYNTH $(FLUIDSYNTH_CFLAGS) \
	    -o $@ tests/synth_render.c src/synth.c \
	    $(FLUIDSYNTH_LIBS) -lm

build/synth-render-m68k: tests/synth_render.c src/synth.c include/midihub/synth.h third_party/TinySoundFont/tsf.h
	mkdir -p build
	$(M68K_CC) $(CPPFLAGS) -Ithird_party/TinySoundFont $(M68K_CFLAGS) -o $@ tests/synth_render.c src/synth.c -lm

build/MIDIHub: $(SOURCES) $(HEADERS) $(PROGRAM_SOURCES)
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SOURCES) $(PROGRAM_SOURCES)

build/MIDIHub-m68k: $(SOURCES) $(HEADERS) $(PROGRAM_SOURCES)
	mkdir -p build
	$(M68K_CC) $(CPPFLAGS) $(M68K_CFLAGS) -o $@ $(SOURCES) $(PROGRAM_SOURCES)

build/protocol-test: $(SOURCES) $(HEADERS) tests/protocol.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SOURCES) tests/protocol.c

clean:
	rm -rf build
