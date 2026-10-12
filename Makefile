CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror -std=c99
CPPFLAGS ?= -Iinclude
M68K_CC ?= m68k-aros-gcc
M68K_CFLAGS ?= -O2 -Wall -Wextra -Werror -Wno-volatile-register-var -std=gnu99
FLUIDSYNTH_CFLAGS ?= $(shell pkg-config --cflags fluidsynth 2>/dev/null)
FLUIDSYNTH_LIBS ?= $(shell pkg-config --libs fluidsynth 2>/dev/null)

SOURCES = network/rtpmidi/applemidi.c network/rtpmidi/rtpmidi.c network/rtpmidi/session.c network/rtpmidi/peers.c network/rtpmidi/ports.c src/config.c network/rtpmidi/timing.c network/rtpmidi/clock.c network/rtpmidi/sender.c network/rtpmidi/receiver.c src/routes.c
PROGRAM_SOURCES = ports/aros/midihub/main.c ports/aros/midihub/camd.c \
                  ports/aros/midihub/network.c \
                  ports/aros/midihub/discovery.c \
                  ports/aros/camd_bridge.c \
                  src/netmidi2.c src/ump.c
HEADERS = include/rtpmidi/applemidi.h include/rtpmidi/rtpmidi.h \
          include/rtpmidi/session.h include/rtpmidi/peers.h include/rtpmidi/ports.h include/midihub/config.h include/rtpmidi/timing.h include/rtpmidi/clock.h include/rtpmidi/receiver.h \
          include/rtpmidi/sender.h include/midihub/routes.h \
          ports/aros/midihub/camd.h ports/aros/midihub/network.h \
          ports/aros/camd_bridge.h

.PHONY: test check-patches test-network test-synth test-synth-fluid demo m68k synth-m68k clean

test: build/protocol-test build/ble-midi-test build/routes-test build/ump-test build/netmidi2-test build/camd-endpoint-core-test build/camd-provider-contract-test build/camd-legacy-driver-adapter-test build/camd-legacy-port-refs-test build/camd-native-event-queue-test build/camd-native-event-pump-test build/camd-native-event-worker-test build/camd-native-worker-fanout-test build/camd-legacy-output-backend-test build/camd-identity-map-test build/camd-identity-file-test build/camd-identity-store-recovery-test build/camd-legacy-identity-set-test build/camd-app-endpoint-test
	./build/protocol-test
	./build/ble-midi-test
	./build/routes-test
	./build/ump-test
	./build/netmidi2-test
	./build/camd-endpoint-core-test
	./build/camd-provider-contract-test
	./build/camd-legacy-driver-adapter-test
	./build/camd-legacy-port-refs-test
	./build/camd-native-event-queue-test
	./build/camd-native-event-pump-test
	./build/camd-native-event-worker-test
	./build/camd-native-worker-fanout-test
	./build/camd-legacy-output-backend-test
	./build/camd-identity-map-test
	./build/camd-identity-file-test
	./build/camd-identity-store-recovery-test
	./build/camd-legacy-identity-set-test
	./build/camd-app-endpoint-test

check-patches:
	tools/check-camd-patches.sh

build/camd-endpoint-core-test: tests/camd_endpoint_core.c \
                               prototypes/camd/endpoint_registry.c \
                               prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h \
                               prototypes/camd/native_event_queue.c \
                               prototypes/camd/native_event_queue.h \
                               prototypes/camd/provider_contract.c \
                               prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_endpoint_core.c prototypes/camd/endpoint_registry.c \
		prototypes/camd/provider_contract.c \
		prototypes/camd/native_event_queue.c -pthread

build/camd-provider-contract-test: tests/camd_provider_contract.c \
                                   prototypes/camd/provider_contract.c \
                                   prototypes/camd/provider_contract.h \
                                   prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_provider_contract.c prototypes/camd/provider_contract.c

