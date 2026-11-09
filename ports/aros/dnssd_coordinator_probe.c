/*
 * SPDX-License-Identifier: MIT
 *
 * AROS-native DNS-SD coordinator lifecycle experiment.  This is not a
 * dnssd.library ABI and contains no DNS protocol or socket implementation.
 */

#include <aros/asmcall.h>
#include <dos/dos.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/task.h>
#include <resources/task.h>
#include <utility/hooks.h>

#include <stdio.h>
#include <string.h>

#include "coordinator.h"

#define SERVICE_PORT_NAME "DNS-SD coordinator prototype"

#define COMMAND_OPEN_BEGIN 1u
#define COMMAND_CANCEL     2u
#define COMMAND_STATS      3u
#define COMMAND_RESTART    4u
#define COMMAND_STOP       5u

struct CoordinatorRequest {
    struct Message message;
    ULONG command;
    struct dnssd_coord_owner owner;
    struct dnssd_coord_handle session;
    struct dnssd_coord_handle operation;
    ULONG kind;
    uint64_t conflict_key;
    ULONG flags;
    LONG result;
    struct dnssd_coord_stats stats;
    ULONG removed_objects;
};

struct ClientContext {
    struct MsgPort *reply_port;
    struct CoordinatorRequest begin;
    struct CoordinatorRequest cancel;
    struct Task *task;
    volatile ULONG task_id;
    ULONG kind;
    uint64_t conflict_key;
    ULONG flags;
};

struct ServiceState {
    struct dnssd_coordinator coordinator;
    struct SignalSemaphore semaphore;
    struct Hook task_hook;
    struct MsgPort *port;
    struct Task *parent;
    volatile ULONG removed_objects;
};

APTR TaskResBase;

static struct ServiceState service_state;
static int checks, failures;

#define CHECK(condition, label) do { \
    ++checks; \
    if (condition) printf("PASS %s\n", label); \
    else { ++failures; printf("FAIL %s\n", label); } \
} while (0)

static void init_request(struct CoordinatorRequest *request,
                         struct MsgPort *reply, ULONG command)
{
    memset(request, 0, sizeof(*request));
    request->message.mn_Length = sizeof(*request);
    request->message.mn_ReplyPort = reply;
    request->command = command;
}

static void reply_request(struct CoordinatorRequest *request, LONG result)
{
    request->result = result;
    ReplyMsg(&request->message);
}

AROS_UFH3(static IPTR, coordinator_task_notify,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct Task *, caller, A2),
    AROS_UFHA(struct TaskNotifyMsg *, message, A1))
{
    AROS_USERFUNC_INIT

    struct ServiceState *state = hook->h_Data;
    struct dnssd_coord_owner owner;
    ULONG removed;

    (void)caller;
    if (message == NULL || message->tnm_Action != TNA_REMOVED)
        return 0;
    owner.task = (uintptr_t)message->tnm_Task;
    owner.id = GetETaskID(message->tnm_Task);
    ObtainSemaphore(&state->semaphore);
    removed = dnssd_coord_owner_removed(&state->coordinator, owner);
    state->removed_objects += removed;
    ReleaseSemaphore(&state->semaphore);
    return 0;

    AROS_USERFUNC_EXIT
}

static void handle_request(struct ServiceState *state,
                           struct CoordinatorRequest *request)
{
    LONG result = DNSSD_COORD_STALE;

    ObtainSemaphore(&state->semaphore);
    switch (request->command) {
    case COMMAND_OPEN_BEGIN:
        result = dnssd_coord_open(&state->coordinator, request->owner,
                                  request->owner.task, &request->session);
        if (result == DNSSD_COORD_OK)
            result = dnssd_coord_begin(&state->coordinator, request->session,
                                       request->owner, request->kind,
                                       request->conflict_key, request->flags,
                                       &request->operation);
        break;
    case COMMAND_CANCEL:
        result = dnssd_coord_cancel(&state->coordinator, request->session,
                                    request->owner, request->operation);
        break;
    case COMMAND_STATS:
        dnssd_coord_get_stats(&state->coordinator, &request->stats);
        request->removed_objects = state->removed_objects;
        result = DNSSD_COORD_OK;
        break;
    case COMMAND_RESTART:
        dnssd_coord_restart(&state->coordinator, 0);
        result = DNSSD_COORD_OK;
        break;
    case COMMAND_STOP:
        dnssd_coord_restart(&state->coordinator, 0);
        result = DNSSD_COORD_OK;
        break;
    }
    ReleaseSemaphore(&state->semaphore);
    reply_request(request, result);
}

