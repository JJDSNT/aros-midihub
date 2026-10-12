/* The service's side of the CAMD driver DEVS:Midi/applemidi: the same
 * calls as camd.c, which is a client of camd.library, answered through
 * the driver's adapters instead. The driver may be loaded after the
 * service starts, or not at all: until its adapters are found, what
 * arrives from the network for CAMD is dropped. Each port is one peer's:
 * which, the caller decides. */

#include "camd.h"

#include <proto/exec.h>
#include <exec/memory.h>

#include <string.h>

#include "../applemidi/applemidi_camd.h"
#include "rtpmidi/rtpmidi.h"

int netmidi_camd_open(struct netmidi_camd_runtime *runtime)
{
    if (!runtime)
        return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->sysex_port = -1;
    runtime->signal_bit = AllocSignal(-1);
    runtime->sysex = AllocMem(RTPMIDI_SYSEX_MAX, MEMF_ANY);
    if (runtime->signal_bit < 0 || !runtime->sysex) {
        netmidi_camd_close(runtime);
        return -1;
    }
    runtime->opened = 1;
    return 0;
}

static struct AppleMidiAdapter *port_of(struct netmidi_camd_runtime *runtime,
                                        int number)
{
    struct AppleMidiAdapters *adapters = runtime->adapters;
    return adapters && number >= 0 && number < NETMIDI_CAMD_PORTS ?
           &adapters->aa_Port[number] : NULL;
}

/* Looks for the driver's adapters and takes its ports. */
static void attach(struct netmidi_camd_runtime *runtime)
{
    struct AppleMidiAdapters *adapters;
    struct AppleMidiAdapter *port;
    int number;

    Forbid();
    adapters = (struct AppleMidiAdapters *)
        FindSemaphore((STRPTR)APPLEMIDI_ADAPTERS_NAME);
    /* a driver of another layout is left alone */
    if (adapters && (adapters->aa_Version != APPLEMIDI_ADAPTERS_VERSION ||
                     adapters->aa_Ports < NETMIDI_CAMD_PORTS))
        adapters = NULL;
    Permit();
    if (!adapters)
        return;
    runtime->adapters = adapters;
    for (number = 0; number < NETMIDI_CAMD_PORTS; ++number) {
        port = port_of(runtime, number);
        Disable();
        port->ad_SigMask = 1UL << runtime->signal_bit;
        port->ad_Task = FindTask(NULL);
        Enable();
        /* CAMD may have had the port open all along, with bytes waiting */
        if (port->ad_IsOpen)
            Cause(&port->ad_CAMDTXFunc);
    }
}

void netmidi_camd_close(struct netmidi_camd_runtime *runtime)
{
    struct AppleMidiAdapter *port;
    int number;

    if (!runtime)
        return;
    for (number = 0; number < NETMIDI_CAMD_PORTS; ++number) {
        port = port_of(runtime, number);
        if (!port)
            break;
        Disable();
        port->ad_Task = NULL;
        port->ad_SigMask = 0;
        Enable();
        /* the driver now drops what clients send */
        if (port->ad_IsOpen)
            Cause(&port->ad_CAMDTXFunc);
    }
    /* a delivery to CAMD has finished: there is one task of ours */
    runtime->adapters = NULL;
    if (runtime->signal_bit >= 0)
        FreeSignal(runtime->signal_bit);
    runtime->signal_bit = -1;
    if (runtime->sysex)
        FreeMem(runtime->sysex, RTPMIDI_SYSEX_MAX);
    runtime->sysex = NULL;
    runtime->opened = 0;
}

int netmidi_camd_is_open(const struct netmidi_camd_runtime *runtime)
{
    return runtime && runtime->opened;
}

int netmidi_camd_signal_bit(const struct netmidi_camd_runtime *runtime)
{
    if (!runtime || !runtime->opened)
        return -1;
    return runtime->signal_bit;
}

/* CAMD parses the bytes itself: running status, lengths and SysEx. */
void netmidi_camd_deliver_port(struct netmidi_camd_runtime *runtime,
                               int number, const uint8_t *message,
                               size_t length)
{
    struct AppleMidiAdapter *port;
    size_t i;

    if (!runtime || !runtime->opened)
        return;
    if (!runtime->adapters)
        attach(runtime);
    port = port_of(runtime, number);
    if (!port)
        return;
    /* the driver closes the port under the same lock */
    ObtainSemaphore(&port->ad_Lock);
    if (port->ad_IsOpen)
        for (i = 0; i < length; ++i)
            AROS_UFC2NR(void, port->ad_RXFunc,
                AROS_UFCA(UWORD, message[i], D0),
                AROS_UFCA(APTR, port->ad_UserData, A2));
    ReleaseSemaphore(&port->ad_Lock);
}

