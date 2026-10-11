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
#include <bonami/nipc.h>
#include <envoy/nipc.h>

struct Library *BonamiBase;

/*
 * BonamiProbe [REGISTER name] [TYPE type] [PORT n] [BROWSE type] [SECONDS n]
 *             [LEAK] [PUBLISH entity] [INQUIRE entity]
 *
 * Without arguments it only opens the engine and prints its counters. With
 * REGISTER it announces an instance of TYPE (default _bonamiprobe._udp) for
 * SECONDS; with BROWSE it prints what the engine reports for that type.
 * With LEAK it ends without withdrawing either, so that closing the library
 * has to. PUBLISH announces this host as an Envoy host with that entity and
 * the service "Probe"; INQUIRE asks which Envoy hosts have an entity ("*"
 * for any) the way nipc.library would, through the Bonami bridge.
 */

#define SEEN_MAX 16

static struct Task *mainTask;
static struct MDNSBrowseMsg seen[SEEN_MAX];
static volatile ULONG seenWrite;

#define ANSWER_MAX 8
static char answers[ANSWER_MAX][160];
static volatile ULONG answerWrite;

static void append(char *to, ULONG size, const char *text)
{
    ULONG len = strlen(to);

    if (len + strlen(text) < size)
        strcpy(to + len, text);
}

/* Runs in mdnsd: the tag list and its strings last only for this call. */
AROS_UFH3(static IPTR, inquiry_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(struct TagItem *, tags, A1))
{
    AROS_USERFUNC_INIT

    char *line = answers[answerWrite % ANSWER_MAX];
    UBYTE *b;

    (void)hook;
    (void)object;
    line[0] = '\0';
    if (tags == NULL) {
        strcpy(line, " end");
    } else {
        for (; tags->ti_Tag != TAG_DONE; tags++) {
            switch (tags->ti_Tag) {
            case QUERY_HOSTNAME:
                append(line, 160, " host=");
                append(line, 160, (char *)tags->ti_Data);
                break;
            case QUERY_ENTITY:
                append(line, 160, " entity=");
                append(line, 160, (char *)tags->ti_Data);
                break;
            case QUERY_SERVICE:
                append(line, 160, " service=");
                append(line, 160, (char *)tags->ti_Data);
                break;
            case QUERY_OWNER:
                append(line, 160, " owner=");
                append(line, 160, (char *)tags->ti_Data);
                break;
            case QUERY_IPADDR:
                /* a number, 0xAABBCCDD for A.B.C.D */
                b = (UBYTE *)(line + strlen(line));
                if (strlen(line) + 20 < 160) {
                    ULONG a = (ULONG)tags->ti_Data;
                    ULONG part, shift;

                    strcpy((char *)b, " ip=");
                    b += 4;
                    for (shift = 32; shift != 0; ) {
                        shift -= 8;
                        part = (a >> shift) & 0xff;
                        if (part >= 100) *b++ = (UBYTE)('0' + part / 100);
                        if (part >= 10) *b++ = (UBYTE)('0' + part / 10 % 10);
                        *b++ = (UBYTE)('0' + part % 10);
                        if (shift != 0) *b++ = '.';
                    }
                    *b = '\0';
                }
                break;
            }
        }
    }
    ++answerWrite;
    Signal(mainTask, SIGBREAKF_CTRL_F);
    return 1;

    AROS_USERFUNC_EXIT
}

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
    IPTR arg[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    struct Hook inquiry;
    ULONG answerRead = 0;
    struct Hook hook;
    MDNSRegisterRef registered = NULL;
    MDNSBrowseRef browsing = NULL;
    ULONG seenRead = 0, seconds = 0, tick;
    LONG result;

    mainTask = FindTask(NULL);
    args = ReadArgs("REGISTER/K,TYPE/K,PORT/N,BROWSE/K,SECONDS/N,LEAK/S,PUBLISH/K,INQUIRE/K", arg, NULL);
    if (args == NULL) {
        PrintFault(IoErr(), "BonamiProbe");
        return RETURN_FAIL;
    }
    if (arg[4])
        seconds = *(LONG *)arg[4];
    else if (arg[0] || arg[3] || arg[6] || arg[7])
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

    if (result == MDNS_OK && arg[6]) {
        struct TagItem tags[] = {
            { MDNS_NIPC_Entity, arg[6] },
            { MDNS_NIPC_Service, (IPTR)"Probe" },
            { MDNS_NIPC_Owner, (IPTR)"tester" },
            { TAG_DONE, 0 }
        };

        result = PublishNIPCEntitiesA(tags);
        Printf("BonamiProbe: publish Envoy entity %s: %ld\n", (STRPTR)arg[6],
            result);
    }
    if (result == MDNS_OK && arg[7]) {
        struct TagItem tags[] = {
            { QUERY_HOSTNAME, 0 },
            { QUERY_IPADDR, 0 },
            { QUERY_ENTITY, 0 },
            { QUERY_SERVICE, 0 },
            { QUERY_OWNER, 0 },
            { MATCH_ENTITY, arg[7] },
            { TAG_DONE, 0 }
        };

        if (strcmp((char *)arg[7], "*") == 0)
            tags[5].ti_Tag = TAG_DONE;
        memset(&inquiry, 0, sizeof(inquiry));
        inquiry.h_Entry = (HOOKFUNC)(APTR)AROS_ASMSYMNAME(inquiry_hook);
        result = InquiryFromNIPCTagsA(&inquiry, seconds > 2 ? seconds - 2 : 1,
            16, tags);
        Printf("BonamiProbe: inquire Envoy entity %s: %ld\n", (STRPTR)arg[7],
            result);
    }

    /* Ten looks a second, so that Ctrl-C and the hook are both seen. */
    for (tick = 0; result == MDNS_OK && tick < seconds * 10; ++tick) {
        Delay(5);
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        SetSignal(0, SIGBREAKF_CTRL_F);
        while (answerRead != answerWrite)
            Printf("BonamiProbe: envoy%s\n",
                answers[answerRead++ % ANSWER_MAX]);
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

    if (seconds != 0)
        print_status("done");
    if (!arg[5]) {
        if (browsing != NULL)
            StopBrowse(browsing);
        if (registered != NULL)
            DeregisterService(registered);
        CloseMDNSEngine();
    }
    CloseLibrary(BonamiBase);
    BonamiBase = NULL;
    FreeArgs(args);
    return result == MDNS_OK ? RETURN_OK : RETURN_ERROR;
}