static void service_entry(void)
{
    struct ServiceState *state = FindTask(NULL)->tc_UserData;
    struct MsgPort *port = CreateMsgPort();
    int running = 1;

    if (port == NULL) {
        Signal(state->parent, SIGBREAKF_CTRL_F);
        return;
    }
    port->mp_Node.ln_Name = (char *)SERVICE_PORT_NAME;
    AddPort(port);
    Forbid();
    state->port = port;
    Permit();
    Signal(state->parent, SIGBREAKF_CTRL_F);

    while (running) {
        struct CoordinatorRequest *request;

        WaitPort(port);
        while ((request = (struct CoordinatorRequest *)GetMsg(port)) != NULL) {
            running = request->command != COMMAND_STOP;
            handle_request(state, request);
        }
    }

    Forbid();
    state->port = NULL;
    RemPort(port);
    Permit();
    DeleteMsgPort(port);
    Signal(state->parent, SIGBREAKF_CTRL_F);
}

static void client_entry(void)
{
    struct ClientContext *context = FindTask(NULL)->tc_UserData;
    ULONG wake;

    context->task = FindTask(NULL);
    context->task_id = GetETaskID(context->task);
    init_request(&context->begin, context->reply_port, COMMAND_OPEN_BEGIN);
    context->begin.owner.task = (uintptr_t)context->task;
    context->begin.owner.id = context->task_id;
    context->begin.kind = context->kind;
    context->begin.conflict_key = context->conflict_key;
    context->begin.flags = context->flags;
    PutMsg(service_state.port, &context->begin.message);

    wake = Wait(SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E);
    if ((wake & SIGBREAKF_CTRL_E) != 0) {
        init_request(&context->cancel, context->reply_port, COMMAND_CANCEL);
        context->cancel.owner = context->begin.owner;
        context->cancel.session = context->begin.session;
        context->cancel.operation = context->begin.operation;
        PutMsg(service_state.port, &context->cancel.message);
        Wait(SIGBREAKF_CTRL_D);
    }
}

static struct Task *start_client(struct ClientContext *context,
                                 struct MsgPort *reply, const char *name,
                                 ULONG kind, uint64_t conflict_key,
                                 ULONG flags)
{
    memset(context, 0, sizeof(*context));
    context->reply_port = reply;
    context->kind = kind;
    context->conflict_key = conflict_key;
    context->flags = flags;
    return NewCreateTask(TASKTAG_PC, (IPTR)client_entry,
        TASKTAG_NAME, (IPTR)name,
        TASKTAG_STACKSIZE, 32768,
        TASKTAG_USERDATA, (IPTR)context,
        TAG_DONE);
}

static struct CoordinatorRequest *receive(struct MsgPort *reply)
{
    WaitPort(reply);
    return (struct CoordinatorRequest *)GetMsg(reply);
}

static void wait_for_exit(ULONG task_id)
{
    unsigned ticks;

    for (ticks = 0; ticks < 250 && FindTaskByPID(task_id) != NULL; ++ticks)
        Delay(1);
}

static struct CoordinatorRequest *send_control(struct MsgPort *reply,
                                                ULONG command)
{
    static struct CoordinatorRequest request;

    init_request(&request, reply, command);
    PutMsg(service_state.port, &request.message);
    return receive(reply);
}

