#include "../prototypes/camd/identity_store_recovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct model {
    enum CAMDIdentityStoreLoadState slot[CAMD_IDENTITY_STORE_SLOT_COUNT];
    unsigned int mutations;
    unsigned int fail_mask;
    enum CAMDIdentityStoreSlot selected;
};

static enum CAMDIdentityStoreLoadState load_slot(
    void *context, enum CAMDIdentityStoreSlot slot)
{
    struct model *model = context;

    if (model->slot[slot] == CAMD_IDENTITY_STORE_VALID)
        model->selected = slot;
    return model->slot[slot];
}

static int exists_slot(void *context, enum CAMDIdentityStoreSlot slot)
{
    struct model *model = context;

    return model->slot[slot] != CAMD_IDENTITY_STORE_MISSING;
}

static int mutation_succeeds(struct model *model)
{
    ++model->mutations;
    return model->mutations > sizeof(model->fail_mask) * 8u ||
           (model->fail_mask & (1u << (model->mutations - 1))) == 0;
}

static int remove_slot(void *context, enum CAMDIdentityStoreSlot slot)
{
    struct model *model = context;

    if (!mutation_succeeds(model))
        return 0;
    if (model->slot[slot] == CAMD_IDENTITY_STORE_MISSING)
        return 0;
    model->slot[slot] = CAMD_IDENTITY_STORE_MISSING;
    return 1;
}

static int move_slot(void *context, enum CAMDIdentityStoreSlot from,
                     enum CAMDIdentityStoreSlot to)
{
    struct model *model = context;

    if (!mutation_succeeds(model) ||
        model->slot[from] == CAMD_IDENTITY_STORE_MISSING ||
        model->slot[to] != CAMD_IDENTITY_STORE_MISSING)
        return 0;
    model->slot[to] = model->slot[from];
    model->slot[from] = CAMD_IDENTITY_STORE_MISSING;
    return 1;
}

static struct CAMDIdentityStoreOpsV1 make_ops(struct model *model)
{
    struct CAMDIdentityStoreOpsV1 ops;

    memset(&ops, 0, sizeof(ops));
    ops.Size = sizeof(ops);
    ops.Version = 1;
    ops.Context = model;
    ops.Load = load_slot;
    ops.Exists = exists_slot;
    ops.Remove = remove_slot;
    ops.Move = move_slot;
    return ops;
}

static void assert_recoverable(struct model model)
{
    struct CAMDIdentityStoreOpsV1 ops;

    model.fail_mask = 0;
    model.mutations = 0;
    model.selected = CAMD_IDENTITY_STORE_SLOT_COUNT;
    ops = make_ops(&model);
    assert(camd_identity_store_recover(&ops));
    assert(model.slot[CAMD_IDENTITY_STORE_MAIN] ==
           CAMD_IDENTITY_STORE_VALID);
}

static void test_install_faults(void)
{
    struct CAMDIdentityStoreOpsV1 ops;
    struct model model;
    unsigned int failures;

    for (failures = 0; failures < 32; ++failures) {
        memset(&model, 0, sizeof(model));
        model.slot[CAMD_IDENTITY_STORE_MAIN] = CAMD_IDENTITY_STORE_VALID;
        model.slot[CAMD_IDENTITY_STORE_NEW] = CAMD_IDENTITY_STORE_VALID;
        model.slot[CAMD_IDENTITY_STORE_BACKUP] = CAMD_IDENTITY_STORE_VALID;
        model.fail_mask = failures;
        ops = make_ops(&model);
        (void)camd_identity_store_install(&ops);
        assert_recoverable(model);
    }

    memset(&model, 0, sizeof(model));
    model.slot[CAMD_IDENTITY_STORE_NEW] = CAMD_IDENTITY_STORE_VALID;
    model.fail_mask = 1;
    ops = make_ops(&model);
    assert(!camd_identity_store_install(&ops));
    assert_recoverable(model);
}