build/camd-legacy-driver-adapter-test: tests/camd_legacy_driver_adapter.c \
                                       prototypes/camd/legacy_driver_adapter.c \
                                       prototypes/camd/legacy_driver_adapter.h \
                                       prototypes/camd/endpoint_registry.c \
                                       prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h \
                                       prototypes/camd/provider_contract.c \
                                       prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_legacy_driver_adapter.c \
		prototypes/camd/legacy_driver_adapter.c \
		prototypes/camd/endpoint_registry.c \
		prototypes/camd/provider_contract.c -pthread

build/camd-legacy-port-refs-test: tests/camd_legacy_port_refs.c \
                                  prototypes/camd/legacy_port_refs.c \
                                  prototypes/camd/legacy_port_refs.h \
                                  prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_legacy_port_refs.c \
		prototypes/camd/legacy_port_refs.c

build/camd-native-event-queue-test: tests/camd_native_event_queue.c \
                                    prototypes/camd/native_event_queue.c \
                                    prototypes/camd/native_event_queue.h \
                                    prototypes/camd/provider_contract.c \
                                    prototypes/camd/provider_contract.h \
                                    prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_native_event_queue.c \
		prototypes/camd/native_event_queue.c \
		prototypes/camd/provider_contract.c -pthread

build/camd-native-event-pump-test: tests/camd_native_event_pump.c \
                                   prototypes/camd/native_event_pump.c \
                                   prototypes/camd/native_event_pump.h \
                                   prototypes/camd/native_event_queue.c \
                                   prototypes/camd/native_event_queue.h \
                                   prototypes/camd/provider_contract.c \
                                   prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_native_event_pump.c \
		prototypes/camd/native_event_pump.c \
		prototypes/camd/native_event_queue.c \
		prototypes/camd/provider_contract.c -pthread

build/camd-native-event-worker-test: tests/camd_native_event_worker.c \
                                     prototypes/camd/native_event_worker.c \
                                     prototypes/camd/native_event_worker.h \
                                     prototypes/camd/native_event_pump.c \
                                     prototypes/camd/native_event_pump.h \
                                     prototypes/camd/native_event_queue.c \
                                     prototypes/camd/native_event_queue.h \
                                     prototypes/camd/provider_contract.c \
                                     prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_native_event_worker.c \
		prototypes/camd/native_event_worker.c \
		prototypes/camd/native_event_pump.c \
		prototypes/camd/native_event_queue.c \
		prototypes/camd/provider_contract.c -pthread

build/camd-native-worker-fanout-test: tests/camd_native_worker_fanout.c \
                                      prototypes/camd/native_worker_fanout.c \
                                      prototypes/camd/native_worker_fanout.h \
                                      prototypes/camd/native_event_worker.c \
                                      prototypes/camd/native_event_worker.h \
                                      prototypes/camd/native_event_pump.c \
                                      prototypes/camd/native_event_pump.h \
                                      prototypes/camd/native_event_queue.c \
                                      prototypes/camd/native_event_queue.h \
                                      prototypes/camd/provider_contract.c \
                                      prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_native_worker_fanout.c \
		prototypes/camd/native_worker_fanout.c \
		prototypes/camd/native_event_worker.c \
		prototypes/camd/native_event_pump.c \
		prototypes/camd/native_event_queue.c \
		prototypes/camd/provider_contract.c -pthread

build/camd-legacy-output-backend-test: tests/camd_legacy_output_backend.c \
                                        prototypes/camd/legacy_output_backend.c \
                                        prototypes/camd/legacy_output_backend.h \
                                        prototypes/camd/legacy_driver_adapter.c \
                                        prototypes/camd/legacy_driver_adapter.h \
                                        prototypes/camd/native_worker_fanout.c \
                                        prototypes/camd/native_worker_fanout.h \
                                        prototypes/camd/native_event_worker.c \
                                        prototypes/camd/native_event_worker.h \
                                        prototypes/camd/native_event_pump.c \
                                        prototypes/camd/native_event_pump.h \
                                        prototypes/camd/native_event_queue.c \
                                        prototypes/camd/native_event_queue.h \
                                        prototypes/camd/endpoint_registry.c \
                                        prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h \
                                        prototypes/camd/provider_contract.c \
                                        prototypes/camd/provider_contract.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_legacy_output_backend.c \
		prototypes/camd/legacy_output_backend.c \
		prototypes/camd/legacy_driver_adapter.c \
		prototypes/camd/native_worker_fanout.c \
		prototypes/camd/native_event_worker.c \
		prototypes/camd/native_event_pump.c \
		prototypes/camd/native_event_queue.c \
		prototypes/camd/endpoint_registry.c \
		prototypes/camd/provider_contract.c -pthread

