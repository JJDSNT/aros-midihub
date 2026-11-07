/*
 * SPDX-License-Identifier: MIT
 *
 * AROS-native MIDI-CI coordinator ownership experiment.  This is deliberately
 * not a midici.library ABI: it validates the Exec task/message model first.
 */

#include <dos/dos.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

#define COORDINATOR_PORT_NAME "MIDI-CI coordinator prototype"
#define REQUEST_BEGIN  1u
#define REQUEST_CANCEL 2u
#define REQUEST_STOP   3u

#define RESULT_OK      0
#define RESULT_BUSY    1
#define RESULT_STALE   2

struct CoordinatorRequest {
    struct Message message;
    ULONG command;
    ULONG owner_id;
    struct Task *owner_task;
    ULONG transaction;
    LONG result;
};

struct ClientContext {
    struct MsgPort *reply_port;
    struct CoordinatorRequest begin;
    struct CoordinatorRequest cancel;
    volatile ULONG task_id;
    struct Task *task;
};

static volatile struct MsgPort *coordinator_port;
static struct Task *parent_task;
static volatile ULONG next_transaction = 1;
static int checks, failures;

#define CHECK(condition, label) do { \
    ++checks; \
    if (condition) printf("PASS %s\n", label); \
    else { ++failures; printf("FAIL %s\n", label); } \
} while (0)

static void finish_request(struct CoordinatorRequest *request, LONG result)
{
    request->result = result;
    ReplyMsg(&request->message);
}

static int owner_is_live(ULONG owner_id, struct Task *owner_task)
{
    return owner_id != 0 && FindTaskByPID(owner_id) == owner_task;
}

static void coordinator_entry(void)
{
    struct MsgPort *port = CreateMsgPort();
    ULONG owner_id = 0, transaction = 0;
    struct Task *owner_task = NULL;
    int running = 1;

    if (port == NULL) {
        Signal(parent_task, SIGBREAKF_CTRL_F);
        return;
    }
    port->mp_Node.ln_Name = (char *)COORDINATOR_PORT_NAME;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    Forbid();
    coordinator_port = port;
    Permit();
    Signal(parent_task, SIGBREAKF_CTRL_F);

    while (running) {
        struct CoordinatorRequest *request;

        WaitPort(port);
        while ((request = (struct CoordinatorRequest *)GetMsg(port)) != NULL) {
            if (owner_id != 0 && !owner_is_live(owner_id, owner_task)) {
                owner_id = 0;
                owner_task = NULL;
                transaction = 0;
            }
            switch (request->command) {
            case REQUEST_BEGIN:
                if (!owner_is_live(request->owner_id, request->owner_task)) {
                    finish_request(request, RESULT_STALE);
                } else if (owner_id != 0) {
                    request->transaction = transaction;
                    finish_request(request, RESULT_BUSY);
                } else {
                    owner_id = request->owner_id;
                    owner_task = request->owner_task;
                    transaction = next_transaction++;
                    if (transaction == 0)
                        transaction = next_transaction++;
                    request->transaction = transaction;
                    finish_request(request, RESULT_OK);
                }
                break;
            case REQUEST_CANCEL:
                if (owner_id == request->owner_id &&
                    owner_task == request->owner_task &&
                    transaction == request->transaction) {
                    owner_id = 0;
                    owner_task = NULL;
                    transaction = 0;
                    finish_request(request, RESULT_OK);
                } else {
                    finish_request(request, RESULT_STALE);
                }
                break;
            case REQUEST_STOP:
                owner_id = 0;
                owner_task = NULL;
                transaction = 0;
                running = 0;
                finish_request(request, RESULT_OK);
                break;
            default:
                finish_request(request, RESULT_STALE);
                break;
            }
        }
    }

    Forbid();
    coordinator_port = NULL;
    RemPort(port);
    Permit();
    DeleteMsgPort(port);
    Signal(parent_task, SIGBREAKF_CTRL_F);
}

static void init_request(struct CoordinatorRequest *request,
                         struct MsgPort *reply, ULONG command)
{
    memset(request, 0, sizeof(*request));
    request->message.mn_Length = sizeof(*request);
    request->message.mn_ReplyPort = reply;
    request->command = command;
}

static void client_entry(void)
{
    struct ClientContext *context = FindTask(NULL)->tc_UserData;
    ULONG wake;

    context->task = FindTask(NULL);
    context->task_id = GetETaskID(context->task);
    init_request(&context->begin, context->reply_port, REQUEST_BEGIN);
    context->begin.owner_id = context->task_id;
    context->begin.owner_task = context->task;
    PutMsg((struct MsgPort *)coordinator_port, &context->begin.message);

    wake = Wait(SIGBREAKF_CTRL_D | SIGBREAKF_CTRL_E);
    if (wake & SIGBREAKF_CTRL_E) {
        init_request(&context->cancel, context->reply_port, REQUEST_CANCEL);
        context->cancel.owner_id = context->task_id;
        context->cancel.owner_task = context->task;
        context->cancel.transaction = context->begin.transaction;
        PutMsg((struct MsgPort *)coordinator_port, &context->cancel.message);
        /* The request borrows this task identity until the coordinator has
         * acknowledged it. The parent signals us after receiving that ACK. */
        Wait(SIGBREAKF_CTRL_D);
    }
}

