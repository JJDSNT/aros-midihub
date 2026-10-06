/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright 2026 amigazen project
 * Copyright 2026 Jaime Dias
 *
 * AROS library entry points for the Bonami engine.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <aros/libcall.h>
#include <string.h>

#include <bonami/mdns.h>
#include <bonami/nipc.h>
#include "private/bonami_internal.h"
#include "private/bonami_ipc.h"

static void store_ref(struct TagItem *tags, ULONG which, APTR value)
{
    struct TagItem *tag;

    if (tags == NULL)
        return;
    tag = FindTagItem(which, tags);
    if (tag != NULL && tag->ti_Data != 0)
        *((APTR *)tag->ti_Data) = value;
}

AROS_LH0(LONG, OpenMDNSEngine,
    struct BonamiPrivate *, BonamiBase, 5, Bonami)
{
    AROS_LIBFUNC_INIT
    return MDNSEnsureEngine(BonamiBase);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, CloseMDNSEngine,
    struct BonamiPrivate *, BonamiBase, 6, Bonami)
{
    AROS_LIBFUNC_INIT
    MDNSTokenDetach(BonamiBase);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, RegisterServiceA,
    AROS_LHA(struct TagItem *, tags, A0),
    struct BonamiPrivate *, BonamiBase, 7, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_REGISTER;
    rc = MDNSTagsToRegisterMsg(tags, &msg);
    if (rc == MDNS_OK) {
        rc = MDNSSendIPC(BonamiBase, &msg);
        if (rc == MDNS_OK)
            store_ref(tags, MDNS_RegisterRef, msg.bim_Handle);
    }
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, UpdateServiceA,
    AROS_LHA(MDNSRegisterRef, ref, A0),
    AROS_LHA(struct TagItem *, tags, A1),
    struct BonamiPrivate *, BonamiBase, 8, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_UPDATE;
    msg.bim_Handle = ref;
    rc = MDNSTagsToRegisterMsg(tags, &msg);
    return rc == MDNS_OK ? MDNSSendIPC(BonamiBase, &msg) : rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, DeregisterService,
    AROS_LHA(MDNSRegisterRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 9, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_UNREGISTER;
    msg.bim_Handle = ref;
    MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, BrowseServiceTypeA,
    AROS_LHA(STRPTR, type, A0),
    AROS_LHA(struct Hook *, hook, A1),
    AROS_LHA(struct TagItem *, tags, A2),
    struct BonamiPrivate *, BonamiBase, 10, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    if (type == NULL || hook == NULL)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_BROWSE;
    msg.bim_Hook = hook;
    msg.bim_Task = FindTask(NULL);
    strncpy(msg.bim_ServiceType, type, MDNS_MAX_TYPE);
    msg.bim_ServiceType[MDNS_MAX_TYPE] = '\0';
    rc = MDNSSendIPC(BonamiBase, &msg);
    if (rc == MDNS_OK)
        store_ref(tags, MDNS_BrowseRef, msg.bim_Handle);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, StopBrowse,
    AROS_LHA(MDNSBrowseRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 11, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_STOPBROWSE;
    msg.bim_Handle = ref;
    MDNSSendIPC(BonamiBase, &msg);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, ResolveServiceA,
    AROS_LHA(STRPTR, name, A0),
    AROS_LHA(STRPTR, type, A1),
    AROS_LHA(struct Hook *, hook, A2),
    AROS_LHA(struct TagItem *, tags, A3),
    struct BonamiPrivate *, BonamiBase, 12, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    LONG rc;

    if (name == NULL || type == NULL || hook == NULL)
        return MDNSERR_BADPARAM;
    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_RESOLVE;
    msg.bim_Hook = hook;
    msg.bim_Task = FindTask(NULL);
    strncpy(msg.bim_Name, name, MDNS_MAX_NAME);
    msg.bim_Name[MDNS_MAX_NAME] = '\0';
    strncpy(msg.bim_ServiceType, type, MDNS_MAX_TYPE);
    msg.bim_ServiceType[MDNS_MAX_TYPE] = '\0';
    rc = MDNSSendIPC(BonamiBase, &msg);
    if (rc == MDNS_OK)
        store_ref(tags, MDNS_ResolveRef, msg.bim_Handle);
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, StopResolve,
    AROS_LHA(MDNSResolveRef, ref, A0),
    struct BonamiPrivate *, BonamiBase, 13, Bonami)
{
    AROS_LIBFUNC_INIT
    /* The imported Bonami engine resolves from its cache synchronously and
     * does not create a persistent resolve operation yet. */
    (void)ref;
    (void)BonamiBase;
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, MakeTXTRecordA,
    AROS_LHA(struct TagItem *, tags, A0),
    AROS_LHA(APTR *, record, A1),
    AROS_LHA(ULONG *, recordLen, A2),
    struct BonamiPrivate *, BonamiBase, 14, Bonami)
{
    AROS_LIBFUNC_INIT
    struct TagItem *tag;
    STRPTR text;
    UBYTE *buffer;
    ULONG length;

    (void)BonamiBase;
    if (tags == NULL || record == NULL || recordLen == NULL)
        return MDNSERR_BADPARAM;
    tag = FindTagItem(MDNS_TXTRecord, tags);
    text = tag != NULL ? (STRPTR)tag->ti_Data : NULL;
    if (text == NULL || (length = strlen(text)) > MDNS_MAX_TXT)
        return MDNSERR_BADPARAM;
    buffer = AllocMem(MDNS_MAX_TXT + 1, MEMF_CLEAR);
    if (buffer == NULL)
        return MDNSERR_NOMEM;
    memcpy(buffer, text, length + 1);
    *record = buffer;
    *recordLen = length;
    return MDNS_OK;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, FreeTXTRecord,
    AROS_LHA(APTR, record, A0),
    struct BonamiPrivate *, BonamiBase, 15, Bonami)
{
    AROS_LIBFUNC_INIT
    (void)BonamiBase;
    if (record != NULL)
        FreeMem(record, MDNS_MAX_TXT + 1);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, GetMDNSStatusA,
    AROS_LHA(struct TagItem *, tags, A0),
    struct BonamiPrivate *, BonamiBase, 16, Bonami)
{
    AROS_LIBFUNC_INIT
    struct MDNSIPCMsg msg;
    struct TagItem *state;
    struct TagItem *tag;
    ULONG *out;
    LONG rc;

    memset(&msg, 0, sizeof(msg));
    msg.bim_Command = BONAMI_MSG_STATUS;
    rc = MDNSSendIPC(BonamiBase, &msg);
    state = tags;
    while (rc == MDNS_OK &&
           (tag = NextTagItem(&state)) != NULL) {
        out = (ULONG *)tag->ti_Data;
        if (out == NULL)
            continue;
        switch (tag->ti_Tag) {
        case MDNS_StatusRxPackets: *out = msg.bim_HostAddr; break;
        case MDNS_StatusTxPackets: *out = msg.bim_Flags; break;
        case MDNS_StatusRxAnswers: *out = msg.bim_Port; break;
        case MDNS_StatusDbgRecs: *out = msg.bim_Dbg0; break;
        case MDNS_StatusDbgPtr: *out = msg.bim_Dbg1; break;
        case MDNS_StatusDbgNotify: *out = msg.bim_Dbg2; break;
        default: break;
        }
    }
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, InquiryFromNIPCTagsA,
    AROS_LHA(struct Hook *, hook, A0),
    AROS_LHA(ULONG, maxTime, D0),
    AROS_LHA(ULONG, maxResponses, D1),
    AROS_LHA(struct TagItem *, tags, A1),
    struct BonamiPrivate *, BonamiBase, 17, Bonami)
{
    AROS_LIBFUNC_INIT
    (void)hook;
    (void)maxTime;
    (void)maxResponses;
    (void)tags;
    (void)BonamiBase;
    return MDNSERR_NOTREADY;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, PublishNIPCEntitiesA,
    AROS_LHA(struct TagItem *, tags, A0),
    struct BonamiPrivate *, BonamiBase, 18, Bonami)
{
    AROS_LIBFUNC_INIT
    (void)tags;
    (void)BonamiBase;
    return MDNSERR_NOTREADY;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, UnpublishNIPCEntitiesA,
    struct BonamiPrivate *, BonamiBase, 19, Bonami)
{
    AROS_LIBFUNC_INIT
    (void)BonamiBase;
    AROS_LIBFUNC_EXIT
}