static void test_recovery_selection_and_faults(void)
{
    struct CAMDIdentityStoreOpsV1 ops;
    struct model model;
    unsigned int failures;

    memset(&model, 0, sizeof(model));
    model.slot[CAMD_IDENTITY_STORE_MAIN] = CAMD_IDENTITY_STORE_VALID;
    model.slot[CAMD_IDENTITY_STORE_NEW] = CAMD_IDENTITY_STORE_VALID;
    model.slot[CAMD_IDENTITY_STORE_BACKUP] = CAMD_IDENTITY_STORE_VALID;
    model.selected = CAMD_IDENTITY_STORE_SLOT_COUNT;
    ops = make_ops(&model);
    assert(camd_identity_store_recover(&ops));
    assert(model.selected == CAMD_IDENTITY_STORE_MAIN);

    memset(&model, 0, sizeof(model));
    model.slot[CAMD_IDENTITY_STORE_NEW] = CAMD_IDENTITY_STORE_VALID;
    model.slot[CAMD_IDENTITY_STORE_BACKUP] = CAMD_IDENTITY_STORE_VALID;
    model.selected = CAMD_IDENTITY_STORE_SLOT_COUNT;
    ops = make_ops(&model);
    assert(camd_identity_store_recover(&ops));
    assert(model.selected == CAMD_IDENTITY_STORE_NEW);
    assert(model.slot[CAMD_IDENTITY_STORE_MAIN] ==
           CAMD_IDENTITY_STORE_VALID);

    for (failures = 0; failures < 16; ++failures) {
        memset(&model, 0, sizeof(model));
        model.slot[CAMD_IDENTITY_STORE_MAIN] = CAMD_IDENTITY_STORE_INVALID;
        model.slot[CAMD_IDENTITY_STORE_NEW] = CAMD_IDENTITY_STORE_VALID;
        model.slot[CAMD_IDENTITY_STORE_BACKUP] = CAMD_IDENTITY_STORE_VALID;
        model.fail_mask = failures;
        ops = make_ops(&model);
        (void)camd_identity_store_recover(&ops);
        assert_recoverable(model);
    }

    memset(&model, 0, sizeof(model));
    model.slot[CAMD_IDENTITY_STORE_MAIN] = CAMD_IDENTITY_STORE_INVALID;
    model.slot[CAMD_IDENTITY_STORE_NEW] = CAMD_IDENTITY_STORE_INVALID;
    model.slot[CAMD_IDENTITY_STORE_BACKUP] = CAMD_IDENTITY_STORE_VALID;
    ops = make_ops(&model);
    assert(camd_identity_store_recover(&ops));
    assert(model.selected == CAMD_IDENTITY_STORE_BACKUP);
    assert(model.slot[CAMD_IDENTITY_STORE_MAIN] ==
           CAMD_IDENTITY_STORE_VALID);

    memset(&model, 0, sizeof(model));
    ops = make_ops(&model);
    assert(camd_identity_store_recover(&ops));

    model.slot[CAMD_IDENTITY_STORE_MAIN] = CAMD_IDENTITY_STORE_INVALID;
    assert(!camd_identity_store_recover(&ops));
}

static void test_invalid_ops(void)
{
    struct CAMDIdentityStoreOpsV1 ops;
    struct model model;

    memset(&model, 0, sizeof(model));
    ops = make_ops(&model);
    ops.Version = 2;
    assert(!camd_identity_store_recover(&ops));
    assert(!camd_identity_store_install(&ops));
    ops = make_ops(&model);
    ops.Load = NULL;
    assert(!camd_identity_store_recover(&ops));
    ops = make_ops(&model);
    ops.Exists = NULL;
    assert(!camd_identity_store_install(&ops));
}

int main(void)
{
    test_install_faults();
    test_recovery_selection_and_faults();
    test_invalid_ops();
    puts("CAMD identity store recovery OK");
    return 0;
}