int main(void)
{
    struct MsgPort *reply = NULL;
    struct Task *service = NULL;
    struct ClientContext first, independent, contender, recovery, fresh;
    struct CoordinatorRequest *messages[3], *message;
    struct ClientContext *winner, *busy;
    ULONG removed_before;
    int i, exclusive_ok = 0, independent_ok = 0, busy_count = 0;

    memset(&service_state, 0, sizeof(service_state));
    InitSemaphore(&service_state.semaphore);
    dnssd_coord_init(&service_state.coordinator, 0x100u);
    service_state.parent = FindTask(NULL);

    TaskResBase = OpenResource((CONST_STRPTR)"task.resource");
    CHECK(TaskResBase != NULL,
          "task.resource is available for termination notification");
    if (TaskResBase == NULL)
        return RETURN_FAIL;
    service_state.task_hook.h_Entry = (HOOKFUNC)coordinator_task_notify;
    service_state.task_hook.h_Data = &service_state;
    CHECK(AddTaskNotifyHook(&service_state.task_hook),
          "TNA_REMOVED hook registers before clients start");
    if (service_state.task_hook.h_MinNode.mln_Succ == NULL)
        return RETURN_FAIL;

    reply = CreateMsgPort();
    if (reply == NULL)
        return RETURN_FAIL;
    SetSignal(0, SIGBREAKF_CTRL_F);
    service = NewCreateTask(TASKTAG_PC, (IPTR)service_entry,
        TASKTAG_NAME, (IPTR)SERVICE_PORT_NAME,
        TASKTAG_STACKSIZE, 32768,
        TASKTAG_USERDATA, (IPTR)&service_state,
        TAG_DONE);
    CHECK(service != NULL, "coordinator service task starts");
    if (service == NULL)
        return RETURN_FAIL;
    Wait(SIGBREAKF_CTRL_F);
    CHECK(service_state.port != NULL, "coordinator publishes one Exec port");

    first.task = start_client(&first, reply, "DNS-SD exclusive A", 1u, 11u,
                              DNSSD_COORD_EXCLUSIVE);
    independent.task = start_client(&independent, reply,
                                    "DNS-SD independent", 1u, 22u,
                                    DNSSD_COORD_EXCLUSIVE);
    contender.task = start_client(&contender, reply,
                                  "DNS-SD exclusive B", 1u, 11u,
                                  DNSSD_COORD_EXCLUSIVE);
    CHECK(first.task != NULL && independent.task != NULL &&
          contender.task != NULL, "three independent client tasks start");
    for (i = 0; i < 3; ++i)
        messages[i] = receive(reply);
    for (i = 0; i < 3; ++i) {
        if (messages[i] == &independent.begin &&
            messages[i]->result == DNSSD_COORD_OK)
            ++independent_ok;
        else if ((messages[i] == &first.begin ||
                  messages[i] == &contender.begin) &&
                 messages[i]->result == DNSSD_COORD_OK)
            ++exclusive_ok;
        else if ((messages[i] == &first.begin ||
                  messages[i] == &contender.begin) &&
                 messages[i]->result == DNSSD_COORD_BUSY)
            ++busy_count;
    }
    CHECK(exclusive_ok == 1 && busy_count == 1 && independent_ok == 1,
          "conflict is exclusive while an independent transaction proceeds");

    winner = first.begin.result == DNSSD_COORD_OK ? &first : &contender;
    busy = winner == &first ? &contender : &first;
    Signal(busy->task, SIGBREAKF_CTRL_D);
    wait_for_exit(busy->task_id);

    message = send_control(reply, COMMAND_STATS);
    CHECK(message->result == DNSSD_COORD_OK &&
          message->stats.operations == 2 &&
          message->stats.notification_targets == 2,
          "two active owners retain independent operations and notifications");
    removed_before = message->removed_objects;

    RemTask(winner->task);
    wait_for_exit(winner->task_id);
    message = send_control(reply, COMMAND_STATS);
    CHECK(message->stats.sessions == 1 && message->stats.operations == 1 &&
          message->stats.notification_targets == 1 &&
          message->removed_objects >= removed_before + 2,
          "forced task removal reclaims operation and stale notification");

    Signal(independent.task, SIGBREAKF_CTRL_E);
    message = receive(reply);
    CHECK(message == &independent.cancel &&
          message->result == DNSSD_COORD_OK,
          "live owner cancellation is acknowledged exactly");
    Signal(independent.task, SIGBREAKF_CTRL_D);
    wait_for_exit(independent.task_id);
    message = send_control(reply, COMMAND_STATS);
    CHECK(message->stats.sessions == 0 && message->stats.operations == 0 &&
          message->stats.notification_targets == 0,
          "normal exit leaves no session, operation or notification target");

    recovery.task = start_client(&recovery, reply, "DNS-SD recovery", 1u,
                                 11u, DNSSD_COORD_EXCLUSIVE);
    message = receive(reply);
    CHECK(message == &recovery.begin && message->result == DNSSD_COORD_OK,
          "replacement task recovers the released exclusive transaction");
    message = send_control(reply, COMMAND_RESTART);
    CHECK(message->result == DNSSD_COORD_OK,
          "coordinator restart invalidates its previous incarnation");
    Signal(recovery.task, SIGBREAKF_CTRL_E);
    message = receive(reply);
    CHECK(message == &recovery.cancel &&
          message->result == DNSSD_COORD_STALE,
          "old operation token is stale after restart");
    Signal(recovery.task, SIGBREAKF_CTRL_D);
    wait_for_exit(recovery.task_id);

    fresh.task = start_client(&fresh, reply, "DNS-SD post-restart", 1u, 11u,
                              DNSSD_COORD_EXCLUSIVE);
    message = receive(reply);
    CHECK(message == &fresh.begin && message->result == DNSSD_COORD_OK,
          "fresh session operates after restart");
    RemTask(fresh.task);
    wait_for_exit(fresh.task_id);

    message = send_control(reply, COMMAND_STOP);
    CHECK(message->result == DNSSD_COORD_OK,
          "service shutdown clears logical ownership");
    Wait(SIGBREAKF_CTRL_F);
    CHECK(service_state.port == NULL, "service removes its public port");

    RemTaskNotifyHook(&service_state.task_hook);
    TaskResBase = NULL;
    DeleteMsgPort(reply);
    printf("DNSSD-COORDINATOR %d checks, %d failures\n", checks, failures);
    return failures ? RETURN_ERROR : RETURN_OK;
}
