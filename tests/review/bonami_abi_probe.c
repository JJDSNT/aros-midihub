/* Native ABI smoke test for the register-only AROS Bonami implementation. */
#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bonami.h>
#include <bonami/bonami.h>
#include <stdio.h>

struct Library *BonamiBase;

int main(void)
{
    APTR record = (APTR)123;
    ULONG length = 77;
    struct TagItem zero[] = {{MDNS_TimeToLive, 0}, {TAG_END, 0}};
    struct TagItem high[] = {{MDNS_TimeToLive, 4501}, {TAG_END, 0}};
    struct MDNSServiceInstance instance;
    struct MDNSBrowseMsg browse;
    struct MDNSResolveMsg resolve;
    ULONG id = GetETaskID(FindTask(NULL));
    int failures = 0;

    (void)instance;
    (void)browse;
    (void)resolve;
    BonamiBase = OpenLibrary((CONST_STRPTR)"bonami.library", 40);
    if (!BonamiBase) {
        puts("BONAMI-ABI: cannot open library");
        return 20;
    }
    failures += BonamiBase->lib_Version != BONAMI_LIB_VERSION;
    failures += BonamiBase->lib_Revision != BONAMI_LIB_REVISION;
    failures += BrowseServiceTypeA(NULL, NULL, NULL) != MDNSERR_NOTREADY;
    StopBrowse((MDNSBrowseRef)1);
    failures += ResolveServiceA(NULL, NULL, NULL, NULL) != MDNSERR_NOTREADY;
    StopResolve((MDNSResolveRef)1);
    failures += MakeTXTRecordA(NULL, &record, &length) != MDNSERR_NOTREADY;
    failures += record != (APTR)123 || length != 77;
    FreeTXTRecord((APTR)1);
    failures += GetMDNSStatusA(NULL) != MDNSERR_NOTREADY;
    failures += InquiryFromNIPCTagsA(NULL, 0, 0, NULL) != MDNSERR_NOTREADY;
    failures += PublishNIPCEntitiesA(NULL) != MDNSERR_NOTREADY;
    UnpublishNIPCEntitiesA();
    failures += UpdateServiceA((MDNSRegisterRef)1, zero) != MDNSERR_BADPARAM;
    failures += UpdateServiceA((MDNSRegisterRef)1, high) != MDNSERR_BADPARAM;
    failures += id == 0 || FindTaskByPID(id) != FindTask(NULL);
    CloseLibrary(BonamiBase);
    printf("BONAMI-ABI: %s (%d failures; slots 6-15, version, TTL, task ID)\n",
        failures ? "FAIL" : "OK", failures);
    return failures ? 20 : 0;
}
