#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned long ULONG;
typedef int BOOL;
typedef void *APTR;
#define TRUE 1
#define FALSE 0
#define NT_MESSAGE 5
#define NT_REPLYMSG 7
#define NETINFO_UNITS 2
#define IOERR_OPENFAIL -1
#define D(x)
struct Node { int ln_Type; void *ln_Name; };
struct Message { struct Node mn_Node; };
struct MsgPort { unsigned int mp_SigBit; };
struct Process { int unused; };
struct Device { int unused; };
struct Unit { int unused; };
struct Lock { int held; };
struct Base {
    struct { int lib_OpenCnt; } nid_Lib;
    void *nid_Task;
    struct Message *nid_Death;
    struct Lock nid_Lock[1];
    ULONG nid_WorkerID, nid_OpenWaiters;
    void *nid_DOSBase;
};
#define LIBBASETYPEPTR struct Base *
struct IORequest { int io_Error; struct Device *io_Device; struct Unit *io_Unit; };
static int NetInfo__DevExpunge(LIBBASETYPEPTR base);
static struct Base *active;
static struct Message death, startup;
static struct MsgPort port;
static struct Process process;
static struct Unit unit;
static int worker_alive, library_fails, expunge_calls, unload_allowed;
static ULONG observed_waiters;

static void ObtainSemaphore(struct Lock *lock) { ++lock->held; }
static void ReleaseSemaphore(struct Lock *lock) { assert(lock->held == 1); --lock->held; }
static int AttemptSemaphore(struct Lock *lock)
{
    if (lock->held) return FALSE;
    ++lock->held;
    return TRUE;
}
static void *FindTaskByPID(ULONG id)
{
    return worker_alive && id == active->nid_WorkerID ? &process : NULL;
}
static void ReplyMsg(struct Message *message) { message->mn_Node.ln_Type = NT_REPLYMSG; }
static void Delay(int ticks)
{
    assert(ticks == 1 && active->nid_Lock->held == 0);
    assert(active->nid_Lib.lib_OpenCnt == 0);
    observed_waiters = active->nid_OpenWaiters;
    /* Finish the old worker during the opener's sleep. The real generated
     * open count is still zero, so another expunge reaches the callback. */
    active->nid_Task = NULL;
    active->nid_Death = NULL;
    worker_alive = 0;
    for (int i = 0; i < 2; ++i) {
        ++expunge_calls;
        if (NetInfo__DevExpunge(active)) ++unload_allowed;
    }
}
static void *OpenLibrary(const char *name, long version)
{
    assert(!strcmp(name, "dos.library") && version == 37);
    return library_fails ? NULL : &process;
}
static struct MsgPort *CreateMsgPort(void) { return &port; }
static struct Message *CreateIORequest(struct MsgPort *p, unsigned long size)
{
    assert(p == &port && size == sizeof(startup));
    return &startup;
}
static struct Process *start_worker(void)
{
    active->nid_Task = &process;
    active->nid_WorkerID = 99;
    active->nid_Death = &death;
    death.mn_Node.ln_Type = NT_MESSAGE;
    worker_alive = 1;
    return &process;
}
#define CreateNewProcTags(...) start_worker()
static void Wait(unsigned long signal) { (void)signal; }
static struct Message *GetMsg(struct MsgPort *p) { assert(p == &port); return &startup; }
static void DeleteIORequest(struct Message *message) { assert(message == &startup); }
static void DeleteMsgPort(struct MsgPort *p) { assert(p == &port); }
static struct Unit *CreateNewUnit(struct Base *base, ULONG number)
{
    assert(base->nid_Task && base->nid_Death->mn_Node.ln_Type == NT_MESSAGE);
    assert(number < NETINFO_UNITS);
    return &unit;
}
