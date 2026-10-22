#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <string.h>
#include "netmidi_status.h"

static int failures, checks;

static void check(int condition, const char *label)
{
    ++checks;
    printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) ++failures;
}

static LONG query(char *buffer, size_t size)
{
    LONG result = SystemTags((CONST_STRPTR)"C:NetMIDIStatus >SYS:status-review.out", TAG_DONE);
    FILE *file = fopen("SYS:status-review.out", "r");
    size_t length = file ? fread(buffer, 1, size - 1, file) : 0;
    if (file) fclose(file);
    buffer[length] = 0;
    return result;
}

int main(void)
{
    struct NetMidiStatus *status;
    char buffer[2048];
    LONG result;

    check(!FindSemaphore((STRPTR)NETMIDI_STATUS_NAME), "no service before fixture");
    if (failures) return 20;
    result = query(buffer, sizeof(buffer));
    check(result == 5 && strstr(buffer, "not running"), "absent service: warning and diagnostic");
    status = AllocMem(sizeof(*status), MEMF_PUBLIC | MEMF_CLEAR);
    if (!status) return 20;
    status->ns_Semaphore.ss_Link.ln_Name = NETMIDI_STATUS_NAME;
    status->ns_Version = NETMIDI_STATUS_VERSION + 1;
    AddSemaphore(&status->ns_Semaphore);
    result = query(buffer, sizeof(buffer));
    check(result == 5 && strstr(buffer, "another version"), "incompatible version: warning and diagnostic");
    ObtainSemaphore(&status->ns_Semaphore);
    status->ns_Version = NETMIDI_STATUS_VERSION;
    memcpy(status->ns_Session, "Status fixture", sizeof("Status fixture"));
    status->ns_Refused = 7;
    status->ns_Port[3].ps_Present = 1;
    memcpy(status->ns_Port[3].ps_Name, "Apple peer", sizeof("Apple peer"));
    status->ns_Port[3].ps_MessagesIn = 4;
    status->ns_Port[3].ps_PacketsIn = 1;
    status->ns_Port[3].ps_MessagesOut = 17;
    status->ns_Port[17].ps_Present = 1;
    memcpy(status->ns_Port[17].ps_Name, "MIDI2 peer", sizeof("MIDI2 peer"));
    status->ns_Port[17].ps_SysExDropped = 2;
    ReleaseSemaphore(&status->ns_Semaphore);
    result = query(buffer, sizeof(buffer));
    check(result == 0 && strstr(buffer, "session \"Status fixture\", 7 invitations refused"),
          "session name and refused invitations");
    check(strstr(buffer, "applemidi.3 \"Apple peer\": in 4 messages in 1 packets, 0 lost; out 17 messages") != NULL,
          "AppleMIDI port and traffic counters");
    check(strstr(buffer, "networkmidi2.1 \"MIDI2 peer\"") && strstr(buffer, "SysEx dropped 2") &&
          !strstr(buffer, "applemidi.0 "), "MIDI2 port numbering and inactive-port omission");
    RemSemaphore(&status->ns_Semaphore);
    FreeMem(status, sizeof(*status));
    result = query(buffer, sizeof(buffer));
    check(result == 5 && strstr(buffer, "not running"), "query after publisher removal");
    printf("STATUS-PROBE %d checks, %d failures\n", checks, failures);
    return failures ? 20 : 0;
}
