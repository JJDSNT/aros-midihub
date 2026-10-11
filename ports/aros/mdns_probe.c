/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 Jaime Dias
 *
 * Small native diagnostic for the incubating Bonami AROS port.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bonami.h>
#include <utility/hooks.h>
#include <aros/asmcall.h>
#include <string.h>
#include <bonami/mdns.h>

struct Library *BonamiBase;

/*
 * BonamiProbe [REGISTER name] [TYPE type] [PORT n] [BROWSE type] [SECONDS n]
 *
 * Without arguments it only opens the engine and prints its counters. With
 * REGISTER it announces an instance of TYPE (default _bonamiprobe._udp) for
 * SECONDS; with BROWSE it prints what the engine reports for that type.
 */

#define SEEN_MAX 16

static struct Task *mainTask;
static struct MDNSBrowseMsg seen[SEEN_MAX];
static volatile ULONG seenWrite;

/* Runs in the engine's task: copy and wake the program. */
AROS_UFH3(static IPTR, browse_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(struct MDNSBrowseMsg *, msg, A1))
{
    AROS_USERFUNC_INIT

    (void)hook;
    (void)object;
    Forbid();
    seen[seenWrite % SEEN_MAX] = *msg;
    ++seenWrite;
    Permit();
    Signal(mainTask, SIGBREAKF_CTRL_F);
    return 0;

    AROS_USERFUNC_EXIT
}

static void print_status(const char *when)
{
    ULONG received = 0, sent = 0, answers = 0;
    struct TagItem status[] = {
        { MDNS_StatusRxPackets, 0 },
        { MDNS_StatusTxPackets, 0 },
        { MDNS_StatusRxAnswers, 0 },
        { TAG_DONE, 0 }
    };
    LONG result;

    status[0].ti_Data = (IPTR)&received;
    status[1].ti_Data = (IPTR)&sent;
    status[2].ti_Data = (IPTR)&answers;
    result = GetMDNSStatusA(status);
    if (result == MDNS_OK)
        Printf("BonamiProbe: %s, rx=%lu tx=%lu answers=%lu\n", when,
            received, sent, answers);
    else
        Printf("BonamiProbe: status failed: %ld\n", result);
}

int main(void)
{
    struct Library *socketBase;
    struct RDArgs *args;
    IPTR arg[5] = { 0, 0, 0, 0, 0 };
    struct Hook hook;
    MDNSRegisterRef registered = NULL;
    MDNSBrowseRef browsing = NULL;
    ULONG seenRead = 0, seconds = 0, tick;
    LONG result;

    mainTask = FindTask(NULL);
    args = ReadArgs("REGISTER/K,TYPE/K,PORT/N,BROWSE/K,SECONDS/N", arg, NULL);
    if (args == NULL) {
        PrintFault(IoErr(), "BonamiProbe");
        return RETURN_FAIL;
    }
    if (arg[4])
        seconds = *(LONG *)arg[4];
    else if (arg[0] || arg[3])
        seconds = 10;

    BonamiBase = OpenLibrary("bonami.library", 40);
    if (BonamiBase == NULL) {
        Printf("BonamiProbe: cannot open bonami.library version 40\n");
        FreeArgs(args);
        return RETURN_FAIL;
    }

    socketBase = OpenLibrary("bsdsocket.library", 4);
    if (socketBase == NULL) {
        Printf("BonamiProbe: bsdsocket.library is unavailable\n");
    } else {
        Printf("BonamiProbe: bsdsocket.library is available\n");
        CloseLibrary(socketBase);
    }

    result = OpenMDNSEngine();
    if (result != MDNS_OK) {
        Printf("BonamiProbe: OpenMDNSEngine failed: %ld\n", result);
        CloseLibrary(BonamiBase);
        BonamiBase = NULL;
        FreeArgs(args);
        return RETURN_ERROR;
    }
    print_status("engine ready");

    if (arg[0]) {
        struct TagItem tags[] = {
            { MDNS_ServiceName, arg[0] },
            { MDNS_ServiceType,
              arg[1] ? arg[1] : (IPTR)"_bonamiprobe._udp" },
            { MDNS_Port, arg[2] ? (IPTR)*(LONG *)arg[2] : 5004 },
            { MDNS_RegisterRef, (IPTR)&registered },
            { TAG_DONE, 0 }
        };

        result = RegisterServiceA(tags);
        Printf("BonamiProbe: register \"%s\" %s: %ld\n", (STRPTR)arg[0],
            (STRPTR)tags[1].ti_Data, result);
    }
    if (result == MDNS_OK && arg[3]) {
        struct TagItem tags[] = {
            { MDNS_BrowseRef, (IPTR)&browsing },
            { TAG_DONE, 0 }
        };

        memset(&hook, 0, sizeof(hook));
        hook.h_Entry = (HOOKFUNC)(APTR)AROS_ASMSYMNAME(browse_hook);
        result = BrowseServiceTypeA((STRPTR)arg[3], &hook, tags);
        Printf("BonamiProbe: browse %s: %ld\n", (STRPTR)arg[3], result);
    }

    /* Ten looks a second, so that Ctrl-C and the hook are both seen. */
    for (tick = 0; result == MDNS_OK && tick < seconds * 10; ++tick) {
        Delay(5);
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        SetSignal(0, SIGBREAKF_CTRL_F);
        while (seenRead != seenWrite) {
            struct MDNSBrowseMsg *m = &seen[seenRead++ % SEEN_MAX];
            ULONG a = m->bm_Instance.si_HostAddr;
            UBYTE *b = (UBYTE *)&a;

            Printf("BonamiProbe: %s \"%s\" %s host %s %ld.%ld.%ld.%ld port %ld\n",
                m->bm_Code == MEVENT_ADD ? "add" :
                m->bm_Code == MEVENT_REMOVE ? "remove" : "event",
                m->bm_Instance.si_Name, m->bm_Instance.si_Type,
                m->bm_Instance.si_HostName,
                (LONG)b[0], (LONG)b[1], (LONG)b[2], (LONG)b[3],
                (LONG)m->bm_Instance.si_Port);
        }
    }

    if (browsing != NULL)
        StopBrowse(browsing);
    if (registered != NULL)
        DeregisterService(registered);
    if (seconds != 0)
        print_status("done");

    CloseMDNSEngine();
    CloseLibrary(BonamiBase);
    BonamiBase = NULL;
    FreeArgs(args);
    return result == MDNS_OK ? RETURN_OK : RETURN_ERROR;
}