static struct CoordinatorRequest *receive(struct MsgPort *port)
{
    WaitPort(port);
    return (struct CoordinatorRequest *)GetMsg(port);
}

static struct Task *start_client(struct ClientContext *context,
                                 struct MsgPort *reply, const char *name)
{
    memset(context, 0, sizeof(*context));
    context->reply_port = reply;
    return NewCreateTask(TASKTAG_PC, (IPTR)client_entry,
        TASKTAG_NAME, (IPTR)name,
        TASKTAG_STACKSIZE, 32768,
        TASKTAG_USERDATA, (IPTR)context,
        TAG_DONE);
}

static void wait_for_exit(ULONG task_id)
{
    unsigned int ticks;

    for (ticks = 0; ticks < 250 && FindTaskByPID(task_id) != NULL; ++ticks)
        Delay(1);
}

int main(void)
{
    struct MsgPort *reply = CreateMsgPort();
    struct ClientContext first, second, recovered, after_cancel;
    struct CoordinatorRequest stop;
    struct CoordinatorRequest *one, *two, *message;
    struct Task *service, *winner, *loser;
    struct ClientContext *winner_context;
    ULONG winner_id;

    if (reply == NULL)
        return RETURN_FAIL;
    parent_task = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_F);
    service = NewCreateTask(TASKTAG_PC, (IPTR)coordinator_entry,
        TASKTAG_NAME, (IPTR)COORDINATOR_PORT_NAME,
        TASKTAG_STACKSIZE, 32768,
        TAG_DONE);
    if (service == NULL) {
        DeleteMsgPort(reply);
        return RETURN_FAIL;
    }
    Wait(SIGBREAKF_CTRL_F);
    CHECK(coordinator_port != NULL, "coordinator publishes one Exec port");
    if (coordinator_port == NULL) {
        DeleteMsgPort(reply);
        return RETURN_FAIL;
    }

    first.task = start_client(&first, reply, "MIDI-CI client A");
    second.task = start_client(&second, reply, "MIDI-CI client B");
    CHECK(first.task != NULL && second.task != NULL,
          "two independent client tasks start");
    one = receive(reply);
    two = receive(reply);
    CHECK((one->result == RESULT_OK && two->result == RESULT_BUSY) ||
          (one->result == RESULT_BUSY && two->result == RESULT_OK),
          "exactly one task owns the conflicting transaction");

    winner_context = one->result == RESULT_OK
        ? (one == &first.begin ? &first : &second)
        : (two == &first.begin ? &first : &second);
    winner = winner_context->task;
    loser = winner == first.task ? second.task : first.task;
    winner_id = winner_context->task_id;
    Signal(loser, SIGBREAKF_CTRL_D);
    Signal(winner, SIGBREAKF_CTRL_D);
    wait_for_exit(first.task_id);
    wait_for_exit(second.task_id);
    CHECK(FindTaskByPID(winner_id) == NULL,
          "the transaction owner terminates without cancellation");

    recovered.task = start_client(&recovered, reply, "MIDI-CI recovery client");
    CHECK(recovered.task != NULL, "recovery client starts");
    message = receive(reply);
    CHECK(message == &recovered.begin && message->result == RESULT_OK,
          "the coordinator reaps the dead owner and grants recovery");
    Signal(recovered.task, SIGBREAKF_CTRL_E);
    message = receive(reply);
    CHECK(message == &recovered.cancel && message->result == RESULT_OK,
          "the live owner cancels its exact transaction");
    Signal(recovered.task, SIGBREAKF_CTRL_D);
    wait_for_exit(recovered.task_id);

    after_cancel.task = start_client(&after_cancel, reply,
                                     "MIDI-CI post-cancel client");
    message = receive(reply);
    CHECK(message == &after_cancel.begin && message->result == RESULT_OK,
          "a cancelled transaction releases ownership immediately");
    Signal(after_cancel.task, SIGBREAKF_CTRL_D);
    wait_for_exit(after_cancel.task_id);

    init_request(&stop, reply, REQUEST_STOP);
    PutMsg((struct MsgPort *)coordinator_port, &stop.message);
    message = receive(reply);
    CHECK(message == &stop && message->result == RESULT_OK,
          "coordinator shutdown cancels remaining ownership");
    Wait(SIGBREAKF_CTRL_F);
    CHECK(coordinator_port == NULL, "coordinator removes its public port");

    DeleteMsgPort(reply);
    printf("MIDI-CI-COORDINATOR %d checks, %d failures\n", checks, failures);
    return failures ? RETURN_ERROR : RETURN_OK;
}
