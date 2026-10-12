// The other end of a Network MIDI 2.0 session, played by NetUMP
// (https://github.com/bbouchez/NetUMP, MIT), Benoit Bouchez's endpoint
// class, which shares no code with ours. It is the engine of his UMP-OX.
//
//   netump-peer LOCAL_PORT [REMOTE_ADDRESS REMOTE_PORT]
//
// With an address it invites that Host; without one it waits to be invited.
// Once the session is open it sends a MIDI 1.0 Note On, Note Off and
// Control Change and a SysEx message of ten bytes, as UMPs, and it prints
// one line for every UMP it receives. SIGTERM closes the session.
// Build with NetUMP and BEBSDK beside it and -D__TARGET_LINUX__.
#include "NetUMP.h"
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

// How many words a UMP has is in its first word's top four bits.
static unsigned words_of(uint32_t first)
{
    static const unsigned char table[16] = {
        1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4
    };
    return table[first >> 28];
}

static void received(void*, uint32_t* data)
{
    unsigned n = words_of(data[0]);
    printf("ump");
    for (unsigned i = 0; i < n; ++i)
        printf(" %08x", data[i]);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char** argv)
{
    static char name[] = "NetUMP peer";
    static char id[] = "netump-1";
    // group 0: Note On, Note Off, Control Change, then F0 7D 01..0A F7 as
    // a SysEx7 Start of six bytes and an End of five
    static uint32_t messages[][2] = {
        {0x20903c64u, 0}, {0x20803c40u, 0}, {0x20b00764u, 0},
        {0x30167d01u, 0x02030405u}, {0x30350607u, 0x08090a00u}
    };
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: netump-peer LOCAL_PORT [REMOTE_ADDRESS REMOTE_PORT]\n");
        return 2;
    }
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    CNetUMPHandler handler(received, NULL);
    handler.SetEndpointName(name);
    handler.SetProductInstanceID(id);
    handler.SelectErrorCorrectionMode(ERROR_CORRECTION_FEC);
    bool initiator = argc == 4;
    unsigned int address = initiator ? ntohl(inet_addr(argv[2])) : 0;
    unsigned short remote = initiator ? (unsigned short)atoi(argv[3]) : 0;
    if (handler.InitiateSession(address, remote, (unsigned short)atoi(argv[1]),
                                initiator) != 0) {
        fprintf(stderr, "cannot open the socket\n");
        return 1;
    }
    printf("ready port=%s role=%s\n", argv[1], initiator ? "client" : "host");
    fflush(stdout);

    bool open = false, sent = false;
    unsigned since = 0;
    while (!stopping) {
        handler.RunSession();
        int status = handler.GetSessionStatus();
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
        if (open && !sent && ++since > 500) {
            for (unsigned i = 0; i < 5; ++i)
                handler.SendUMPMessage(messages[i]);
            printf("sent\n");
            fflush(stdout);
            sent = true;
        }
        int reason = handler.RemotePeerClosedSessionReason();
        if (reason >= 0) {
            printf("bye reason=%d\n", reason);
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
