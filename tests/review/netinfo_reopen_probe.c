/* Force the reviewed ordering with the actual device and worker: keep the
 * device lock, queue an opener, reply the worker's real death message, then
 * release the lock to the opener before the worker can clean up. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <exec/execbase.h>
#include <exec/lists.h>
#include <stdio.h>
#include <string.h>
#include "base.h"

struct Reopen {
    struct MsgPort *parent;
    struct Message attempting, done;
    ULONG previous_worker, opener_id, unit;
    LONG open_error, reset_error, group_error;
    BOOL fresh, maps_live;
};
static int checks, failures;
#define CHECK(e, label) do { ++checks; printf("%s %s\n", (e) ? "PASS" : (++failures, "FAIL"), label); } while (0)

static void opener(void)
{
    struct Reopen *context = (struct Reopen *)((struct Process *)FindTask(NULL))->pr_ExitData;
    struct MsgPort *port = CreateMsgPort();
    struct NetInfoReq *request = port ? (struct NetInfoReq *)CreateIORequest(port, sizeof(*request)) : NULL;
    context->opener_id = GetETaskID(FindTask(NULL));
    context->open_error = context->reset_error = context->group_error = -99;
    PutMsg(context->parent, &context->attempting);
    if (request) {
        context->open_error = OpenDevice((CONST_STRPTR)NETINFONAME, context->unit,
                                        (struct IORequest *)request, 0);
        if (!context->open_error) {
            struct NetInfoDevice *device = (struct NetInfoDevice *)request->io_Device;
            struct NetInfoPointer *pointer = (struct NetInfoPointer *)request->io_Unit;
            ObtainSemaphore(device->nid_Lock);
            context->fresh = device->nid_WorkerID != context->previous_worker &&
                device->nid_WorkerID != 0 && FindTaskByPID(context->previous_worker) == NULL &&
                device->nid_Task != NULL && device->nid_Death != NULL &&
                device->nid_Death->mn_Node.ln_Type == NT_MESSAGE;
            context->maps_live = device->nid_Maps[context->unit] != NULL &&
                pointer->nip_Map == device->nid_Maps[context->unit];
            ReleaseSemaphore(device->nid_Lock);
            /* With the old code the returned unit belongs to the dying
             * worker. Close it immediately at this task's higher priority;
             * deliberately do not issue I/O through a known stale unit. */
            if (context->fresh && context->maps_live) {
                request->io_Command = CMD_RESET;
                context->reset_error = DoIO((struct IORequest *)request);
            }
            CloseDevice((struct IORequest *)request);
            if (context->fresh) {
                context->group_error = OpenDevice((CONST_STRPTR)NETINFONAME,
                    context->unit ^ 1, (struct IORequest *)request, 0);
                if (!context->group_error) {
                    request->io_Command = CMD_RESET;
                    context->group_error = DoIO((struct IORequest *)request);
                    CloseDevice((struct IORequest *)request);
                }
            }
        }
        DeleteIORequest((struct IORequest *)request);
    }
    if (port) DeleteMsgPort(port);
    PutMsg(context->parent, &context->done);
}

static void receive(struct MsgPort *port, struct Message *expected)
{
    struct Message *message;
    do {
        WaitPort(port);
        message = GetMsg(port);
    } while (message != expected);
}

static BOOL flush_device(const char *name)
{
    int attempt;
    for (attempt = 0; attempt < 100; ++attempt) {
        struct Device *device;
        Forbid();
        device = (struct Device *)FindName(&SysBase->DeviceList, (CONST_STRPTR)name);
        if (device) RemDevice(device);
        device = (struct Device *)FindName(&SysBase->DeviceList, (CONST_STRPTR)name);
        Permit();
        if (!device) return TRUE;
        Delay(1);
    }
    return FALSE;
}

int main(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct NetInfoReq *request = port ? (struct NetInfoReq *)CreateIORequest(port, sizeof(*request)) : NULL;
    unsigned int iteration;
    if (!request) return 20;
    for (iteration = 0; iteration < 8; ++iteration) {
        struct Reopen context;
        struct NetInfoDevice *device;
        struct Process *process;
        char device_name[128];
        LONG error;
        memset(&context, 0, sizeof(context));
        context.parent = port;
        context.unit = iteration & 1;
        context.attempting.mn_Length = sizeof(context.attempting);
        context.done.mn_Length = sizeof(context.done);
        error = OpenDevice((CONST_STRPTR)NETINFONAME, context.unit,
                           (struct IORequest *)request, 0);
        CHECK(error == 0, "initial open");
        if (error) break;
        device = (struct NetInfoDevice *)request->io_Device;
        snprintf(device_name, sizeof(device_name), "%s", device->nid_Lib.lib_Node.ln_Name);
        ObtainSemaphore(device->nid_Lock);
        context.previous_worker = device->nid_WorkerID;
        SetTaskPri(device->nid_Task, -10);
        CloseDevice((struct IORequest *)request);
        process = CreateNewProcTags(NP_Entry, (IPTR)opener, NP_Name,
            (IPTR)"netinfo reopen probe", NP_Priority, 5,
            NP_ExitData, (IPTR)&context, TAG_DONE);
        if (!process) { ReleaseSemaphore(device->nid_Lock); return 20; }
        receive(port, &context.attempting);
        CHECK(!IsListEmpty((struct List *)&device->nid_Lock->ss_WaitQueue),
              "opener is waiting for the device lock");
        /* Same transition as DevExpunge, forced after queuing the opener
         * so the semaphore hands the lock to that opener first on UP. */
        CHECK(device->nid_Death && device->nid_Death->mn_Node.ln_Type == NT_MESSAGE,
              "worker death message is pending");
        ReplyMsg(device->nid_Death);
        ReleaseSemaphore(device->nid_Lock);
        receive(port, &context.done);
        CHECK(context.open_error == 0, "reopen succeeds");
        CHECK(context.fresh, "reopen waits for old worker exit and starts a fresh worker");
        CHECK(context.maps_live, "returned unit belongs to current maps");
        CHECK(context.reset_error == 0, "real I/O completes through the new worker");
        CHECK(context.group_error == 0, "the other database unit also handles I/O");
        while (FindTaskByPID(context.opener_id) != NULL) Delay(1);
        CHECK(flush_device(device_name), "close and repeated expunge remove the device");
    }
    DeleteIORequest((struct IORequest *)request);
    DeleteMsgPort(port);
    printf("NETINFO-REOPEN %d checks, %d failures\n", checks, failures);
    return failures ? 20 : 0;
}
