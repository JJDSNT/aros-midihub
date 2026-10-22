static int checks, failures;
#define CHECK(e, label) do { ++checks; if (!(e)) { ++failures; printf("FAIL %s\n", label); } } while (0)

static void run_case(ULONG other_waiters, int fail_open)
{
    struct Base base;
    struct IORequest request;
    memset(&base, 0, sizeof(base));
    memset(&request, 0, sizeof(request));
    active = &base;
    base.nid_Task = &process;
    base.nid_WorkerID = 7;
    base.nid_OpenWaiters = other_waiters;
    base.nid_Death = &death;
    death.mn_Node.ln_Type = NT_REPLYMSG;
    worker_alive = 1;
    library_fails = fail_open;
    expunge_calls = unload_allowed = 0;
    observed_waiters = 0;
    CHECK(NetInfo__DevOpen(&base, &request, 0, 0) == !fail_open, "open result");
    CHECK(expunge_calls == 2, "two expunges while the opener is sleeping");
    CHECK(unload_allowed == 0, "no expunge may unload an active waiting opener");
    CHECK(observed_waiters == other_waiters + 1, "waiting opener is counted before dropping its lock");
    CHECK(base.nid_OpenWaiters == other_waiters, "opener count restored after reacquiring lock");
    CHECK(base.nid_Lock->held == 0, "callback releases its lock");
    if (fail_open)
        CHECK(request.io_Error == IOERR_OPENFAIL, "failed restart remains an open failure");
    else
        CHECK(request.io_Error == 0 && request.io_Unit == &unit && base.nid_WorkerID == 99,
              "successful open uses a new worker");
    base.nid_Task = NULL;
    base.nid_Death = NULL;
    worker_alive = 0;
    base.nid_WorkerID = 0;
    CHECK(NetInfo__DevExpunge(&base) == (other_waiters == 0), "remaining openers still prevent expunge");
    base.nid_OpenWaiters = 0;
    CHECK(NetInfo__DevExpunge(&base), "expunge succeeds after all opens and workers leave");
}

int main(void)
{
    struct Base base;
    struct IORequest request;
    run_case(0, 0);
    run_case(2, 0);
    run_case(0, 1);
    memset(&base, 0, sizeof(base));
    memset(&request, 0, sizeof(request));
    active = &base;
    CHECK(!NetInfo__DevOpen(&base, &request, NETINFO_UNITS, 0), "invalid unit fails");
    CHECK(!base.nid_OpenWaiters && !base.nid_Lock->held, "invalid unit leaks no waiter or lock");
    base.nid_Lock->held = 1;
    CHECK(!NetInfo__DevExpunge(&base), "busy device lock still prevents expunge");
    base.nid_Lock->held = 0;
    base.nid_WorkerID = 13;
    worker_alive = 1;
    CHECK(!NetInfo__DevExpunge(&base), "worker ID still prevents premature unload");
    worker_alive = 0;
    CHECK(NetInfo__DevExpunge(&base), "completed worker allows expunge");
    printf("WAITING-OPEN %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