void netmidi_camd_deliver(struct netmidi_camd_runtime *runtime,
                          const uint8_t *message, size_t length)
{
    netmidi_camd_deliver_port(runtime, 0, message, length);
}

void netmidi_camd_deliver_sysex(struct netmidi_camd_runtime *runtime,
                                const uint8_t *message, size_t length)
{
    netmidi_camd_deliver_port(runtime, 0, message, length);
}

int netmidi_camd_ports(const struct netmidi_camd_runtime *runtime)
{
    (void)runtime;
    return NETMIDI_CAMD_PORTS;
}

static unsigned data_bytes(uint8_t status)
{
    if (status < 0xf0)
        return (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 1 : 2;
    if (status == 0xf1 || status == 0xf3)
        return 1;
    return status == 0xf2 ? 2 : 0;
}

/* One byte of the stream a CAMD client sent, by the rules of a MIDI 1.0
 * byte stream (MIDI 1.0 Detailed Specification 4.2.1, pages 5, 6, 29 and
 * 30): whole messages go to output. */
static void take_byte(struct netmidi_camd_runtime *all, int number,
                      uint8_t byte, aros_camd_output output, void *context)
{
#define runtime (&all->stream[number])
    if (byte >= 0xf8) {
        /* System Real Time: anywhere, and no new status */
        if (byte != 0xf9 && byte != 0xfd)
            output(context, &byte, 1);
        return;
    }
    if (runtime->in_sysex) {
        if (!(byte & 0x80)) {
            /* one buffer for all ports: a second port's SysEx message
               while the first is unfinished is left out, as one that is
               too long is */
            if (runtime->in_sysex == 1 && all->sysex_port == number &&
                runtime->sysex_length < RTPMIDI_SYSEX_MAX - 1)
                all->sysex[runtime->sysex_length++] = byte;
            else
                runtime->in_sysex = 2;
            return;
        }
        /* EOX, or any other status, ends it */
        if (all->sysex_port == number) {
            if (runtime->in_sysex == 1) {
                all->sysex[runtime->sysex_length++] = 0xf7;
                output(context, all->sysex, runtime->sysex_length);
            }
            all->sysex_port = -1;
        }
        runtime->in_sysex = 0;
        if (byte == 0xf7)
            return;
    }
    if (byte == 0xf0) {
        if (all->sysex_port < 0) {
            all->sysex_port = number;
            runtime->in_sysex = 1;
            all->sysex[0] = 0xf0;
        } else {
            runtime->in_sysex = 2;
        }
        runtime->sysex_length = 1;
        runtime->status = 0;
        return;
    }
    if (byte & 0x80) {
        runtime->have = 0;
        runtime->status = 0;
        if (byte == 0xf7 || byte == 0xf4 || byte == 0xf5)
            return;
        runtime->need = (uint8_t)data_bytes(byte);
        if (!runtime->need) {
            output(context, &byte, 1);
            return;
        }
        runtime->status = byte;
        return;
    }
    if (!runtime->status)
        return;                 /* data with no status before it */
    runtime->message[0] = runtime->status;
    runtime->message[1 + runtime->have++] = byte;
    if (runtime->have < runtime->need)
        return;
    output(context, runtime->message, (size_t)runtime->need + 1);
    runtime->have = 0;
    if (runtime->status >= 0xf0)
        runtime->status = 0;    /* running status is for channel messages */
#undef runtime
}

void netmidi_camd_poll(struct netmidi_camd_runtime *runtime,
                       aros_camd_output output, void *context)
{
    struct AppleMidiAdapter *port;
    ULONG pos;
    int number;

    if (!runtime || !runtime->opened)
        return;
    if (!runtime->adapters)
        attach(runtime);
    for (number = 0; number < NETMIDI_CAMD_PORTS; ++number) {
        port = port_of(runtime, number);
        if (!port)
            return;
        runtime->port_now = number;
        pos = port->ad_TXReadPos;
        while (pos != port->ad_TXWritePos) {
            take_byte(runtime, number, port->ad_TXBuffer[pos], output,
                      context);
            pos = (pos + 1) & (port->ad_TXBufSize - 1);
        }
        port->ad_TXReadPos = pos;
        if (port->ad_TXMore) {
            /* the ring was full: there is room now */
            port->ad_TXMore = FALSE;
            Cause(&port->ad_CAMDTXFunc);
        }
    }
    runtime->port_now = 0;
}
