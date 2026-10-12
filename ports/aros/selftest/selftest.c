#include <rtpmidi/applemidi.h>
#include <rtpmidi/rtpmidi.h>
#include <rtpmidi/session.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    static const uint8_t note[] = {0x90, 60, 100};
    static const uint8_t name[] = "AROS MIDIHub";
    struct applemidi_packet session = {0};
    struct applemidi_packet decoded_session;
    struct rtpmidi_packet decoded_rtp;
    struct applemidi_session local;
    uint8_t data[64];
    size_t length;

    session.command = APPLEMIDI_IN;
    session.token = 0x12345678;
    session.ssrc = 0x89abcdef;
    session.name = name;
    session.name_length = sizeof(name) - 1;
    if (applemidi_encode(&session, data, sizeof(data), &length) != 0 ||
        applemidi_decode(data, length, &decoded_session) != 0 ||
        decoded_session.token != session.token ||
        decoded_session.ssrc != session.ssrc ||
        decoded_session.name_length != session.name_length ||
        memcmp(decoded_session.name, name, session.name_length) != 0) {
        puts("MIDIHub: AppleMIDI codec failed");
        return 20;
    }

    if (rtpmidi_encode_short(1, 10000, session.ssrc, note, sizeof(note),
                            data, sizeof(data), &length) != 0 ||
        rtpmidi_decode(data, length, &decoded_rtp) != 0 ||
        decoded_rtp.ssrc != session.ssrc ||
        decoded_rtp.timestamp != 10000 ||
        decoded_rtp.midi_length != sizeof(note) ||
        memcmp(decoded_rtp.midi, note, sizeof(note)) != 0) {
        puts("MIDIHub: RTP-MIDI codec failed");
        return 20;
    }

    if (applemidi_session_init(&local, session.ssrc, name,
                        sizeof(name) - 1) != 0 ||
        applemidi_session_invite(&local, session.token, &session) != 0 ||
        session.command != APPLEMIDI_IN ||
        local.phase != APPLEMIDI_SESSION_INVITING_CONTROL) {
        puts("MIDIHub: session state failed");
        return 20;
    }

    puts("MIDIHub: protocol self-test passed");
    return 0;
}
