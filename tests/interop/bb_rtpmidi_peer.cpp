// The other end of an AppleMIDI session, played by RTP-MIDI
// (https://github.com/bbouchez/RTP-MIDI, MIT), Benoit Bouchez's endpoint
// class, which shares no code with ours. It does not implement the
// recovery journal: it sends none and ignores ours.
//
//   bb-rtpmidi-peer LOCAL_CONTROL_PORT [REMOTE_ADDRESS REMOTE_CONTROL_PORT]
//
// With an address it invites that session; without one it waits to be
// invited. Once the session is open it sends a Note On, a Note Off, a
// Control Change and a short SysEx message, each with a delta time of
// zero in front as the class requires, and it prints one line for every
// block of MIDI it receives. SIGTERM closes the session.
// Build with RTP-MIDI and BEBSDK beside it and -D__TARGET_LINUX__.
#include "RTP_MIDI.h"
#include "SystemSleep.h"

#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t stopping;

static void stop(int)
{
    stopping = 1;
}

static void received(void*, unsigned int size, unsigned char* data,
                     unsigned int delta)
{
    printf("midi delta=%u ", delta);
    for (unsigned i = 0; i < size; ++i)
        printf("%02x", data[i]);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char** argv)
{
    static char name[] = "BB peer";
    static unsigned char note_on[] = {0x00, 0x90, 0x45, 0x50};
    static unsigned char note_off[] = {0x00, 0x80, 0x45, 0x40};
    static unsigned char control[] = {0x00, 0xb0, 0x07, 0x64};
    static unsigned char sysex[] = {0x00, 0xf0, 0x7d, 0x01, 0x02, 0x03, 0xf7};
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: bb-rtpmidi-peer LOCAL_CONTROL_PORT [REMOTE_ADDRESS REMOTE_CONTROL_PORT]\n");
        return 2;
    }
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    CRTP_MIDI handler(4096, received, NULL);
    handler.setSessionName(name);
    bool initiator = argc == 4;
    unsigned int address = initiator ? ntohl(inet_addr(argv[2])) : 0;
    unsigned short remote = initiator ? (unsigned short)atoi(argv[3]) : 0;
    unsigned short local = (unsigned short)atoi(argv[1]);
    int result = handler.InitiateSession(address, remote,
                                         initiator ? remote + 1 : 0, local,
                                         local + 1, initiator);
    if (result != 0) {
        fprintf(stderr, "cannot open the sockets (%d)\n", result);
        return 1;
    }
    printf("ready port=%s role=%s\n", argv[1],
           initiator ? "inviting" : "invited");
    fflush(stdout);

    bool open = false, sent = false;
    unsigned since = 0;
    while (!stopping) {
        handler.RunSession();
        int status = handler.getSessionStatus();
        if (status == 3 && !open) {
            printf("open\n");
            fflush(stdout);
            open = true;
            since = 0;
        }
        if (status != 3 && open) {
            printf("closed\n");
            fflush(stdout);
            open = sent = false;
        }
        if (open && !sent && ++since > 1500) {
            handler.SendRTPMIDIBlock(sizeof(note_on), note_on);
            handler.SendRTPMIDIBlock(sizeof(note_off), note_off);
            handler.SendRTPMIDIBlock(sizeof(control), control);
            handler.SendRTPMIDIBlock(sizeof(sysex), sysex);
            printf("sent\n");
            fflush(stdout);
            sent = true;
        }
        if (handler.RemotePeerHasClosedSession()) {
            printf("bye\n");
            fflush(stdout);
        }
        if (handler.RemotePeerHasRefusedSession()) {
            printf("refused\n");
            fflush(stdout);
        }
        SystemSleepMillis(1);
    }
    handler.CloseSession();
    for (int i = 0; i < 20; ++i) {
        handler.RunSession();
        SystemSleepMillis(1);
    }
    return 0;
}
