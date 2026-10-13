/*
 * mdnsreg - registers a service through bonami.library, for the tests of
 * the cut that has the library's registering half and nothing else. It is
 * installed under the name of the package's dnssd, whose -R it takes, so
 * that the tests written for that run against the cut as they are.
 *
 *   -R name type domain port [-t seconds] [k=v ...]   until Ctrl-C
 *   -P name type domain port host a.b.c.d [k=v ...]   for another host
 *   -U name type domain port newport [k=v ...]        registers with the
 *        text "v=1"; six seconds later the port and the text change
 *   -X name type domain port     registers and ends without closing
 *
 * And, under the name of the package's BonamiProbe, the one form of it the
 * tests of registering use:
 *
 *   REGISTER name TYPE type PORT port SECONDS n
 *
 * Not part of what is sent upstream.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bonami.h>
#include <bonami/mdns.h>
#include <aros/macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* opened by hand: -X has to end with the library still open */
struct Library *BonamiBase;

static void join(char *txt, size_t size, int argc, char **argv)
{
    size_t out = 0;
    int i;

    for (i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (i > 0 && out + 1 < size)
            txt[out++] = ' ';
        for (; *arg != '\0' && out + 2 < size; arg++) {
            if (*arg == ' ' || *arg == '\\')
                txt[out++] = '\\';
            txt[out++] = *arg;
        }
    }
    txt[out] = '\0';
}

int main(int argc, char **argv)
{
    struct TagItem tags[10];
    char txt[MDNS_MAX_TXT + 1];
    MDNSRegisterRef ref = NULL;
    char mode;
    int next = 6, nt = 0;
    LONG rc;

    if (argc == 9 && strcmp(argv[1], "REGISTER") == 0 &&
        strcmp(argv[3], "TYPE") == 0 && strcmp(argv[5], "PORT") == 0 &&
        strcmp(argv[7], "SECONDS") == 0) {
        BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 0);
        if (BonamiBase == NULL)
            return RETURN_FAIL;
        tags[0].ti_Tag = MDNS_ServiceName;
        tags[0].ti_Data = (IPTR)argv[2];
        tags[1].ti_Tag = MDNS_ServiceType;
        tags[1].ti_Data = (IPTR)argv[4];
        tags[2].ti_Tag = MDNS_Port;
        tags[2].ti_Data = (IPTR)atoi(argv[6]);
        tags[3].ti_Tag = MDNS_RegisterRef;
        tags[3].ti_Data = (IPTR)&ref;
        tags[4].ti_Tag = TAG_END;
        rc = RegisterServiceA(tags);
        printf("register %s: %ld\n", argv[2], (long)rc);
        if (rc == MDNS_OK) {
            Delay(atoi(argv[8]) * 50);
            DeregisterService(ref);
        }
        CloseLibrary(BonamiBase);
        return rc == MDNS_OK ? RETURN_OK : RETURN_WARN;
    }
    if (argc < 6 || argv[1][0] != '-' || strchr("RPUX", argv[1][1]) == NULL) {
        printf("mdnsreg -R|-P|-U|-X name type domain port ...\n");
        return RETURN_ERROR;
    }
    mode = argv[1][1];
    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 0);
    if (BonamiBase == NULL) {
        printf("no bonami.library\n");
        return RETURN_FAIL;
    }
    tags[nt].ti_Tag = MDNS_ServiceName;
    tags[nt++].ti_Data = (IPTR)argv[2];
    tags[nt].ti_Tag = MDNS_ServiceType;
    tags[nt++].ti_Data = (IPTR)argv[3];
    tags[nt].ti_Tag = MDNS_Port;
    tags[nt++].ti_Data = (IPTR)atoi(argv[5]);
    tags[nt].ti_Tag = MDNS_RegisterRef;
    tags[nt++].ti_Data = (IPTR)&ref;
    if (mode == 'R' && argc >= 8 && strcmp(argv[6], "-t") == 0) {
        tags[nt].ti_Tag = MDNS_TimeToLive;
        tags[nt++].ti_Data = (IPTR)atoi(argv[7]);
        next = 8;
    }
    if (mode == 'P' && argc >= 8) {
        unsigned a, b, c, d;

        if (sscanf(argv[7], "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
            printf("bad address\n");
            return RETURN_ERROR;
        }
        tags[nt].ti_Tag = MDNS_HostName;
        tags[nt++].ti_Data = (IPTR)argv[6];
        tags[nt].ti_Tag = MDNS_HostAddr;
        tags[nt++].ti_Data = (IPTR)AROS_LONG2BE((ULONG)
            ((a << 24) | (b << 16) | (c << 8) | d));
        next = 8;
    }
    if (mode == 'U')
        next = 7;
    join(txt, sizeof(txt), argc - next, argv + next);
    if (mode == 'U' || txt[0] != '\0') {
        tags[nt].ti_Tag = MDNS_TXTRecord;
        tags[nt++].ti_Data = (IPTR)(mode == 'U' ? "v=1" : txt);
    }
    tags[nt].ti_Tag = TAG_END;

    rc = RegisterServiceA(tags);
    if (rc != MDNS_OK) {
        printf("register failed: %ld\n", (long)rc);
        CloseLibrary(BonamiBase);
        return RETURN_WARN;
    }
    printf("registered %s\n", argv[2]);
    fflush(stdout);
    if (mode == 'X')
        return RETURN_OK;       /* neither withdrawn nor closed */
    if (mode == 'U') {
        struct TagItem change[3];

        Delay(6 * 50);
        change[0].ti_Tag = MDNS_Port;
        change[0].ti_Data = (IPTR)atoi(argv[6]);
        change[1].ti_Tag = MDNS_TXTRecord;
        change[1].ti_Data = (IPTR)txt;
        change[2].ti_Tag = TAG_END;
        rc = UpdateServiceA(ref, change);
        printf("update: %ld\n", (long)rc);
        fflush(stdout);
    }
    Wait(SIGBREAKF_CTRL_C);
    DeregisterService(ref);
    CloseLibrary(BonamiBase);
    return RETURN_OK;
}