build/camd-identity-map-test: tests/camd_identity_map.c \
                              prototypes/camd/identity_map.c \
                              prototypes/camd/identity_map.h \
                              prototypes/camd/legacy_identity_key.c \
                              prototypes/camd/legacy_identity_key.h \
                              prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_identity_map.c \
		prototypes/camd/identity_map.c \
		prototypes/camd/legacy_identity_key.c -pthread

build/camd-identity-file-test: tests/camd_identity_file.c \
                               prototypes/camd/identity_file.c \
                               prototypes/camd/identity_file.h \
                               prototypes/camd/identity_map.h \
                               prototypes/camd/endpoint_registry.h prototypes/camd/camdendpoint.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_identity_file.c prototypes/camd/identity_file.c

build/camd-identity-store-recovery-test: \
		tests/camd_identity_store_recovery.c \
		prototypes/camd/identity_store_recovery.c \
		prototypes/camd/identity_store_recovery.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -o $@ \
		tests/camd_identity_store_recovery.c \
		prototypes/camd/identity_store_recovery.c

build/camd-app-endpoint-test: tests/camd_app_endpoint.c prototypes/camd/app_endpoint.c prototypes/camd/legacy_driver_adapter.c prototypes/camd/native_event_queue.c prototypes/camd/native_event_pump.c prototypes/camd/native_event_worker.c prototypes/camd/provider_contract.c prototypes/camd/endpoint_registry.c \
		prototypes/camd/app_endpoint.h prototypes/camd/camdendpoint.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -pthread -o $@ \
		tests/camd_app_endpoint.c prototypes/camd/app_endpoint.c prototypes/camd/legacy_driver_adapter.c prototypes/camd/native_event_queue.c prototypes/camd/native_event_pump.c prototypes/camd/native_event_worker.c prototypes/camd/provider_contract.c prototypes/camd/endpoint_registry.c

build/camd-legacy-identity-set-test: tests/camd_legacy_identity_set.c \
		prototypes/camd/legacy_identity_set.c \
		prototypes/camd/legacy_identity_set.h \
		prototypes/camd/legacy_identity_key.c \
		prototypes/camd/legacy_identity_key.h \
		prototypes/camd/identity_map.c prototypes/camd/identity_map.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iprototypes/camd -pthread -o $@ \
		tests/camd_legacy_identity_set.c \
		prototypes/camd/legacy_identity_set.c \
		prototypes/camd/legacy_identity_key.c prototypes/camd/identity_map.c

build/ump-test: tests/ump.c src/ump.c include/ump/ump.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/ump.c src/ump.c

build/netmidi2-test: tests/netmidi2.c src/netmidi2.c src/ump.c \
                     include/netmidi2/netmidi2.h include/ump/ump.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/netmidi2.c src/netmidi2.c src/ump.c

build/routes-test: tests/routes.c src/routes.c include/midihub/routes.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/routes.c src/routes.c

build/ble-midi-test: tests/ble_midi.c src/ble_midi.c src/ble_peripheral.c \
                     include/blemidi/ble_midi.h include/blemidi/ble_peripheral.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/ble_midi.c src/ble_midi.c src/ble_peripheral.c

# The codec of btmidi.class lives in the AROS tree; its test is run where
# a checkout of the class is at hand.
BTMIDI_CLASS ?= $(HOME)/AROS-btmidi/rom/bluetooth/classes/btmidi

build/blemidi-class-test: tests/blemidi_class.c $(BTMIDI_CLASS)/blemidi.c \
                          $(BTMIDI_CLASS)/blemidi.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(BTMIDI_CLASS) -o $@ tests/blemidi_class.c $(BTMIDI_CLASS)/blemidi.c

test-blemidi-class: build/blemidi-class-test
	./build/blemidi-class-test

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
