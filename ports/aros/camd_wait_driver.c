/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Slow loopback CAMD driver for the legacy CamdWait regression.
*/

#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dostags.h>
#include <exec/types.h>
#include <midi/camddevices.h>
#include <libcore/compiler.h>


struct ExecBase *SysBase;

static struct Task *worker_task;
static BYTE worker_sig = -1;
static volatile LONG worker_status;
static ULONG (ASM *transmit_func)(REG(a2) APTR userdata);
static void (ASM *receive_func)(REG(d0) UWORD input, REG(a2) APTR userdata);
static APTR port_userdata;


int main(void)
{
    return -1;
}


static SAVEDS void WaitDriverWorker(void)
{
    ULONG signals;

    worker_sig = AllocSignal(-1);
    if (worker_sig == -1) {
        worker_status = 2;
        return;
    }
    worker_task = FindTask(NULL);
    worker_status = 1;

    for (;;) {
        ULONG data;

        signals = Wait((1UL << worker_sig) | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) break;
        if (!(signals & (1UL << worker_sig))) continue;

        while (transmit_func != NULL) {
            data = transmit_func(port_userdata);
            if (data == 0x100) break;
            if (receive_func != NULL)
                receive_func((UWORD)data, port_userdata);
            Delay(1);
        }
    }

    FreeSignal(worker_sig);
    worker_sig = -1;
    worker_status = 3;
}


SAVEDS ASM BOOL Init(REG(a6) APTR sysbase)
{
    struct Process *process;

    SysBase = sysbase;
    DOSBase = (APTR)OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return FALSE;
    worker_status = 0;
    process = CreateNewProcTags(
        NP_Entry, (IPTR)WaitDriverWorker,
        NP_Name, (IPTR)"CAMD wait test driver",
        NP_Priority, 10,
        TAG_DONE);
    if (process == NULL) {
        CloseLibrary((struct Library *)DOSBase);
        DOSBase = NULL;
        return FALSE;
    }
    while (worker_status == 0) Delay(1);
    if (worker_status == 1) return TRUE;
    CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
    return FALSE;
}


void Expunge(void)
{
    if (worker_status != 1) return;
    Signal(worker_task, SIGBREAKF_CTRL_C);
    while (worker_status != 3) Delay(1);
    CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
}


SAVEDS ASM void ActivateXmit(REG(a2) APTR userdata, ULONG REG(d0) portnum)
{
    (void)userdata;
    if (portnum == 0 && worker_status == 1)
        Signal(worker_task, 1UL << worker_sig);
}


static struct MidiPortData midiportdata = {
    ActivateXmit
};


SAVEDS ASM struct MidiPortData *OpenPort(
    REG(a3) struct MidiDeviceData *data,
    REG(d0) LONG portnum,
    REG(a0) ULONG (ASM *transmit)(REG(a2) APTR userdata),
    REG(a1) void (ASM *receive)(REG(d0) UWORD input, REG(a2) APTR userdata),
    REG(a2) APTR userdata)
{
    (void)data;
    if (portnum != 0) return NULL;
    port_userdata = userdata;
    receive_func = receive;
    transmit_func = transmit;
    return &midiportdata;
}


ASM void ClosePort(REG(a3) struct MidiDeviceData *data, REG(d0) LONG portnum)
{
    (void)data;
    if (portnum == 0) transmit_func = NULL;
}


const struct MidiDeviceData mididevicedata = {
    MDD_Magic,
    "waitdriver",
    "CAMD wait test driver V1.0",
    1,
    0,
    Init,
    Expunge,
    OpenPort,
    ClosePort,
    1,
    1
};
