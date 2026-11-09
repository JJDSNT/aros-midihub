/* Minimal resident native-UMP driver and end-to-end CAMD 43 probe. */
#include <dos/dos.h>
#include <exec/libraries.h>
#include <midi/camd.h>
#include <midi/camddevices.h>
#include <midi/camdendpoint.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

struct Library *CamdBase;
static const struct MidiUMPDriverHostV1 *driver_host;
static APTR driver_host_context;
static struct MidiPortData fallback_port;
static int failures;

static void check(int condition, const char *name)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

static BOOL ASM fallback_init(REG(a6) APTR sysbase)
{ (void)sysbase; return TRUE; }
static void fallback_expunge(void) { }
static void ASM fallback_xmit(REG(a2) APTR context, REG(d0) ULONG port)
{ (void)context; (void)port; }
static struct MidiPortData *ASM fallback_open(
    REG(a3) struct MidiDeviceData *data, REG(d0) LONG port,
    REG(a0) ULONG (* ASM transmit)(APTR REG(a2) context),
    REG(a1) void (* ASM receive)(UWORD REG(d0) input, APTR REG(a2) context),
    REG(a2) APTR context)
{
    (void)data; (void)port; (void)transmit; (void)receive; (void)context;
    fallback_port.ActivateXmit = fallback_xmit;
    return &fallback_port;
}
static void ASM fallback_close(REG(a3) struct MidiDeviceData *data,
                               REG(d0) LONG port)
{ (void)data; (void)port; }

static LONG ASM ump_attach(REG(a0) APTR context,
    REG(a1) const struct MidiUMPDriverHostV1 *host,
    REG(a2) APTR host_context)
{
    (void)context;
    if (!host || host->Size != sizeof(*host) || host->Version != 1)
        return CAMD_UMP_DRIVER_INVALID;
    driver_host = host;
    driver_host_context = host_context;
    return CAMD_UMP_DRIVER_OK;
}
static void ASM ump_detach(REG(a0) APTR context)
{ (void)context; driver_host = NULL; driver_host_context = NULL; }
static LONG ASM ump_open(REG(a0) APTR context, REG(d0) ULONG port,
                         REG(d1) ULONG direction)
{
    (void)context;
    return port == 0 && (direction == CAMD_DIRECTION_INPUT ||
                         direction == CAMD_DIRECTION_OUTPUT)
               ? CAMD_UMP_DRIVER_OK : CAMD_UMP_DRIVER_INVALID;
}
static void ASM ump_close(REG(a0) APTR context, REG(d0) ULONG port,
                          REG(d1) ULONG direction)
{ (void)context; (void)port; (void)direction; }
static LONG ASM ump_send(REG(a0) APTR context, REG(d0) ULONG port,
    REG(a1) const struct CAMDUMPEventV1 *events, REG(d1) ULONG count)
{
    ULONG dropped = 0;
    (void)context;
    if (!driver_host) return CAMD_UMP_DRIVER_OFFLINE;
    return driver_host->SubmitUMP(driver_host_context, port, events, count,
                                  &dropped);
}
static LONG ASM ump_query(REG(a0) APTR context, REG(d0) ULONG port,
                          REG(a1) struct MidiUMPDriverPortInfoV1 *info)
{
    static struct CAMDGroupInfoV1 group;
    static struct CAMDFunctionBlockInfoV1 block;
    (void)context;
    if (port != 0 || !info || info->Size != sizeof(*info) || info->Version != 1)
        return CAMD_UMP_DRIVER_INVALID;
    memset(&group, 0, sizeof(group));
    group.Size = sizeof(group); group.Version = 1; group.Group = 0;
    group.Flags = CAMD_GROUP_FLAG_INPUT | CAMD_GROUP_FLAG_OUTPUT;
    group.Protocol = CAMD_PROTOCOL_MIDI2; strcpy(group.Name, "Reference");
    memset(&block, 0, sizeof(block));
    block.Size = sizeof(block); block.Version = 1; block.Number = 0;
    block.Flags = CAMD_BLOCK_FLAG_INPUT | CAMD_BLOCK_FLAG_OUTPUT |
                  CAMD_BLOCK_FLAG_ACTIVE;
    block.FirstGroup = 0; block.GroupCount = 1; strcpy(block.Name, "Loopback");
    info->Directions = CAMD_DIRECTION_INPUT | CAMD_DIRECTION_OUTPUT;
    info->State = CAMD_ENDPOINT_AVAILABLE;
    info->ProtocolCapabilities = CAMD_PROTOCOL_MIDI1 | CAMD_PROTOCOL_MIDI2;
    info->CurrentProtocol = CAMD_PROTOCOL_MIDI2;
    info->ProductInstance = "native-ump-reference";
    info->Groups = &group; info->GroupCount = 1;
    info->FunctionBlocks = &block; info->FunctionBlockCount = 1;
    return CAMD_UMP_DRIVER_OK;
}

