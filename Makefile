CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror -std=c99
CPPFLAGS ?= -Iinclude
M68K_CC ?= m68k-aros-gcc
M68K_CFLAGS ?= -O2 -Wall -Wextra -Werror -Wno-volatile-register-var -std=gnu99

SOURCES = src/applemidi.c src/rtpmidi.c src/session.c src/config.c src/timing.c src/sender.c
PROGRAM_SOURCES = ports/aros/midihub/main.c ports/aros/midihub/camd_bridge.c
HEADERS = include/midihub/applemidi.h include/midihub/rtpmidi.h \
          include/midihub/session.h include/midihub/config.h include/midihub/timing.h \
          include/midihub/sender.h \
          ports/aros/midihub/camd_bridge.h

.PHONY: test test-network test-synth demo m68k synth-m68k clean

test: build/protocol-test
	./build/protocol-test

demo: build/MIDIHub

m68k: build/MIDIHub-m68k

test-network: build/MIDIHub
	python3 tests/network_loopback.py build/MIDIHub

test-synth: build/synth-render
	./build/synth-render soundfonts/GeneralUser-GS/GeneralUser-GS.sf2 build/generaluser-test.wav

synth-m68k: build/synth-render-m68k

build/synth-render: tests/synth_render.c src/synth.c include/midihub/synth.h third_party/TinySoundFont/tsf.h
	mkdir -p build
	$(CC) $(CPPFLAGS) -Ithird_party/TinySoundFont $(CFLAGS) -o $@ tests/synth_render.c src/synth.c -lm

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
