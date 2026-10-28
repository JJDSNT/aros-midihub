/* Native regression for bonami.library's TXT allocation helpers. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bonami.h>
#include <bonami/bonami.h>
#include <stdio.h>
#include <string.h>

struct Library *BonamiBase;

static int failures;

static void check(int condition, const char *name)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}

int main(void)
{
    static char maximum[MDNS_MAX_TXT + 1];
    static char too_long[MDNS_MAX_TXT + 2];
    struct TagItem missing[] = {{TAG_IGNORE, 0}, {TAG_END, 0}};
    struct TagItem null_text[] = {{MDNS_TXTRecord, 0}, {TAG_END, 0}};
    struct TagItem empty[] = {{MDNS_TXTRecord, (IPTR)""}, {TAG_END, 0}};
    struct TagItem ordinary[] = {
        {MDNS_TXTRecord, (IPTR)"name=AROS escaped\\ value=yes"},
        {TAG_END, 0}
    };
    struct TagItem longest[] = {
        {MDNS_TXTRecord, (IPTR)maximum}, {TAG_END, 0}
    };
    struct TagItem excessive[] = {
        {MDNS_TXTRecord, (IPTR)too_long}, {TAG_END, 0}
    };
    APTR record = (APTR)123;
    ULONG length = 77;
    LONG result;

    memset(maximum, 'x', MDNS_MAX_TXT);
    maximum[MDNS_MAX_TXT] = '\0';
    memset(too_long, 'y', MDNS_MAX_TXT + 1);
    too_long[MDNS_MAX_TXT + 1] = '\0';

    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 41);
    if (BonamiBase == NULL) {
        puts("BONAMI-TXT: cannot open library");
        return 20;
    }
    if (BonamiBase->lib_Version < 41 ||
        (BonamiBase->lib_Version == 41 && BonamiBase->lib_Revision < 3)) {
        puts("BONAMI-TXT: library predates the reserved helper slots");
        CloseLibrary(BonamiBase);
        return 20;
    }
    check(BonamiBase->lib_Version > 41 ||
          (BonamiBase->lib_Version == 41 && BonamiBase->lib_Revision >= 4),
          "the helper release is 41.4 or newer");

    result = MakeTXTRecordA(NULL, &record, &length);
    check(result == MDNSERR_BADPARAM && record == (APTR)123 && length == 77,
          "NULL tags fail without changing outputs");
    result = MakeTXTRecordA(missing, &record, &length);
    check(result == MDNSERR_BADPARAM && record == (APTR)123 && length == 77,
          "a missing TXT tag fails without changing outputs");
    result = MakeTXTRecordA(null_text, &record, &length);
    check(result == MDNSERR_BADPARAM && record == (APTR)123 && length == 77,
          "a NULL TXT value fails without changing outputs");
    check(MakeTXTRecordA(empty, NULL, &length) == MDNSERR_BADPARAM &&
          length == 77, "a NULL record output is rejected");
    check(MakeTXTRecordA(empty, &record, NULL) == MDNSERR_BADPARAM &&
          record == (APTR)123, "a NULL length output is rejected");

    result = MakeTXTRecordA(empty, &record, &length);
    check(result == MDNS_OK && record != NULL && length == 0 &&
          ((char *)record)[0] == '\0', "an empty TXT string is copied");
    if (result == MDNS_OK) FreeTXTRecord(record);

    record = NULL;
    length = 0;
    result = MakeTXTRecordA(ordinary, &record, &length);
    check(result == MDNS_OK && record != NULL &&
          length == strlen((char *)ordinary[0].ti_Data) &&
          strcmp((char *)record, (char *)ordinary[0].ti_Data) == 0,
          "TXT bytes and length are preserved");
    if (result == MDNS_OK) FreeTXTRecord(record);

    record = NULL;
    length = 0;
    result = MakeTXTRecordA(longest, &record, &length);
    check(result == MDNS_OK && record != NULL && length == MDNS_MAX_TXT &&
          memcmp(record, maximum, MDNS_MAX_TXT + 1) == 0,
          "MDNS_MAX_TXT bytes are accepted");
    if (result == MDNS_OK) FreeTXTRecord(record);

    record = (APTR)456;
    length = 88;
    result = MakeTXTRecordA(excessive, &record, &length);
    check(result == MDNSERR_BADPARAM && record == (APTR)456 && length == 88,
          "a longer string is rejected without changing outputs");
    FreeTXTRecord(NULL);
    check(1, "freeing NULL is harmless");

    CloseLibrary(BonamiBase);
    printf("BONAMI-TXT: %s (%d failures)\n",
        failures ? "FAIL" : "OK", failures);
    return failures ? 20 : 0;
}