static struct MidiDeviceDataUMPV1 reference_driver = {
    {
        MDD_Magic, "umpref", "CAMD native UMP reference", 1, 0,
        fallback_init, fallback_expunge, fallback_open, fallback_close, 1,
        MDD_F_NEW_FORMAT | MDD_F_UMP_EXTENSION
    },
    {
        sizeof(struct MidiUMPDriverExtensionV1), 1, 0, "camd-ump-reference",
        NULL, ump_attach, ump_detach, ump_open, ump_close, ump_send, ump_query
    }
};

int main(void)
{
    struct CAMDHandleV1 snapshot, input, output;
    struct CAMDEndpointInfoV1 info;
    struct CAMDSessionRequestV1 request;
    struct CAMDUMPEventV1 sent, received;
    ULONG count = 0, i;
    LONG index = -1, result;

    setvbuf(stdout, NULL, _IONBF, 0);
    CamdBase = OpenLibrary((CONST_STRPTR)"camd.library", 43);
    if (!CamdBase) { puts("FAIL camd.library 43 unavailable"); return 20; }
    check(AddMidiDevice(&reference_driver.Device),
          "AddMidiDevice accepts the adjacent native-UMP extension");
    if (ObtainEndpointSnapshot(&snapshot, &count, NULL) == CAMD_RESULT_OK) {
        for (i = 0; i < count; ++i) {
            memset(&info, 0, sizeof(info)); info.Size = sizeof(info); info.Version = 1;
            if (GetEndpointInfo(&snapshot, i, &info) == CAMD_RESULT_OK &&
                !strcmp(info.Name, "umpref.0")) { index = (LONG)i; break; }
        }
        check(index >= 0 && info.NativeDataFormats == CAMD_DATA_FORMAT_UMP &&
              info.CurrentProtocol == CAMD_PROTOCOL_MIDI2 &&
              !strcmp(info.Transport, "camd-ump-reference"),
              "the resident driver publishes a native MIDI 2.0 UMP endpoint");
        ReleaseEndpointSnapshot(&snapshot);
    } else check(0, "the resident driver publishes a native MIDI 2.0 UMP endpoint");
    if (index < 0) goto remove;
    memset(&request, 0, sizeof(request)); request.Size = sizeof(request);
    request.Version = 1; request.EndpointID = info.ID;
    request.DataFormat = CAMD_DATA_FORMAT_UMP; request.Protocol = CAMD_PROTOCOL_MIDI2;
    request.QueueCapacity = 8; request.Signal = CAMD_SIGNAL_NONE;
    request.Direction = CAMD_DIRECTION_INPUT;
    result = OpenEndpointSession(&request, &input, NULL);
    request.Direction = CAMD_DIRECTION_OUTPUT;
    if (result == CAMD_RESULT_OK) result = OpenEndpointSession(&request, &output, NULL);
    check(result == CAMD_RESULT_OK, "independent native UMP input and output sessions open");
    if (result != CAMD_RESULT_OK) goto remove;
    memset(&sent, 0, sizeof(sent)); sent.Size = sizeof(sent); sent.Version = 1;
    sent.WordCount = 4; sent.Words[0] = 0x50160d01UL;
    sent.Words[1] = 0x02030405UL; sent.Words[2] = 0x06070809UL;
    sent.Words[3] = 0x0a0b0c0dUL;
    check(PutEndpointUMP(&output, &sent, 1) == CAMD_RESULT_OK,
          "a complete 128-bit UMP is accepted without MIDI 1.0 conversion");
    result = CAMD_RESULT_EMPTY;
    for (i = 0; i < 100 && result == CAMD_RESULT_EMPTY; ++i) {
        Delay(1); memset(&received, 0, sizeof(received));
        received.Size = sizeof(received); received.Version = 1;
        result = GetEndpointUMP(&input, &received);
    }
    check(result == CAMD_RESULT_OK && received.WordCount == 4 &&
          !memcmp(received.Words, sent.Words, sizeof(sent.Words)),
          "the reference driver returns the same four UMP words");
    check(!RemoveMidiDevice(&reference_driver.Device),
          "removal is refused while endpoint sessions pin the driver");
    CloseEndpointSession(&output); CloseEndpointSession(&input);
remove:
    check(RemoveMidiDevice(&reference_driver.Device),
          "the native UMP driver detaches after its sessions close");
    CloseLibrary(CamdBase);
    printf("ump-driver-probe: %d failure(s)\n", failures);
    return failures ? 5 : 0;
}
