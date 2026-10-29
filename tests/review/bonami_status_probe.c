/* Native regression for bonami.library's daemon status snapshot. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bonami.h>
#include <bonami/bonami.h>
#include <stdio.h>

struct Library *BonamiBase;

static int failures;

static void check(int condition, const char *name)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}

int main(void)
{
    ULONG first[6] = {
        0xdeadbeef, 0xdeadbeef, 0xdeadbeef,
        0xdeadbeef, 0xdeadbeef, 0xdeadbeef
    };
    ULONG second[6] = {0};
    struct TagItem tags[] = {
        {MDNS_StatusRxPackets, (IPTR)&first[0]},
        {MDNS_StatusTxPackets, (IPTR)&first[1]},
        {MDNS_StatusRxAnswers, (IPTR)&first[2]},
        {MDNS_StatusDbgRecs, (IPTR)&first[3]},
        {MDNS_StatusDbgPtr, (IPTR)&first[4]},
        {MDNS_StatusDbgNotify, (IPTR)&first[5]},
        {MDNS_StatusDbgNotify, 0},
        {TAG_USER + 0x7fff, (IPTR)&failures},
        {TAG_END, 0}
    };
    struct TagItem again[] = {
        {MDNS_StatusRxPackets, (IPTR)&second[0]},
        {MDNS_StatusTxPackets, (IPTR)&second[1]},
        {MDNS_StatusRxAnswers, (IPTR)&second[2]},
        {MDNS_StatusDbgRecs, (IPTR)&second[3]},
        {MDNS_StatusDbgPtr, (IPTR)&second[4]},
        {MDNS_StatusDbgNotify, (IPTR)&second[5]},
        {TAG_END, 0}
    };
    LONG result;

    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 41);
    if (BonamiBase == NULL) {
        puts("BONAMI-STATUS: cannot open library");
        return 20;
    }
    check(BonamiBase->lib_Version > 41 ||
          (BonamiBase->lib_Version == 41 && BonamiBase->lib_Revision >= 5),
          "the status release is 41.5 or newer");

    result = GetMDNSStatusA(tags);
    check(result == MDNS_OK, "a running daemon returns a status snapshot");
    if (result == MDNS_OK) {
        check(first[0] != 0xdeadbeef && first[1] != 0xdeadbeef &&
              first[2] != 0xdeadbeef && first[3] != 0xdeadbeef &&
              first[4] != 0xdeadbeef && first[5] != 0xdeadbeef,
              "all six requested counters are written");
        check(first[4] <= first[3],
              "PTR records do not exceed discovery records");
        check(failures == 0, "unknown and NULL-output tags are ignored");
    }

    result = GetMDNSStatusA(again);
    check(result == MDNS_OK, "a second snapshot succeeds");
    if (result == MDNS_OK) {
        check(second[0] >= first[0] && second[1] >= first[1] &&
              second[2] >= first[2] && second[3] >= first[3] &&
              second[4] >= first[4] && second[5] >= first[5],
              "counters are monotonic while mdnsd remains running");
    }
    check(GetMDNSStatusA(NULL) == MDNS_OK,
          "NULL tags can probe daemon reachability");

    CloseLibrary(BonamiBase);
    printf("BONAMI-STATUS: %s (%d failures)\n",
        failures ? "FAIL" : "OK", failures);
    return failures ? 20 : 0;
}
